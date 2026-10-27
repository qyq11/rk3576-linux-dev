#include "history_store.h"
#include "result_project_references.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QTextCodec>
#include <QThread>
#include <QUuid>
#include <QVariant>
#include <cmath>
#include <limits>

namespace
{
const char kSchema[] = "sn100-o04-anonymous-v1";
const char kMarker[] = ".sn100-history-offline";
const char kMarkerContent[] = "SN100 O04 ANONYMOUS OFFLINE DATA v1\n";
const int kApplicationId = 0x534e3034;
const int kMaximumTasksPerReport = 64;
const int kMaximumColumns = 64;
const int kMaximumPatientText = 32767; // Original QLineEdit default maximum.
const qint64 kMaximumCsvBytes = 64 * 1024 * 1024;
const qint64 kMaximumCsvReports = 1000000;

const QString kReportFields = QStringLiteral(
    "id,row_id,sn,sample_id,data_time,state,name,sex,age,department,patient_type,"
    "application_serial,bed_number,jianyan,checker,auditor,remark");

// No transaction is left open on any failure path, including file-write errors.
class Transaction
{
public:
    explicit Transaction(QSqlDatabase &database) : m_database(database) {}
    ~Transaction()
    {
        if (m_active)
            m_database.rollback();
    }
    bool begin(QString *message)
    {
        m_active = m_database.transaction();
        if (!m_active)
            *message = m_database.lastError().text();
        return m_active;
    }
    bool commit(QString *message)
    {
        if (!m_database.commit())
        {
            *message = m_database.lastError().text();
            return false;
        }
        m_active = false;
        return true;
    }
private:
    QSqlDatabase &m_database;
    bool m_active = false;
};

bool execSql(QSqlDatabase &database, const QString &sql, QString *message)
{
    QSqlQuery query(database);
    if (!query.exec(sql))
    {
        *message = query.lastError().text();
        return false;
    }
    return true;
}

bool runPrepared(QSqlQuery &query, const QString &sql,
                 const QVariantList &bindings, QString *message)
{
    if (!query.prepare(sql))
    {
        *message = query.lastError().text();
        return false;
    }
    for (const QVariant &binding : bindings)
    {
        // Original std::string fields represent an empty value, never SQL
        // NULL. QString::clear/default construction must retain that meaning.
        if (binding.type() == QVariant::String && binding.isNull())
            query.addBindValue(QStringLiteral(""));
        else
            query.addBindValue(binding);
    }
    if (!query.exec())
    {
        *message = query.lastError().text();
        return false;
    }
    return true;
}

bool hasReferenceComponent(const QString &path)
{
    const QStringList components = QDir::fromNativeSeparators(path).split('/');
    for (const QString &component : components)
    {
        if (component.compare(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive) == 0
            || component.compare(QStringLiteral("Seerain450_PC"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool hasSymlinkComponent(const QString &path)
{
    QString current = QDir::cleanPath(path);
    while (!current.isEmpty())
    {
        const QFileInfo info(current);
        if (info.isSymLink())
            return true;
        const QString parent = info.dir().absolutePath();
        if (parent == current)
            break;
        current = parent;
    }
    return false;
}

QString whereClause(const HistoryFilter &filter, QVariantList *bindings)
{
    QString where = QStringLiteral("visible=1");
    // The original QueryReportDatas tests start < end, not the dialog checkbox.
    // Keep its actual SQL behavior until a separately approved behavior change.
    if (filter.start < filter.end)
    {
        where += QStringLiteral(" AND data_time BETWEEN ? AND ?");
        bindings->append(filter.start.toString(QStringLiteral("yyyyMMddHHmmss")));
        bindings->append(filter.end.toString(QStringLiteral("yyyyMMddHHmmss")));
    }
    if (!filter.sampleNumber.isEmpty())
    {
        where += QStringLiteral(" AND sn=?");
        bindings->append(filter.sampleNumber.toInt());
    }
    if (!filter.barcode.isEmpty())
    {
        where += QStringLiteral(" AND sample_id=?");
        bindings->append(filter.barcode);
    }
    return where;
}

HistoryReport reportFromQuery(const QSqlQuery &query)
{
    HistoryReport report;
    report.id = query.value(0).toLongLong();
    report.rowId = query.value(1).toLongLong();
    report.sampleNumber = query.value(2).toInt();
    report.barcode = query.value(3).toString();
    report.dateTime = QDateTime::fromString(query.value(4).toString(), QStringLiteral("yyyyMMddHHmmss"));
    report.state = query.value(5).toString();
    HistoryPatient &patient = report.patient;
    patient.reportId = report.id;
    patient.name = query.value(6).toString();
    patient.sex = query.value(7).toInt();
    patient.age = query.value(8).toInt();
    patient.department = query.value(9).toString();
    patient.patientType = query.value(10).toString();
    patient.applicationSerial = query.value(11).toString();
    patient.bedNumber = query.value(12).toString();
    patient.examiner = query.value(13).toString();
    patient.sender = query.value(14).toString();
    patient.auditor = query.value(15).toString();
    patient.remark = query.value(16).toString();
    return report;
}

QString columnKey(const QString &project, int resultType)
{
    return project + QChar(0x1f) + QString::number(resultType);
}

bool boundedText(const QString &text, int maximum)
{
    return text.size() <= maximum && !text.contains(QChar(0));
}

bool validatePatient(const HistoryPatient &patient, QString *message)
{
    if (patient.reportId <= 0 || patient.sex < -1 || patient.sex > 1)
    {
        *message = QStringLiteral("报告编号或性别值无效。");
        return false;
    }
    const QStringList fields = {patient.name, patient.department, patient.patientType,
        patient.applicationSerial, patient.bedNumber, patient.examiner,
        patient.sender, patient.auditor, patient.remark};
    for (const QString &field : fields)
    {
        if (!boundedText(field, kMaximumPatientText))
        {
            *message = QStringLiteral("患者字段超过32767字符或包含空字符。");
            return false;
        }
    }
    // The original detail UI converts age with QString::toInt and has no
    // additional clinical age rule. Keep the signed int instead of inventing one.
    return true;
}

QString csvLine(const QStringList &cells)
{
    QStringList quoted;
    quoted.reserve(cells.size());
    for (QString cell : cells)
    {
        cell.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        quoted.append(QChar('"') + cell + QChar('"'));
    }
    return quoted.join(QChar(',')) + QStringLiteral("\r\n");
}

bool writeCsvLine(QFile &file, QTextCodec *codec, const QStringList &cells,
                  qint64 *bytes, QString *message)
{
    const QString line = csvLine(cells);
    QTextCodec::ConverterState state;
    const QByteArray encoded = codec->fromUnicode(line.constData(), line.size(), &state);
    if (state.invalidChars != 0)
    {
        *message = QStringLiteral("存在GBK不能表示的字符；导出已取消，未替换原字符。");
        return false;
    }
    if (*bytes + encoded.size() > kMaximumCsvBytes)
    {
        *message = QStringLiteral("CSV超过64 MiB离线容量上限；请缩小筛选范围。");
        return false;
    }
    if (file.write(encoded) != encoded.size())
    {
        *message = file.errorString();
        return false;
    }
    *bytes += encoded.size();
    return true;
}
}

HistoryStore::HistoryStore(QObject *parent) : QObject(parent) {}

HistoryStore::~HistoryStore()
{
    // The owner must invoke close before quitting the worker, then deleteLater.
    Q_ASSERT(!m_database.isValid() || QThread::currentThread() == thread());
    releaseDatabase();
}

void HistoryStore::registerTypes()
{
    qRegisterMetaType<AccountSession>("AccountSession");
    qRegisterMetaType<HistoryDisplayColumn>("HistoryDisplayColumn");
    qRegisterMetaType<HistoryDisplayColumns>("HistoryDisplayColumns");
    qRegisterMetaType<HistoryFilter>("HistoryFilter");
    qRegisterMetaType<HistoryPatient>("HistoryPatient");
    qRegisterMetaType<HistoryResultValue>("HistoryResultValue");
    qRegisterMetaType<HistoryResultValues>("HistoryResultValues");
    qRegisterMetaType<HistoryResult>("HistoryResult");
    qRegisterMetaType<HistoryResults>("HistoryResults");
    qRegisterMetaType<HistoryReport>("HistoryReport");
    qRegisterMetaType<HistoryReports>("HistoryReports");
    qRegisterMetaType<HistoryIds>("HistoryIds");
    qRegisterMetaType<HistoryIds>("QList<qint64>");
    qRegisterMetaType<HistoryTaskReservation>("HistoryTaskReservation");
    qRegisterMetaType<HistoryTaskReservations>("HistoryTaskReservations");
    qRegisterMetaType<HistoryReportReservation>("HistoryReportReservation");
    qRegisterMetaType<HistoryReportReservations>("HistoryReportReservations");
    qRegisterMetaType<HistoryReservationBatch>("HistoryReservationBatch");
    qRegisterMetaType<HistoryComputedResult>("HistoryComputedResult");
}

bool HistoryStore::checkThread(qint64 requestId, const QString &operation)
{
    if (QThread::currentThread() != thread())
    {
        emit error(requestId, operation, QStringLiteral("历史存储只能通过队列在其所属工作线程调用。"));
        return false;
    }
    return true;
}

bool HistoryStore::checkReady(qint64 requestId, const QString &operation)
{
    if (!checkThread(requestId, operation))
        return false;
    if (!m_ready)
    {
        emit error(requestId, operation, QStringLiteral("历史存储尚未初始化或已关闭。"));
        return false;
    }
    return true;
}

bool HistoryStore::validateFilter(const HistoryFilter &filter, QString *message) const
{
    if (!filter.start.isValid() || !filter.end.isValid() || filter.start > filter.end)
    {
        *message = QStringLiteral("查询日期无效或起始时间晚于结束时间。");
        return false;
    }
    bool numberOk = false;
    if (!filter.sampleNumber.isEmpty())
    {
        filter.sampleNumber.toInt(&numberOk);
        if (!numberOk || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(filter.sampleNumber).hasMatch())
        {
            *message = QStringLiteral("样本号必须为非负整数；按原sn字段精确查询。");
            return false;
        }
    }
    if (filter.offset < 0 || filter.limit < 1 || filter.limit > 100
        || !boundedText(filter.barcode, 32767))
    {
        *message = QStringLiteral("分页范围无效（每页1至100条）或条码过长。");
        return false;
    }
    return true;
}

bool HistoryStore::prepareDirectory(const QString &directory, QString *message)
{
    if (!QDir::isAbsolutePath(directory) || hasReferenceComponent(directory)
        || hasSymlinkComponent(directory))
    {
        *message = QStringLiteral("必须显式选择绝对隔离目录；禁止参考工程路径和符号链接。");
        return false;
    }
    QDir folder(QDir::cleanPath(directory));
    if (!folder.exists() && !QDir().mkpath(folder.absolutePath()))
    {
        *message = QStringLiteral("不能创建历史隔离目录。");
        return false;
    }
    if (!QFileInfo(folder.absolutePath()).isDir())
    {
        *message = QStringLiteral("历史隔离路径不是目录。");
        return false;
    }
    const QString canonical = QFileInfo(folder.absolutePath()).canonicalFilePath();
    if (canonical.isEmpty() || hasReferenceComponent(canonical))
    {
        *message = QStringLiteral("不能确认历史隔离目录的真实路径。");
        return false;
    }
    folder.setPath(canonical);
    const QString markerPath = folder.filePath(QString::fromLatin1(kMarker));
    if (QFileInfo::exists(markerPath))
    {
        QFile marker(markerPath);
        if (QFileInfo(markerPath).isSymLink() || !marker.open(QIODevice::ReadOnly)
            || marker.size() != qint64(sizeof(kMarkerContent) - 1)
            || marker.readAll() != QByteArray(kMarkerContent))
        {
            *message = QStringLiteral("隔离目录标记不匹配；不会读取其中数据库。");
            return false;
        }
    }
    else
    {
        // An existing unmarked database is never opened, even if it happens
        // to have familiar report_data/report_results table names.
        if (!folder.entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                              | QDir::NoDotAndDotDot).isEmpty())
        {
            *message = QStringLiteral("现有目录没有O04匿名标记且非空；不会接触已有库或配置。");
            return false;
        }
        QSaveFile marker(markerPath);
        marker.setDirectWriteFallback(false);
        if (!marker.open(QIODevice::WriteOnly)
            || marker.write(kMarkerContent) != qint64(sizeof(kMarkerContent) - 1)
            || !marker.commit())
        {
            *message = QStringLiteral("不能创建匿名隔离标记：") + marker.errorString();
            return false;
        }
    }
    const QString databasePath = folder.filePath(QStringLiteral("history.sqlite"));
    const QString exportPath = folder.filePath(QStringLiteral("exports"));
    if (QFileInfo(databasePath).isSymLink() || hasSymlinkComponent(exportPath)
        || (QFileInfo::exists(databasePath) && !QFileInfo(databasePath).isFile()))
    {
        *message = QStringLiteral("数据库或导出目录类型不安全，拒绝打开。");
        return false;
    }
    if (!folder.mkpath(QStringLiteral("exports")))
    {
        *message = QStringLiteral("不能创建隔离导出目录。");
        return false;
    }
    m_directory = folder.absolutePath();
    return true;
}

void HistoryStore::initialize(QString directory)
{
    if (!checkThread(0, QStringLiteral("initialize")))
        return;
    releaseDatabase();
    QString message;
    if (!prepareDirectory(directory, &message))
    {
        emit error(0, QStringLiteral("initialize"), message);
        return;
    }
    // Serialize only initialization across processes. Without this guard two
    // first launches could both see a missing DB, then one failed creator could
    // remove the other creator's committed file during its own rollback cleanup.
    QLockFile initializationLock(QDir(m_directory).filePath(QStringLiteral(".history-init.lock")));
    if (!initializationLock.tryLock(200))
    {
        emit error(0, QStringLiteral("initialize"), QStringLiteral("另一进程正在初始化该匿名目录；请稍后重试。"));
        return;
    }
    const QString path = QDir(m_directory).filePath(QStringLiteral("history.sqlite"));
    const bool existing = QFileInfo::exists(path);
    m_connectionName = QStringLiteral("sn100-history-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(path);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=200"));
    bool ok = m_database.open();
    if (!ok)
        message = m_database.lastError().text();
    if (ok)
        ok = execSql(m_database, QStringLiteral("PRAGMA busy_timeout=200"), &message)
            && execSql(m_database, QStringLiteral("PRAGMA foreign_keys=ON"), &message);
    if (ok)
        ok = existing ? validateDatabase(&message) : createAndSeed(&message);
    if (!ok)
    {
        releaseDatabase();
        if (!existing)
            QFile::remove(path); // Only remove the newly created failed offline DB.
        emit error(0, QStringLiteral("initialize"), message);
        return;
    }
    m_ready = true;
    emit ready(0, path);
}

bool HistoryStore::createAndSeed(QString *message)
{
    QFile fixture(QStringLiteral(":/fixtures/history_reports.json"));
    if (!fixture.open(QIODevice::ReadOnly) || fixture.size() > 1024 * 1024)
    {
        *message = QStringLiteral("内嵌匿名夹具缺失或超过1 MiB；不从外部路径寻找数据。");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(fixture.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(QStringLiteral("schema")).toString() != QLatin1String(kSchema))
    {
        *message = QStringLiteral("内嵌匿名夹具格式/版本错误：") + parseError.errorString();
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonArray columns = root.value(QStringLiteral("columns")).toArray();
    const QJsonArray reports = root.value(QStringLiteral("reports")).toArray();
    if (columns.isEmpty() || columns.size() > kMaximumColumns
        || reports.isEmpty() || reports.size() > 100)
    {
        *message = QStringLiteral("匿名夹具列数或报告数超出离线容量范围。");
        return false;
    }
    Transaction transaction(m_database);
    if (!transaction.begin(message))
        return false;
    const QStringList schema = {
        QStringLiteral("PRAGMA application_id=%1").arg(kApplicationId),
        QStringLiteral("PRAGMA user_version=1"),
        QStringLiteral("CREATE TABLE offline_metadata(schema_marker TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE report_data(id INTEGER PRIMARY KEY,row_id INTEGER NOT NULL UNIQUE,"
            "sn INTEGER NOT NULL,sample_id TEXT NOT NULL,data_time TEXT NOT NULL,visible INTEGER NOT NULL,"
            "state TEXT NOT NULL,name TEXT NOT NULL,sex INTEGER NOT NULL,age INTEGER NOT NULL,"
            "department TEXT NOT NULL,patient_type TEXT NOT NULL,application_serial TEXT NOT NULL,"
            "bed_number TEXT NOT NULL,jianyan TEXT NOT NULL,checker TEXT NOT NULL,auditor TEXT NOT NULL,remark TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX history_date ON report_data(visible,data_time,id)"),
        QStringLiteral("CREATE INDEX history_barcode ON report_data(sample_id)"),
        QStringLiteral("CREATE TABLE report_columns(position INTEGER NOT NULL UNIQUE,project TEXT NOT NULL,"
            "result_type INTEGER NOT NULL,label TEXT NOT NULL,unit TEXT NOT NULL,decimals INTEGER NOT NULL,"
            "factor REAL NOT NULL,PRIMARY KEY(project,result_type))"),
        QStringLiteral("CREATE TABLE report_results(task_id INTEGER PRIMARY KEY,fid INTEGER NOT NULL REFERENCES report_data(row_id),"
            "project_name TEXT NOT NULL,date_time TEXT NOT NULL,abnormal_type TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX history_result_fid ON report_results(fid,task_id)"),
        QStringLiteral("CREATE TABLE report_values(task_id INTEGER NOT NULL REFERENCES report_results(task_id),"
            "result_type INTEGER NOT NULL,raw_value TEXT NOT NULL,PRIMARY KEY(task_id,result_type))")
    };
    for (const QString &sql : schema)
    {
        if (!execSql(m_database, sql, message))
            return false;
    }
    QSqlQuery metadata(m_database);
    if (!runPrepared(metadata, QStringLiteral("INSERT INTO offline_metadata VALUES(?)"),
                     {QString::fromLatin1(kSchema)}, message))
        return false;
    QSet<QString> knownColumns;
    for (int index = 0; index < columns.size(); ++index)
    {
        const QJsonObject column = columns.at(index).toObject();
        const QString project = column.value(QStringLiteral("project")).toString();
        const int type = column.value(QStringLiteral("resultType")).toInt();
        const QString label = column.value(QStringLiteral("label")).toString();
        const QString unit = column.value(QStringLiteral("unit")).toString();
        const int decimals = column.value(QStringLiteral("decimals")).toInt(-1);
        const double factor = column.value(QStringLiteral("factor")).toDouble(1.0);
        const QString key = columnKey(project, type);
        if (project.isEmpty() || !boundedText(project, 128) || !boundedText(label, 128)
            || !boundedText(unit, 64) || type < 1 || type > 6 || decimals < -1 || decimals > 12
            || !std::isfinite(factor) || factor <= 0 || knownColumns.contains(key))
        {
            *message = QStringLiteral("匿名结果列契约无效或重复。");
            return false;
        }
        knownColumns.insert(key);
        QSqlQuery insert(m_database);
        if (!runPrepared(insert, QStringLiteral("INSERT INTO report_columns VALUES(?,?,?,?,?,?,?)"),
                          {index, project, type, label, unit, decimals, factor}, message))
            return false;
    }
    for (const QJsonValue &reportValue : reports)
    {
        const QJsonObject report = reportValue.toObject();
        const QJsonObject patientObject = report.value(QStringLiteral("patient")).toObject();
        const qint64 id = report.value(QStringLiteral("id")).toVariant().toLongLong();
        const qint64 rowId = report.value(QStringLiteral("rowId")).toVariant().toLongLong();
        const int sn = report.value(QStringLiteral("sampleNumber")).toInt(-1);
        const QDateTime dateTime = QDateTime::fromString(report.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
        const QString barcode = report.value(QStringLiteral("barcode")).toString();
        const QString state = report.value(QStringLiteral("state")).toString();
        HistoryPatient patient;
        patient.reportId = id;
        patient.name = patientObject.value(QStringLiteral("name")).toString();
        patient.sex = patientObject.value(QStringLiteral("sex")).toInt(-1);
        patient.age = patientObject.value(QStringLiteral("age")).toInt();
        patient.department = patientObject.value(QStringLiteral("department")).toString();
        patient.patientType = patientObject.value(QStringLiteral("patientType")).toString();
        patient.applicationSerial = patientObject.value(QStringLiteral("applicationSerial")).toString();
        patient.bedNumber = patientObject.value(QStringLiteral("bedNumber")).toString();
        patient.examiner = patientObject.value(QStringLiteral("examiner")).toString();
        patient.sender = patientObject.value(QStringLiteral("sender")).toString();
        patient.auditor = patientObject.value(QStringLiteral("auditor")).toString();
        patient.remark = patientObject.value(QStringLiteral("remark")).toString();
        if (!validatePatient(patient, message) || rowId <= 0 || sn < 0 || !dateTime.isValid()
            || !boundedText(barcode, 32767) || !boundedText(state, 32767))
        {
            if (message->isEmpty())
                *message = QStringLiteral("匿名报告字段无效。");
            return false;
        }
        QSqlQuery insert(m_database);
        if (!runPrepared(insert, QStringLiteral("INSERT INTO report_data VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
            {id, rowId, sn, barcode, dateTime.toString(QStringLiteral("yyyyMMddHHmmss")),
             report.value(QStringLiteral("visible")).toBool() ? 1 : 0, state, patient.name,
             patient.sex, patient.age, patient.department, patient.patientType,
             patient.applicationSerial, patient.bedNumber, patient.examiner, patient.sender,
             patient.auditor, patient.remark}, message))
            return false;
        const QJsonArray results = report.value(QStringLiteral("results")).toArray();
        if (results.isEmpty() || results.size() > kMaximumTasksPerReport)
        {
            *message = QStringLiteral("匿名报告任务数必须为1至64。");
            return false;
        }
        for (const QJsonValue &resultValue : results)
        {
            const QJsonObject result = resultValue.toObject();
            const qint64 taskId = result.value(QStringLiteral("taskId")).toVariant().toLongLong();
            const QString project = result.value(QStringLiteral("project")).toString();
            const QString abnormal = result.value(QStringLiteral("abnormalType")).toString();
            const QDateTime testedAt = QDateTime::fromString(result.value(QStringLiteral("testedAt")).toString(), Qt::ISODate);
            const QJsonObject values = result.value(QStringLiteral("values")).toObject();
            if (taskId <= 0 || !testedAt.isValid() || values.isEmpty() || values.size() > 6
                || !boundedText(abnormal, 4096))
            {
                *message = QStringLiteral("匿名任务字段无效或结果数超出1至6。");
                return false;
            }
            QSqlQuery task(m_database);
            if (!runPrepared(task, QStringLiteral("INSERT INTO report_results VALUES(?,?,?,?,?)"),
                {taskId, rowId, project, testedAt.toString(QStringLiteral("yyyyMMddHHmmss")), abnormal}, message))
                return false;
            for (auto iterator = values.begin(); iterator != values.end(); ++iterator)
            {
                bool ok = false;
                const int type = iterator.key().toInt(&ok);
                const QString raw = iterator.value().toString();
                if (!ok || !knownColumns.contains(columnKey(project, type))
                    || !iterator.value().isString() || !boundedText(raw, 4096))
                {
                    *message = QStringLiteral("匿名任务包含未知列或超长原始结果。");
                    return false;
                }
                QSqlQuery value(m_database);
                if (!runPrepared(value, QStringLiteral("INSERT INTO report_values VALUES(?,?,?)"),
                                  {taskId, type, raw}, message))
                    return false;
            }
        }
    }
    return transaction.commit(message);
}

bool HistoryStore::validateDatabase(QString *message)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA application_id")) || !query.next()
        || query.value(0).toInt() != kApplicationId)
    {
        *message = QStringLiteral("已有库不是O04匿名库；不会迁移或读取业务表。");
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next() || query.value(0).toInt() != 1)
    {
        *message = QStringLiteral("已有O04匿名库版本不兼容；保留原文件。");
        return false;
    }
    if (!query.exec(QStringLiteral("SELECT schema_marker FROM offline_metadata"))
        || !query.next() || query.value(0).toString() != QLatin1String(kSchema) || query.next())
    {
        *message = QStringLiteral("已有库匿名元数据标记无效。");
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA quick_check")) || !query.next()
        || query.value(0).toString() != QStringLiteral("ok"))
    {
        *message = QStringLiteral("匿名库完整性检查未通过：") + query.lastError().text();
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA foreign_key_check")) || query.next())
    {
        *message = QStringLiteral("匿名库关联完整性检查未通过。");
        return false;
    }
    return true;
}

bool HistoryStore::readResults(qint64 rowId, HistoryResults *results, QString *message)
{
    results->clear();
    QSqlQuery tasks(m_database);
    if (!runPrepared(tasks, QStringLiteral("SELECT task_id,project_name,date_time,abnormal_type "
        "FROM report_results WHERE fid=? ORDER BY task_id LIMIT 65"), {rowId}, message))
        return false;
    while (tasks.next())
    {
        if (results->size() == kMaximumTasksPerReport)
        {
            *message = QStringLiteral("单份匿名报告超过64任务上限；拒绝截断结果。");
            return false;
        }
        HistoryResult result;
        result.taskId = tasks.value(0).toLongLong();
        result.project = tasks.value(1).toString();
        result.testedAt = QDateTime::fromString(tasks.value(2).toString(), QStringLiteral("yyyyMMddHHmmss"));
        result.abnormalType = tasks.value(3).toString();
        QSqlQuery values(m_database);
        if (!runPrepared(values, QStringLiteral("SELECT v.result_type,v.raw_value,c.label,c.unit,c.decimals,c.factor "
            "FROM report_values v LEFT JOIN report_columns c "
            "ON c.project=? AND c.result_type=v.result_type WHERE v.task_id=? ORDER BY c.position,v.result_type LIMIT 7"),
            {result.project, result.taskId}, message))
            return false;
        while (values.next())
        {
            if (result.values.size() == 6)
            {
                *message = QStringLiteral("单任务超过6结果上限；拒绝截断结果。");
                return false;
            }
            HistoryResultValue value;
            value.resultType = values.value(0).toInt();
            value.rawValue = values.value(1).toString();
            value.label = values.value(2).toString();
            value.unit = values.value(3).toString();
            value.decimals = values.value(4).isNull() ? -1 : values.value(4).toInt();
            value.factor = values.value(5).isNull() ? 1.0 : values.value(5).toDouble();
            if (value.label.isEmpty())
                value.label = result.project + QStringLiteral(" / ") + QString::number(value.resultType);
            result.values.append(value);
        }
        if (values.lastError().isValid())
        {
            *message = values.lastError().text();
            return false;
        }
        results->append(result);
    }
    if (tasks.lastError().isValid())
    {
        *message = tasks.lastError().text();
        return false;
    }
    return true;
}

bool HistoryStore::readReport(qint64 id, HistoryReport *report, QString *message)
{
    QSqlQuery query(m_database);
    if (!runPrepared(query, QStringLiteral("SELECT ") + kReportFields
                     + QStringLiteral(" FROM report_data WHERE id=? AND visible=1"), {id}, message))
        return false;
    if (!query.next())
    {
        *message = QStringLiteral("所选报告不存在或已隐藏；请重新查询。");
        return false;
    }
    *report = reportFromQuery(query);
    return readResults(report->rowId, &report->results, message);
}

void HistoryStore::query(qint64 requestId, HistoryFilter filter)
{
    const QString operation = QStringLiteral("query");
    if (!checkReady(requestId, operation))
        return;
    QString message;
    if (!validateFilter(filter, &message))
    {
        emit error(requestId, operation, message);
        return;
    }
    Transaction transaction(m_database);
    if (!transaction.begin(&message))
    {
        emit error(requestId, operation, message);
        return;
    }
    QVariantList bindings;
    const QString where = whereClause(filter, &bindings);
    QSqlQuery count(m_database);
    if (!runPrepared(count, QStringLiteral("SELECT COUNT(*) FROM report_data WHERE ") + where, bindings, &message)
        || !count.next())
    {
        emit error(requestId, operation, message.isEmpty() ? count.lastError().text() : message);
        return;
    }
    const qint64 totalCount = count.value(0).toLongLong();
    count.finish();
    QVariantList pageBindings = bindings;
    pageBindings << filter.limit << filter.offset;
    QSqlQuery page(m_database);
    if (!runPrepared(page, QStringLiteral("SELECT ") + kReportFields
        + QStringLiteral(" FROM report_data WHERE ") + where
        + QStringLiteral(" ORDER BY id LIMIT ? OFFSET ?"), pageBindings, &message))
    {
        emit error(requestId, operation, message);
        return;
    }
    HistoryReports reports;
    while (page.next())
    {
        HistoryReport report = reportFromQuery(page);
        if (!readResults(report.rowId, &report.results, &message))
        {
            emit error(requestId, operation, message);
            return;
        }
        reports.append(report);
    }
    if (page.lastError().isValid())
    {
        emit error(requestId, operation, page.lastError().text());
        return;
    }
    page.finish();
    HistoryDisplayColumns displayColumns, displayDefaults;
    if (!readDisplay(&displayColumns, &displayDefaults, &message))
    {
        emit error(requestId, operation, message);
        return;
    }
    if (!transaction.commit(&message))
    {
        emit error(requestId, operation, message);
        return;
    }
    emit displayLoaded(requestId, displayColumns, displayDefaults);
    emit queryReady(requestId, filter, reports, totalCount);
}

void HistoryStore::detail(qint64 requestId, qint64 reportId)
{
    const QString operation = QStringLiteral("detail");
    if (!checkReady(requestId, operation))
        return;
    QString message;
    HistoryReport report;
    Transaction transaction(m_database);
    if (!transaction.begin(&message) || !readReport(reportId, &report, &message)
        || !transaction.commit(&message))
    {
        emit error(requestId, operation, message);
        return;
    }
    emit detailReady(requestId, report);
}

void HistoryStore::savePatient(qint64 requestId, HistoryPatient patient)
{
    const QString operation = QStringLiteral("savePatient");
    if (!checkReady(requestId, operation))
        return;
    QString message;
    if (!validatePatient(patient, &message))
    {
        emit error(requestId, operation, message);
        return;
    }
    Transaction transaction(m_database);
    if (!transaction.begin(&message))
    {
        emit error(requestId, operation, message);
        return;
    }
    QSqlQuery update(m_database);
    if (!runPrepared(update, QStringLiteral("UPDATE report_data SET name=?,sex=?,age=?,department=?,"
        "patient_type=?,application_serial=?,bed_number=?,jianyan=?,checker=?,auditor=?,remark=? WHERE id=? AND visible=1"),
        {patient.name, patient.sex, patient.age, patient.department, patient.patientType,
         patient.applicationSerial, patient.bedNumber, patient.examiner, patient.sender,
         patient.auditor, patient.remark, patient.reportId}, &message))
    {
        emit error(requestId, operation, message);
        return;
    }
    if (update.numRowsAffected() != 1)
    {
        emit error(requestId, operation, QStringLiteral("所选报告不存在或已隐藏；未保存任何字段。"));
        return;
    }
    update.finish();
    HistoryReport saved;
    if (!readReport(patient.reportId, &saved, &message) || !transaction.commit(&message))
    {
        emit error(requestId, operation, message);
        return;
    }
    // Only acknowledge the persisted value after commit; UI must not announce
    // success merely because it accepted an edit or queued this request.
    emit patientSaved(requestId, saved.patient);
}

void HistoryStore::exportCsv(qint64 requestId, HistoryFilter filter,
                            HistoryIds selectedIds, QString path)
{
    const QString operation = QStringLiteral("exportCsv");
    if (!checkReady(requestId, operation))
        return;
    if (m_resultIngest)
    {
        emit error(requestId, operation, QStringLiteral("本地结果的单位和精度尚未映射，暂不开放文件输出。"));
        return;
    }
    QString message;
    if (!validateFilter(filter, &message))
    {
        emit error(requestId, operation, message);
        return;
    }
    if (selectedIds.size() > 100)
    {
        emit error(requestId, operation, QStringLiteral("单次选中导出最多100份；全筛选导出请传空选中列表。"));
        return;
    }
    QSet<qint64> uniqueIds;
    for (qint64 id : selectedIds)
    {
        if (id <= 0 || uniqueIds.contains(id))
        {
            emit error(requestId, operation, QStringLiteral("选中报告编号无效或重复。"));
            return;
        }
        uniqueIds.insert(id);
    }
    const QString exportDirectory = QDir(m_directory).filePath(QStringLiteral("exports"));
    const QFileInfo output(path);
    if (!QDir::isAbsolutePath(path) || hasReferenceComponent(path) || hasSymlinkComponent(path)
        || output.dir().canonicalPath() != QFileInfo(exportDirectory).canonicalFilePath()
        || output.suffix().compare(QStringLiteral("csv"), Qt::CaseInsensitive) != 0
        || output.fileName().startsWith('.') || QFileInfo::exists(path))
    {
        emit error(requestId, operation, QStringLiteral("请选择隔离目录exports下新的.csv文件；不会覆盖现有文件。"));
        return;
    }
    QTextCodec *codec = QTextCodec::codecForName("GBK");
    if (!codec)
    {
        emit error(requestId, operation, QStringLiteral("当前Qt没有GBK编码支持，不能执行原格式导出。"));
        return;
    }
    Transaction transaction(m_database);
    if (!transaction.begin(&message))
    {
        emit error(requestId, operation, message);
        return;
    }
    QVariantList bindings;
    QString where = whereClause(filter, &bindings);
    if (!selectedIds.isEmpty())
    {
        QStringList placeholders;
        for (qint64 id : selectedIds)
        {
            placeholders.append(QStringLiteral("?"));
            bindings.append(id);
        }
        where += QStringLiteral(" AND id IN (") + placeholders.join(',') + QChar(')');
    }
    QSqlQuery count(m_database);
    if (!runPrepared(count, QStringLiteral("SELECT COUNT(*) FROM report_data WHERE ") + where,
                     bindings, &message) || !count.next())
    {
        emit error(requestId, operation, message.isEmpty() ? count.lastError().text() : message);
        return;
    }
    const qint64 expectedReports = count.value(0).toLongLong();
    count.finish();
    if (expectedReports == 0 || expectedReports > kMaximumCsvReports
        || (!selectedIds.isEmpty() && expectedReports != selectedIds.size()))
    {
        emit error(requestId, operation, QStringLiteral("导出为空、超过100万报告容量，或选中报告已不符合筛选；请重新查询。"));
        return;
    }
    struct CsvColumn { QString project; int type; QString title; };
    QList<CsvColumn> columns;
    QStringList header = {QStringLiteral("样本号"), QStringLiteral("样本条码"), QStringLiteral("报告日期"),
        QStringLiteral("姓名"), QStringLiteral("年龄"), QStringLiteral("性别")};
    QSqlQuery columnQuery(m_database);
    if (!columnQuery.exec(QStringLiteral("SELECT project,result_type,label,unit FROM report_columns ORDER BY position LIMIT 65")))
    {
        emit error(requestId, operation, columnQuery.lastError().text());
        return;
    }
    while (columnQuery.next())
    {
        if (columns.size() == kMaximumColumns)
        {
            emit error(requestId, operation, QStringLiteral("导出结果列超过64列上限；拒绝截断。"));
            return;
        }
        CsvColumn column{columnQuery.value(0).toString(), columnQuery.value(1).toInt(), columnQuery.value(2).toString()};
        const QString unit = columnQuery.value(3).toString();
        if (!unit.isEmpty())
            column.title += QChar(' ') + unit;
        columns.append(column);
        header.append(column.title);
    }
    columnQuery.finish();
    QTemporaryFile file(QDir(exportDirectory).filePath(QStringLiteral(".history-XXXXXX.tmp")));
    if (!file.open())
    {
        emit error(requestId, operation, file.errorString());
        return;
    }
    qint64 bytes = 0;
    if (!writeCsvLine(file, codec, header, &bytes, &message))
    {
        emit error(requestId, operation, message);
        return;
    }
    // Forward-only cursor and one report's bounded result set: all-query export
    // does not accumulate the entire database or a QtCSV StringData document.
    QSqlQuery reports(m_database);
    reports.setForwardOnly(true);
    if (!runPrepared(reports, QStringLiteral("SELECT ") + kReportFields
        + QStringLiteral(" FROM report_data WHERE ") + where + QStringLiteral(" ORDER BY id"), bindings, &message))
    {
        emit error(requestId, operation, message);
        return;
    }
    qint64 reportCount = 0;
    qint64 rowCount = 0;
    while (reports.next())
    {
        if (QThread::currentThread()->isInterruptionRequested())
        {
            emit error(requestId, operation, QStringLiteral("导出已取消；未保留不完整CSV。"));
            return;
        }
        HistoryReport report = reportFromQuery(reports);
        if (!readResults(report.rowId, &report.results, &message))
        {
            emit error(requestId, operation, message);
            return;
        }
        QMap<QString, QList<HistoryResult>> byProject;
        int repeatedRows = 1;
        for (const HistoryResult &result : report.results)
        {
            // Refuse a silently omitted project after external anonymous-db edits.
            for (const HistoryResultValue &value : result.values)
            {
                bool found = false;
                for (const CsvColumn &column : columns)
                {
                    if (column.project == result.project && column.type == value.resultType)
                        found = true;
                }
                if (!found)
                {
                    emit error(requestId, operation, QStringLiteral("结果缺少已知导出列；请补充匿名字段契约，未猜测项目配置。"));
                    return;
                }
            }
            byProject[result.project].append(result);
            repeatedRows = qMax(repeatedRows, byProject[result.project].size());
        }
        for (int occurrence = 0; occurrence < repeatedRows; ++occurrence)
        {
            QStringList cells = {QString::number(report.sampleNumber), report.barcode,
                report.dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                report.patient.name, QString::number(report.patient.age), sexText(report.patient.sex)};
            for (const CsvColumn &column : columns)
            {
                QString cell = QStringLiteral(" "); // Original missing-project placeholder.
                const auto project = byProject.constFind(column.project);
                if (project != byProject.constEnd() && occurrence < project.value().size())
                {
                    for (const HistoryResultValue &value : project.value().at(occurrence).values)
                    {
                        if (value.resultType == column.type)
                            cell = displayValue(value);
                    }
                }
                cells.append(cell);
            }
            if (!writeCsvLine(file, codec, cells, &bytes, &message))
            {
                emit error(requestId, operation, message);
                return;
            }
            ++rowCount;
        }
        ++reportCount;
    }
    if (reports.lastError().isValid())
    {
        emit error(requestId, operation, reports.lastError().text());
        return;
    }
    reports.finish();
    if (reportCount != expectedReports || !file.flush())
    {
        emit error(requestId, operation, reportCount != expectedReports
            ? QStringLiteral("导出数量不一致，未生成CSV。") : file.errorString());
        return;
    }
    if (!transaction.commit(&message))
    {
        emit error(requestId, operation, message);
        return;
    }
    file.close();
    // QFile::rename refuses an existing target, unlike a replacing save. A
    // failed export removes only its own temporary file via autoRemove.
    if (!file.rename(output.absoluteFilePath()))
    {
        emit error(requestId, operation, QStringLiteral("不能发布CSV（目标已存在或目录不可写）：") + file.errorString());
        return;
    }
    file.setAutoRemove(false);
    emit csvExported(requestId, output.absoluteFilePath(), reportCount, rowCount);
}

void HistoryStore::releaseDatabase()
{
    m_ready = false;
    m_resultProjects.reset(); // Close the owned real catalog connection in this worker.
    m_resultIngest = false;
    m_resultStoreId.clear();
    m_resultCatalogDirectory.clear();
    if (m_database.isValid())
        m_database.close();
    m_database = QSqlDatabase();
    if (!m_connectionName.isEmpty())
        QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
}

void HistoryStore::close()
{
    if (!checkThread(0, QStringLiteral("close")))
        return;
    releaseDatabase();
    emit closed();
}
