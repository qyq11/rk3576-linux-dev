#include "original_project_device_repository.h"
#include "original_files/original_business_database.h"
#include <QDir>
#include <QLockFile>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSet>
#include <cmath>

using OriginalProjectWire::Fields;
using OriginalBusiness::Database;
namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message=text;
    return false;
}
bool rawRows(Database &database, const QString &table, const QString &order, QList<Fields> *rows, QString *message)
{
    QSqlQuery query(database.db);
    if (!query.exec(QStringLiteral("SELECT * FROM %1 ORDER BY %2,rowid").arg(table,order))) return fail(message,query.lastError().text());
    const auto columns=query.record(); rows->clear();
    while (query.next())
    {
        if (rows->size()>=256) return fail(message,QStringLiteral("原项目步骤超过256行，未截断。"));
        Fields row;
        for (int i=0;i<columns.count();++i) row.insert(columns.fieldName(i),query.value(i).toString());
        rows->append(row);
    }
    return !query.lastError().isValid() || fail(message,query.lastError().text());
}
bool numericKey(const QString &value, QString *canonical)
{
    bool ok=false; const qlonglong number=value.toLongLong(&ok);
    if (!ok || number<0 || number>2147483647 || !QRegularExpression("^[0-9]+$").match(value).hasMatch()) return false;
    *canonical=QString::number(number); return true;
}
bool updateRows(Database &database, const QString &table, const QString &key, const QList<Fields> &incoming,
                bool removeMissing, QString *message)
{
    QSqlQuery schema(database.db);
    if (!schema.exec("SELECT rowid,* FROM "+table+" LIMIT 0")) return fail(message,schema.lastError().text());
    const QSqlRecord columns=schema.record();
    QSqlQuery existing(database.db);
    if (!existing.exec("SELECT rowid,"+key+" FROM "+table)) return fail(message,existing.lastError().text());
    QMap<QString,qint64> ids;
    while (existing.next())
    {
        QString id;
        if (!numericKey(existing.value(1).toString(),&id) || ids.contains(id)) return fail(message,QStringLiteral("原项目步骤或结果编号无效/重复，未选择其中一条。"));
        ids.insert(id,existing.value(0).toLongLong());
    }
    QSet<QString> received;
    for (const auto &row : incoming)
    {
        QString id;
        if (!numericKey(row.value(key),&id) || received.contains(id)) return fail(message,QStringLiteral("设备项目步骤或结果编号无效/重复。"));
        received.insert(id);
        QStringList assignments,names,marks; QVariantList values;
        for (auto it=row.cbegin();it!=row.cend();++it)
        {
            if (columns.indexOf(it.key())<0) return fail(message,QStringLiteral("原项目表%1缺少设备返回字段%2，未建列或丢弃参数。").arg(table,it.key()));
            const QString column='"'+it.key()+'"';
            assignments.append(column+"=?"); names.append(column); marks.append("?"); values.append(it.value());
        }
        if (ids.contains(id))
        {
            values.append(ids.value(id));
            if (!database.execute("UPDATE "+table+" SET "+assignments.join(',')+" WHERE rowid=?",values,message,1)) return false;
        }
        else
        {
            // 原结果表只更新已有result_id；未识别的新结果不能假装已经保存。
            if (!removeMissing) return fail(message,QStringLiteral("设备返回了原项目未配置的结果编号：")+id);
            if (!database.execute("INSERT INTO "+table+"("+names.join(',')+") VALUES("+marks.join(',')+")",values,message,1)) return false;
        }
    }
    if (removeMissing)
        for (auto it=ids.cbegin();it!=ids.cend();++it)
            if (!received.contains(it.key()) && !database.execute("DELETE FROM "+table+" WHERE rowid=?",{it.value()},message,1)) return false;
    return true;
}
bool equivalent(const QString &left, const QString &right)
{
    if (left==right) return true;
    bool leftOk=false,rightOk=false;
    const double a=left.toDouble(&leftOk),b=right.toDouble(&rightOk);
    return leftOk && rightOk && std::isfinite(a) && std::isfinite(b) && a==b;
}
bool compareRows(const QList<Fields> &expected, const QList<Fields> &actual, const QString &key, bool reagent, QString *message)
{
    if (expected.size()!=actual.size()) return fail(message,QStringLiteral("仪器返回的项目步骤数量与本次发送不同。"));
    for (const auto &row : expected)
    {
        const Fields *found=nullptr;
        for (const auto &candidate : actual) if (equivalent(candidate.value(key),row.value(key))) { found=&candidate; break; }
        if (!found) return fail(message,QStringLiteral("仪器项目缺少发送过的步骤/结果编号：")+row.value(key));
        for (auto it=row.cbegin();it!=row.cend();++it)
        {
            // 只比较双方同名且原Up实际发送的字段，设备额外字段随后按原UP规则保存。
            if (!found->contains(it.key()) || it.key()=="id" || it.key()=="after_clean_name" ||
                it.key()=="after_clean_amount_ul" || it.key()=="sample_add_length" || it.key()=="reagent_add_length") continue;
            QString value=it.value();
            if (reagent && it.key()=="needle_select") value=(value=="1" || value==QStringLiteral("左试剂针")) ? "1" : "2";
            if (!equivalent(value,found->value(it.key()))) return fail(message,QStringLiteral("仪器项目字段%1与本次发送值不同，尚未确认同步。").arg(it.key()));
        }
    }
    return true;
}
}

bool OriginalProjectDeviceRepository::initialize(const QString &root, QString *message)
{
    if (!OriginalBusiness::workingRoot(root,message)) return false;
    root_=root; return true;
}

bool OriginalProjectDeviceRepository::load(QList<OriginalDeviceProject> *output, QString *message)
{
    OriginalProjectRepository repository;
    ProjectRecords projects; ProjectUnits units; QStringList reagents,cleaners,diluents;
    if (!output || !repository.initialize(root_,QString(),message) || !repository.load(&projects,&units,&reagents,&cleaners,&diluents,message)) return false;
    QList<OriginalDeviceProject> result;
    for (const auto &project : projects)
    {
        OriginalDeviceProject value; value.record=project; value.wire.base=project.originalBaseValues;
        Database database;
        if (!database.open(root_,QStringLiteral("project/")+project.originalFileName,false,message) ||
            !rawRows(database,"project_sample_rate","xuhao",&value.wire.samples,message) ||
            !rawRows(database,"project_reagent","xuhao",&value.wire.reagents,message) ||
            !rawRows(database,"project_results","result_id",&value.wire.results,message)) return false;
        if (project.originalFingerprint!=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(QStringLiteral("project/")+project.originalFileName),message))
            return fail(message,QStringLiteral("读取项目完整设备参数时原库变化，请重新读取。"));
        result.append(value);
    }
    *output=result; return true;
}

bool OriginalProjectDeviceRepository::matchesSentParameters(const OriginalDeviceProject &sent,
    const OriginalProjectWire::Project &received, QString *message)
{
    if (sent.wire.base.value("project_en_name")!=received.base.value("project_en_name")) return fail(message,QStringLiteral("仪器返回了其他项目，未确认本次同步。"));
    const auto expected=OriginalProjectWire::parametersInUpdate(sent.wire);
    for (auto it=expected.base.cbegin();it!=expected.base.cend();++it)
    {
        if (!received.base.contains(it.key()) || it.key()=="change_time" || it.key()=="project_id" || it.key()=="xuhao" || it.key()=="collect_rate_ms") continue;
        if (!equivalent(it.value(),received.base.value(it.key()))) return fail(message,QStringLiteral("仪器项目基础参数%1与本次发送不同。").arg(it.key()));
    }
    return compareRows(expected.samples,received.samples,"xuhao",false,message) &&
           compareRows(expected.reagents,received.reagents,"xuhao",true,message) &&
           compareRows(expected.results,received.results,"result_id",false,message);
}

bool OriginalProjectDeviceRepository::applyDeviceProject(const QByteArray &payload, const QString &expectedProject,
    const QString &expectedFingerprint, QString *message)
{
    OriginalProjectWire::Project received;
    if (!OriginalProjectWire::parseDeviceProject(payload,&received,message)) return false;
    if (received.base.value("project_en_name")!=expectedProject) return fail(message,QStringLiteral("项目回包与本次请求名称不符，未写入其他项目。"));
    QLockFile guard(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    if (!guard.tryLock(0)) return fail(message,QStringLiteral("原项目正在编辑保存，本次设备返回未覆盖草稿，请稍后重读。"));
    QList<OriginalDeviceProject> projects;
    if (!load(&projects,message)) return false;
    const OriginalDeviceProject *target=nullptr;
    for (const auto &project : projects) if (project.record.name==expectedProject) { if (target) return fail(message,QStringLiteral("原库项目重名，未猜测目标文件。")); target=&project; }
    if (!target || target->record.originalFingerprint!=expectedFingerprint) return fail(message,QStringLiteral("请求后原项目已变化或不存在，未用迟到返回覆盖新配置。"));
    QString number;
    if (!numericKey(received.base.value("xuhao"),&number)) return fail(message,QStringLiteral("设备返回的项目排序编号无效。"));
    for (const auto &other : projects)
        if (other.record.name!=expectedProject && equivalent(other.wire.base.value("xuhao"),number)) return fail(message,QStringLiteral("设备项目编号与另一原项目重复，未改写。"));
    Database database;
    if (!database.open(root_,QStringLiteral("project/")+target->record.originalFileName,true,message) || !database.begin(message)) return false;
    if (target->record.originalFingerprint!=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(QStringLiteral("project/")+target->record.originalFileName),message))
        return fail(message,QStringLiteral("写入前原项目已变化，设备返回未覆盖较新配置。"));
    for (auto it=received.base.cbegin();it!=received.base.cend();++it)
    {
        QSqlQuery existing(database.db); existing.prepare("SELECT COUNT(*) FROM project_base WHERE name=?"); existing.addBindValue(it.key());
        if (!existing.exec() || !existing.next()) return fail(message,existing.lastError().text());
        const int count=existing.value(0).toInt();
        if (count>1) return fail(message,QStringLiteral("原项目基础参数重名，未覆盖多条记录。"));
        if (!database.execute(count ? "UPDATE project_base SET value=? WHERE name=?" : "INSERT INTO project_base(value,name) VALUES(?,?)",{it.value(),it.key()},message,1)) return false;
    }
    if (!updateRows(database,"project_sample_rate","xuhao",received.samples,true,message) ||
        !updateRows(database,"project_reagent","xuhao",received.reagents,true,message) ||
        !updateRows(database,"project_results","result_id",received.results,false,message)) return false;
    // 与原slotReceiveProjectInfo一致，这条返回不改AlgParam；所有未涉及的原键/列保留。
    return database.commit(message);
}
