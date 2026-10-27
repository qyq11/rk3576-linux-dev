#include "history_store.h"
#include "result_project_references.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QVariant>
#include <limits>

namespace
{
const int resultApplicationId = 0x534e4931;
const char resultSchema[] = "sn100-local-result-ingest-v1";
const char resultMarker[] = ".sn100-local-result-ingest-v1";
const QByteArray resultMarkerContent("SN100 LOCAL RESULT INGEST v1\n");
const QStringList resultNames{"Original", "Calibration", "Rate", "INR"};

bool bounded(const QString &text, int maximum = 32767)
{
    return text.size() <= maximum && !text.contains(QChar::Null);
}

bool executionId(qint64 value)
{
    // First local slice follows the original positive int32 task/group allocator.
    return value > 0 && value <= std::numeric_limits<qint32>::max();
}

bool symlinkComponent(QString path)
{
    for (;;)
    {
        const QFileInfo info(path);
        if (info.isSymLink()) return true;
        const QString parent = info.dir().absolutePath();
        if (parent == path) return false;
        path = parent;
    }
}

bool statement(QSqlDatabase &database, const QString &sql, QString *message)
{
    QSqlQuery query(database);
    if (query.exec(sql)) return true;
    *message = query.lastError().text();
    return false;
}

bool prepared(QSqlQuery *query, const QString &sql, const QVariantList &values, QString *message)
{
    if (!query->prepare(sql)) { *message = query->lastError().text(); return false; }
    for (const auto &value : values)
        query->addBindValue(value.type() == QVariant::String && value.isNull() ? QVariant(QStringLiteral("")) : value);
    if (!query->exec()) { *message = query->lastError().text(); return false; }
    return true;
}

class IngestTransaction
{
public:
    explicit IngestTransaction(QSqlDatabase &database) : database_(database) {}
    ~IngestTransaction() { rollback(); }
    bool begin(QString *message)
    {
        active_ = statement(database_, QStringLiteral("BEGIN IMMEDIATE"), message);
        return active_;
    }
    bool commit(QString *message)
    {
        if (!database_.commit()) { *message = database_.lastError().text(); return false; }
        active_ = false;
        return true;
    }
    bool rollback(QString *message = nullptr)
    {
        if (!active_) return true;
        if (!database_.rollback())
        {
            if (message) *message += QStringLiteral("；回滚未获确认：") + database_.lastError().text();
            return false;
        }
        active_ = false;
        return true;
    }
private:
    QSqlDatabase &database_;
    bool active_ = false;
};

struct StoredTask
{
    HistoryComputedResult value;
    QString reference;
    bool completed = false;
};

bool taskIdentity(QSqlDatabase &database, qint64 taskId, bool *exists, StoredTask *task, QString *message)
{
    QSqlQuery query(database);
    if (!prepared(&query, QStringLiteral("SELECT r.fid,r.project_name,i.project_id,i.instrument_project_id,"
            "i.sample_id,i.dilution,i.reference_key,i.completed,r.date_time,r.abnormal_type,i.calibration_id,i.task_type,i.mda_name "
            "FROM report_results r JOIN result_task_identity i ON i.task_id=r.task_id WHERE r.task_id=?"), {taskId}, message)) return false;
    *exists = query.next();
    if (*exists)
    {
        auto &value = task->value;
        value.taskId = taskId;
        value.reportRowId = query.value(0).toLongLong();
        value.projectName = query.value(1).toString();
        value.projectId = query.value(2).toLongLong();
        value.instrumentProjectId = query.value(3).toString();
        value.sampleId = query.value(4).toString();
        value.dilution = query.value(5).toString();
        task->reference = query.value(6).toString();
        task->completed = query.value(7).toBool();
        value.testedAt = QDateTime::fromString(query.value(8).toString(), QStringLiteral("yyyyMMddHHmmss"));
        value.abnormalType = query.value(9).toString();
        value.calibrationId = query.value(10).toString();
        value.taskType = query.value(11).toString();
        value.mdaName = query.value(12).toString();
    }
    const bool okay = !query.lastError().isValid();
    if (!okay) *message = query.lastError().text();
    return okay;
}

bool matches(const StoredTask &stored, const HistoryTaskReservation &input)
{
    const auto &value = stored.value;
    return value.taskType == input.taskType && value.mdaName == input.mdaName
            && value.taskId == input.taskId && value.reportRowId == input.reportRowId
            && value.projectId == input.projectId && value.projectName == input.projectName
            && value.instrumentProjectId == input.instrumentProjectId && value.sampleId == input.sampleId;
}

bool matchesProject(const ProjectRecords &records, qint64 id, const QString &name,
                    const QString &instrumentId, bool requireEnabled, QString *message)
{
    for (const auto &project : records)
    {
        if (project.id != id) continue;
        if (project.name == name && project.instrumentProjectId == instrumentId && (!requireEnabled || project.enabled)) return true;
        *message = QStringLiteral("任务项目身份与真实已提交项目不一致，或预约项目已停用。");
        return false;
    }
    *message = QStringLiteral("真实项目快照中没有该稳定项目ID。");
    return false;
}
}

void HistoryStore::initializeLocalResults(QString directory, QString catalogDirectory)
{
    const QString operation = QStringLiteral("initializeLocalResults");
    if (!checkThread(0, operation)) return;
    if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
    { emit error(0, operation, QStringLiteral("结果初始化必须在数据库工作线程，不能在GUI线程执行。")); return; }
    releaseDatabase();
    directory = QDir::cleanPath(directory);
    catalogDirectory = QDir::cleanPath(catalogDirectory);
    // The caller chooses two isolated stores explicitly. Only siblings beneath
    // the same existing canonical parent are accepted; no runtime discovery,
    // reference-engineering data, symlink aliases or private-profile migration.
    const QString parent = QFileInfo(catalogDirectory).dir().canonicalPath();
    if (!QDir::isAbsolutePath(directory) || !QDir::isAbsolutePath(catalogDirectory)
            || parent.isEmpty() || QFileInfo(directory).dir().canonicalPath() != parent
            || QFileInfo(directory).dir().absolutePath() != parent
            || QFileInfo(catalogDirectory).dir().absolutePath() != parent
            || directory == catalogDirectory
            || QFileInfo(catalogDirectory).canonicalFilePath() != catalogDirectory
            || (QFileInfo::exists(directory) && QFileInfo(directory).canonicalFilePath() != directory)
            || directory.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
            || catalogDirectory.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
            || symlinkComponent(directory) || symlinkComponent(catalogDirectory))
    { emit error(0, operation, QStringLiteral("需要同一既有规范父目录下不同的绝对历史/项目目录；禁止参考工程和符号链接。")); return; }

    QString message;
    std::unique_ptr<ResultProjectReferences> catalog(new ResultProjectReferences(this));
    if (!catalog->open(catalogDirectory, m_session, &message)) { emit error(0, operation, message); return; }
    QDir folder(directory);
    if (!folder.exists() && !QDir().mkpath(directory)) { emit error(0, operation, QStringLiteral("不能创建历史目录。")); return; }
    const QString markerPath = folder.filePath(QString::fromLatin1(resultMarker));
    if (QFileInfo::exists(markerPath))
    {
        QFile marker(markerPath);
        if (QFileInfo(markerPath).isSymLink() || !marker.open(QIODevice::ReadOnly)
                || marker.size() != resultMarkerContent.size() || marker.readAll() != resultMarkerContent)
        { emit error(0, operation, QStringLiteral("结果目录标记错误；不打开已有库。")); return; }
    }
    else
    {
        if (!folder.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty())
        { emit error(0, operation, QStringLiteral("非空未标记历史目录不能初始化结果。")); return; }
        QSaveFile marker(markerPath);
        marker.setDirectWriteFallback(false);
        if (!marker.open(QIODevice::WriteOnly) || marker.write(resultMarkerContent) != resultMarkerContent.size() || !marker.commit())
        { emit error(0, operation, QStringLiteral("目录标记写入失败。")); return; }
    }
    QLockFile lock(folder.filePath(QStringLiteral(".history-result-write.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { emit error(0, operation, QStringLiteral("另一进程正在使用该结果目录。")); return; }
    const QString path = folder.filePath(QStringLiteral("history.sqlite"));
    const bool existing = QFileInfo::exists(path);
    if (QFileInfo(path).isSymLink() || (existing && !QFileInfo(path).isFile()) || symlinkComponent(folder.filePath(QStringLiteral("exports"))))
    { emit error(0, operation, QStringLiteral("数据库/导出路径类型不安全。")); return; }
    if (!folder.mkpath(QStringLiteral("exports"))) { emit error(0, operation, QStringLiteral("导出目录创建失败。")); return; }
    m_directory = QFileInfo(directory).canonicalFilePath();
    m_resultCatalogDirectory = catalogDirectory;
    m_resultIngest = true;
    m_resultProjects = std::move(catalog);
    m_connectionName = QStringLiteral("sn100-result-ingest-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(path);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=200"));
    bool okay = m_database.open();
    if (!okay) message = m_database.lastError().text();
    if (okay) okay = statement(m_database, QStringLiteral("PRAGMA foreign_keys=ON"), &message)
            && statement(m_database, QStringLiteral("PRAGMA busy_timeout=200"), &message);
    if (okay) okay = existing ? validateResultSchema(&message) : createEmptyResultSchema(&message);
    if (!okay)
    {
        releaseDatabase(); // Preserve failed newly created files for diagnosis; never erase an old DB.
        emit error(0, operation, message);
        return;
    }
    m_ready = true;
    emit ready(0, path);
}

bool HistoryStore::createEmptyResultSchema(QString *message)
{
    IngestTransaction transaction(m_database);
    if (!transaction.begin(message)) return false;
    const QStringList schema{
        QStringLiteral("PRAGMA application_id=%1").arg(resultApplicationId),
        QStringLiteral("PRAGMA user_version=1"),
        QStringLiteral("CREATE TABLE result_ingest_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE report_data(id INTEGER PRIMARY KEY,row_id INTEGER NOT NULL UNIQUE CHECK(row_id BETWEEN 1 AND 2147483647),sn INTEGER NOT NULL,"
            "sample_id TEXT NOT NULL,data_time TEXT NOT NULL,visible INTEGER NOT NULL,state TEXT NOT NULL,name TEXT NOT NULL,"
            "sex INTEGER NOT NULL,age INTEGER NOT NULL,department TEXT NOT NULL,patient_type TEXT NOT NULL,"
            "application_serial TEXT NOT NULL,bed_number TEXT NOT NULL,jianyan TEXT NOT NULL,checker TEXT NOT NULL,"
            "auditor TEXT NOT NULL,remark TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX history_date ON report_data(visible,data_time,id)"),
        QStringLiteral("CREATE INDEX history_barcode ON report_data(sample_id)"),
        QStringLiteral("CREATE TABLE report_columns(position INTEGER NOT NULL UNIQUE,project TEXT NOT NULL,result_type INTEGER NOT NULL,"
            "label TEXT NOT NULL,unit TEXT NOT NULL,decimals INTEGER NOT NULL,factor REAL NOT NULL,PRIMARY KEY(project,result_type))"),
        QStringLiteral("CREATE TABLE report_results(task_id INTEGER PRIMARY KEY CHECK(task_id BETWEEN 1 AND 2147483647),fid INTEGER NOT NULL REFERENCES report_data(row_id),"
            "project_name TEXT NOT NULL,date_time TEXT NOT NULL,abnormal_type TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX history_result_fid ON report_results(fid,task_id)"),
        QStringLiteral("CREATE TABLE report_values(task_id INTEGER NOT NULL REFERENCES report_results(task_id),result_type INTEGER NOT NULL "
            "CHECK(result_type BETWEEN 1 AND 4),raw_value TEXT NOT NULL,PRIMARY KEY(task_id,result_type))"),
        QStringLiteral("CREATE TABLE result_report_identity(row_id INTEGER PRIMARY KEY REFERENCES report_data(row_id),"
            "report_type INTEGER NOT NULL CHECK(report_type=0),is_auto_id INTEGER NOT NULL CHECK(is_auto_id IN(0,1)),sample_hole TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE result_task_identity(task_id INTEGER PRIMARY KEY REFERENCES report_results(task_id),"
            "project_id INTEGER NOT NULL CHECK(project_id>0),instrument_project_id TEXT NOT NULL,sample_id TEXT NOT NULL,dilution TEXT NOT NULL,"
            "calibration_id TEXT NOT NULL,completed INTEGER NOT NULL CHECK(completed IN(0,1)),uploaded INTEGER NOT NULL CHECK(uploaded IN(0,1)),"
            "reference_key TEXT NOT NULL UNIQUE,task_type TEXT NOT NULL CHECK(task_type='SAM'),mda_name TEXT NOT NULL CHECK(mda_name=''))")
    };
    for (const auto &sql : schema) if (!statement(m_database, sql, message)) return false;
    m_resultStoreId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QMap<QString, QString> metadata{{"schema", QString::fromLatin1(resultSchema)}, {"store_id", m_resultStoreId},
        {"history_path", m_directory}, {"catalog_path", m_resultCatalogDirectory}};
    for (auto row = metadata.cbegin(); row != metadata.cend(); ++row)
    {
        QSqlQuery query(m_database);
        if (!prepared(&query, QStringLiteral("INSERT INTO result_ingest_metadata VALUES(?,?)"), {row.key(), row.value()}, message)) return false;
    }
    // No anonymous reports, result columns, synthetic defaults or demo results.
    return transaction.commit(message);
}

bool HistoryStore::validateResultSchema(QString *message)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA application_id")) || !query.next() || query.value(0).toInt() != resultApplicationId)
    { *message = QStringLiteral("不是本地结果库，不读取或迁移匿名/生产业务表。"); return false; }
    query.finish();
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next() || query.value(0).toInt() != 1)
    { *message = QStringLiteral("结果库版本不支持。"); return false; }
    query.finish();
    if (!query.exec(QStringLiteral("SELECT key,value FROM result_ingest_metadata"))) { *message = query.lastError().text(); return false; }
    QMap<QString, QString> metadata;
    while (query.next()) metadata.insert(query.value(0).toString(), query.value(1).toString());
    if (query.lastError().isValid() || metadata.size() != 4 || metadata.value("schema") != QLatin1String(resultSchema)
            || metadata.value("history_path") != m_directory || metadata.value("catalog_path") != m_resultCatalogDirectory
            || QUuid(metadata.value("store_id")).isNull())
    { *message = QStringLiteral("结果元数据/目录绑定不匹配；不把复制库自动绑定到另一个项目库。"); return false; }
    query.finish();
    m_resultStoreId = metadata.value("store_id");
    if (!query.exec(QStringLiteral("PRAGMA quick_check")) || !query.next() || query.value(0).toString() != QStringLiteral("ok"))
    { *message = QStringLiteral("结果库完整性检查失败。"); return false; }
    query.finish();
    if (!query.exec(QStringLiteral("PRAGMA foreign_key_check")) || query.next() || query.lastError().isValid())
    { *message = QStringLiteral("结果库关联检查失败。"); return false; }
    query.finish();
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM report_results r LEFT JOIN result_task_identity i ON i.task_id=r.task_id WHERE i.task_id IS NULL"))
            || !query.next() || query.value(0).toLongLong() != 0)
    { *message = QStringLiteral("存在缺少真实任务身份的结果，不能继续写入。"); return false; }
    query.finish();
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM report_columns WHERE unit<>'' OR decimals<>-1 OR factor<>1 OR result_type NOT BETWEEN 1 AND 4"))
            || !query.next() || query.value(0).toLongLong() != 0)
    { *message = QStringLiteral("结果发现未经贯通的单位/精度配置；不采用匿名默认映射。"); return false; }
    query.finish();
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM report_data r LEFT JOIN result_report_identity i ON i.row_id=r.row_id WHERE i.row_id IS NULL"))
            || !query.next() || query.value(0).toLongLong() != 0)
    { *message = QStringLiteral("报告组缺少本地预约身份。"); return false; }
    query.finish();
    ProjectRecords projects;
    if (!m_resultProjects->projects(&projects, message)) return false;
    if (!query.exec(QStringLiteral("SELECT r.task_id,r.project_name,i.project_id,i.instrument_project_id,i.reference_key,"
            "i.task_type,i.mda_name,i.completed,(SELECT COUNT(*) FROM report_values v WHERE v.task_id=r.task_id) "
            "FROM report_results r JOIN result_task_identity i ON i.task_id=r.task_id ORDER BY r.task_id")))
    { *message = query.lastError().text(); return false; }
    while (query.next())
    {
        const qint64 taskId = query.value(0).toLongLong(), projectId = query.value(2).toLongLong();
        const QString key = query.value(4).toString();
        const int values = query.value(8).toInt();
        if (!executionId(taskId) || key != m_resultStoreId + QChar(':') + QString::number(taskId)
                || query.value(5).toString() != QStringLiteral("SAM") || !query.value(6).toString().isEmpty()
                || values != (query.value(7).toBool() ? 4 : 0)
                || !matchesProject(projects, projectId, query.value(1).toString(), query.value(3).toString(), false, message))
        { if (message->isEmpty()) *message = QStringLiteral("已有任务身份/完成状态/四值关联不符。"); return false; }
        bool held = false;
        if (!m_resultProjects->has(projectId, key, &held, message) || !held)
        { if (message->isEmpty()) *message = QStringLiteral("已有任务真实项目引用缺失；不自动清理或伪造恢复。"); return false; }
    }
    if (query.lastError().isValid()) { *message = query.lastError().text(); return false; }
    return true;
}

bool HistoryStore::checkResultIngest(qint64 requestId, const QString &operation)
{
    if (!checkReady(requestId, operation)) return false;
    if (!m_resultIngest || !m_resultProjects || m_resultStoreId.isEmpty())
    { emit error(requestId, operation, QStringLiteral("此库不是空库初始化的本地结果库，或真实项目引用存储不可用。")); return false; }
    // This is a trusted result-worker API, not a new UI permission endpoint.
    // New reference acquisition is gated by the REAL ProjectsStore API. Existing
    // protected tasks must still finish while actionReady=false or the UI logs out.
    return true;
}

bool HistoryStore::resultProject(qint64 projectId, const QString &name, const QString &instrumentId,
                                 bool requireEnabled, QString *message)
{
    ProjectRecords records;
    return m_resultProjects->projects(&records, message) && matchesProject(records, projectId, name, instrumentId, requireEnabled, message);
}

bool HistoryStore::validateReservations(const HistoryReservationBatch &batch, QString *message) const
{
    if (batch.mode != HistoryReservationMode::Initial && batch.mode != HistoryReservationMode::ErrorResetSameIds
            && batch.mode != HistoryReservationMode::RetestNewIds)
    { *message = QStringLiteral("预约模式无效。"); return false; }
    if (batch.tasks.isEmpty() || batch.tasks.size() > 64 || batch.reports.isEmpty() || batch.reports.size() > 64)
    { *message = QStringLiteral("每批需要1至64个明确任务/报告组，不截断。"); return false; }
    QSet<qint64> groups, ids;
    for (const auto &group : batch.reports)
    {
        const auto &report = group.report;
        const auto &patient = report.patient;
        if (!executionId(report.rowId) || groups.contains(report.rowId) || report.id != 0 || patient.reportId != 0
                || !report.results.isEmpty() || report.sampleNumber < 0 || !report.dateTime.isValid()
                || patient.sex < -1 || patient.sex > 1 || !bounded(report.barcode) || !bounded(report.state)
                || !bounded(group.sampleHole, 128))
        { *message = QStringLiteral("报告组必须给明确正row_id、样本/日期，不接受伪造SQL主键或夹具结果。"); return false; }
        for (const auto &text : {patient.name, patient.department, patient.patientType, patient.applicationSerial,
                                patient.bedNumber, patient.examiner, patient.sender, patient.auditor, patient.remark})
            if (!bounded(text)) { *message = QStringLiteral("患者字段超长或含空字符。"); return false; }
        groups.insert(report.rowId);
    }
    QSet<qint64> usedGroups;
    for (const auto &task : batch.tasks)
    {
        if (task.taskType != QStringLiteral("SAM") || !task.mdaName.isEmpty()
                || !executionId(task.taskId) || ids.contains(task.taskId) || !groups.contains(task.reportRowId)
                || task.projectId <= 0 || task.projectName.isEmpty() || !bounded(task.projectName, 128)
                || !bounded(task.instrumentProjectId, 128) || !bounded(task.sampleId) || !bounded(task.dilution, 4096)
                || !task.reservedAt.isValid())
        { *message = QStringLiteral("任务身份/项目/报告分组或输入范围无效；不代替调度器生成编号。"); return false; }
        ids.insert(task.taskId);
        usedGroups.insert(task.reportRowId);
    }
    if (usedGroups != groups) { *message = QStringLiteral("每个预约报告组都必须含有明确任务。"); return false; }
    return true;
}

void HistoryStore::reserveLocalTasks(qint64 requestId, HistoryReservationBatch batch)
{
    const QString operation = QStringLiteral("reserveLocalTasks");
    if (!checkResultIngest(requestId, operation)) return;
    QString message;
    if (!validateReservations(batch, &message)) { emit error(requestId, operation, message); return; }
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral(".history-result-write.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { emit error(requestId, operation, QStringLiteral("另一个进程正在预约或提交本库结果。")); return; }
    ProjectRecords projects;
    if (!m_resultProjects->projects(&projects, &message)) { emit error(requestId, operation, message); return; }
    for (const auto &task : batch.tasks)
    {
        bool exists = false;
        StoredTask stored;
        if (!taskIdentity(m_database, task.taskId, &exists, &stored, &message)
                || !matchesProject(projects, task.projectId, task.projectName, task.instrumentProjectId, true, &message))
        { emit error(requestId, operation, message); return; }
        if ((batch.mode == HistoryReservationMode::ErrorResetSameIds && (!exists || !matches(stored, task)))
                || (batch.mode != HistoryReservationMode::ErrorResetSameIds && exists))
        { emit error(requestId, operation, QStringLiteral("错误复位必须复用原身份；初次/复测任务ID必须是未用的新编号。")); return; }
        if (exists && stored.reference != m_resultStoreId + QChar(':') + QString::number(task.taskId))
        { emit error(requestId, operation, QStringLiteral("任务的持久项目引用键损坏。")); return; }
    }

    QStringList newHolds;
    auto undoHolds = [&]()
    {
        for (const auto &key : newHolds)
        {
            QString failure;
            if (!m_resultProjects->release(key, &failure)) message += QStringLiteral("；引用回收待核：") + failure;
        }
    };
    for (const auto &task : batch.tasks)
    {
        const QString key = m_resultStoreId + QChar(':') + QString::number(task.taskId);
        bool added = false;
        const bool held = m_resultProjects->hold(task.projectId, key, &added, &message);
        if (added) newHolds.append(key);
        if (!held) { undoHolds(); emit error(requestId, operation, message); return; }
    }
    // Holds now prohibit rename/delete. Verify identity from the real catalog
    // again after acquisition, not from a caller's cached project name.
    if (!m_resultProjects->projects(&projects, &message)) { undoHolds(); emit error(requestId, operation, message); return; }
    for (const auto &task : batch.tasks)
        if (!matchesProject(projects, task.projectId, task.projectName, task.instrumentProjectId, true, &message))
        { undoHolds(); emit error(requestId, operation, message); return; }

    IngestTransaction transaction(m_database);
    auto fail = [&]()
    {
        if (message.isEmpty()) message = QStringLiteral("预约事务的目标行数或读回结果不符合合同。");
        if (transaction.rollback(&message)) undoHolds();
        else { m_ready = false; message += QStringLiteral("；保留项目引用并停止该库写入，需先核对事务结果。"); }
        emit error(requestId, operation, message);
    };
    if (!transaction.begin(&message)) { fail(); return; }
    HistoryIds reportIds;
    for (const auto &group : batch.reports)
    {
        const auto &report = group.report;
        QSqlQuery existing(m_database);
        if (!prepared(&existing, QStringLiteral("SELECT r.id,r.sn,r.sample_id,i.is_auto_id,i.sample_hole FROM report_data r "
                "JOIN result_report_identity i ON i.row_id=r.row_id WHERE r.row_id=?"), {report.rowId}, &message)) { fail(); return; }
        const bool present = existing.next();
        const qint64 id = present ? existing.value(0).toLongLong() : 0;
        const bool sameSample = !present || (existing.value(1).toInt() == report.sampleNumber && existing.value(2).toString() == report.barcode
                && existing.value(3).toBool() == group.automaticNumber && existing.value(4).toString() == group.sampleHole);
        if (existing.lastError().isValid()) { message = existing.lastError().text(); fail(); return; }
        existing.finish();
        if (!sameSample || (batch.mode == HistoryReservationMode::RetestNewIds && present)
                || (batch.mode == HistoryReservationMode::ErrorResetSameIds && !present))
        { message = QStringLiteral("复测必须新报告组；复位须原报告组且样本身份不变。"); fail(); return; }
        if (present) { reportIds.append(id); continue; } // Preserve patient edits and soft visibility.
        const auto &patient = report.patient;
        QSqlQuery insert(m_database);
        if (!prepared(&insert, QStringLiteral("INSERT INTO report_data(row_id,sn,sample_id,data_time,visible,state,name,sex,age,department,"
                "patient_type,application_serial,bed_number,jianyan,checker,auditor,remark) VALUES(?,?,?,?,1,?,?,?,?,?,?,?,?,?,?,?,?)"),
                {report.rowId, report.sampleNumber, report.barcode, report.dateTime.toString(QStringLiteral("yyyyMMddHHmmss")), report.state,
                 patient.name, patient.sex, patient.age, patient.department, patient.patientType, patient.applicationSerial,
                 patient.bedNumber, patient.examiner, patient.sender, patient.auditor, patient.remark}, &message)) { fail(); return; }
        reportIds.append(insert.lastInsertId().toLongLong());
        if (!prepared(&insert, QStringLiteral("INSERT INTO result_report_identity VALUES(?,0,?,?)"),
                {report.rowId, group.automaticNumber ? 1 : 0, group.sampleHole}, &message)) { fail(); return; }
    }
    for (const auto &task : batch.tasks)
    {
        QSqlQuery query(m_database);
        const QString date = task.reservedAt.toString(QStringLiteral("yyyyMMddHHmmss"));
        if (batch.mode == HistoryReservationMode::ErrorResetSameIds)
        {
            if (!prepared(&query, QStringLiteral("UPDATE report_results SET date_time=?,abnormal_type='' WHERE task_id=? AND fid=? AND project_name=?"),
                    {date, task.taskId, task.reportRowId, task.projectName}, &message) || query.numRowsAffected() != 1)
            { if (message.isEmpty()) message = QStringLiteral("复位目标不存在或身份变化。"); fail(); return; }
            if (!prepared(&query, QStringLiteral("DELETE FROM report_values WHERE task_id=?"), {task.taskId}, &message)
                    || !prepared(&query, QStringLiteral("UPDATE result_task_identity SET dilution=?,calibration_id='',completed=0,uploaded=0 WHERE task_id=?"),
                        {task.dilution, task.taskId}, &message) || query.numRowsAffected() != 1) { fail(); return; }
        }
        else
        {
            if (!prepared(&query, QStringLiteral("INSERT INTO report_results VALUES(?,?,?,?,'')"),
                    {task.taskId, task.reportRowId, task.projectName, date}, &message)
                    || !prepared(&query, QStringLiteral("INSERT INTO result_task_identity VALUES(?,?,?,?,?,'',0,0,?,?,?)"),
                        {task.taskId, task.projectId, task.instrumentProjectId, task.sampleId, task.dilution,
                         m_resultStoreId + QChar(':') + QString::number(task.taskId), task.taskType, task.mdaName}, &message)) { fail(); return; }
        }
        for (int type = 1; type <= 4; ++type)
        {
            if (!prepared(&query, QStringLiteral("SELECT COUNT(*) FROM report_columns WHERE project=? AND result_type=?"), {task.projectName, type}, &message)
                    || !query.next()) { fail(); return; }
            const bool known = query.value(0).toInt() == 1;
            query.finish();
            if (!known)
            {
                if (!query.exec(QStringLiteral("SELECT COUNT(*),COALESCE(MAX(position),-1)+1 FROM report_columns")) || !query.next())
                { message = query.lastError().text(); fail(); return; }
                const int count = query.value(0).toInt(), position = query.value(1).toInt();
                query.finish();
                if (count >= 64) { message = QStringLiteral("结果列超出当前读取链64列上限，不截断。"); fail(); return; }
                const QString label = task.projectName + QStringLiteral(" / ") + resultNames.at(type - 1) + QStringLiteral("（单位/精度未知）");
                if (!prepared(&query, QStringLiteral("INSERT INTO report_columns VALUES(?,?,?,?,'',-1,1)"), {position, task.projectName, type, label}, &message)) { fail(); return; }
            }
        }
        if (!prepared(&query, QStringLiteral("SELECT COUNT(*) FROM report_results WHERE fid=?"), {task.reportRowId}, &message) || !query.next()) { fail(); return; }
        if (query.value(0).toInt() > 64) { message = QStringLiteral("单报告超过现有读取链64任务上限，不截断。"); fail(); return; }
    }
    if (!transaction.commit(&message)) { fail(); return; }
    emit localTasksReserved(requestId, reportIds); // Real SQL IDs, only after commit.
}

void HistoryStore::commitComputedResult(qint64 requestId, HistoryComputedResult result)
{
    const QString operation = QStringLiteral("commitComputedResult");
    if (!checkResultIngest(requestId, operation)) return;
    if (result.taskType != QStringLiteral("SAM") || !result.mdaName.isEmpty()
            || !executionId(result.taskId) || !executionId(result.reportRowId) || result.projectId <= 0 || !result.testedAt.isValid())
    { emit error(requestId, operation, QStringLiteral("完成结果必须指向明确已预约任务/报告组和有效日期。")); return; }
    for (const auto &text : {result.original, result.calibration, result.rate, result.inr, result.dilution, result.calibrationId, result.abnormalType})
        if (!bounded(text, 4096)) { emit error(requestId, operation, QStringLiteral("结果或来源字段超长/含空字符，不截断。")); return; }
    if (!bounded(result.projectName, 128) || !bounded(result.instrumentProjectId, 128) || !bounded(result.sampleId))
    { emit error(requestId, operation, QStringLiteral("结果身份文本超长或含空字符。")); return; }
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral(".history-result-write.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { emit error(requestId, operation, QStringLiteral("另一个进程正在预约或提交本库结果。")); return; }
    QString message;
    bool exists = false;
    StoredTask stored;
    if (!taskIdentity(m_database, result.taskId, &exists, &stored, &message)) { emit error(requestId, operation, message); return; }
    HistoryTaskReservation identity;
    identity.taskType = result.taskType; identity.mdaName = result.mdaName;
    identity.taskId = result.taskId; identity.reportRowId = result.reportRowId; identity.projectId = result.projectId;
    identity.instrumentProjectId = result.instrumentProjectId; identity.projectName = result.projectName; identity.sampleId = result.sampleId;
    if (!exists || !matches(stored, identity) || stored.reference != m_resultStoreId + QChar(':') + QString::number(result.taskId))
    { emit error(requestId, operation, QStringLiteral("未知任务或身份换绑；不能用完成结果自动创建报告。")); return; }
    bool held = false;
    if (!m_resultProjects->has(result.projectId, stored.reference, &held, &message) || !held
            || !resultProject(result.projectId, result.projectName, result.instrumentProjectId, false, &message))
    { if (message.isEmpty()) message = QStringLiteral("真实项目引用缺失；不把持久键字符串当成已经持有。"); emit error(requestId, operation, message); return; }

    IngestTransaction transaction(m_database);
    auto fail = [&]()
    {
        if (message.isEmpty()) message = QStringLiteral("完成结果事务的目标行数或读回结果不符合合同。");
        if (!transaction.rollback(&message)) m_ready = false;
        emit error(requestId, operation, message);
    };
    if (!transaction.begin(&message)) { fail(); return; }
    QSqlQuery query(m_database);
    if (!prepared(&query, QStringLiteral("UPDATE report_results SET date_time=?,abnormal_type=? WHERE task_id=? AND fid=? AND project_name=?"),
            {result.testedAt.toString(QStringLiteral("yyyyMMddHHmmss")), result.abnormalType, result.taskId, result.reportRowId, result.projectName}, &message)
            || query.numRowsAffected() != 1)
    { if (message.isEmpty()) message = QStringLiteral("完成结果目标不存在；拒绝SQLITE_DONE式假成功。"); fail(); return; }
    const QStringList values{result.original, result.calibration, result.rate, result.inr};
    for (int type = 1; type <= 4; ++type)
    {
        if (!prepared(&query, QStringLiteral("INSERT INTO report_values(task_id,result_type,raw_value) VALUES(?,?,?) "
                "ON CONFLICT(task_id,result_type) DO UPDATE SET raw_value=excluded.raw_value"),
                {result.taskId, type, values.at(type - 1)}, &message)) { fail(); return; }
    }
    if (!prepared(&query, QStringLiteral("UPDATE result_task_identity SET dilution=?,calibration_id=?,completed=1,uploaded=0 WHERE task_id=?"),
            {result.dilution, result.calibrationId, result.taskId}, &message) || query.numRowsAffected() != 1) { fail(); return; }
    // Read the actually stored strings back inside the transaction; a repeated
    // same-task completion overwrites these four values, never appends a task.
    StoredTask persisted;
    if (!taskIdentity(m_database, result.taskId, &exists, &persisted, &message) || !exists)
    { fail(); return; }
    if (!prepared(&query, QStringLiteral("SELECT result_type,raw_value FROM report_values WHERE task_id=? ORDER BY result_type"), {result.taskId}, &message)) { fail(); return; }
    QStringList actual;
    int expected = 1;
    while (query.next())
    {
        if (query.value(0).toInt() != expected++) { message = QStringLiteral("结果类型读回不连续或不是四类型合同。"); fail(); return; }
        actual.append(query.value(1).toString());
    }
    if (query.lastError().isValid() || actual.size() != 4 || actual != values)
    { message = QStringLiteral("完成结果四字符串读回失败。"); fail(); return; }
    query.finish();
    persisted.value.original = actual.at(0); persisted.value.calibration = actual.at(1);
    persisted.value.rate = actual.at(2); persisted.value.inr = actual.at(3);
    if (!transaction.commit(&message)) { fail(); return; }
    emit computedResultCommitted(requestId, persisted.value);
}
