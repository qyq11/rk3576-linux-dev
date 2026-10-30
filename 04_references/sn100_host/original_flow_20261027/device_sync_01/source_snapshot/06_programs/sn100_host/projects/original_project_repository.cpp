#include "original_project_repository.h"
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
struct OriginalRow
{
    qint64 rowid = 0;
    QMap<QString, QVariant> values;
};
using OriginalRows = QList<OriginalRow>;

bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}

bool plainName(const QString &value, int maximum = 64, bool allowEmpty = false)
{
    if ((!allowEmpty && value.isEmpty()) || value.size() > maximum || value != value.trimmed()) return false;
    for (const QChar character : value)
        if (character.isNull() || character.category() == QChar::Other_Control ||
            character == QLatin1Char('/') || character == QLatin1Char('\\')) return false;
    return true;
}

bool regularPath(const QString &path, bool directory, QString *message)
{
    QFileInfo info(path);
    if (!info.isAbsolute() || !info.exists() || (directory ? !info.isDir() : !info.isFile()))
        return fail(message, QStringLiteral("原资料路径不存在或类型不符。"));
    for (QFileInfo current(info.absoluteFilePath()); ; current.setFile(current.absolutePath()))
    {
        if (current.isSymLink()) return fail(message, QStringLiteral("原资料路径不能经过符号链接。"));
        if (current.absoluteFilePath() == current.absolutePath()) break;
    }
    return true;
}

QString fingerprint(const QString &path, QString *message)
{
    if (!regularPath(path, false, message)) return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    for (const QString &suffix : {QString(), QStringLiteral("-wal")})
    {
        const QString fileName = path + suffix;
        if (!QFileInfo::exists(fileName)) continue;
        if (!regularPath(fileName, false, message)) return {};
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly) || !digest.addData(&file))
        {
            fail(message, QStringLiteral("无法核对原数据库内容，请检查读取权限。"));
            return {};
        }
        digest.addData(suffix.toUtf8());
    }
    return QString::fromLatin1(digest.result().toHex());
}

QString unitVersion(const QString &root, const QString &fileFingerprint)
{
    if (fileFingerprint.isEmpty()) return {};
    // 两个目录可以有完全相同的库字节，旧目录的单位草稿仍不能写到新目录。
    QByteArray input = root.toUtf8();
    input.append('\0');
    input.append(fileFingerprint.toLatin1());
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

class Connection
{
public:
    QSqlDatabase db;
    QString name;
    ~Connection()
    {
        if (!name.isEmpty())
        {
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(name);
        }
    }
    bool open(const QString &path, bool writable, QString *message)
    {
        if (!regularPath(path, false, message)) return false;
        if (writable && !QFileInfo(path).isWritable())
            return fail(message, QStringLiteral("原数据库不可写；输入尚未保存，请解除只读状态后重试。"));
        name = QStringLiteral("OriginalProject-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(writable ? QStringLiteral("QSQLITE_BUSY_TIMEOUT=750") :
                                      QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=750"));
        if (!db.open()) return fail(message, QStringLiteral("原数据库打开失败：") + db.lastError().text());
        // 不更改原库日志模式，也不执行迁移/建表语句。
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("PRAGMA foreign_keys=ON")))
            return fail(message, QStringLiteral("原数据库引用保护无法启用。"));
        return true;
    }
};

bool sql(QSqlDatabase &db, const QString &statement, const QVariantList &arguments, QString *message)
{
    QSqlQuery query(db);
    if (!query.prepare(statement)) return fail(message, QStringLiteral("原数据库语句准备失败：") + query.lastError().text());
    for (const QVariant &argument : arguments) query.addBindValue(argument);
    if (!query.exec()) return fail(message, QStringLiteral("原数据库操作失败：") + query.lastError().text());
    return true;
}

bool rollback(QSqlDatabase &db, QString *message)
{
    if (!db.rollback())
        if (message) *message += QStringLiteral("；回滚未得到确认，请关闭后重新打开资料核对，不能推定已撤销。");
    return false;
}

bool readRows(QSqlDatabase &db, const QString &table, const QString &order, OriginalRows *rows, QString *message)
{
    rows->clear();
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT rowid,* FROM \"%1\"%2").arg(table, order)))
        return fail(message, QStringLiteral("原数据库缺少需要的表或字段：") + table);
    const QSqlRecord record = query.record();
    while (query.next())
    {
        if (rows->size() >= 4096) return fail(message, QStringLiteral("原项目参数行数超出本次维护上限，未截断或改写。"));
        OriginalRow row;
        row.rowid = query.value(0).toLongLong();
        for (int column = 1; column < record.count(); ++column)
            row.values.insert(record.fieldName(column), query.value(column));
        rows->append(row);
    }
    return !query.lastError().isValid() || fail(message, QStringLiteral("原项目参数读取未完成。"));
}

bool readPairs(QSqlDatabase &db, const QString &table, const QString &keyColumn,
               QMap<QString, QString> *values, QString *message)
{
    OriginalRows rows;
    if (!readRows(db, table, QStringLiteral(" ORDER BY rowid"), &rows, message)) return false;
    values->clear();
    for (const OriginalRow &row : rows)
    {
        const QString key = row.values.value(keyColumn).toString();
        if (key.isEmpty() || values->contains(key) || !row.values.contains(QStringLiteral("value")))
            return fail(message, QStringLiteral("原参数键为空或重复，未擅自选择其中一条。"));
        values->insert(key, row.values.value(QStringLiteral("value")).toString());
    }
    return true;
}

bool numeric(const QString &value, double *number)
{
    bool ok = false;
    *number = value.toDouble(&ok);
    return ok && std::isfinite(*number);
}

bool integer(const QString &value, qint64 *number)
{
    bool ok = false;
    *number = value.toLongLong(&ok);
    return ok;
}

bool switchValue(const QString &value, bool *result)
{
    if (value != QStringLiteral("0") && value != QStringLiteral("1")) return false;
    *result = value == QStringLiteral("1");
    return true;
}

bool schema(QSqlDatabase &db, QString *message)
{
    const QMap<QString, QStringList> required = {
        {QStringLiteral("project_base"), {QStringLiteral("name"), QStringLiteral("value")}},
        {QStringLiteral("AlgParam"), {QStringLiteral("key"), QStringLiteral("value")}},
        {QStringLiteral("project_sample_rate"), {QStringLiteral("xuhao"), QStringLiteral("sample_rate"), QStringLiteral("sample_default_option")}},
        {QStringLiteral("project_reagent"), {QStringLiteral("xuhao"), QStringLiteral("reagent_name")}},
        {QStringLiteral("project_results"), {QStringLiteral("result_id"), QStringLiteral("result_type"), QStringLiteral("unit_name")}},
        {QStringLiteral("project_mda"), {QStringLiteral("name"), QStringLiteral("rate_select")}},
        {QStringLiteral("project_quality"), {QStringLiteral("control_name"), QStringLiteral("sample_rate")}}};
    for (auto table = required.cbegin(); table != required.cend(); ++table)
    {
        if (!db.tables().contains(table.key())) return fail(message, QStringLiteral("文件不是完整的原项目数据库，缺少表：") + table.key());
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("PRAGMA table_info(\"%1\")").arg(table.key())))
            return fail(message, QStringLiteral("无法核对原项目表结构。"));
        QSet<QString> columns;
        while (query.next()) columns.insert(query.value(1).toString());
        for (const QString &column : table.value())
            if (!columns.contains(column)) return fail(message, QStringLiteral("原项目表缺少需要的字段：%1.%2").arg(table.key(), column));
    }
    return true;
}

QStringList resultTypes()
{
    return {QString(), QStringLiteral("Original"), QStringLiteral("Calibration"), QStringLiteral("Rate"),
            QStringLiteral("INR"), QStringLiteral("MDA")};
}

ProjectActionFields actionFields(const OriginalRow &row, bool sample)
{
    ProjectActionFields result;
    const QStringList columns = sample ? ProjectAdvancedEditor::sampleColumns() : ProjectAdvancedEditor::reagentColumns();
    for (const QString &column : columns)
        if (row.values.contains(column)) result.insert(column, row.values.value(column).toString());
    result.insert(QStringLiteral("_original_rowid"), QString::number(row.rowid));
    return result;
}

bool readProjectData(QSqlDatabase &db, ProjectRecord *project, QString *message)
{
    if (!schema(db, message) || !readPairs(db, QStringLiteral("project_base"), QStringLiteral("name"), &project->originalBaseValues, message) ||
        !readPairs(db, QStringLiteral("AlgParam"), QStringLiteral("key"), &project->originalAlgorithmValues, message)) return false;
    const auto &base = project->originalBaseValues;
    if (!integer(base.value(QStringLiteral("xuhao")), &project->id) || project->id <= 0 || project->id > 1000000000)
        return fail(message, QStringLiteral("原项目显示顺序编号无效，未根据文件名猜测。"));
    project->instrumentProjectId = base.value(QStringLiteral("project_id"));
    project->name = base.value(QStringLiteral("project_en_name"));
    project->fullName = base.value(QStringLiteral("project_cn_name"));
    project->lisCode = base.value(QStringLiteral("project_lis_code"));
    project->changedAt = base.value(QStringLiteral("change_time"));
    if (project->name.isEmpty() || !switchValue(base.value(QStringLiteral("is_used")), &project->enabled) ||
        !switchValue(base.value(QStringLiteral("fixed")), &project->fixed))
        return fail(message, QStringLiteral("原项目名称或状态缺失/无效，未静默补默认值。"));
    project->algorithm.configured = base.contains(QStringLiteral("xishu_a")) && base.contains(QStringLiteral("xishu_b")) &&
                                    base.contains(QStringLiteral("sample_warmup_length"));
    if (project->algorithm.configured)
    {
        if (!numeric(base.value(QStringLiteral("xishu_a")), &project->algorithm.coefficientA) ||
            !numeric(base.value(QStringLiteral("xishu_b")), &project->algorithm.coefficientB) ||
            !numeric(base.value(QStringLiteral("sample_warmup_length")), &project->algorithm.coefficientC) ||
            !switchValue(base.value(QStringLiteral("auto_apply"), QStringLiteral("0")), &project->algorithm.autoApply))
            return fail(message, QStringLiteral("原系数不是可识别的有限数值，未转换成0。"));
    }
    project->advanced.configured = true;
    for (const QString &field : ProjectAdvancedEditor::scalarFields())
        if (base.contains(field)) project->advanced.fields.insert(field, base.value(field));
    OriginalRows rows;
    if (!readRows(db, QStringLiteral("project_sample_rate"), QStringLiteral(" ORDER BY xuhao,rowid"), &rows, message)) return false;
    for (const OriginalRow &row : rows)
    {
        ProjectSampleRate rate;
        rate.sampleRate = row.values.value(QStringLiteral("sample_rate")).toString();
        qint64 order = 0;
        if (!integer(row.values.value(QStringLiteral("xuhao")).toString(), &order) || order < 0 || order > std::numeric_limits<int>::max() ||
            !switchValue(row.values.value(QStringLiteral("sample_option")).toString(), &rate.sampleOption) ||
            !switchValue(row.values.value(QStringLiteral("sample_default_option")).toString(), &rate.defaultOption))
            return fail(message, QStringLiteral("原稀释方案状态或顺序无效，未自动重排或修正。"));
        rate.order = int(order);
        project->sampleRates.append(rate);
        project->advanced.samples.append(actionFields(row, true));
    }
    if (!readRows(db, QStringLiteral("project_reagent"), QStringLiteral(" ORDER BY xuhao,rowid"), &rows, message)) return false;
    for (const OriginalRow &row : rows) project->advanced.reagents.append(actionFields(row, false));
    if (!readRows(db, QStringLiteral("project_results"), QStringLiteral(" ORDER BY result_id,rowid"), &rows, message)) return false;
    QSet<QString> seen;
    for (const OriginalRow &row : rows)
    {
        ProjectResult result;
        result.type = row.values.value(QStringLiteral("result_type")).toString();
        result.name = row.values.value(QStringLiteral("result_name")).toString();
        result.unit = row.values.value(QStringLiteral("unit_name")).toString();
        result.lisCode = row.values.value(QStringLiteral("lis_code")).toString();
        qint64 id = 0, decimals = 0;
        if (seen.contains(result.type) || !integer(row.values.value(QStringLiteral("result_id")).toString(), &id) ||
            !integer(row.values.value(QStringLiteral("decimal_point")).toString(), &decimals) || decimals < 0 || decimals > 15 ||
            !numeric(row.values.value(QStringLiteral("test_low_range")).toString(), &result.testLow) ||
            !numeric(row.values.value(QStringLiteral("test_up_range")).toString(), &result.testHigh) ||
            !numeric(row.values.value(QStringLiteral("normal_low_range")).toString(), &result.normalLow) ||
            !numeric(row.values.value(QStringLiteral("normal_up_range")).toString(), &result.normalHigh) ||
            !switchValue(row.values.value(QStringLiteral("is_used")).toString(), &result.used) ||
            !switchValue(row.values.value(QStringLiteral("display")).toString(), &result.display) ||
            !switchValue(row.values.value(QStringLiteral("main_result")).toString(), &result.mainResult))
            return fail(message, QStringLiteral("原结果参数存在不可识别或重复的值，未丢弃该行。"));
        seen.insert(result.type);
        result.decimals = int(decimals);
        result.sourceResultId = int(id);
        result.selectionKnown = id > 0 && id < resultTypes().size() && resultTypes().value(int(id)) == result.type;
        project->results.append(result);
    }
    return true;
}

bool sameResult(const ProjectResult &left, const ProjectResult &right)
{
    return left.type == right.type && left.name == right.name && left.decimals == right.decimals && left.testLow == right.testLow &&
        left.testHigh == right.testHigh && left.normalLow == right.normalLow && left.normalHigh == right.normalHigh &&
        left.lisCode == right.lisCode && left.unit == right.unit && left.mainResult == right.mainResult &&
        left.selectionKnown == right.selectionKnown && left.sourceResultId == right.sourceResultId &&
        left.used == right.used && left.display == right.display;
}

bool sameRates(const ProjectSampleRates &left, const ProjectSampleRates &right)
{
    if (left.size() != right.size()) return false;
    for (int index = 0; index < left.size(); ++index)
        if (left[index].sampleRate != right[index].sampleRate || left[index].sampleOption != right[index].sampleOption ||
            left[index].defaultOption != right[index].defaultOption || left[index].order != right[index].order) return false;
    return true;
}

bool sameAlgorithm(const ProjectAlgorithm &left, const ProjectAlgorithm &right)
{
    return left.configured == right.configured && left.coefficientA == right.coefficientA && left.coefficientB == right.coefficientB &&
        left.coefficientC == right.coefficientC && left.autoApply == right.autoApply;
}

bool updateColumns(QSqlDatabase &db, const QString &table, const OriginalRow &original,
                   const QMap<QString, QVariant> &requested, bool *changed, QString *message)
{
    QStringList set;
    QVariantList arguments;
    for (auto field = requested.cbegin(); field != requested.cend(); ++field)
    {
        if (!original.values.contains(field.key())) return fail(message, QStringLiteral("原表缺少待保存字段，未添加新字段。"));
        if (original.values.value(field.key()).toString() == field.value().toString()) continue;
        set.append(QStringLiteral("\"%1\"=?").arg(field.key()));
        arguments.append(field.value());
    }
    if (set.isEmpty()) return true;
    arguments.append(original.rowid);
    if (!sql(db, QStringLiteral("UPDATE \"%1\" SET %2 WHERE rowid=?").arg(table, set.join(QLatin1Char(','))), arguments, message)) return false;
    *changed = true;
    return true;
}

bool updatePair(QSqlDatabase &db, const QString &table, const QString &keyColumn, const QString &key,
                const QString &value, bool *changed, QString *message)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT value FROM \"%1\" WHERE \"%2\"=?").arg(table, keyColumn));
    query.addBindValue(key);
    if (!query.exec() || !query.next()) return fail(message, QStringLiteral("原库没有待编辑参数，未自动添加：") + key);
    const QString current = query.value(0).toString();
    if (query.next()) return fail(message, QStringLiteral("原参数键重复，未选择其中一条覆盖。"));
    if (current == value) return true;
    if (!sql(db, QStringLiteral("UPDATE \"%1\" SET value=? WHERE \"%2\"=?").arg(table, keyColumn), {value, key}, message)) return false;
    *changed = true;
    return true;
}

bool snapshotDatabase(const QString &source, const QString &destination, QString *message)
{
    if (QFileInfo::exists(destination)) return fail(message, QStringLiteral("导出目标已存在，未覆盖。"));
    Connection connection;
    if (!connection.open(source, false, message)) return false;
    // VACUUM INTO从一致的只读快照生成新DB，不复制仍在变化的主文件。
    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("VACUUM main INTO ?"));
    query.addBindValue(destination);
    if (!query.exec()) return fail(message, QStringLiteral("原数据库快照未完成：") + query.lastError().text());
    QFile::setPermissions(destination, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    Connection checked;
    if (!checked.open(destination, false, message)) return false;
    QSqlQuery integrity(checked.db);
    if (!integrity.exec(QStringLiteral("PRAGMA quick_check")) || !integrity.next() || integrity.value(0).toString() != QStringLiteral("ok"))
        return fail(message, QStringLiteral("导出数据库完整性核对未通过。"));
    return true;
}

bool hasValue(const QString &file, const QString &table, const QString &column, const QString &value,
              bool *exists, QString *message)
{
    *exists = false;
    if (!QFileInfo::exists(file)) return true;
    Connection connection;
    if (!connection.open(file, false, message)) return false;
    if (!connection.db.tables().contains(table)) return true;
    QSqlQuery query(connection.db);
    query.prepare(QStringLiteral("SELECT 1 FROM \"%1\" WHERE \"%2\"=? LIMIT 1").arg(table, column));
    query.addBindValue(value);
    if (!query.exec()) return fail(message, QStringLiteral("关联资料结构无法核对，未放行修改或删除。"));
    *exists = query.next();
    return true;
}
}

void OriginalProjectRepository::close()
{
    m_root.clear();
    m_referenceCatalogDirectory.clear();
    m_files.clear();
}

QString OriginalProjectRepository::unitsDatabasePath() const
{
    return QDir(m_root).filePath(QStringLiteral("db/systemInfo/option_Unit.db"));
}

bool OriginalProjectRepository::checkRoot(QString *message) const
{
    if (m_root.isEmpty() || !regularPath(m_root, true, message)) return false;
    const QString lower = m_root.toLower();
    if (lower.contains(QStringLiteral("sn100_host_reference")) || lower.split(QLatin1Char('/')).contains(QStringLiteral("baseline")))
        return fail(message, QStringLiteral("参考原件或只读基准目录不能作为维护工作副本。"));
    QFile marker(QDir(m_root).filePath(QStringLiteral(".sn100-original-work-v1")));
    if (QFileInfo(marker.fileName()).isSymLink() || !marker.open(QIODevice::ReadOnly) ||
        marker.readAll() != QByteArray("sn100-original-work-v1\n"))
        return fail(message, QStringLiteral("请选择已明确部署的原库工作副本；没有写许可标记，未自动创建或改库。"));
    return regularPath(QDir(m_root).filePath(QStringLiteral("project")), true, message);
}

bool OriginalProjectRepository::initialize(QString root, QString referenceCatalogDirectory, QString *message)
{
    close();
    root = QDir::cleanPath(root);
    if (!QDir::isAbsolutePath(root)) return fail(message, QStringLiteral("原库根目录必须是绝对路径。"));
    // 先核对输入路径；先取真实路径会抹掉用户所选路径中的符号链接。
    if (!regularPath(root, true, message)) return false;
    m_root = QFileInfo(root).canonicalFilePath();
    m_referenceCatalogDirectory = referenceCatalogDirectory;
    ProjectRecords projects;
    ProjectUnits units;
    if (!checkRoot(message) || !readUnits(&units, message) || !readProjects(&projects, message))
    {
        close();
        return false;
    }
    return true;
}

bool OriginalProjectRepository::readProject(QString filePath, ProjectRecord *project, QString *message) const
{
    *project = ProjectRecord();
    const QString before = fingerprint(filePath, message);
    if (before.isEmpty()) return false;
    Connection connection;
    if (!connection.open(filePath, false, message) || !connection.db.transaction())
        return fail(message, QStringLiteral("原项目读取快照无法开始。"));
    if (!readProjectData(connection.db, project, message)) return false;
    if (!connection.db.commit()) return fail(message, QStringLiteral("原项目读取快照未完成。"));
    const QString after = fingerprint(filePath, message);
    if (after.isEmpty() || before != after) return fail(message, QStringLiteral("读取期间原项目发生改变，请重新读取。"));
    project->originalFileName = QFileInfo(filePath).fileName();
    project->originalRoot = m_root;
    project->originalFingerprint = after;
    return true;
}

bool OriginalProjectRepository::readProjects(ProjectRecords *projects, QString *message)
{
    projects->clear();
    m_files.clear();
    if (!checkRoot(message)) return false;
    const QDir directory(QDir(m_root).filePath(QStringLiteral("project")));
    const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
    QSet<QString> names;
    for (const QFileInfo &entry : entries)
    {
        if (!entry.fileName().endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)) continue;
        if (projects->size() >= 1024) return fail(message, QStringLiteral("原项目文件超过1024份，未截断列表。"));
        ProjectRecord project;
        if (!readProject(entry.absoluteFilePath(), &project, message)) return false;
        if (m_files.contains(project.id) || names.contains(project.name.toCaseFolded()))
            return fail(message, QStringLiteral("原项目编号或名称重复，未根据文件名或顺序合并。"));
        m_files.insert(project.id, entry.absoluteFilePath());
        names.insert(project.name.toCaseFolded());
        projects->append(project);
    }
    std::sort(projects->begin(), projects->end(), [](const ProjectRecord &a, const ProjectRecord &b) { return a.id < b.id; });
    return true;
}

bool OriginalProjectRepository::readUnits(ProjectUnits *units, QString *message)
{
    units->clear();
    const QString before = fingerprint(unitsDatabasePath(), message);
    if (before.isEmpty()) return false;
    Connection connection;
    if (!connection.open(unitsDatabasePath(), false, message) || !connection.db.transaction())
        return fail(message, QStringLiteral("原单位读取快照无法开始。"));
    QSqlQuery query(connection.db);
    if (!query.exec(QStringLiteral("SELECT unit,type,ratio FROM option_unit ORDER BY id,rowid")))
        return fail(message, QStringLiteral("原单位库结构不匹配，未转换或建新表。"));
    QSet<QString> names;
    while (query.next())
    {
        ProjectUnit unit;
        unit.name = query.value(0).toString();
        unit.originalType = query.value(1).toString();
        if (names.contains(unit.name) || !numeric(query.value(2).toString(), &unit.ratio))
            return fail(message, QStringLiteral("原单位名称重复或倍率不是有限数值，未默认为1。"));
        names.insert(unit.name);
        unit.originalFingerprint = unitVersion(m_root, before);
        units->append(unit);
    }
    if (query.lastError().isValid() || !connection.db.commit())
        return fail(message, QStringLiteral("原单位读取快照未完成。"));
    if (fingerprint(unitsDatabasePath(), message) != before)
        return fail(message, QStringLiteral("读取期间原单位发生变化，请重新读取。"));
    return true;
}

bool OriginalProjectRepository::load(ProjectRecords *projects, ProjectUnits *units, QStringList *reagents,
                                     QStringList *cleaners, QStringList *diluents, QString *message)
{
    reagents->clear(); cleaners->clear(); diluents->clear();
    if (!readProjects(projects, message) || !readUnits(units, message)) return false;
    Connection connection;
    const QString path = QDir(m_root).filePath(QStringLiteral("db/userSet/reagent_Information.db"));
    if (!connection.open(path, false, message)) return false;
    QSqlQuery query(connection.db);
    if (!query.exec(QStringLiteral("SELECT reagent_name,reagent_type FROM reagent_list WHERE valid=1 ORDER BY id")))
        return fail(message, QStringLiteral("原试剂目录结构不匹配。"));
    while (query.next())
    {
        const QString name = query.value(0).toString();
        const QString type = query.value(1).toString();
        if (type == QStringLiteral("试剂") || type == QStringLiteral("r_reagent")) reagents->append(name);
        else if (type == QStringLiteral("清洗液") || type == QStringLiteral("cleaner")) cleaners->append(name);
        else if (type == QStringLiteral("缓冲稀释液") || type == QStringLiteral("s_ibs")) diluents->append(name);
    }
    return true;
}

QString OriginalProjectRepository::validate(const ProjectRecord &project, const ProjectUnits &units)
{
    Q_UNUSED(units)
    if (!plainName(project.name) || !plainName(project.fullName, 64, true) || project.lisCode.size() > 64 || project.lisCode.contains(QChar::Null))
        return QStringLiteral("项目名称/全称或传输代码无效，不能含首尾空白、控制或路径字符。");
    if (project.id <= 0 || project.originalFileName.isEmpty() || project.originalFingerprint.size() != 64)
        return QStringLiteral("原项目来源或读取版本缺失，请重新读取后编辑。");
    if (project.results.isEmpty() || project.results.size() > 64 || project.sampleRates.size() > 64 || project.advanced.reagents.size() > 64)
        return QStringLiteral("原项目结果/动作行数无效或超出维护上限，未截断保存。");
    if (project.algorithm.configured)
        for (double number : {project.algorithm.coefficientA, project.algorithm.coefficientB, project.algorithm.coefficientC})
            if (!std::isfinite(number)) return QStringLiteral("算法系数不是有限数值，未转换成0。");
    return {};
}

QString OriginalProjectRepository::databaseFingerprint(QString path, QString *message)
{
    return fingerprint(path, message);
}

namespace
{
bool updateActionRows(QSqlDatabase &db, const QString &table, const QList<ProjectActionFields> &requested,
                      const ProjectSampleRates &rates, bool sample, bool *changed, QString *message)
{
    OriginalRows originals;
    if (!readRows(db, table, QStringLiteral(" ORDER BY xuhao,rowid"), &originals, message)) return false;
    QMap<qint64, OriginalRow> byId;
    for (const auto &row : originals) byId.insert(row.rowid, row);
    QSet<qint64> retained;
    const QStringList columns = sample ? ProjectAdvancedEditor::sampleColumns() : ProjectAdvancedEditor::reagentColumns();
    for (int index = 0; index < requested.size(); ++index)
    {
        const auto &input = requested[index];
        QMap<QString, QVariant> changes;
        changes.insert(QStringLiteral("xuhao"), index + 1);
        for (const QString &column : columns)
            if (input.contains(column)) changes.insert(column, input.value(column));
        if (sample)
        {
            changes.insert(QStringLiteral("sample_rate"), rates[index].sampleRate);
            changes.insert(QStringLiteral("sample_option"), rates[index].sampleOption ? QStringLiteral("1") : QStringLiteral("0"));
            changes.insert(QStringLiteral("sample_default_option"), rates[index].defaultOption ? QStringLiteral("1") : QStringLiteral("0"));
        }
        qint64 rowid = 0;
        const QString sourceId = input.value(QStringLiteral("_original_rowid"));
        if (!sourceId.isEmpty())
        {
            if (!integer(sourceId, &rowid) || !byId.contains(rowid) || retained.contains(rowid))
                return fail(message, QStringLiteral("动作行的原始身份失效或重复，请重新读取；未按位置猜测覆盖。"));
            retained.insert(rowid);
            if (!updateColumns(db, table, byId.value(rowid), changes, changed, message)) return false;
        }
        else
        {
            QStringList names, placeholders;
            QVariantList values;
            for (auto field = changes.cbegin(); field != changes.cend(); ++field)
            {
                names.append(QStringLiteral("\"%1\"").arg(field.key())); placeholders.append(QStringLiteral("?")); values.append(field.value());
            }
            if (!sql(db, QStringLiteral("INSERT INTO \"%1\"(%2) VALUES(%3)").arg(table, names.join(QLatin1Char(',')), placeholders.join(QLatin1Char(','))), values, message)) return false;
            *changed = true;
        }
    }
    for (const auto &row : originals)
        if (!retained.contains(row.rowid))
        {
            if (!sql(db, QStringLiteral("DELETE FROM \"%1\" WHERE rowid=?").arg(table), {row.rowid}, message)) return false;
            *changed = true;
        }
    return true;
}

bool validChangedActions(const ProjectRecord &project, const ProjectRecord &before, QString *message)
{
    if (project.sampleRates.size() != project.advanced.samples.size())
        return fail(message, QStringLiteral("原稀释方案与动作参数必须逐行对应。"));
    int defaults = 0;
    QSet<QString> names;
    QStringList labels;
    for (int index = 0; index < project.sampleRates.size(); ++index)
    {
        const auto &rate = project.sampleRates[index];
        if (rate.sampleRate.isEmpty() || rate.sampleRate != rate.sampleRate.trimmed() || rate.sampleRate.size() > 64 ||
            rate.sampleRate.contains(QChar::Null) || rate.sampleRate.contains(QLatin1Char('\\')) || names.contains(rate.sampleRate))
            return fail(message, QStringLiteral("稀释方案名称为空、重复或含不支持字符。"));
        for (QChar character : rate.sampleRate)
            if (character.category() == QChar::Other_Control) return fail(message, QStringLiteral("稀释方案名称不能含控制字符。"));
        names.insert(rate.sampleRate); labels.append(rate.sampleRate);
        if (rate.defaultOption)
        {
            ++defaults;
            if (!sameRates(project.sampleRates, before.sampleRates) && !rate.sampleOption)
                return fail(message, QStringLiteral("修改后的默认方案须明确选用。"));
        }
        if (project.advanced.samples[index].value(QStringLiteral("sample_rate")) != rate.sampleRate ||
            project.advanced.samples[index].value(QStringLiteral("sample_option")) != (rate.sampleOption ? QStringLiteral("1") : QStringLiteral("0")))
            return fail(message, QStringLiteral("原稀释方案的名称或选用状态与动作行不一致。"));
    }
    if (!sameRates(project.sampleRates, before.sampleRates) && !project.sampleRates.isEmpty() && defaults != 1)
        return fail(message, QStringLiteral("修改稀释方案后须明确选择唯一默认方案。"));
    ProjectAdvanced checked = project.advanced;
    // 未动的动作行只保留，不把新界面的校验规则追溯施加给旧资料。
    if (project.advanced.samples == before.advanced.samples) checked.samples.clear();
    if (project.advanced.reagents == before.advanced.reagents) checked.reagents.clear();
    for (auto &row : checked.samples) row.remove(QStringLiteral("_original_rowid"));
    for (auto &row : checked.reagents) row.remove(QStringLiteral("_original_rowid"));
    const QString error = ProjectAdvancedEditor::validate(checked, labels, true);
    return error.isEmpty() || fail(message, error);
}

bool referenceChoices(const QString &root, const ProjectAdvanced &advanced, QString *message)
{
    const QString file = QDir(root).filePath(QStringLiteral("db/userSet/reagent_Information.db"));
    Connection connection;
    if (!connection.open(file, false, message)) return false;
    QList<QPair<QString, QStringList>> required;
    if (advanced.fields.contains(QStringLiteral("IBS_name")))
        required.append(qMakePair(advanced.fields.value(QStringLiteral("IBS_name")), QStringList{QStringLiteral("缓冲稀释液"), QStringLiteral("s_ibs")}));
    for (const auto &row : advanced.samples)
        required.append(qMakePair(row.value(QStringLiteral("before_clean_name")), QStringList{QStringLiteral("清洗液"), QStringLiteral("cleaner")}));
    for (const auto &row : advanced.reagents)
    {
        required.append(qMakePair(row.value(QStringLiteral("reagent_name")), QStringList{QStringLiteral("试剂"), QStringLiteral("r_reagent")}));
        required.append(qMakePair(row.value(QStringLiteral("before_clean_name")), QStringList{QStringLiteral("清洗液"), QStringLiteral("cleaner")}));
    }
    for (const auto &item : required)
    {
        if (item.first.isEmpty() || item.first == QStringLiteral("none")) continue;
        QSqlQuery query(connection.db);
        query.prepare(QStringLiteral("SELECT reagent_type FROM reagent_list WHERE reagent_name=? AND valid=1"));
        query.addBindValue(item.first);
        if (!query.exec() || !query.next() || !item.second.contains(query.value(0).toString()))
            return fail(message, QStringLiteral("修改后的动作引用不在原有效试剂/清洗液/稀释液目录中，请核对原资料。"));
    }
    return true;
}
}

bool OriginalProjectRepository::save(ProjectRecord project, qint64 *savedId, bool *changed, QString *message)
{
    *changed = false; *savedId = 0;
    if (!checkRoot(message)) return false;
    if (project.originalRoot != m_root) return fail(message, QStringLiteral("项目草稿来自其他原库目录，请重新读取。"));
    QLockFile guard(QDir(m_root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    guard.setStaleLockTime(0);
    if (!guard.tryLock(0)) return fail(message, QStringLiteral("原库正在处理另一个维护操作，请稍后重试。"));
    ProjectRecords all;
    ProjectUnits units;
    if (!readProjects(&all, message) || !readUnits(&units, message)) return false;
    const QString general = validate(project, units);
    if (!general.isEmpty()) return fail(message, general);
    const auto found = std::find_if(all.cbegin(), all.cend(), [&](const ProjectRecord &value) { return value.id == project.id; });
    if (found == all.cend() || found->originalFileName != project.originalFileName || found->originalFingerprint != project.originalFingerprint)
        return fail(message, QStringLiteral("原项目文件已经变化或不再存在；旧草稿没有覆盖新内容，请重新读取。"));
    const ProjectRecord before = *found;
    if (project.instrumentProjectId != before.instrumentProjectId || project.fixed != before.fixed ||
        project.originalBaseValues != before.originalBaseValues || project.originalAlgorithmValues != before.originalAlgorithmValues)
        return fail(message, QStringLiteral("原编号、固定状态或只读原参数被改变，未保存隐藏修改。"));
    for (const auto &other : all)
        if (other.id != project.id && other.name.compare(project.name, Qt::CaseInsensitive) == 0)
            return fail(message, QStringLiteral("项目名称已存在，没有覆盖其他项目。"));
    if (project.name != before.name)
    {
        QStringList used;
        if (!references(project.id, &used, message)) return false;
        if (!used.isEmpty()) return fail(message, QStringLiteral("原项目仍被其他资料引用，不能改名。请先核对关联资料。"));
    }
    const bool algorithmChanged = !sameAlgorithm(project.algorithm, before.algorithm);
    if (algorithmChanged)
    {
        if (!project.algorithm.configured) return fail(message, QStringLiteral("原系数须完整填写，不能把已有原参数清空为未配置。"));
        const QList<QPair<double, double>> coefficients = {{project.algorithm.coefficientA, before.algorithm.coefficientA},
            {project.algorithm.coefficientB, before.algorithm.coefficientB}, {project.algorithm.coefficientC, before.algorithm.coefficientC}};
        for (const auto &coefficient : coefficients)
            if (coefficient.first != coefficient.second && (!std::isfinite(coefficient.first) || coefficient.first < -9999 || coefficient.first > 9999 ||
                std::abs(coefficient.first * 10000 - std::round(coefficient.first * 10000)) > .000001))
                return fail(message, QStringLiteral("系数A、B、C须在-9999至9999之间，最多四位小数。"));
    }
    const bool sampleChanged = !sameRates(project.sampleRates, before.sampleRates) || project.advanced.samples != before.advanced.samples;
    const bool reagentChanged = project.advanced.reagents != before.advanced.reagents;
    const bool scalarChanged = project.advanced.fields != before.advanced.fields;
    if (sampleChanged || reagentChanged || scalarChanged)
    {
        // 只改系数/名称时保留旧方案，包括旧库已有但未选用的默认，不替用户修正。
        if (!validChangedActions(project, before, message)) return false;
        ProjectAdvanced edited = project.advanced;
        if (!sampleChanged) edited.samples.clear();
        if (!reagentChanged) edited.reagents.clear();
        if (edited.fields.value(QStringLiteral("IBS_name")) == before.advanced.fields.value(QStringLiteral("IBS_name")))
            edited.fields.remove(QStringLiteral("IBS_name"));
        if (!referenceChoices(m_root, edited, message)) return false;
    }
    if (project.results.size() != before.results.size()) return fail(message, QStringLiteral("原结果行不能被截断或新增；保留全部原结果类型。"));
    bool resultsChanged = false;
    bool mainSelectionChanged = false;
    int mainCount = 0;
    for (int index = 0; index < project.results.size(); ++index)
    {
        const auto &result = project.results[index];
        const auto &old = before.results[index];
        if (result.mainResult) ++mainCount;
        mainSelectionChanged |= result.mainResult != old.mainResult;
        if (sameResult(result, old)) continue;
        resultsChanged = true;
        if (result.sourceResultId != old.sourceResultId || result.type != old.type || !result.selectionKnown ||
            result.sourceResultId < 1 || result.sourceResultId > 4)
            return fail(message, QStringLiteral("未开放或未知的原结果行须原样保留，不能借保存替换。"));
        if ((result.name != old.name && !plainName(result.name, 64)) ||
            (result.lisCode != old.lisCode && (result.lisCode.size() > 64 || result.lisCode.contains(QChar::Null))) ||
            (result.decimals != old.decimals && (result.decimals < 1 || result.decimals > 6)))
            return fail(message, QStringLiteral("修改后的结果名称、代码或小数位无效。"));
        if (result.unit != old.unit)
        {
            bool exists = false;
            for (const auto &unit : units) if (unit.name == result.unit) exists = true;
            if (!exists) return fail(message, QStringLiteral("修改后的单位不在原单位字典中；原有无量纲或内置单位只能原样保留。"));
        }
        const QList<QPair<double, double>> ranges = {{result.testLow, old.testLow}, {result.testHigh, old.testHigh},
            {result.normalLow, old.normalLow}, {result.normalHigh, old.normalHigh}};
        for (const auto &range : ranges)
            if (range.first != range.second && (!std::isfinite(range.first) || range.first < 0 || range.first > 999))
                return fail(message, QStringLiteral("修改后的结果范围须为0至999的有限数值。"));
        if (((result.testLow != old.testLow || result.testHigh != old.testHigh) && result.testLow > result.testHigh) ||
            ((result.normalLow != old.normalLow || result.normalHigh != old.normalHigh) && result.normalLow > result.normalHigh))
            return fail(message, QStringLiteral("修改后的结果下限不能大于上限。"));
    }
    if (mainSelectionChanged && mainCount != 1) return fail(message, QStringLiteral("修改主结果选择后须保留唯一主结果。"));
    Connection connection;
    const QString path = m_files.value(project.id);
    if (!connection.open(path, true, message) || !sql(connection.db, QStringLiteral("BEGIN IMMEDIATE"), {}, message)) return false;
    if (fingerprint(path, message) != project.originalFingerprint)
    {
        fail(message, QStringLiteral("取得写入权限时原项目已变化，请重新读取。"));
        return rollback(connection.db, message);
    }
    QMap<QString, QString> base;
    if (project.name != before.name) base.insert(QStringLiteral("project_en_name"), project.name);
    if (project.fullName != before.fullName) base.insert(QStringLiteral("project_cn_name"), project.fullName);
    if (project.lisCode != before.lisCode) base.insert(QStringLiteral("project_lis_code"), project.lisCode);
    if (project.enabled != before.enabled) base.insert(QStringLiteral("is_used"), project.enabled ? QStringLiteral("1") : QStringLiteral("0"));
    if (algorithmChanged)
    {
        if (project.algorithm.coefficientA != before.algorithm.coefficientA) base.insert(QStringLiteral("xishu_a"), QString::number(project.algorithm.coefficientA, 'g', 15));
        if (project.algorithm.coefficientB != before.algorithm.coefficientB) base.insert(QStringLiteral("xishu_b"), QString::number(project.algorithm.coefficientB, 'g', 15));
        if (project.algorithm.coefficientC != before.algorithm.coefficientC) base.insert(QStringLiteral("sample_warmup_length"), QString::number(project.algorithm.coefficientC, 'g', 15));
        if (project.algorithm.autoApply != before.algorithm.autoApply) base.insert(QStringLiteral("auto_apply"), project.algorithm.autoApply ? QStringLiteral("1") : QStringLiteral("0"));
    }
    if (scalarChanged)
    {
        if (project.advanced.fields.keys() != before.advanced.fields.keys())
        {
            fail(message, QStringLiteral("原基础参数字段不能被删除或隐式添加。"));
            return rollback(connection.db, message);
        }
        for (auto field = project.advanced.fields.cbegin(); field != project.advanced.fields.cend(); ++field)
            if (field.value() != before.advanced.fields.value(field.key())) base.insert(field.key(), field.value());
    }
    for (auto field = base.cbegin(); field != base.cend(); ++field)
        if (!updatePair(connection.db, QStringLiteral("project_base"), QStringLiteral("name"), field.key(), field.value(), changed, message))
            return rollback(connection.db, message);
    const QMap<QString, QString> linked = {{QStringLiteral("min_test_time"), QStringLiteral("Sort_startTime")},
                                          {QStringLiteral("stopTime"), QStringLiteral("Sort_stopTime")}};
    for (auto key = linked.cbegin(); key != linked.cend(); ++key)
        if (base.contains(key.key()) && before.originalAlgorithmValues.contains(key.value()))
            if (!updatePair(connection.db, QStringLiteral("AlgParam"), QStringLiteral("key"), key.value(), base.value(key.key()), changed, message))
                return rollback(connection.db, message);
    if (sampleChanged && !updateActionRows(connection.db, QStringLiteral("project_sample_rate"), project.advanced.samples, project.sampleRates, true, changed, message))
        return rollback(connection.db, message);
    if (reagentChanged && !updateActionRows(connection.db, QStringLiteral("project_reagent"), project.advanced.reagents, {}, false, changed, message))
        return rollback(connection.db, message);
    if (resultsChanged)
    {
        OriginalRows rows;
        if (!readRows(connection.db, QStringLiteral("project_results"), QStringLiteral(" ORDER BY result_id,rowid"), &rows, message)) return rollback(connection.db, message);
        for (int index = 0; index < project.results.size(); ++index)
        {
            const auto &result = project.results[index]; const auto &old = before.results[index];
            if (sameResult(result, old)) continue;
            QMap<QString, QVariant> fields;
            if (result.name != old.name) fields.insert(QStringLiteral("result_name"), result.name);
            if (result.unit != old.unit) fields.insert(QStringLiteral("unit_name"), result.unit);
            if (result.decimals != old.decimals) fields.insert(QStringLiteral("decimal_point"), QString::number(result.decimals));
            if (result.testLow != old.testLow) fields.insert(QStringLiteral("test_low_range"), QString::number(result.testLow, 'g', 15));
            if (result.testHigh != old.testHigh) fields.insert(QStringLiteral("test_up_range"), QString::number(result.testHigh, 'g', 15));
            if (result.normalLow != old.normalLow) fields.insert(QStringLiteral("normal_low_range"), QString::number(result.normalLow, 'g', 15));
            if (result.normalHigh != old.normalHigh) fields.insert(QStringLiteral("normal_up_range"), QString::number(result.normalHigh, 'g', 15));
            if (result.lisCode != old.lisCode) fields.insert(QStringLiteral("lis_code"), result.lisCode);
            if (result.used != old.used) fields.insert(QStringLiteral("is_used"), result.used ? QStringLiteral("1") : QStringLiteral("0"));
            if (result.display != old.display) fields.insert(QStringLiteral("display"), result.display ? QStringLiteral("1") : QStringLiteral("0"));
            if (result.mainResult != old.mainResult) fields.insert(QStringLiteral("main_result"), result.mainResult ? QStringLiteral("1") : QStringLiteral("0"));
            if (!updateColumns(connection.db, QStringLiteral("project_results"), rows[index], fields, changed, message)) return rollback(connection.db, message);
        }
    }
    if (*changed && before.originalBaseValues.contains(QStringLiteral("change_time")))
        if (!updatePair(connection.db, QStringLiteral("project_base"), QStringLiteral("name"), QStringLiteral("change_time"), QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")), changed, message))
            return rollback(connection.db, message);
    if (!connection.db.commit())
    {
        fail(message, QStringLiteral("原项目提交失败：") + connection.db.lastError().text());
        return rollback(connection.db, message);
    }
    *savedId = project.id;
    return true;
}

namespace
{
bool requiredReference(const QString &root, const QString &relative, const QString &table,
                       const QString &column, const QString &value, bool *found, QString *message)
{
    const QString file = QDir(root).filePath(relative);
    if (!QFileInfo::exists(file))
        return fail(message, QStringLiteral("缺少关联核对库 %1，不能证明改名/删除/复制安全；配置快照不含全部历史关联。").arg(relative));
    Connection connection;
    if (!connection.open(file, false, message)) return false;
    if (!connection.db.tables().contains(table))
        return fail(message, QStringLiteral("关联资料缺少原表 %1，未放行操作。").arg(table));
    QSqlQuery query(connection.db);
    if (!query.prepare(QStringLiteral("SELECT 1 FROM \"%1\" WHERE \"%2\"=? LIMIT 1").arg(table, column)))
        return fail(message, QStringLiteral("关联核对字段不匹配，未放行操作。"));
    query.addBindValue(value);
    if (!query.exec()) return fail(message, QStringLiteral("关联资料无法读取，未放行操作。"));
    *found = query.next();
    return !query.lastError().isValid() || fail(message, QStringLiteral("关联核对未完成。"));
}

QStringList reagentDatabases()
{
    return {QStringLiteral("db/userSet/reagent_Information.db"),
            QStringLiteral("db/userSet/reagent_Information_hongkui.db"),
            QStringLiteral("db/userSet/reagent_Information_liangchen.db"),
            QStringLiteral("db/userSet/reagent_Information_seerain.db")};
}

bool externalCopyReferences(const QString &root, const QString &name, QString *message)
{
    bool used = false;
    for (const QString &table : {QStringLiteral("calibration_base"), QStringLiteral("calibration_batch_group")})
    {
        if (!requiredReference(root, QStringLiteral("result/calibration.db"), table, QStringLiteral("project_name"), name, &used, message)) return false;
        if (used) return fail(message, QStringLiteral("该项目有外部校准资料（result/calibration.db/%1）；本次复制尚不支持跨库复制校准曲线，未创建任何项目。").arg(table));
    }
    for (const QString &file : reagentDatabases())
    {
        if (!requiredReference(root, file, QStringLiteral("reagent_batch_group"), QStringLiteral("project_name"), name, &used, message)) return false;
        if (used) return fail(message, QStringLiteral("该项目有外部试剂批组（%1/reagent_batch_group）；本次复制尚不支持跨库关联复制，未创建任何项目。").arg(file));
    }
    return true;
}

bool cleanSidecars(const QString &path, QString *message)
{
    for (const QString &suffix : {QStringLiteral("-wal"), QStringLiteral("-journal")})
        if (QFileInfo(path + suffix).exists())
            return fail(message, QStringLiteral("原数据库还有写入日志，请关闭其他写入程序并重新读取后操作。"));
    return true;
}

bool allowedUnitName(const QString &value)
{
    if (value.isEmpty() || value != value.trimmed() || value.size() > 64) return false;
    for (QChar character : value)
        if (character.isNull() || character.category() == QChar::Other_Control || character == QLatin1Char('\\')) return false;
    return true;
}

bool validateUnit(const ProjectUnit &unit, const ProjectUnits &units, QString *message)
{
    if (!allowedUnitName(unit.name) || !std::isfinite(unit.ratio) || unit.ratio <= 0)
        return fail(message, QStringLiteral("单位须为1至64字且倍率为正的有限数值；不自动取整。"));
    QSet<QString> types;
    for (const auto &old : units) types.insert(old.originalType);
    if (types.isEmpty()) types.insert(QStringLiteral("1"));
    if (!types.contains(unit.originalType))
        return fail(message, QStringLiteral("单位类型必须选原单位库已存在的分类，未猜测新增类型。"));
    return true;
}

bool commit(QSqlDatabase &db, QString *message)
{
    if (db.commit()) return true;
    fail(message, QStringLiteral("原数据库提交失败：") + db.lastError().text());
    return rollback(db, message);
}
}

bool OriginalProjectRepository::references(qint64 projectId, QStringList *references, QString *message)
{
    references->clear();
    ProjectRecords projects;
    if (!readProjects(&projects, message)) return false;
    QString name;
    for (const auto &project : projects) if (project.id == projectId) name = project.name;
    if (name.isEmpty()) return fail(message, QStringLiteral("原项目不存在，请重新读取。"));
    const QList<QStringList> checks = {
        {QStringLiteral("result/calibration.db"), QStringLiteral("calibration_base"), QStringLiteral("project_name")},
        {QStringLiteral("result/calibration.db"), QStringLiteral("calibration_batch_group"), QStringLiteral("project_name")},
        {QStringLiteral("result/quality.db"), QStringLiteral("quality_item_proj"), QStringLiteral("proj_name")},
        {QStringLiteral("result/quality_Data.db"), QStringLiteral("quality_data"), QStringLiteral("project_name")},
        {QStringLiteral("result/results.db"), QStringLiteral("report_results"), QStringLiteral("project_name")},
        {QStringLiteral("result/results.db"), QStringLiteral("report_column_header"), QStringLiteral("project_name")}};
    for (const auto &check : checks)
    {
        bool found = false;
        if (!requiredReference(m_root, check[0], check[1], check[2], name, &found, message)) return false;
        if (found) references->append(check[0] + QLatin1Char('/') + check[1]);
    }
    for (const QString &file : reagentDatabases())
    {
        bool found = false;
        if (!requiredReference(m_root, file, QStringLiteral("reagent_batch_group"), QStringLiteral("project_name"), name, &found, message)) return false;
        if (found) references->append(file + QStringLiteral("/reagent_batch_group"));
    }
    // 原组合用分号逐项存项目名称，精确比对，不把PT误当作APTT的子串。
    Connection combinations;
    if (!combinations.open(QDir(m_root).filePath(QStringLiteral("db/userSet/sample_Combination.db")), false, message)) return false;
    QSqlQuery query(combinations.db);
    if (!query.exec(QStringLiteral("SELECT project_contain FROM sample_combination")))
        return fail(message, QStringLiteral("原样本组合结构无法核对，未放行删除。"));
    while (query.next())
        if (query.value(0).toString().split(QLatin1Char(';')).contains(name))
        {
            references->append(QStringLiteral("db/userSet/sample_Combination.db/sample_combination"));
            break;
        }
    if (query.lastError().isValid()) return fail(message, QStringLiteral("原样本组合核对未完成。"));
    for (const auto &project : projects)
    {
        if (project.id == projectId) continue;
        bool found = false;
        if (!hasValue(m_files.value(project.id), QStringLiteral("project_mda"), QStringLiteral("name"), name, &found, message)) return false;
        if (found) references->append(QStringLiteral("project/%1/project_mda").arg(project.originalFileName));
    }
    // 新模块的库与原库隔离。只按明确的同名项目核对，绝不把相同数字id当作映射。
    const QString catalog = QDir(m_referenceCatalogDirectory).filePath(QStringLiteral("catalog.db"));
    if (!m_referenceCatalogDirectory.isEmpty() && QFileInfo::exists(catalog))
    {
        Connection auxiliary;
        if (!auxiliary.open(catalog, false, message)) return false;
        if (auxiliary.db.tables().contains(QStringLiteral("catalog_project_reference")))
        {
            QSqlQuery held(auxiliary.db);
            held.prepare(QStringLiteral("SELECT 1 FROM catalog_project_reference r JOIN project_base p ON p.id=r.project_id WHERE p.project_en_name=? COLLATE NOCASE LIMIT 1"));
            held.addBindValue(name);
            if (!held.exec()) return fail(message, QStringLiteral("新模块保留的项目引用无法核对，未放行删除。"));
            if (held.next()) references->append(QStringLiteral("当前新模块/catalog_project_reference（同名引用，需人工核对）"));
        }
    }
    references->removeDuplicates();
    return true;
}

bool OriginalProjectRepository::copy(qint64 sourceId, QString newName, qint64 *newId, QString *message)
{
    *newId = 0;
    if (!checkRoot(message)) return false;
    QLockFile guard(QDir(m_root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    guard.setStaleLockTime(0);
    if (!guard.tryLock(0)) return fail(message, QStringLiteral("原库正在维护，请稍后重试。"));
    if (!plainName(newName)) return fail(message, QStringLiteral("新项目名称无效，不能含路径字符。"));
    ProjectRecords projects;
    if (!readProjects(&projects, message)) return false;
    ProjectRecord source;
    qint64 id = 1;
    for (const auto &project : projects)
    {
        id = qMax(id, project.id + 1);
        if (project.id == sourceId) source = project;
        if (project.name.compare(newName, Qt::CaseInsensitive) == 0) return fail(message, QStringLiteral("新项目名称已存在，未覆盖。"));
    }
    if (!source.id || id > 1000000000) return fail(message, QStringLiteral("复制源不存在或编号超出上限。"));
    if (!externalCopyReferences(m_root, source.name, message)) return false;
    const QString sourceFile = m_files.value(sourceId);
    const QString destination = QDir(m_root).filePath(QStringLiteral("project/%1 %2.db").arg(id).arg(newName));
    if (QFileInfo::exists(destination)) return fail(message, QStringLiteral("原命名规则下的目标文件已存在，未覆盖。"));
    QTemporaryDir stage(QDir(m_root).filePath(QStringLiteral("project/.sn100-copy-XXXXXX")));
    if (!stage.isValid()) return fail(message, QStringLiteral("无法创建复制暂存目录。"));
    const QString staged = QDir(stage.path()).filePath(QStringLiteral("project.db"));
    if (!snapshotDatabase(sourceFile, staged, message)) return false;
    {
        Connection connection;
        if (!connection.open(staged, true, message) || !sql(connection.db, QStringLiteral("BEGIN IMMEDIATE"), {}, message)) return false;
        bool changed = false;
        const QMap<QString, QString> base = {{QStringLiteral("xuhao"), QString::number(id)}, {QStringLiteral("project_en_name"), newName},
                                             {QStringLiteral("fixed"), QStringLiteral("0")}, {QStringLiteral("is_related"), QStringLiteral("0")}};
        for (auto field = base.cbegin(); field != base.cend(); ++field)
            if (!updatePair(connection.db, QStringLiteral("project_base"), QStringLiteral("name"), field.key(), field.value(), &changed, message)) return rollback(connection.db, message);
        OriginalRows results;
        if (!readRows(connection.db, QStringLiteral("project_results"), QStringLiteral(" ORDER BY result_id,rowid"), &results, message)) return rollback(connection.db, message);
        for (const auto &row : results)
        {
            QString name = row.values.value(QStringLiteral("result_name")).toString();
            name.replace(source.name, newName);
            if (!updateColumns(connection.db, QStringLiteral("project_results"), row, {{QStringLiteral("result_name"), name}}, &changed, message)) return rollback(connection.db, message);
        }
        if (!commit(connection.db, message)) return false;
    }
    if (fingerprint(sourceFile, message) != source.originalFingerprint)
        return fail(message, QStringLiteral("复制期间源项目变化，未发布复制文件，请重新读取。"));
    if (!QFile::rename(staged, destination)) return fail(message, QStringLiteral("复制文件无法发布，原文件没有覆盖。"));
    *newId = id;
    return true;
}

bool OriginalProjectRepository::remove(qint64 projectId, QString *message)
{
    if (!checkRoot(message)) return false;
    QLockFile guard(QDir(m_root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    guard.setStaleLockTime(0);
    if (!guard.tryLock(0)) return fail(message, QStringLiteral("原库正在维护，请稍后重试。"));
    ProjectRecords projects;
    if (!readProjects(&projects, message)) return false;
    ProjectRecord source;
    for (const auto &project : projects) if (project.id == projectId) source = project;
    if (!source.id) return fail(message, QStringLiteral("原项目不存在。"));
    if (source.fixed) return fail(message, QStringLiteral("原固定项目不能删除。"));
    QStringList used;
    if (!references(projectId, &used, message)) return false;
    if (!used.isEmpty()) return fail(message, QStringLiteral("项目仍有原资料或新模块引用，未删除：") + used.join(QStringLiteral("；")));
    const QString file = m_files.value(projectId);
    if (!cleanSidecars(file, message)) return false;
    // 删除是整份项目文件的操作，不向剩余项目或历史库写入任何内容。
    if (fingerprint(file, message) != source.originalFingerprint) return fail(message, QStringLiteral("删除前原项目发生改变，请重新读取。"));
    if (!QFile::remove(file)) return fail(message, QStringLiteral("原项目文件删除失败，请检查目录权限；未报为成功。"));
    return true;
}

bool OriginalProjectRepository::importFile(QString filePath, qint64 *newId, QString *message)
{
    *newId = 0;
    if (!checkRoot(message) || !regularPath(filePath, false, message)) return false;
    QLockFile guard(QDir(m_root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    guard.setStaleLockTime(0);
    if (!guard.tryLock(0)) return fail(message, QStringLiteral("原库正在维护，请稍后重试。"));
    if (!filePath.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)) return fail(message, QStringLiteral("请选择完整的原项目DB文件。"));
    ProjectRecord incoming;
    ProjectRecords projects;
    if (!readProject(filePath, &incoming, message) || !readProjects(&projects, message)) return false;
    if (!plainName(incoming.name)) return fail(message, QStringLiteral("原项目名称不能安全用于原文件命名规则，未导入。"));
    for (const auto &project : projects)
        if (project.id == incoming.id || project.name.compare(incoming.name, Qt::CaseInsensitive) == 0)
            return fail(message, QStringLiteral("原编号或项目名称已存在，导入不会覆盖；如需新增，请使用复制并指定新名称。"));
    // 不改选入文件里的id、表或参数，文件名称也原样保留。
    const QString destination = QDir(m_root).filePath(QStringLiteral("project/") + QFileInfo(filePath).fileName());
    if (QFileInfo::exists(destination)) return fail(message, QStringLiteral("目标原文件名已存在，未覆盖。"));
    QTemporaryDir stage(QDir(m_root).filePath(QStringLiteral("project/.sn100-import-XXXXXX")));
    if (!stage.isValid()) return fail(message, QStringLiteral("无法创建导入暂存目录。"));
    const QString staged = QDir(stage.path()).filePath(QStringLiteral("project.db"));
    if (!snapshotDatabase(filePath, staged, message)) return false;
    if (fingerprint(filePath, message) != incoming.originalFingerprint)
        return fail(message, QStringLiteral("导入期间源文件发生改变，未发布项目，请重新选择。"));
    if (!QFile::rename(staged, destination)) return fail(message, QStringLiteral("导入文件无法发布，已有项目未改变。"));
    *newId = incoming.id;
    return true;
}

bool OriginalProjectRepository::unitReferenced(QString name, bool *referenced, QString *message)
{
    *referenced = false;
    ProjectRecords projects;
    if (!readProjects(&projects, message)) return false;
    for (const auto &project : projects)
        for (const auto &result : project.results)
            if (result.unit == name) { *referenced = true; return true; }
    for (const QString &column : {QStringLiteral("result_unit"), QStringLiteral("con_unit")})
    {
        bool found = false;
        if (!requiredReference(m_root, QStringLiteral("result/calibration.db"), QStringLiteral("calibration_base"), column, name, &found, message)) return false;
        if (found) { *referenced = true; return true; }
    }
    const QString catalog = QDir(m_referenceCatalogDirectory).filePath(QStringLiteral("catalog.db"));
    if (!m_referenceCatalogDirectory.isEmpty() && QFileInfo::exists(catalog))
    {
        bool found = false;
        if (!hasValue(catalog, QStringLiteral("project_result"), QStringLiteral("unit_name"), name, &found, message)) return false;
        if (found) *referenced = true;
    }
    return true;
}

bool OriginalProjectRepository::addUnit(ProjectUnit unit, QString *message)
{
    if (!checkRoot(message)) return false;
    QLockFile guard(QDir(m_root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    guard.setStaleLockTime(0);
    if (!guard.tryLock(0)) return fail(message, QStringLiteral("原库正在维护，请稍后重试。"));
    ProjectUnits units;
    if (!readUnits(&units, message)) return false;
    if (unit.originalType.isEmpty()) unit.originalType = QStringLiteral("1");
    if (!validateUnit(unit, units, message)) return false;
    for (const auto &old : units) if (old.name == unit.name) return fail(message, QStringLiteral("单位名称已经存在，未重复添加。"));
    Connection connection;
    if (!connection.open(unitsDatabasePath(), true, message) || !sql(connection.db, QStringLiteral("BEGIN IMMEDIATE"), {}, message)) return false;
    if (!sql(connection.db, QStringLiteral("INSERT INTO option_unit(unit,type,ratio) VALUES(?,?,?)"), {unit.name, unit.originalType, unit.ratio}, message)) return rollback(connection.db, message);
    return commit(connection.db, message);
}

bool OriginalProjectRepository::updateUnit(QString originalName, ProjectUnit unit, QString *message)
{
    if (!checkRoot(message)) return false;
    QLockFile guard(QDir(m_root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    guard.setStaleLockTime(0);
    if (!guard.tryLock(0)) return fail(message, QStringLiteral("原库正在维护，请稍后重试。"));
    ProjectUnits units;
    if (!readUnits(&units, message)) return false;
    const auto found = std::find_if(units.cbegin(), units.cend(), [&](const ProjectUnit &old) { return old.name == originalName; });
    if (found == units.cend()) return fail(message, QStringLiteral("待修改单位已不存在。"));
    if (unit.originalFingerprint.size() != 64 || unit.originalFingerprint != found->originalFingerprint)
        return fail(message, QStringLiteral("原单位库已变化或草稿版本缺失，请重新读取后修改。"));
    if (unit.originalType.isEmpty()) unit.originalType = found->originalType;
    if (!validateUnit(unit, units, message)) return false;
    for (const auto &old : units) if (old.name != originalName && old.name == unit.name) return fail(message, QStringLiteral("新单位名称已经存在。"));
    if (unit.name == found->name && unit.ratio == found->ratio && unit.originalType == found->originalType) return true;
    bool used = false;
    if (!unitReferenced(originalName, &used, message)) return false;
    if (used) return fail(message, QStringLiteral("该单位已用于原项目/校准或当前新模块；不能改名、倍率或分类。"));
    Connection connection;
    if (!connection.open(unitsDatabasePath(), true, message) || !sql(connection.db, QStringLiteral("BEGIN IMMEDIATE"), {}, message)) return false;
    if (unitVersion(m_root, fingerprint(unitsDatabasePath(), message)) != unit.originalFingerprint)
    {
        fail(message, QStringLiteral("取得写入权限时原单位已变化，请重新读取。"));
        return rollback(connection.db, message);
    }
    if (!sql(connection.db, QStringLiteral("UPDATE option_unit SET unit=?,type=?,ratio=? WHERE unit=? AND type=? AND ratio=?"),
             {unit.name, unit.originalType, unit.ratio, originalName, found->originalType, found->ratio}, message)) return rollback(connection.db, message);
    QSqlQuery changed(connection.db);
    if (!changed.exec(QStringLiteral("SELECT changes()")) || !changed.next() || changed.value(0).toInt() != 1)
    {
        fail(message, QStringLiteral("原单位在保存前已变化，请重新读取。"));
        return rollback(connection.db, message);
    }
    return commit(connection.db, message);
}

bool OriginalProjectRepository::removeUnit(QString name, QString *message)
{
    Q_UNUSED(name)
    return fail(message, QStringLiteral("原单位删除缺少读取版本，请重新读取后删除。"));
}

bool OriginalProjectRepository::removeUnit(QString name, QString expectedFingerprint, QString *message)
{
    if (!checkRoot(message)) return false;
    QLockFile guard(QDir(m_root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    guard.setStaleLockTime(0);
    if (!guard.tryLock(0)) return fail(message, QStringLiteral("原库正在维护，请稍后重试。"));
    bool used = false;
    if (!unitReferenced(name, &used, message)) return false;
    if (used) return fail(message, QStringLiteral("该单位仍被原项目/校准或当前新模块使用，未删除。"));
    Connection connection;
    if (!connection.open(unitsDatabasePath(), true, message) || !sql(connection.db, QStringLiteral("BEGIN IMMEDIATE"), {}, message)) return false;
    if (expectedFingerprint.size() != 64 || unitVersion(m_root, fingerprint(unitsDatabasePath(), message)) != expectedFingerprint)
    {
        fail(message, QStringLiteral("原单位库已变化或删除版本缺失，请重新读取后删除。"));
        return rollback(connection.db, message);
    }
    if (!sql(connection.db, QStringLiteral("DELETE FROM option_unit WHERE unit=?"), {name}, message)) return rollback(connection.db, message);
    QSqlQuery changed(connection.db);
    if (!changed.exec(QStringLiteral("SELECT changes()")) || !changed.next() || changed.value(0).toInt() != 1)
    {
        fail(message, QStringLiteral("单位已不存在或名称不唯一，未报为成功。"));
        return rollback(connection.db, message);
    }
    return commit(connection.db, message);
}

bool OriginalProjectRepository::exportBundle(qint64 projectId, QString targetDirectory, QString *message)
{
    if (!checkRoot(message) || !QDir::isAbsolutePath(targetDirectory)) return fail(message, QStringLiteral("导出目录必须是绝对路径。"));
    targetDirectory = QDir::cleanPath(targetDirectory);
    const QFileInfo target(targetDirectory);
    if (target.exists() || !regularPath(target.absolutePath(), true, message))
        return fail(message, QStringLiteral("请选择尚不存在的导出目录；不会覆盖已有目录。"));
    const QString destinationParent = QFileInfo(target.absolutePath()).canonicalFilePath();
    if (destinationParent == m_root || destinationParent.startsWith(m_root + QLatin1Char('/')) ||
        destinationParent.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive))
        return fail(message, QStringLiteral("导出目录须位于原工作副本和参考目录之外。"));
    QLockFile guard(QDir(m_root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    guard.setStaleLockTime(0);
    if (!guard.tryLock(0)) return fail(message, QStringLiteral("原库正在维护，请稍后重试。"));
    ProjectRecords projects;
    if (!readProjects(&projects, message)) return false;
    QStringList files = {QStringLiteral("db/systemInfo/option_Unit.db"), QStringLiteral("db/userSet/reagent_Information.db")};
    bool found = projectId == 0;
    for (const auto &project : projects)
        if (projectId == 0 || project.id == projectId)
        {
            found = true;
            files.append(QStringLiteral("project/") + project.originalFileName);
        }
    if (!found) return fail(message, QStringLiteral("待导出原项目已不存在。"));
    QTemporaryDir stage(QDir(destinationParent).filePath(QStringLiteral(".sn100-export-XXXXXX")));
    if (!stage.isValid()) return fail(message, QStringLiteral("无法创建导出暂存目录。"));
    QMap<QString, QString> fingerprints;
    for (const QString &relative : files)
    {
        const QString hash = fingerprint(QDir(m_root).filePath(relative), message);
        if (hash.isEmpty()) return false;
        fingerprints.insert(relative, hash);
    }
    QJsonArray manifestFiles;
    for (const QString &relative : files)
    {
        const QString destination = QDir(stage.path()).filePath(relative);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !snapshotDatabase(QDir(m_root).filePath(relative), destination, message)) return false;
        const QString hash = fingerprint(destination, message);
        if (hash.isEmpty()) return false;
        manifestFiles.append(QJsonObject{{QStringLiteral("path"), relative}, {QStringLiteral("sha256"), hash}});
    }
    for (const QString &relative : files)
        if (fingerprint(QDir(m_root).filePath(relative), message) != fingerprints.value(relative))
            return fail(message, QStringLiteral("导出期间源资料变化，未发布不一致的配置包，请重试。"));
    QSaveFile marker(QDir(stage.path()).filePath(QStringLiteral(".sn100-original-work-v1")));
    if (!marker.open(QIODevice::WriteOnly) || marker.write("sn100-original-work-v1\n") != 23 || !marker.commit())
        return fail(message, QStringLiteral("导出工作副本标记写入失败。"));
    QSaveFile manifest(QDir(stage.path()).filePath(QStringLiteral("original-config-manifest.json")));
    const QJsonDocument document(QJsonObject{{QStringLiteral("kind"), QStringLiteral("sn100-original-project-configuration-v1")},
        {QStringLiteral("scope"), QStringLiteral("配置快照：保留原7表、全部基参与AlgParam、单位及试剂配置；不含患者、账户和外部历史关联。缺少关联库时禁止改名、删除及涉及校准/批组的复制。")},
        {QStringLiteral("files"), manifestFiles}});
    const QByteArray bytes = document.toJson(QJsonDocument::Indented);
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(bytes) != bytes.size() || !manifest.commit())
        return fail(message, QStringLiteral("导出清单写入失败。"));
    if (!QDir().rename(stage.path(), targetDirectory)) return fail(message, QStringLiteral("导出目录发布失败；没有覆盖已有目录。"));
    stage.setAutoRemove(false);
    return true;
}
