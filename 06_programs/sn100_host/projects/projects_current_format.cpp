#include "projects_store.h"

#include <QCoreApplication>
#include <QThread>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const char formatVersion[] = "sn100-current-result-format-source-v1";
int slotType(const QString &name)
{
    const QStringList names{QString(), QStringLiteral("Original"), QStringLiteral("Calibration"),
                            QStringLiteral("Rate"), QStringLiteral("INR")};
    return names.indexOf(name); // Unknown names, including Time, are not aliases.
}
QString key(qint64 id, const QString &type)
{
    return QString::number(id) + QChar(0x1f) + type;
}
bool bounded(const QString &text, int maximum)
{
    return text.size() <= maximum && !text.contains(QChar::Null);
}
struct Selection
{
    int resultId = 0;
    bool used = false;
    bool display = false;
};
}

// Caller owns a real read or write transaction. This helper neither commits nor
// caches values; a failed attempt cannot reuse a prior committed snapshot.
bool ProjectsStore::readCurrentResultFormat(ProjectCurrentFormatSnapshot *output, QString *message)
{
    *output = ProjectCurrentFormatSnapshot();
    const QFileInfo database(m_database.databaseName());
    const QString databasePath = database.canonicalFilePath();
    const QString directory = database.dir().canonicalPath();
    QFile marker(QDir(directory).filePath(QStringLiteral(".sn100-catalog-offline-v1")));
    if (databasePath.isEmpty() || database.isSymLink() || !database.isFile()
            || QFileInfo(marker.fileName()).isSymLink() || !marker.open(QIODevice::ReadOnly)
            || marker.size() != qint64(sizeof("sn100-catalog-offline-v1\n") - 1)
            || marker.readAll() != QByteArray("sn100-catalog-offline-v1\n"))
    {
        *message = QStringLiteral("格式来源目录或项目标记不匹配。");
        return false;
    }
    for (QFileInfo current(database.absoluteFilePath()); ; current.setFile(current.absolutePath()))
    {
        if (current.isSymLink()) { *message = QStringLiteral("格式来源不能经过符号链接。"); return false; }
        if (current.absoluteFilePath() == current.absolutePath()) break;
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT key,value FROM catalog_metadata WHERE key IN ('schema','current_format_schema')")))
    { *message = query.lastError().text(); return false; }
    QMap<QString, QString> metadata;
    while (query.next()) metadata.insert(query.value(0).toString(), query.value(1).toString());
    if (query.lastError().isValid()) { *message = query.lastError().text(); return false; }
    query.finish();
    if (metadata.value(QStringLiteral("schema")) != QStringLiteral("sn100-catalog-offline-v1"))
    { *message = QStringLiteral("项目库版本不匹配。"); return false; }
    const bool profile = metadata.contains(QStringLiteral("current_format_schema"));
    if (profile && metadata.value(QStringLiteral("current_format_schema")) != QLatin1String(formatVersion))
    { *message = QStringLiteral("本地格式配置版本不支持；未迁移旧行。"); return false; }
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='project_result_format_source'"))
            || !query.next())
    { *message = query.lastError().text(); return false; }
    const bool haveTable = query.value(0).toInt() == 1;
    query.finish();
    if (haveTable != profile)
    { *message = QStringLiteral("本地格式表和版本标记不一致。"); return false; }
    // Query guards, not instrument capacities. No partial/truncated snapshot.
    if (!query.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM project_base),"
            "(SELECT COUNT(*) FROM project_result),(SELECT COUNT(*) FROM option_unit)")) || !query.next())
    { *message = query.lastError().text(); return false; }
    if (query.value(0).toLongLong() > 1024 || query.value(1).toLongLong() > 4096 || query.value(2).toLongLong() > 256)
    { *message = QStringLiteral("格式来源超过本次有界查询容量，未截断。"); return false; }
    query.finish();
    ProjectRecords projects;
    ProjectUnits units;
    if (!read(&projects, &units, message)) return false;
    ProjectCurrentFormatSnapshot snapshot;
    snapshot.directory_ = directory;
    snapshot.databasePath_ = databasePath;
    snapshot.catalogVersion_ = metadata.value(QStringLiteral("schema"));
    snapshot.formatVersion_ = QString::fromLatin1(formatVersion);
    snapshot.selectionProfilePresent_ = profile;
    snapshot.generation_ = ++m_formatGeneration;
    QMap<QString, double> ratios;
    for (const auto &unit : units)
    {
        if (unit.name.isEmpty() || !bounded(unit.name, 64) || ratios.contains(unit.name))
        { *message = QStringLiteral("单位名称为空、重复或无效。"); return false; }
        ratios.insert(unit.name, unit.ratio);
        snapshot.units_.append({unit.name, unit.ratio});
    }
    QMap<QString, Selection> selections;
    if (profile)
    {
        if (!query.exec(QStringLiteral("SELECT project_id,result_type,result_id,is_used,display,"
                "typeof(result_id),typeof(is_used),typeof(display) FROM project_result_format_source ORDER BY project_id,result_type LIMIT 4097")))
        { *message = query.lastError().text(); return false; }
        while (query.next())
        {
            const QString selectionKey = key(query.value(0).toLongLong(), query.value(1).toString());
            const qint64 resultId = query.value(2).toLongLong();
            if (selections.size() == 4096 || selections.contains(selectionKey) || query.value(0).toLongLong() <= 0
                    || !bounded(query.value(1).toString(), 32) || resultId < 1 || resultId > 4
                    || query.value(5).toString() != QStringLiteral("integer")
                    || query.value(6).toString() != QStringLiteral("integer") || query.value(7).toString() != QStringLiteral("integer")
                    || (query.value(3).toLongLong() != 0 && query.value(3).toLongLong() != 1)
                    || (query.value(4).toLongLong() != 0 && query.value(4).toLongLong() != 1))
            { *message = QStringLiteral("原结果选择字段无效、重复或超出有界容量；不推定选用状态。"); return false; }
            selections.insert(selectionKey, {static_cast<int>(resultId), query.value(3).toBool(), query.value(4).toBool()});
        }
        if (query.lastError().isValid()) { *message = query.lastError().text(); return false; }
        query.finish();
    }
    for (const auto &project : projects)
    {
        snapshot.projects_.append({project.id, project.instrumentProjectId, project.name, project.enabled});
        for (const auto &result : project.results)
        {
            ProjectCurrentFormatEntry entry;
            entry.projectId = project.id;
            entry.instrumentProjectId = project.instrumentProjectId;
            entry.projectName = project.name;
            entry.projectEnabled = project.enabled;
            entry.resultTypeName = result.type;
            entry.resultType = slotType(result.type);
            entry.resultName = result.name;
            entry.unit = result.unit;
            entry.decimals = result.decimals;
            entry.mainResult = result.mainResult;
            const QString selectionKey = key(project.id, result.type);
            if (selections.contains(selectionKey))
            {
                const Selection selection = selections.take(selectionKey);
                entry.sourceResultId = selection.resultId;
                entry.used = selection.used;
                entry.display = selection.display;
                entry.selectionKnown = entry.resultType >= 1 && entry.resultType <= 4
                    && entry.sourceResultId == entry.resultType;
            }
            else entry.unknownReason = QStringLiteral("原result_id/is_used/display未知；需显式保存本地格式配置。");
            if (entry.resultType < 1 || entry.resultType > 4)
                entry.unknownReason = QStringLiteral("未知结果类型：") + result.type + QStringLiteral("；需显式保存四槽配置，不自动改名。");
            else if (entry.unknownReason.isEmpty() && entry.sourceResultId != entry.resultType)
                entry.unknownReason = QStringLiteral("result_id与明确四槽类型不匹配。");
            else if (entry.unknownReason.isEmpty() && (result.decimals < 1 || result.decimals > 6
                    || result.name.isEmpty() || !bounded(result.name, 128) || !bounded(project.name, 128)
                    || !bounded(project.instrumentProjectId, 128)))
                entry.unknownReason = QStringLiteral("结果名称、身份或精度无效。");
            else if (entry.unknownReason.isEmpty() && !ratios.contains(result.unit))
                entry.unknownReason = QStringLiteral("结果单位没有明确字典项。");
            if (entry.unknownReason.isEmpty())
            {
                const double ratio = ratios.value(result.unit);
                if (!std::isfinite(ratio) || ratio < 1 || ratio > std::numeric_limits<int>::max() || std::floor(ratio) != ratio)
                    entry.unknownReason = QStringLiteral("单位倍率不是本切片支持的正整数；不截断或回退。");
                else { entry.integerRatio = static_cast<int>(ratio); entry.known = true; }
            }
            snapshot.entries_.append(entry);
        }
    }
    if (!selections.isEmpty())
    { *message = QStringLiteral("格式来源包含不存在的项目结果关联。"); return false; }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_0);
    stream << snapshot.databasePath_ << snapshot.catalogVersion_ << snapshot.formatVersion_ << snapshot.selectionProfilePresent_;
    // The explicit replacement API also receives ranges/LIS fields. Include
    // every stored result field so CAS cannot overwrite a concurrent edit.
    for (const auto &project : projects)
    {
        stream << project.id << project.instrumentProjectId << project.name << project.fullName
               << project.enabled << project.changedAt;
        for (const auto &result : project.results)
            stream << result.type << result.name << result.decimals << result.testLow << result.testHigh
                   << result.normalLow << result.normalHigh << result.lisCode << result.unit << result.mainResult;
    }
    for (const auto &unit : snapshot.units_) stream << unit.name << unit.storedRatio;
    for (const auto &entry : snapshot.entries_)
        stream << entry.projectId << entry.instrumentProjectId << entry.projectName << entry.projectEnabled
               << entry.resultTypeName << entry.resultType << entry.sourceResultId << entry.resultName << entry.unit
               << entry.decimals << entry.integerRatio << entry.used << entry.display << entry.mainResult << entry.known << entry.unknownReason;
    if (stream.status() != QDataStream::Ok) { *message = QStringLiteral("格式快照序列化失败。"); return false; }
    snapshot.fingerprint_ = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    snapshot.capturedAtUtc_ = QDateTime::currentDateTimeUtc();
    // Valid means a complete SQL view, not that every result entry is known.
    snapshot.valid_ = true;
    *output = snapshot;
    return true;
}

void ProjectsStore::loadCurrentResultFormat(qint64 requestId)
{
    const QString operation = QStringLiteral("loadCurrentResultFormat");
    if (!check(requestId, operation)) return;
    if (!m_database.transaction()) { fail(requestId, operation, m_database.lastError().text()); return; }
    ProjectCurrentFormatSnapshot snapshot;
    QString message;
    if (!readCurrentResultFormat(&snapshot, &message) || !m_database.commit())
    {
        if (message.isEmpty()) message = m_database.lastError().text();
        if (!m_database.rollback()) message += QStringLiteral("；读取事务回滚失败：") + m_database.lastError().text();
        fail(requestId, operation, message);
        return;
    }
    emit currentResultFormatReady(requestId, snapshot);
}

void ProjectsStore::replaceCurrentResultFormat(qint64 requestId, qint64 projectId, QString expectedFingerprint,
                                               ProjectResults configurations, ProjectCurrentFormatSelections selections)
{
    const QString operation = QStringLiteral("replaceCurrentResultFormat");
    if (!check(requestId, operation, true)) return; // Exact existing canEdit gate.
    if (projectId <= 0 || expectedFingerprint.size() != 64 || configurations.size() != 4 || selections.size() != 4)
    { fail(requestId, operation, QStringLiteral("请先读取当前快照，并明确提供完整四槽结果配置和选择字段。")); return; }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) { fail(requestId, operation, query.lastError().text()); return; }
    QString message;
    auto reject = [&]()
    {
        if (!m_database.rollback())
        {
            const QString rollbackError = m_database.lastError().text();
            m_ready = false;
            m_database.close(); // Keep the named connection until local queries are destroyed.
            message += QStringLiteral("；配置事务结束未经确认：") + rollbackError
                + QStringLiteral("；连接已关闭并停止读写，须显式重新initialize核查。未推断已提交或已取消，未清理引用登记。");
        }
        fail(requestId, operation, message);
    };
    ProjectCurrentFormatSnapshot before;
    if (!readCurrentResultFormat(&before, &message)) { reject(); return; }
    if (before.fingerprint() != expectedFingerprint)
    { message = QStringLiteral("已提交配置已变化；请重新读取，不使用旧草稿覆盖。"); reject(); return; }
    ProjectRecords projects;
    ProjectUnits units;
    if (!read(&projects, &units, &message)) { reject(); return; }
    auto found = std::find_if(projects.cbegin(), projects.cend(),
                              [projectId](const ProjectRecord &project) { return project.id == projectId; });
    if (found == projects.cend()) { message = QStringLiteral("格式项目已不存在。"); reject(); return; }
    QMap<QString, ProjectCurrentFormatSelection> requested;
    for (const auto &selection : selections)
    {
        const int type = slotType(selection.resultType);
        if (type < 1 || type > 4 || requested.contains(selection.resultType) || selection.resultId != type
                || (selection.used != 0 && selection.used != 1) || (selection.display != 0 && selection.display != 1))
        { message = QStringLiteral("须明确提供不重复的Original/Calibration/Rate/INR及各result_id/is_used/display。"); reject(); return; }
        requested.insert(selection.resultType, selection);
    }
    QSet<QString> seen;
    for (const auto &configuration : configurations)
    {
        if (!requested.contains(configuration.type) || seen.contains(configuration.type))
        { message = QStringLiteral("结果配置与四槽选择字段不一致；没有自动Time别名。"); reject(); return; }
        seen.insert(configuration.type);
        if (configuration.lisCode.contains(QChar::Null))
        { message = QStringLiteral("结果传输代号含空字符，未保存。 "); reject(); return; }
        const auto unit = std::find_if(units.cbegin(), units.cend(),
            [&](const ProjectUnit &candidate) { return candidate.name == configuration.unit; });
        if (unit == units.cend() || !std::isfinite(unit->ratio) || unit->ratio < 1
                || unit->ratio > std::numeric_limits<int>::max() || std::floor(unit->ratio) != unit->ratio)
        { message = QStringLiteral("须选择真实已存的正整数倍率单位；本API不创建单位或截断小数倍率。"); reject(); return; }
    }
    ProjectRecord replacement = *found; // Identity, advanced scheme and labels remain actual committed values.
    replacement.results = configurations; // Explicit replacement, never read-time migration.
    message = validate(replacement, units);
    if (!message.isEmpty()) { reject(); return; }
    const QStringList schema{
        QStringLiteral("CREATE TABLE IF NOT EXISTS project_result_format_source("
            "project_id INTEGER NOT NULL REFERENCES project_base(id) ON DELETE CASCADE,"
            "result_type TEXT NOT NULL,result_id INTEGER NOT NULL CHECK(result_id BETWEEN 1 AND 4),"
            "is_used INTEGER NOT NULL CHECK(is_used IN(0,1)),display INTEGER NOT NULL CHECK(display IN(0,1)),"
            "PRIMARY KEY(project_id,result_type))"),
        QStringLiteral("INSERT OR IGNORE INTO catalog_metadata(key,value) VALUES('current_format_schema','sn100-current-result-format-source-v1')")
    };
    for (const auto &sql : schema)
        if (!query.exec(sql)) { message = query.lastError().text(); reject(); return; }
    qint64 actualId = 0;
    if (!writeProject(replacement, false, &message, &actualId) || actualId != projectId)
    { if (message.isEmpty()) message = QStringLiteral("项目配置保存未命中原项目。"); reject(); return; }
    if (!query.prepare(QStringLiteral("DELETE FROM project_result_format_source WHERE project_id=?")))
    { message = query.lastError().text(); reject(); return; }
    query.addBindValue(projectId);
    if (!query.exec()) { message = query.lastError().text(); reject(); return; }
    for (const auto &selection : selections)
    {
        if (!query.prepare(QStringLiteral("INSERT INTO project_result_format_source(project_id,result_type,result_id,is_used,display) VALUES(?,?,?,?,?)")))
        { message = query.lastError().text(); reject(); return; }
        query.addBindValue(projectId); query.addBindValue(selection.resultType); query.addBindValue(selection.resultId);
        query.addBindValue(selection.used); query.addBindValue(selection.display);
        if (!query.exec()) { message = query.lastError().text(); reject(); return; }
    }
    ProjectCurrentFormatSnapshot after;
    if (!readCurrentResultFormat(&after, &message)) { reject(); return; }
    int verified = 0;
    for (const auto &entry : after.entries()) if (entry.projectId == projectId)
    {
        const auto selection = requested.value(entry.resultTypeName);
        if (!entry.known || entry.sourceResultId != selection.resultId
                || entry.used != bool(selection.used) || entry.display != bool(selection.display))
        { message = QStringLiteral("保存后的四槽配置读回不符合明确输入。"); reject(); return; }
        ++verified;
    }
    if (verified != 4) { message = QStringLiteral("保存后结果配置不是四槽。"); reject(); return; }
    ProjectRecords persisted;
    ProjectUnits persistedUnits;
    if (!read(&persisted, &persistedUnits, &message)) { reject(); return; }
    const auto actual = std::find_if(persisted.cbegin(), persisted.cend(),
        [projectId](const ProjectRecord &project) { return project.id == projectId; });
    if (actual == persisted.cend() || actual->results.size() != configurations.size())
    { message = QStringLiteral("保存后真实项目配置数量不符。 "); reject(); return; }
    for (const auto &saved : actual->results)
    {
        const auto expected = std::find_if(configurations.cbegin(), configurations.cend(),
            [&](const ProjectResult &configuration) { return configuration.type == saved.type; });
        if (expected == configurations.cend() || saved.name != expected->name || saved.decimals != expected->decimals
                || saved.unit != expected->unit || saved.mainResult != expected->mainResult || saved.lisCode != expected->lisCode
                || saved.testLow != expected->testLow || saved.testHigh != expected->testHigh
                || saved.normalLow != expected->normalLow || saved.normalHigh != expected->normalHigh)
        { message = QStringLiteral("已保存格式/结果字段读回与明确输入不一致。 "); reject(); return; }
    }
    if (!m_database.commit()) { message = m_database.lastError().text(); reject(); return; }
    emit succeeded(requestId, operation, projectId); // Reload gives a new generation and committed fingerprint.
}

// The editor must not pair an old Page record with a fresh source fingerprint.
void ProjectsStore::loadCurrentProjectFormat(qint64 requestId, qint64 projectId)
{
    const QString operation = QStringLiteral("loadCurrentProjectFormat");
    if (!check(requestId, operation)) return;
    if (requestId <= 0 || projectId <= 0 || (QCoreApplication::instance() &&
        QThread::currentThread() == QCoreApplication::instance()->thread()))
    { fail(requestId, operation, QStringLiteral("格式读取须在存储工作线程使用有效请求和项目ID。")); return; }
    const QFileInfo actual(m_database.databaseName());
    const QString databasePath = actual.canonicalFilePath();
    if (databasePath.isEmpty() || actual.fileName() != QStringLiteral("catalog.db") ||
        databasePath.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive))
    { fail(requestId, operation, QStringLiteral("当前项目资料源路径不可确认，未读取编辑快照。")); return; }
    if (!m_database.transaction())
    { fail(requestId, operation, m_database.lastError().text()); return; }
    QString message;
    auto reject = [&]()
    {
        if (!m_database.rollback())
        {
            message += QStringLiteral("；读取事务结束未经确认：") + m_database.lastError().text();
            m_ready = false;
            m_database.close();
            message += QStringLiteral("；连接已关闭，须显式重新initialize核查。");
        }
        fail(requestId, operation, message);
    };
    ProjectRecords projects;
    ProjectUnits units;
    ProjectCurrentFormatSnapshot snapshot;
    // Apply the format reader's size/source guards before the full editor record read.
    if (!readCurrentResultFormat(&snapshot, &message) || !read(&projects, &units, &message))
    { reject(); return; }
    const auto selected = std::find_if(projects.cbegin(), projects.cend(),
        [projectId](const ProjectRecord &project) { return project.id == projectId; });
    if (selected == projects.cend())
    { message = QStringLiteral("所选项目已不存在，未返回旧页面记录。"); reject(); return; }
    const auto identity = std::find_if(snapshot.projects().cbegin(), snapshot.projects().cend(),
        [projectId](const ProjectCurrentFormatProject &project) { return project.id == projectId; });
    if (!snapshot.valid() || snapshot.fingerprint().size() != 64 ||
        QFileInfo(snapshot.databasePath()).canonicalFilePath() != databasePath ||
        QDir(snapshot.directory()).canonicalPath() != actual.absoluteDir().canonicalPath() ||
        identity == snapshot.projects().cend() || identity->instrumentProjectId != selected->instrumentProjectId ||
        identity->name != selected->name || identity->enabled != selected->enabled)
    { message = QStringLiteral("项目身份或当前格式资料源不一致，未返回编辑快照。"); reject(); return; }
    const ProjectRecord record = *selected;
    if (!m_database.commit())
    { message = m_database.lastError().text(); reject(); return; }
    emit currentProjectFormatReady(requestId, record, snapshot);
}
