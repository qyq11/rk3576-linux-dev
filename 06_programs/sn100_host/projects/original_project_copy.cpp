#include "original_project_copy.h"
#include "original_project_repository.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariantMap>
#include <cstring>
#include <limits>
#if defined(Q_OS_LINUX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace OriginalProjectCopy
{
namespace
{
const QString MarkerPrefix = QStringLiteral(".sn100-project-copy-");
const QString CalibrationFile = QStringLiteral("result/calibration.db");
const QString ReagentFile = QStringLiteral("db/userSet/reagent_Information.db");
const int MaximumGroups = 64;
const int MaximumPoints = 4096;
const qint64 MaximumFileBytes = 32 * 1024 * 1024;
const qint64 MaximumMarkerBytes = 8 * 1024 * 1024;
const qint64 MaximumPlanBytes = 2 * 1024 * 1024;

bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}

QString identifier(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

bool regular(const QString &path, bool directory, QString *message)
{
    QFileInfo info(path);
    if (!info.isAbsolute() || !info.exists() || (directory ? !info.isDir() : !info.isFile()))
        return fail(message, QStringLiteral("项目复制所需路径不存在或类型不符。"));
    for (QFileInfo entry(info.absoluteFilePath()); ; entry.setFile(entry.absolutePath()))
    {
        if (entry.isSymLink()) return fail(message, QStringLiteral("项目复制路径不能经过符号链接。"));
        if (entry.absoluteFilePath() == entry.absolutePath()) break;
    }
    return true;
}

QString databaseFingerprint(const QString &path, QString *message)
{
    return OriginalProjectRepository::databaseFingerprint(path, message);
}

class Database
{
public:
    QSqlDatabase db;
    QString name;
    ~Database()
    {
        if (!name.isEmpty())
        {
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(name);
        }
    }
    bool open(const QString &path, bool write, QString *message)
    {
        if (!regular(path, false, message) || QFileInfo(path).size() > MaximumFileBytes)
            return fail(message, QStringLiteral("项目复制数据库不可读或超过32MiB范围。"));
        name = QStringLiteral("OriginalProjectCopy-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(write ? QStringLiteral("QSQLITE_BUSY_TIMEOUT=0") :
                                    QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=0"));
        if (!db.open()) return fail(message, QStringLiteral("复制资料打开失败：") + db.lastError().text());
        QSqlQuery query(db);
        return query.exec(QStringLiteral("PRAGMA foreign_keys=ON")) ||
               fail(message, QStringLiteral("复制资料引用保护无法启用。"));
    }
};

bool query(QSqlQuery *output, const QString &text, const QVariantList &values, QString *message)
{
    if (!output->prepare(text)) return fail(message, QStringLiteral("复制语句准备失败：") + output->lastError().text());
    for (const QVariant &value : values) output->addBindValue(value);
    return output->exec() || fail(message, QStringLiteral("复制资料操作失败：") + output->lastError().text());
}

bool execute(QSqlDatabase &db, const QString &text, const QVariantList &values, QString *message, int affected = -1)
{
    QSqlQuery command(db);
    if (!query(&command, text, values, message)) return false;
    return affected < 0 || command.numRowsAffected() == affected ||
           fail(message, QStringLiteral("复制写入行数与本次来源不符，未报完成。"));
}

bool attach(QSqlDatabase &db, const QString &root, QString *message)
{
    for (const auto &item : {qMakePair(CalibrationFile, QStringLiteral("copy_cal")),
                             qMakePair(ReagentFile, QStringLiteral("copy_reagent"))})
    {
        const QString path = QDir(root).filePath(item.first);
        if (!regular(path, false, message) || !QFileInfo(path).isWritable() || QFileInfo(path).size()>MaximumFileBytes)
            return fail(message, QStringLiteral("原校准或默认试剂库不可写，未开始关联复制。"));
        if (!execute(db, QStringLiteral("ATTACH DATABASE ? AS ") + item.second, {path}, message)) return false;
    }
    return true;
}

bool recoverableModes(QSqlDatabase &db, QString *message)
{
    for (const QString &schema : {QStringLiteral("main"), QStringLiteral("copy_cal"), QStringLiteral("copy_reagent")})
    {
        QSqlQuery mode(db), sync(db);
        if (!mode.exec(QStringLiteral("PRAGMA %1.journal_mode").arg(schema)) || !mode.next() ||
            !sync.exec(QStringLiteral("PRAGMA %1.synchronous").arg(schema)) || !sync.next())
            return fail(message, QStringLiteral("无法核对复制的多库事务条件。"));
        const QString journal = mode.value(0).toString().toLower();
        if ((journal != QStringLiteral("delete") && journal != QStringLiteral("truncate") && journal != QStringLiteral("persist")) ||
            sync.value(0).toInt() < 2)
            return fail(message, QStringLiteral("原库日志/同步方式不能保证本次多库一起恢复，未改PRAGMA或放行复制。"));
    }
    return true;
}

bool exactColumns(QSqlDatabase &db, const QString &schema, const QString &table, const QStringList &expected, QString *message)
{
    QSqlQuery columns(db);
    if (!columns.exec(QStringLiteral("PRAGMA %1.table_xinfo(%2)").arg(schema, identifier(table))))
        return fail(message, QStringLiteral("复制关联表列无法核对。"));
    QStringList actual;
    while (columns.next())
    {
        if (columns.record().count()<7 || columns.value(6).toInt()!=0)
            return fail(message,QStringLiteral("关联表含生成列/隐藏列或无法完整查列，未开始复制：")+table);
        if (actual.isEmpty() && (columns.value(1).toString()!="id" ||
            columns.value(2).toString().compare("INTEGER",Qt::CaseInsensitive)!=0 || columns.value(5).toInt()!=1))
            return fail(message,QStringLiteral("关联表行号不是原INTEGER主键，未猜新增行号：")+table);
        actual << columns.value(1).toString();
    }
    if (columns.lastError().isValid() || actual != expected)
        return fail(message, QStringLiteral("复制关联表与原DAL列顺序不同，未猜未知扩展列：") + table);
    QSqlQuery triggers(db);
    if (!query(&triggers, QStringLiteral("SELECT 1 FROM %1.sqlite_master WHERE type='trigger' AND tbl_name=? LIMIT 1").arg(schema), {table}, message)) return false;
    return !triggers.next() || fail(message, QStringLiteral("复制关联表带额外触发器，未猜其副作用：") + table);
}

bool copySchema(QSqlDatabase &db, QString *message)
{
    return exactColumns(db, "copy_cal", "calibration_base", {"id","calibration_id","project_name","calibration_datetime","boold_batch","point_size","repeat_number","calculate_id","is_checked","is_edited","result_unit","con_unit","x_change","y_change","visible"}, message) &&
        exactColumns(db, "copy_cal", "calibration_data", {"id","calibration_id","point_index","sample_rate","concentration","result_flag","result_value","only_count"}, message) &&
        exactColumns(db, "copy_cal", "calibration_batch_group", {"id","project_name","batch_group","calibration_id"}, message) &&
        exactColumns(db, "copy_cal", "calibraiton_total_count", {"id","total_count"}, message) &&
        exactColumns(db, "copy_reagent", "reagent_batch_group", {"id","project_name","batch_group"}, message);
}

QVariantMap row(const QSqlQuery &query)
{
    QVariantMap fields;
    const QSqlRecord record = query.record();
    for (int column = 0; column < record.count(); ++column) fields.insert(record.fieldName(column), query.value(column));
    return fields;
}

QString textSelection(const QStringList &fields)
{
    QString selection=QStringLiteral("*");
    for (const QString &field:fields)
        selection+=QStringLiteral(",CAST(%1 AS BLOB) AS %2").arg(identifier(field),identifier("_sn100_raw_"+field));
    return selection;
}

bool exactSourceText(const QSqlQuery &source, const QStringList &fields, QString *message)
{
    for (const QString &field:fields)
    {
        if (source.value(field).isNull()) continue;
        const QByteArray raw=source.value("_sn100_raw_"+field).toByteArray();
        if (raw.size()>65536 || raw.contains('\0') || source.value(field).toString().toUtf8()!=raw)
            return fail(message,QStringLiteral("原关联文字不是可完整保留的规范UTF-8文字，未替换编码或截断复制。"));
    }
    return true;
}

bool sourceText(QVariant value, QString *text, QString *message)
{
    *text = value.isNull() ? QStringLiteral("") : value.toString();
    if (text->contains(QChar::Null) || text->toUtf8().size() > 65536)
        return fail(message, QStringLiteral("复制来源含NUL或超长文字，原C字符串会截断；本轮保留来源并拒绝。"));
    return true;
}

bool sourceInt(QVariant value, int *number, QString *message)
{
    if (value.isNull()) { *number = 0; return true; }
    bool ok = false;
    const qlonglong converted = value.toString().toLongLong(&ok);
    if (!ok || converted < std::numeric_limits<int>::min() || converted > std::numeric_limits<int>::max())
        return fail(message, QStringLiteral("复制来源整数不是原32位规范值，未按弱转换猜数。"));
    *number = int(converted);
    return true;
}

struct Plan
{
    qint64 counterRow = 0;
    int counterBefore = 0;
    int counterAfter = 0;
    qint64 estimatedBytes = 0;
    QList<QVariantMap> bases, points, bindings, groups;
};

bool budget(Plan *plan, const QVariantMap &values, QString *message)
{
    qint64 bytes=256;
    for (auto field=values.cbegin();field!=values.cend();++field)
        bytes+=field.key().toUtf8().size()+field.value().toString().toUtf8().size();
    if (bytes>MaximumPlanBytes-plan->estimatedBytes)
        return fail(message,QStringLiteral("关联复制清单超过2MiB预备容量，未继续扩大内存或截断资料。"));
    plan->estimatedBytes+=bytes;
    return true;
}

bool absentName(QSqlDatabase &db, const QString &name, QString *message, const QString &calibrationSchema = QStringLiteral("copy_cal"))
{
    for (const QString &table : {calibrationSchema+QStringLiteral(".calibration_base"), calibrationSchema+QStringLiteral(".calibration_batch_group"),
                                  QStringLiteral("copy_reagent.reagent_batch_group")})
    {
        QSqlQuery existing(db);
        if (!query(&existing, QStringLiteral("SELECT 1 FROM ") + table + QStringLiteral(" WHERE project_name=? LIMIT 1"), {name}, message)) return false;
        if (existing.next()) return fail(message, QStringLiteral("新项目名已有外部关联，未混入其他来源。"));
    }
    return true;
}

bool buildPlan(QSqlDatabase &db, const QString &sourceName, const QString &newName, Plan *plan, QString *message)
{
    if (!copySchema(db, message) || !absentName(db, newName, message)) return false;
    QSqlQuery counter(db);
    if (!counter.exec(QStringLiteral("SELECT id,total_count FROM copy_cal.calibraiton_total_count ORDER BY id")) || !counter.next())
        return fail(message, QStringLiteral("原校准计数不存在，未自动新建。"));
    plan->counterRow = counter.value(0).toLongLong();
    if (plan->counterRow<=0 || plan->counterRow>std::numeric_limits<int>::max() ||
        !sourceInt(counter.value(1), &plan->counterBefore, message) || plan->counterBefore < 0 || counter.next())
        return fail(message, QStringLiteral("原校准计数不唯一或无效，未另找编号。"));
    counter.finish();
    plan->counterAfter = plan->counterBefore;
    const QStringList bindingTexts={"project_name","batch_group"};
    QSqlQuery bindings(db);
    if (!query(&bindings, "SELECT "+textSelection(bindingTexts)+" FROM copy_cal.calibration_batch_group WHERE project_name=? ORDER BY id", {sourceName}, message)) return false;
    QList<QVariantMap> oldBindings;
    while (bindings.next())
    {
        if (oldBindings.size() >= MaximumGroups) return fail(message, QStringLiteral("原绑定超过64项，未截断复制。"));
        if (!exactSourceText(bindings,bindingTexts,message)) return false;
        oldBindings << row(bindings);
    }
    if (bindings.lastError().isValid() || oldBindings.isEmpty()) return fail(message, QStringLiteral("关联来源已变化或读取失败，请重读。"));
    bindings.finish();
    for (const QVariantMap &oldBinding : oldBindings)
    {
        int oldId = 0;
        if (!sourceInt(oldBinding.value("calibration_id"), &oldId, message) || oldId < 0 || plan->counterAfter == std::numeric_limits<int>::max())
            return fail(message, QStringLiteral("原校准绑定编号或新计数范围无效。"));
        const int newCurve = plan->counterAfter++;
        QSqlQuery collision(db);
        for (const QString &table : {QStringLiteral("calibration_base"), QStringLiteral("calibration_data"), QStringLiteral("calibration_batch_group")})
        {
            if (!query(&collision, QStringLiteral("SELECT 1 FROM copy_cal.%1 WHERE calibration_id=? LIMIT 1").arg(table), {newCurve}, message)) return false;
            if (collision.next()) return fail(message, QStringLiteral("原校准计数与已有资料冲突，未覆盖或拼接。"));
            collision.finish();
        }
        QString batch;
        if (!sourceText(oldBinding.value("batch_group"), &batch, message)) return false;
        const QVariantMap binding{{"project_name",newName},{"batch_group",batch},{"calibration_id",newCurve}};
        const QVariantMap group{{"project_name",newName},{"batch_group",batch}};
        if (!budget(plan,binding,message) || !budget(plan,group,message)) return false;
        plan->bindings << binding;plan->groups << group;
        const QStringList baseTexts={"project_name","calibration_datetime","boold_batch","calculate_id","result_unit","con_unit","x_change","y_change"};
        QSqlQuery base(db);
        if (!query(&base, "SELECT "+textSelection(baseTexts)+" FROM copy_cal.calibration_base WHERE calibration_id=? AND project_name=?", {oldId,sourceName}, message) || !base.next())
            return fail(message, QStringLiteral("原绑定找不到曲线基项，未复制默认空曲线。"));
        if (!exactSourceText(base,baseTexts,message)) return false;
        const QVariantMap original = row(base);
        if (base.next()) return fail(message, QStringLiteral("原曲线编号和项目名不唯一，未猜其中一项。"));
        base.finish();
        QVariantMap copied{{"calibration_id",newCurve},{"project_name",newName}};
        for (const QString &key : {QStringLiteral("boold_batch"),QStringLiteral("calculate_id"),QStringLiteral("result_unit"),QStringLiteral("con_unit"),QStringLiteral("x_change"),QStringLiteral("y_change")})
        {
            QString value;
            if (!sourceText(original.value(key), &value, message)) return false;
            copied.insert(key, value);
        }
        QString date;
        if (!sourceText(original.value("calibration_datetime"), &date, message)) return false;
        QString normalizedDate=QDateTime::fromString(date,"yyyyMMddHHmmss").toString("yyyyMMddHHmmss");
        if (normalizedDate.isNull()) normalizedDate=QStringLiteral(""); // 原sqlite3_bind_text写空TEXT，不是SQL NULL。
        copied.insert("calibration_datetime", normalizedDate);
        for (const QString &key : {QStringLiteral("point_size"),QStringLiteral("repeat_number"),QStringLiteral("is_checked"),QStringLiteral("is_edited"),QStringLiteral("visible")})
        {
            int value = 0;
            if (!sourceInt(original.value(key), &value, message)) return false;
            copied.insert(key, (key=="point_size" || key=="repeat_number") ? value : int(value!=0));
        }
        if (!budget(plan,copied,message)) return false;
        plan->bases << copied;
        const QStringList pointTexts={"sample_rate","concentration","result_flag","result_value","only_count"};
        QSqlQuery points(db);
        if (!query(&points, "SELECT "+textSelection(pointTexts)+" FROM copy_cal.calibration_data WHERE calibration_id=?", {oldId}, message)) return false;
        while (points.next())
        {
            if (plan->points.size() >= MaximumPoints) return fail(message, QStringLiteral("原校准点超过4096项，未截断复制。"));
            if (!exactSourceText(points,pointTexts,message)) return false;
            const QVariantMap originalPoint = row(points);
            int index = 0;
            if (!sourceInt(originalPoint.value("point_index"), &index, message)) return false;
            QVariantMap point{{"calibration_id",newCurve},{"point_index",index}};
            for (const QString &key : {QStringLiteral("sample_rate"),QStringLiteral("concentration"),QStringLiteral("result_flag"),QStringLiteral("result_value"),QStringLiteral("only_count")})
            {
                QString value;
                if (!sourceText(originalPoint.value(key), &value, message)) return false;
                point.insert(key, value);
            }
            if (!budget(plan,point,message)) return false;
            plan->points << point;
        }
        if (points.lastError().isValid()) return fail(message, QStringLiteral("原校准点没有完整读完。"));
    }
    return true;
}

void digestPart(QCryptographicHash &digest, const QByteArray &value)
{
    digest.addData(QByteArray::number(value.size()));
    digest.addData(":",1);
    digest.addData(value);
}

QString projectDigest(QSqlDatabase &db, QString *message)
{
    QCryptographicHash digest(QCryptographicHash::Sha256);
    QSqlQuery objects(db);
    if (!objects.exec(QStringLiteral("SELECT type,name,tbl_name,sql FROM main.sqlite_master WHERE name NOT LIKE 'sqlite_autoindex%' ORDER BY type,name")))
    { fail(message, QStringLiteral("项目结构核对失败。")); return {}; }
    QStringList tables;
    int objectCount = 0;
    while (objects.next())
    {
        if (++objectCount > 256) { fail(message,QStringLiteral("项目结构超过复制核对范围。")); return {}; }
        for (int n=0;n<4;++n) digestPart(digest,objects.value(n).toString().toUtf8());
        if (objects.value(0).toString()=="trigger") { fail(message,QStringLiteral("项目带自定义触发器，未猜复制副作用。")); return {}; }
        if (objects.value(0).toString()=="table")
        {
            const QString definition=objects.value(3).toString();
            if (!definition.startsWith("CREATE TABLE",Qt::CaseInsensitive) || definition.contains("WITHOUT ROWID",Qt::CaseInsensitive))
            { fail(message,QStringLiteral("项目含未支持的非普通行表。")); return {}; }
            tables << objects.value(1).toString();
        }
    }
    if (objects.lastError().isValid() || tables.size()>64) { fail(message,QStringLiteral("项目结构读取失败或超过64表。")); return {}; }
    qint64 bytes=0;
    for (const QString &table : tables)
    {
        QSqlQuery columns(db);
        if (!columns.exec("SELECT * FROM main."+identifier(table)+" LIMIT 0")) { fail(message,QStringLiteral("项目列无法核对。")); return {}; }
        const QSqlRecord record=columns.record();
        if (record.count()>256) { fail(message,QStringLiteral("项目列超过256项。")); return {}; }
        QStringList fields;
        for (int n=0;n<record.count();++n)
        {
            const QString field=identifier(record.fieldName(n));
            fields << "typeof("+field+")" << field << "CAST("+field+" AS BLOB)";
            digestPart(digest,record.fieldName(n).toUtf8());
        }
        QSqlQuery rows(db);
        if (!rows.exec("SELECT rowid,"+fields.join(',')+" FROM main."+identifier(table)+" ORDER BY rowid"))
        { fail(message,QStringLiteral("项目行身份无法核对。")); return {}; }
        int count=0;
        while (rows.next())
        {
            if (++count>4096) { fail(message,QStringLiteral("项目单表超过4096行。")); return {}; }
            digestPart(digest,QByteArray::number(rows.value(0).toLongLong()));
            for (int n=0;n<record.count();++n)
            {
                const QString type=rows.value(1+n*3).toString();
                QByteArray value;
                if (type=="integer") value=QByteArray::number(rows.value(2+n*3).toLongLong());
                else if (type=="real")
                {
                    const double number=rows.value(2+n*3).toDouble();
                    value=QByteArray(reinterpret_cast<const char *>(&number),sizeof(number));
                }
                else if (type=="text" || type=="blob") value=rows.value(3+n*3).toByteArray();
                else if (type!="null") { fail(message,QStringLiteral("项目字段类型无法核对。")); return {}; }
                bytes+=value.size();
                if (bytes>64*1024*1024) { fail(message,QStringLiteral("项目内容超过64MiB核对范围。")); return {}; }
                digestPart(digest,type.toUtf8());digestPart(digest,value);
            }
        }
        if (rows.lastError().isValid()) { fail(message,QStringLiteral("项目行读取未完成。")); return {}; }
    }
    for (const QString &key : {QStringLiteral("user_version"),QStringLiteral("application_id"),QStringLiteral("encoding")})
    {
        QSqlQuery value(db);
        if (!value.exec("PRAGMA main."+key) || !value.next()) { fail(message,QStringLiteral("项目标记读取失败。")); return {}; }
        digestPart(digest,key.toUtf8());digestPart(digest,value.value(0).toString().toUtf8());
    }
    return QString::fromLatin1(digest.result().toHex());
}

bool changeProject(QSqlDatabase &db, const QString &sourceName, const QString &newName, qint64 newId, QString *message)
{
    const QVariantMap base{{"xuhao",QString::number(newId)},{"project_en_name",newName},{"fixed",QStringLiteral("0")},{"is_related",QStringLiteral("0")}};
    QSqlQuery baseRows(db);
    if (!baseRows.exec("SELECT rowid,name,value FROM main.project_base ORDER BY rowid"))
        return fail(message,QStringLiteral("原项目基本参数无法读取。"));
    QList<QPair<qint64,QString>> baseChanges;
    QStringList required;
    while (baseRows.next())
    {
        if (baseChanges.size()>=4096) return fail(message,QStringLiteral("原项目基本参数超过复制范围。"));
        QString value;
        if (!sourceText(baseRows.value(2),&value,message)) return false;
        const QString key=baseRows.value(1).toString();
        if (base.contains(key)) {value=base.value(key).toString();required<<key;}
        baseChanges << qMakePair(baseRows.value(0).toLongLong(),value);
    }
    if (baseRows.lastError().isValid() || required.size()!=4 || required.removeDuplicates()!=0)
        return fail(message,QStringLiteral("原项目复制四键缺失、重复或未读完。"));
    baseRows.finish();
    // 原代码按QString重写全部基本参数；NULL文字也会写成空TEXT。
    for (const auto &change:baseChanges)
        if (!execute(db,"UPDATE main.project_base SET value=? WHERE rowid=?",{change.second,change.first},message,1)) return false;
    QSqlQuery results(db);
    if (!results.exec("SELECT rowid,* FROM main.project_results ORDER BY result_id,rowid"))
        return fail(message,QStringLiteral("原结果名称无法读取。"));
    const QSqlRecord fields=results.record();
    QStringList assignments;
    for (int n=1;n<fields.count();++n)
        if (fields.fieldName(n)!="result_id") assignments << identifier(fields.fieldName(n))+"=?";
    QList<QVariantList> changes;
    while (results.next())
    {
        if (changes.size()>=4096) return fail(message,QStringLiteral("项目结果行超出复制范围。"));
        QVariantList change;
        for (int n=1;n<fields.count();++n)
        {
            if (fields.fieldName(n)=="result_id") continue;
            QString value;
            if (!sourceText(results.value(n),&value,message)) return false;
            if (fields.fieldName(n)=="result_name") value.replace(sourceName,newName);
            change << value;
        }
        change << results.value(0).toLongLong();
        changes << change;
    }
    if (results.lastError().isValid()) return fail(message,QStringLiteral("项目结果名称读取未完成。"));
    results.finish();
    for (const auto &change:changes)
        if (!execute(db,"UPDATE main.project_results SET "+assignments.join(',')+" WHERE rowid=?",change,message,1)) return false;
    return true;
}

bool identity(const QString &path, QJsonObject *result, QString *message, bool owned = true)
{
    if (!regular(path,false,message)) return false;
#if defined(Q_OS_LINUX)
    const QByteArray directory=QFile::encodeName(QFileInfo(path).absolutePath());
    const QByteArray name=QFile::encodeName(QFileInfo(path).fileName());
    const int dir=::open(directory.constData(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (dir<0) return fail(message,QStringLiteral("无法核对复制目标目录身份。"));
    const int file=::openat(dir,name.constData(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    struct stat parent,actual,named,pathParent;
    const bool ok=file>=0 && ::fstat(dir,&parent)==0 && ::fstat(file,&actual)==0 &&
        ::fstatat(dir,name.constData(),&named,AT_SYMLINK_NOFOLLOW)==0 &&
        ::lstat(directory.constData(),&pathParent)==0 && S_ISDIR(pathParent.st_mode) &&
        parent.st_dev==pathParent.st_dev && parent.st_ino==pathParent.st_ino &&
        S_ISREG(actual.st_mode) && actual.st_nlink==1 && (!owned || actual.st_uid==::geteuid()) &&
        actual.st_dev==named.st_dev && actual.st_ino==named.st_ino;
    if (file>=0) ::close(file);
    ::close(dir);
    if (!ok) return fail(message,QStringLiteral("复制目标不是本用户创建的同一普通文件，保留现场待核。"));
    *result={{"directory_device",QString::number(qulonglong(parent.st_dev))},{"directory_inode",QString::number(qulonglong(parent.st_ino))},
             {"device",QString::number(qulonglong(actual.st_dev))},{"inode",QString::number(qulonglong(actual.st_ino))},
             {"owner",QString::number(qulonglong(actual.st_uid))}};
    return true;
#else
    Q_UNUSED(path);Q_UNUSED(result);
    return fail(message,QStringLiteral("本平台尚无本次跨库复制的文件身份适配，未降低证明条件。"));
#endif
}

bool sameIdentity(const QString &path, const QJsonObject &expected, QString *message, bool owned = true)
{
    QJsonObject actual;
    return !expected.isEmpty() && identity(path,&actual,message,owned) && actual==expected;
}

bool identityDocument(const QJsonObject &identity)
{
    const QStringList keys={"device","directory_device","directory_inode","inode","owner"};
    if (identity.keys()!=keys) return false;
    const QRegularExpression number(QStringLiteral("^(0|[1-9][0-9]*)$"));
    for (const QString &key:keys)
        if (!identity.value(key).isString() || !number.match(identity.value(key).toString()).hasMatch()) return false;
    return true;
}

bool projectIdentity(QSqlDatabase &db, qint64 id, const QString &name, QString *message)
{
    QSqlQuery fields(db);
    if (!fields.exec("SELECT name,value FROM main.project_base WHERE name IN ('xuhao','project_en_name')")) return false;
    QMap<QString,QString> values;
    while (fields.next())
    {
        if (values.contains(fields.value(0).toString())) return false;
        values.insert(fields.value(0).toString(),fields.value(1).toString());
    }
    return (!fields.lastError().isValid() && values.size()==2 && values.value("xuhao")==QString::number(id) && values.value("project_en_name")==name) ||
           fail(message,QStringLiteral("复制项目内部编号/名称与清单身份不符，保留待核。"));
}

bool syncDirectory(const QString &path, QString *message)
{
#if defined(Q_OS_LINUX)
    const int directory=::open(QFile::encodeName(path).constData(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (directory<0) return fail(message,QStringLiteral("复制恢复目录无法同步，未报收尾完成。"));
    const bool ok=::fsync(directory)==0;
    ::close(directory);
    return ok || fail(message,QStringLiteral("复制恢复目录落盘未确认，保留结果待核。"));
#else
    Q_UNUSED(path);
    return fail(message,QStringLiteral("本平台尚无复制恢复目录同步适配。"));
#endif
}

bool syncNewFile(const QString &path, QString *message)
{
#if defined(Q_OS_LINUX)
    const int file=::open(QFile::encodeName(path).constData(),O_RDWR|O_CLOEXEC|O_NOFOLLOW);
    if (file<0) return fail(message,QStringLiteral("本次克隆无法同步，未开始关联提交。"));
    const bool ok=::fsync(file)==0;
    ::close(file);
    return (ok && syncDirectory(QFileInfo(path).absolutePath(),message)) ||
           fail(message,QStringLiteral("本次克隆落盘未确认，保留清单待核。"));
#else
    Q_UNUSED(path);
    return fail(message,QStringLiteral("本平台尚无复制文件同步适配。"));
#endif
}

bool syncMarkerFile(const QString &path, QString *message)
{
    QJsonObject expected;
    if (!identity(path,&expected,message)) return false;
#if defined(Q_OS_LINUX)
    const int file=::open(QFile::encodeName(path).constData(),O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    struct stat actual;
    const bool same=file>=0 && ::fstat(file,&actual)==0 && S_ISREG(actual.st_mode) && actual.st_nlink==1 &&
        actual.st_uid==::geteuid() && expected.value("device").toString()==QString::number(qulonglong(actual.st_dev)) &&
        expected.value("inode").toString()==QString::number(qulonglong(actual.st_ino)) &&
        expected.value("owner").toString()==QString::number(qulonglong(actual.st_uid));
    const bool synced=same && ::fsync(file)==0;
    const bool closed=file<0 || ::close(file)==0;
    if (!synced || !closed)
        return fail(message,QStringLiteral("复制清单同一自有文件的fsync未确认，保留状态待核。"));
    return sameIdentity(path,expected,message) && syncDirectory(QFileInfo(path).absolutePath(),message);
#else
    return fail(message,QStringLiteral("本平台尚无复制清单文件同步适配，未降低持久化条件。"));
#endif
}

bool removeMarker(const QString &path, QString *message)
{
    return (QFile::remove(path) && syncDirectory(QFileInfo(path).absolutePath(),message)) ||
           fail(message,QStringLiteral("复制恢复清单清除未确认，请重新读取核对。"));
}

bool writeMarker(const QString &path, const QJsonObject &marker, QString *message)
{
    const QByteArray bytes=QJsonDocument(marker).toJson(QJsonDocument::Compact);
    if (bytes.size()>MaximumMarkerBytes) return fail(message,QStringLiteral("复制恢复清单超过8MiB，未继续提交。"));
    QSaveFile file(path);file.setDirectWriteFallback(false);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner) || file.write(bytes)!=bytes.size() || !file.commit())
        return fail(message,QStringLiteral("复制恢复清单无法原子保存，未报完成。"));
    return syncMarkerFile(path,message);
}

QJsonArray jsonRows(const QList<QVariantMap> &rows)
{
    QJsonArray result;
    for (const QVariantMap &row:rows) result.append(QJsonObject::fromVariantMap(row));
    return result;
}

bool insert(QSqlDatabase &db, const QString &table, QVariantMap *values, QString *message)
{
    QStringList columns,marks;QVariantList arguments;
    for (auto item=values->cbegin();item!=values->cend();++item)
    {columns<<identifier(item.key());marks<<"?";arguments<<item.value();}
    QSqlQuery command(db);
    if (!query(&command,"INSERT INTO "+table+"("+columns.join(',')+") VALUES("+marks.join(',')+")",arguments,message) || command.numRowsAffected()!=1)
        return fail(message,QStringLiteral("校准或批组复制插入未完成。"));
    const qint64 id=command.lastInsertId().toLongLong();
    if (id<=0 || id>std::numeric_limits<int>::max())
        return fail(message,QStringLiteral("复制自增行号超过原DAL的32位范围，未继续提交。"));
    values->insert("id",id);
    return true;
}

bool insertPlan(QSqlDatabase &db, Plan *plan, QString *message)
{
    for (int n=0;n<plan->bindings.size();++n)
    {
        if (!insert(db,"copy_reagent.reagent_batch_group",&plan->groups[n],message) ||
            !insert(db,"copy_cal.calibration_batch_group",&plan->bindings[n],message) ||
            !insert(db,"copy_cal.calibration_base",&plan->bases[n],message)) return false;
        const int id=plan->bases[n].value("calibration_id").toInt();
        for (auto &point:plan->points)
            if (point.value("calibration_id").toInt()==id && !insert(db,"copy_cal.calibration_data",&point,message)) return false;
    }
    return execute(db,"UPDATE copy_cal.calibraiton_total_count SET total_count=? WHERE id=? AND total_count=?",
                   {plan->counterAfter,plan->counterRow,plan->counterBefore},message,1);
}

QStringList markerNames(const QString &root)
{
    return QDir(QDir(root).filePath("project")).entryList({MarkerPrefix+"*.json"},QDir::Files|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDir::Name);
}

bool noNewReferences(QSqlDatabase &db, const QJsonObject &marker, QString *message, const QString &calibrationSchema = QStringLiteral("copy_cal"))
{
    if (!absentName(db,marker.value("new_name").toString(),message,calibrationSchema)) return false;
    for (const QJsonValue &id:marker.value("planned_curves").toArray())
        for (const QString &table : {QStringLiteral("calibration_base"),QStringLiteral("calibration_batch_group"),QStringLiteral("calibration_data")})
        {
            QSqlQuery present(db);
            if (!query(&present,"SELECT 1 FROM "+calibrationSchema+"."+table+" WHERE calibration_id=? LIMIT 1",{id.toInt()},message) || present.next())
                return fail(message,QStringLiteral("本次预留校准号仍有资料，未判作未提交克隆。"));
        }
    return true;
}

bool committedRows(QSqlDatabase &db, const QString &table, const QJsonArray &expected, const QString &name, QString *message)
{
    for (const QJsonValue &entry:expected)
    {
        const QVariantMap wanted=entry.toObject().toVariantMap();
        QSqlQuery current(db);
        if (!query(&current,"SELECT * FROM "+table+" WHERE id=?",{wanted.value("id")},message) || !current.next()) return false;
        const QVariantMap actual=row(current);
        if (current.next() || actual.keys()!=wanted.keys()) return false;
        for (auto field=wanted.cbegin();field!=wanted.cend();++field)
            if (actual.value(field.key()).isNull() || actual.value(field.key()).toString()!=field.value().toString()) return false;
    }
    if (!name.isEmpty())
    {
        QSqlQuery count(db);
        if (!query(&count,"SELECT count(*) FROM "+table+" WHERE project_name=?",{name},message) || !count.next() || count.value(0).toInt()!=expected.size()) return false;
    }
    return true;
}

bool fullCommit(QSqlDatabase &db, const QJsonObject &marker, QString *message)
{
    if (marker.value("phase").toString()!="ready" ||
        marker.value("bases").toArray().isEmpty() ||
        marker.value("bases").toArray().size()!=marker.value("bindings").toArray().size() ||
        marker.value("bases").toArray().size()!=marker.value("groups").toArray().size() ||
        projectDigest(db,message)!=marker.value("final_project_digest").toString()) return false;
    const QString name=marker.value("new_name").toString();
    if (!committedRows(db,"copy_cal.calibration_base",marker.value("bases").toArray(),name,message) ||
        !committedRows(db,"copy_cal.calibration_batch_group",marker.value("bindings").toArray(),name,message) ||
        !committedRows(db,"copy_reagent.reagent_batch_group",marker.value("groups").toArray(),name,message) ||
        !committedRows(db,"copy_cal.calibration_data",marker.value("points").toArray(),QString(),message)) return false;
    const QJsonArray bases=marker.value("bases").toArray();
    QJsonArray curves;
    for (const QJsonValue &base:bases) curves.append(base.toObject().value("calibration_id"));
    if (curves!=marker.value("planned_curves").toArray()) return false;
    const QJsonArray points=marker.value("points").toArray();
    for (const QJsonValue &base:bases)
    {
        const int id=base.toObject().value("calibration_id").toInt();
        int expected=0;
        for (const QJsonValue &point:points)
            if (point.toObject().value("calibration_id").toInt()==id) ++expected;
        QSqlQuery count(db);
        if (!query(&count,"SELECT count(*) FROM copy_cal.calibration_data WHERE calibration_id=?",{id},message) ||
            !count.next() || count.value(0).toInt()!=expected) return false;
    }
    QSqlQuery count(db);
    return query(&count,"SELECT total_count FROM copy_cal.calibraiton_total_count WHERE id=?",{marker.value("counter_row").toString()},message) &&
           count.next() && count.value(0).toLongLong()>=marker.value("counter_after").toInt() && !count.next();
}

QJsonArray uncommittedChecks()
{
    return {"sqlite-recovery-finished","owned-initial-clone","old-project-identity",
            "source-identity-and-version","no-new-name-references","planned-curve-ids-empty"};
}

QString uncommittedEvidence(QJsonObject marker, const QString &originalPhase)
{
    marker.remove("uncommitted_proof");
    marker.insert("phase",originalPhase);
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(marker).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}

bool validUncommittedProof(const QJsonObject &marker, QString *message)
{
    const QJsonObject proof=marker.value("uncommitted_proof").toObject();
    const QStringList keys={"checked_at_utc","evidence_sha256","format","from_phase","verified_checks"};
    const QString originalPhase=proof.value("from_phase").toString();
    const QString checked=proof.value("checked_at_utc").toString();
    if (proof.keys()!=keys || proof.value("format").toString()!="sn100-uncommitted-copy-proof-v1" ||
        (originalPhase!="prepared" && originalPhase!="ready") ||
        !checked.endsWith('Z') || !QDateTime::fromString(checked,Qt::ISODateWithMs).isValid() ||
        proof.value("verified_checks").toArray()!=uncommittedChecks() ||
        proof.value("evidence_sha256").toString()!=uncommittedEvidence(marker,originalPhase))
        return fail(message,QStringLiteral("未提交证明与本次源/目标完整清单不符，保留现场待核。"));
    return true;
}

QJsonObject provenUncommitted(QJsonObject marker)
{
    const QString originalPhase=marker.value("phase").toString();
    const QJsonObject proof{{"format","sn100-uncommitted-copy-proof-v1"},{"from_phase",originalPhase},
        {"checked_at_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"evidence_sha256",uncommittedEvidence(marker,originalPhase)},{"verified_checks",uncommittedChecks()}};
    marker.insert("phase","uncommitted-proven");
    marker.insert("uncommitted_proof",proof);
    return marker;
}

bool markerMatches(const QString &path, const QJsonObject &expected, QString *message)
{
    QJsonObject before;
    if (!identity(path,&before,message)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size()>MaximumMarkerBytes ||
        file.readAll()!=QJsonDocument(expected).toJson(QJsonDocument::Compact) || !sameIdentity(path,before,message))
        return fail(message,QStringLiteral("未提交证明落盘后内容或文件身份不符，未排除待核文件。"));
    return true;
}

bool recoverOne(const QString &root, const QString &markerName, QString *message, bool *observedCommit = nullptr, QString *uncommittedFile = nullptr)
{
    if (observedCommit) *observedCommit=false;
    const QString markerPath=QDir(QDir(root).filePath("project")).filePath(markerName);
    if (!regular(markerPath,false,message) || QFileInfo(markerPath).size()>MaximumMarkerBytes) return false;
    QJsonObject markerIdentity;
    if (!identity(markerPath,&markerIdentity,message) ||
        (QFileInfo(markerPath).permissions() & (QFileDevice::ReadGroup|QFileDevice::WriteGroup|QFileDevice::ReadOther|QFileDevice::WriteOther)))
        return fail(message,QStringLiteral("复制清单不是本用户独占的普通文件，保留现场待核。"));
    QFile source(markerPath);
    if (!source.open(QIODevice::ReadOnly)) return fail(message,QStringLiteral("复制清单无法读取，未删除任何文件。"));
    QJsonParseError parse;
    const QJsonDocument document=QJsonDocument::fromJson(source.readAll(),&parse);
    source.close();
    const QJsonObject marker=document.object();
    if (!sameIdentity(markerPath,markerIdentity,message))
        return fail(message,QStringLiteral("复制清单读取期间被替换，保留现场待核。"));
    bool idOk=false,sourceIdOk=false;const qint64 id=marker.value("new_id").toString().toLongLong(&idOk);
    const qint64 sourceId=marker.value("source_id").toString().toLongLong(&sourceIdOk);
    const QString name=marker.value("new_name").toString();
    const QString targetName=QStringLiteral("%1 %2.db").arg(id).arg(name);
    const QRegularExpression digest(QStringLiteral("^[a-f0-9]{64}$"));
    const QRegularExpression markerPattern(QStringLiteral("^\\.sn100-project-copy-[a-f0-9-]{36}\\.json$"));
    const QString sourceName=marker.value("source_name").toString();
    const QString sourceFileName=marker.value("source_file").toString();
    if (parse.error!=QJsonParseError::NoError || !document.isObject() || !markerPattern.match(markerName).hasMatch() ||
        marker.value("format").toString()!="sn100-project-copy-v1" ||
        marker.value("root").toString()!=root || !idOk || id<=0 || id>1000000000 || !sourceIdOk || sourceId<=0 || sourceId==id ||
        name.isEmpty() || name.contains('/') || name.contains('\\') ||
        name.contains(QChar::Null) || sourceName.isEmpty() || sourceName.contains(QChar::Null) ||
        sourceFileName.isEmpty() || QFileInfo(sourceFileName).fileName()!=sourceFileName || !sourceFileName.endsWith(".db",Qt::CaseInsensitive) ||
        marker.value("target_name").toString()!=targetName ||
        (marker.value("phase").toString()!="creating" && marker.value("phase").toString()!="prepared" &&
         marker.value("phase").toString()!="ready" && marker.value("phase").toString()!="uncommitted-proven") ||
        !digest.match(marker.value("initial_project_sha256").toString()).hasMatch() ||
        !digest.match(marker.value("source_fingerprint").toString()).hasMatch() || !digest.match(marker.value("final_project_digest").toString()).hasMatch())
        return fail(message,QStringLiteral("复制清单身份无法证明，保留现场待核。"));
    const QJsonArray plannedCurves=marker.value("planned_curves").toArray();
    QSet<int> uniqueCurves;
    for (const QJsonValue &curve:plannedCurves)
    {
        const double number=curve.toDouble(-1);
        if (!curve.isDouble() || number<0 || number>std::numeric_limits<int>::max() || number!=int(number) || uniqueCurves.contains(int(number)))
            return fail(message,QStringLiteral("复制清单校准编号无法证明，保留现场待核。"));
        uniqueCurves.insert(int(number));
    }
    if (plannedCurves.isEmpty() || plannedCurves.size()>MaximumGroups)
        return fail(message,QStringLiteral("复制清单绑定数量无法证明，保留现场待核。"));
    for (const QString &key : {QStringLiteral("source_identity"),QStringLiteral("calibration_identity"),QStringLiteral("reagent_identity")})
        if (!identityDocument(marker.value(key).toObject()))
            return fail(message,QStringLiteral("复制清单缺少完整来源文件身份，保留现场待核。"));
    if (marker.value("phase").toString()!="creating" && !identityDocument(marker.value("target_identity").toObject()))
        return fail(message,QStringLiteral("复制清单缺少完整目标文件身份，保留现场待核。"));
    const bool storedProof=marker.value("phase").toString()=="uncommitted-proven";
    if (storedProof ? !validUncommittedProof(marker,message) : marker.contains("uncommitted_proof"))
        return fail(message,QStringLiteral("复制证明阶段或内容无法核对，保留现场待核。"));
    const QString sourceFile=QDir(QDir(root).filePath("project")).filePath(sourceFileName);
    if (!sameIdentity(QDir(root).filePath(CalibrationFile),marker.value("calibration_identity").toObject(),message,false) ||
        !sameIdentity(QDir(root).filePath(ReagentFile),marker.value("reagent_identity").toObject(),message,false))
        return fail(message,QStringLiteral("原关联库文件身份已变化，保留复制清单待核。"));
    const QString target=QDir(QDir(root).filePath("project")).filePath(targetName);
    if (!QFileInfo::exists(target))
    {
        if (storedProof) return fail(message,QStringLiteral("已证未提交的克隆当前缺失，证明不作为清理授权，保留清单待核。"));
        Database probe;
        if (!probe.open(QDir(root).filePath(CalibrationFile),true,message)) return false;
        if (!execute(probe.db,"ATTACH DATABASE ? AS copy_reagent",{QDir(root).filePath(ReagentFile)},message) ||
            !execute(probe.db,"BEGIN IMMEDIATE",{},message) || !noNewReferences(probe.db,marker,message,QStringLiteral("main")))
            return fail(message,QStringLiteral("复制目标缺失但关联状态不能证明，保留清单待核。"));
        probe.db.rollback();
        return removeMarker(markerPath,message);
    }
    if (!sameIdentity(target,marker.value("target_identity").toObject(),message))
        return fail(message,QStringLiteral("复制目标身份已变化，未自动删除或回滚。"));
    bool committed=false,uncommitted=false;
    {
        Database data;
        // 可写打开让SQLite先执行它自己的热日志恢复，之后才判断提交状态。
        if (!data.open(target,true,message) || !attach(data.db,root,message) || !recoverableModes(data.db,message) ||
            !execute(data.db,"BEGIN IMMEDIATE",{},message)) return false;
        QSqlQuery integrity(data.db);
        if (!integrity.exec("PRAGMA main.quick_check") || !integrity.next() || integrity.value(0).toString()!="ok")
        {data.db.rollback();return fail(message,QStringLiteral("待恢复复制文件的完整性无法证明。"));}
        committed=!storedProof && fullCommit(data.db,marker,message);
        if (!committed)
        {
            uncommitted=sameIdentity(target,marker.value("target_identity").toObject(),message) &&
                databaseFingerprint(target,message)==marker.value("initial_project_sha256").toString() &&
                projectIdentity(data.db,sourceId,sourceName,message);
            if (uncommitted && storedProof)
            {
                // 首次完整证明已持久绑定在清单；后来合法CAL复用旧预留号，不改写这项历史事实。
                // 仍核新项目名没有关联，不能把证明当成项目可写或关联可删的授权。
                uncommitted=absentName(data.db,name,message);
            }
            else if (uncommitted)
                uncommitted=(marker.value("phase").toString()=="prepared" || marker.value("phase").toString()=="ready") &&
                    sameIdentity(sourceFile,marker.value("source_identity").toObject(),message,false) &&
                    databaseFingerprint(sourceFile,message)==marker.value("source_fingerprint").toString() && noNewReferences(data.db,marker,message);
            if (uncommitted)
            {
                const QJsonObject proven=storedProof ? marker : provenUncommitted(marker);
                // 只在热日志已恢复、两把QLock及此SQLite事务仍持有时登记证明。
                if ((!storedProof && !writeMarker(markerPath,proven,message)) ||
                    (storedProof && !syncMarkerFile(markerPath,message)) || !markerMatches(markerPath,proven,message) ||
                    !sameIdentity(target,marker.value("target_identity").toObject(),message) ||
                    databaseFingerprint(target,message)!=marker.value("initial_project_sha256").toString())
                {data.db.rollback();return fail(message,QStringLiteral("未提交证明持久化或复核未确认，未排除待核文件。"));}
            }
        }
        if (!data.db.rollback()) return fail(message,QStringLiteral("复制核对事务无法结束，保留清单待核。"));
    }
    if (!sameIdentity(target,marker.value("target_identity").toObject(),message)) return false;
    if (committed)
    {
        if (observedCommit) *observedCommit=true;
        return removeMarker(markerPath,message);
    }
    if (uncommitted)
    {
        // SQLite锁不能阻止其他进程改名；按路径删除仍有身份核对后的竞争窗口。
        const QString explanation=QStringLiteral("已持久证实本次自有克隆未提交；原文件可只读使用，克隆及清单保留待核，未授权写入或清理。");
        if (uncommittedFile) {*uncommittedFile=targetName;if (message) *message=explanation;return true;}
        return fail(message,explanation);
    }
    return fail(message,QStringLiteral("项目复制处于无法证明的状态，保留项目和清单，未自动删改关联。"));
}
}

bool noPending(const QString &root, QString *message)
{
    return markerNames(root).isEmpty() || fail(message,QStringLiteral("有项目复制尚未完成核对，请重新读取原资料；未继续写入或当成普通项目。"));
}

bool recoverPending(const QString &root, QStringList *uncommittedFiles, QString *message)
{
    uncommittedFiles->clear();
    if (markerNames(root).isEmpty()) return true;
    // 多个工作线程会同时首次读项目；串行恢复，避免它们互抢本程序的QLock而误报启动失败。
    // QLock始终tryLock(0)，不持本mutex等待项目/试剂锁；写守卫只查pending，不调用恢复。
    static QMutex recoveryMutex;
    QMutexLocker serialized(&recoveryMutex);
    const QStringList markers=markerNames(root);
    if (markers.isEmpty()) return true;
    if (markers.size()>16) return fail(message,QStringLiteral("复制恢复清单过多，未批量处理。"));
    QLockFile project(QDir(root).filePath(".sn100-original-project-write.lock"));
    QLockFile reagent(QDir(root).filePath(".sn100-original-reagent-write.lock"));
    project.setStaleLockTime(0);reagent.setStaleLockTime(0);
    if (!project.tryLock(0) || !reagent.tryLock(0)) return fail(message,QStringLiteral("复制恢复所需资料正由其他操作占用，请稍后重读。"));
    for (const QString &marker:markers)
    {
        QString uncommitted;
        if (!recoverOne(root,marker,message,nullptr,&uncommitted)) return false;
        if (!uncommitted.isEmpty()) *uncommittedFiles << uncommitted;
    }
    return true;
}

bool hasCalibrationBindings(const QString &root, const QString &projectName, bool *found, QString *message)
{
    *found=false;
    Database source;
    if (!source.open(QDir(root).filePath(CalibrationFile),false,message)) return false;
    QSqlQuery rows(source.db);
    if (!query(&rows,"SELECT 1 FROM calibration_batch_group WHERE project_name=? LIMIT 1",{projectName},message)) return false;
    *found=rows.next();
    return !rows.lastError().isValid() || fail(message,QStringLiteral("原校准绑定读取未完成。"));
}

bool rewriteClone(const QString &path, const QString &sourceName, const QString &newName, qint64 newId, QString *message)
{
    Database data;
    if (!data.open(path,true,message) || !execute(data.db,"BEGIN IMMEDIATE",{},message)) return false;
    if (projectDigest(data.db,message).isEmpty() || !changeProject(data.db,sourceName,newName,newId,message))
    {data.db.rollback();return false;}
    return data.db.commit() || fail(message,QStringLiteral("项目克隆更新未提交，未发布。"));
}

bool publishLinked(const QString &root, const QString &sourceFile, const QString &sourceFingerprint,
                   const QString &stagedSource, const QString &sourceName, const QString &newName,
                   qint64 newId, bool *committed, QString *message)
{
    *committed=false;
    QLockFile reagent(QDir(root).filePath(".sn100-original-reagent-write.lock"));
    reagent.setStaleLockTime(0);
    if (!reagent.tryLock(0)) return fail(message,QStringLiteral("原试剂/校准资料正被维护，未开始关联复制。"));
    const QString targetName=QStringLiteral("%1 %2.db").arg(newId).arg(newName);
    const QString target=QDir(QDir(root).filePath("project")).filePath(targetName);
    if (QFileInfo::exists(target) || QFileInfo(target).isSymLink() || !noPending(root,message))
        return fail(message,QStringLiteral("复制目标已存在或有未核对复制，未覆盖。"));
    QString finalDigest,sourceId;
    {
        Database staged;
        if (!staged.open(stagedSource,true,message) || !execute(staged.db,"BEGIN IMMEDIATE",{},message)) return false;
        QSqlQuery source(staged.db);
        if (!source.exec("SELECT value FROM project_base WHERE name='xuhao'") || !source.next()) return fail(message,QStringLiteral("复制源编号未读到。"));
        sourceId=source.value(0).toString();
        if (source.next()) return fail(message,QStringLiteral("复制源编号不唯一。"));
        source.finish();
        if (!changeProject(staged.db,sourceName,newName,newId,message)) {staged.db.rollback();return false;}
        finalDigest=projectDigest(staged.db,message);
        if (!staged.db.rollback() || finalDigest.isEmpty()) return fail(message,QStringLiteral("复制预备核对未完成，未创建最终文件。"));
    }
    const QString initialFingerprint=databaseFingerprint(stagedSource,message);
    if (initialFingerprint.isEmpty()) return false;
    // 先核外部库的可恢复方式和来源，再创建有记录的最终文件。
    Plan prepared;
    {
        Database check;
        if (!check.open(stagedSource,true,message) || !attach(check.db,root,message) || !recoverableModes(check.db,message) ||
            !execute(check.db,"BEGIN IMMEDIATE",{},message)) return false;
        const bool ok=buildPlan(check.db,sourceName,newName,&prepared,message);
        check.db.rollback();
        if (!ok) return false;
    }
    const QString markerPath=QDir(QDir(root).filePath("project")).filePath(MarkerPrefix+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json");
    QJsonObject marker{{"format","sn100-project-copy-v1"},{"root",root},{"source_name",sourceName},{"source_id",sourceId},
        {"source_file",QFileInfo(sourceFile).fileName()},{"source_fingerprint",sourceFingerprint},
        {"new_id",QString::number(newId)},{"new_name",newName},{"target_name",targetName},
        {"initial_project_sha256",initialFingerprint},{"final_project_digest",finalDigest},{"phase","creating"}};
    QJsonArray plannedCurves;
    for (const QVariantMap &base:prepared.bases) plannedCurves.append(base.value("calibration_id").toInt());
    marker.insert("planned_curves",plannedCurves);
    QJsonObject sourceIdentity,calibrationIdentity,reagentIdentity;
    if (!identity(sourceFile,&sourceIdentity,message,false) ||
        !identity(QDir(root).filePath(CalibrationFile),&calibrationIdentity,message,false) ||
        !identity(QDir(root).filePath(ReagentFile),&reagentIdentity,message,false)) return false;
    marker.insert("source_identity",sourceIdentity);marker.insert("calibration_identity",calibrationIdentity);marker.insert("reagent_identity",reagentIdentity);
    if (!writeMarker(markerPath,marker,message)) return false;
    if (!QFile::copy(stagedSource,target))
        return fail(message,QStringLiteral("项目克隆未完成，清单已保留，请重新读取核对。"));
    QJsonObject targetIdentity;
    if (!identity(target,&targetIdentity,message) || databaseFingerprint(target,message)!=initialFingerprint || !syncNewFile(target,message))
        return fail(message,QStringLiteral("复制文件身份或内容不符，保留清单待核。"));
    marker.insert("target_identity",targetIdentity);marker.insert("phase","prepared");
    if (!writeMarker(markerPath,marker,message)) return false;
    bool transactionFailed=false;
    {
        Database data;
        if (!data.open(target,true,message) || !attach(data.db,root,message) || !recoverableModes(data.db,message) ||
            !execute(data.db,"BEGIN IMMEDIATE",{},message)) transactionFailed=true;
        else
        {
            Plan plan;
            if (!buildPlan(data.db,sourceName,newName,&plan,message) ||
                plan.counterRow!=prepared.counterRow || plan.counterBefore!=prepared.counterBefore || plan.counterAfter!=prepared.counterAfter ||
                !sameIdentity(sourceFile,sourceIdentity,message,false) ||
                !sameIdentity(QDir(root).filePath(CalibrationFile),calibrationIdentity,message,false) ||
                !sameIdentity(QDir(root).filePath(ReagentFile),reagentIdentity,message,false) ||
                databaseFingerprint(sourceFile,message)!=sourceFingerprint ||
                !changeProject(data.db,sourceName,newName,newId,message) || projectDigest(data.db,message)!=finalDigest ||
                !insertPlan(data.db,&plan,message)) transactionFailed=true;
            else
            {
                marker.insert("phase","ready");marker.insert("counter_row",QString::number(plan.counterRow));
                marker.insert("counter_after",plan.counterAfter);marker.insert("bases",jsonRows(plan.bases));
                marker.insert("points",jsonRows(plan.points));marker.insert("bindings",jsonRows(plan.bindings));marker.insert("groups",jsonRows(plan.groups));
                if (!sameIdentity(target,targetIdentity,message) || !writeMarker(markerPath,marker,message) ||
                    !sameIdentity(sourceFile,sourceIdentity,message,false) ||
                    !sameIdentity(QDir(root).filePath(CalibrationFile),calibrationIdentity,message,false) ||
                    !sameIdentity(QDir(root).filePath(ReagentFile),reagentIdentity,message,false) ||
                    databaseFingerprint(sourceFile,message)!=sourceFingerprint) transactionFailed=true;
                else if (!data.db.commit())
                {fail(message,QStringLiteral("多库复制提交未确认，已保留清单，未报回滚或成功。"));transactionFailed=true;}
                else *committed=true;
            }
            if (!*committed) data.db.rollback();
        }
    }
    if (transactionFailed)
    {
        const QString original=message ? *message : QString();
        QString recovery;
        // 当前调用者已持项目及试剂锁；只收尾这次清单，不再递归取锁。
        bool observedCommit=false;
        if (!recoverOne(root,QFileInfo(markerPath).fileName(),&recovery,&observedCommit))
        {
            if (observedCommit) *committed=true;
            return fail(message,original+QStringLiteral("；结果待核：")+recovery);
        }
        if (observedCommit)
        {
            *committed=true;
            return fail(message,original+QStringLiteral("；重读已证实项目及关联完整提交，未回滚，请重新读取。"));
        }
        return fail(message,original+QStringLiteral("；本次资料未留下半份关联，已核对收尾。"));
    }
    if (!recoverOne(root,QFileInfo(markerPath).fileName(),message))
        return fail(message,QStringLiteral("项目和关联已完整提交，但清单收尾未确认，请重新读取核对；未回滚已提交资料。"));
    return true;
}
}
