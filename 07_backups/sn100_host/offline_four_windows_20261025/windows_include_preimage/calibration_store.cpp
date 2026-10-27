#include "calibration_store.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const char *kSchema = "sn100-calibration-local-v3";
const int kMaximumVisibleRecords = 2048;

bool boundedText(const QString &value, int maximum, bool allowEmpty = false)
{
    return (allowEmpty || !value.trimmed().isEmpty()) && value.size() <= maximum
        && !value.contains(QChar::Null);
}

bool rawValue(double value)
{
    // The source page permits four decimal places. Tolerance only absorbs the
    // binary representation of a decimal that was already limited to 4 places.
    if (!std::isfinite(value) || value < 0 || value > 99999) return false;
    const double rounded = std::round(value * 10000.0) / 10000.0;
    const double tolerance = 4 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(value));
    return std::abs(value - rounded) <= tolerance;
}

bool execute(QSqlDatabase database, const QString &sql, const QVariantList &values, QString *message)
{
    QSqlQuery query(database);
    if (!query.prepare(sql))
    {
        *message = query.lastError().text();
        return false;
    }
    for (const QVariant &value : values)
    {
        query.addBindValue(value);
    }
    if (!query.exec())
    {
        *message = query.lastError().text();
        return false;
    }
    return true;
}

QString normalizedPath(const QString &path)
{
    const QFileInfo info(path);
    return QDir::cleanPath(info.exists() ? info.canonicalFilePath() : info.absoluteFilePath());
}
}

CalibrationStore::CalibrationStore(QObject *parent) : QObject(parent)
{
}

CalibrationStore::~CalibrationStore()
{
    // Ownership contract requires close in the owning thread before destruction.
    if (QThread::currentThread() == thread())
    {
        release();
    }
}

void CalibrationStore::registerTypes()
{
    qRegisterMetaType<AccountSession>();
    qRegisterMetaType<CalibrationPoint>();
    qRegisterMetaType<CalibrationRecord>();
    qRegisterMetaType<CalibrationRecords>();
    qRegisterMetaType<ProjectRecords>();
    qRegisterMetaType<ProjectUnits>();
    qRegisterMetaType<ReagentSnapshot>();
}

bool CalibrationStore::canEdit(const AccountSession &session)
{
    return session.authenticated && session.actionReady && session.role >= 1 && session.role <= 5;
}

QString CalibrationStore::validate(const CalibrationRecord &record)
{
    if (record.projectId <= 0 || !boundedText(record.projectName, 64))
    {
        return QStringLiteral("请选择有稳定编号的本地项目。");
    }
    if (record.batchId < 0 || (record.batchId == 0 && (!record.batchCode.isEmpty() || !record.batchGroup.isEmpty()))
        || (record.batchId > 0 && !boundedText(record.batchCode, 256)) || record.batchGroup.size() > 256
        || record.batchGroup.contains(QChar::Null))
    {
        return QStringLiteral("批次编号与批号不一致，请重新选择。");
    }
    if (!boundedText(record.bloodBatch, 64) || !boundedText(record.method, 64)
        || !boundedText(record.xTransform, 64) || !boundedText(record.yTransform, 64)
        || !boundedText(record.concentrationUnit, 32) || !boundedText(record.resultUnit, 32)
        || !std::isfinite(record.unitRatio) || record.unitRatio <= 0 || record.unitRatio > 1e12)
    {
        return QStringLiteral("校准品批号、方法、轴变换和单位不能为空或超长，单位倍率必须为正有限值。");
    }
    if (record.id < 0 || record.revision < 0 || record.sourceId < 0 || !record.visible)
    {
        return QStringLiteral("记录编号、版本或可见状态无效。");
    }
    if (record.points.isEmpty() || record.points.size() > 32)
    {
        return QStringLiteral("原始点须为1至32行；空曲线不能保存。");
    }
    const int repeatCount = record.points.first().measurements.size();
    if (repeatCount < 1 || repeatCount > 8)
    {
        return QStringLiteral("每个点须有1至8次原始测试值。");
    }
    QSet<double> concentrations;
    for (int row = 0; row < record.points.size(); ++row)
    {
        const CalibrationPoint &point = record.points.at(row);
        if (point.index != row + 1 || !boundedText(point.sampleRate, 64))
        {
            return QStringLiteral("原始点索引须从1连续递增，稀释比例不能为空或超长。");
        }
        if (!rawValue(point.concentration) || concentrations.contains(point.concentration))
        {
            return QStringLiteral("浓度须为0至99999、最多4位小数的有限数，且各行浓度不能重复。");
        }
        concentrations.insert(point.concentration);
        if (point.measurements.size() != repeatCount)
        {
            return QStringLiteral("各点原始测试次数须一致。");
        }
        for (double measurement : point.measurements)
        {
            if (!rawValue(measurement))
            {
                return QStringLiteral("原始测试值须为0至99999、最多4位小数的有限数。");
            }
        }
    }
    return QString();
}

void CalibrationStore::fail(qint64 requestId, const QString &operation, const QString &message)
{
    emit error(requestId, operation, message);
}

bool CalibrationStore::check(qint64 requestId, const QString &operation, bool write)
{
    if (QThread::currentThread() != thread())
    {
        fail(requestId, operation, QStringLiteral("校准数据库必须在所属工作线程访问。"));
        return false;
    }
    if (!m_ready || !m_database.isOpen() || !m_catalog.isOpen())
    {
        fail(requestId, operation, QStringLiteral("校准库或只读项目库尚未就绪。"));
        return false;
    }
    if (write && !canEdit(m_session))
    {
        fail(requestId, operation, QStringLiteral("当前登录身份不能修改本地校准记录。"));
        return false;
    }
    return true;
}

void CalibrationStore::setSession(AccountSession session)
{
    if (QThread::currentThread() != thread())
    {
        fail(0, QStringLiteral("session"), QStringLiteral("会话必须在校准工作线程更新。"));
        return;
    }
    m_session = session;
}

void CalibrationStore::initialize(QString absoluteDataDir, QString catalogDatabasePath)
{
    const QString operation = QStringLiteral("initialize");
    if (QThread::currentThread() != thread())
    {
        fail(0, operation, QStringLiteral("初始化必须在校准工作线程执行。"));
        return;
    }
    release();
    if (!QFileInfo(absoluteDataDir).isAbsolute() || !QFileInfo(catalogDatabasePath).isAbsolute()
        || absoluteDataDir.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
        || catalogDatabasePath.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
        || !QFileInfo(catalogDatabasePath).isFile())
    {
        fail(0, operation, QStringLiteral("须显式指定绝对数据目录及已有匿名catalog.db，不能使用参考目录。"));
        return;
    }
    if (!QDir().mkpath(absoluteDataDir))
    {
        fail(0, operation, QStringLiteral("无法创建本地校准数据目录。"));
        return;
    }
    absoluteDataDir = normalizedPath(absoluteDataDir);
    catalogDatabasePath = normalizedPath(catalogDatabasePath);
    const QString databasePath = QDir(absoluteDataDir).filePath(QStringLiteral("calibration_local.db"));
    if (absoluteDataDir.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
        || catalogDatabasePath.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
        || normalizedPath(databasePath) == catalogDatabasePath
        || QFileInfo(databasePath).isSymLink())
    {
        fail(0, operation, QStringLiteral("解析后的数据路径不安全，保留已有文件。"));
        return;
    }
    m_catalogConnectionName = QStringLiteral("calibration-catalog-") + QUuid::createUuid().toString();
    m_catalog = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_catalogConnectionName);
    m_catalog.setDatabaseName(catalogDatabasePath);
    m_catalog.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=250"));
    if (!m_catalog.open())
    {
        fail(0, operation, QStringLiteral("无法打开只读项目库：") + m_catalog.lastError().text());
        release();
        return;
    }
    bool catalogMarkerValid = false;
    {
        QSqlQuery metadata(m_catalog);
        catalogMarkerValid = metadata.exec(QStringLiteral("SELECT value FROM catalog_metadata WHERE key='schema'"))
            && metadata.next() && metadata.value(0).toString() == QStringLiteral("sn100-catalog-offline-v1");
    }
    if (!catalogMarkerValid)
    {
        fail(0, operation, QStringLiteral("项目库结构标识不匹配，未写入项目库。"));
        release();
        return;
    }
    const bool existing = QFileInfo::exists(databasePath);
    m_connectionName = QStringLiteral("calibration-local-") + QUuid::createUuid().toString();
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(databasePath);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=250"));
    if (!m_database.open())
    {
        fail(0, operation, m_database.lastError().text());
        release();
        return;
    }
    if (existing)
    {
        bool localMarkerValid = false;
        {
            QSqlQuery metadata(m_database);
            localMarkerValid = metadata.exec(QStringLiteral("SELECT value FROM calibration_metadata WHERE key='schema'"))
                && metadata.next() && metadata.value(0).toString() == QString::fromLatin1(kSchema);
        }
        if (!localMarkerValid)
        {
            fail(0, operation, QStringLiteral("已有文件不是本地校准库，保留原文件。"));
            release();
            return;
        }
    }
    QString message;
    if (!execute(m_database, QStringLiteral("PRAGMA foreign_keys=ON"), {}, &message)
        || !execute(m_database, QStringLiteral("PRAGMA synchronous=FULL"), {}, &message)
        || !beginWrite(&message))
    {
        fail(0, operation, message);
        release();
        return;
    }
    const QStringList schema = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS calibration_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS calibration_records("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,revision INTEGER NOT NULL CHECK(revision>0),"
            "project_id INTEGER NOT NULL,project_name TEXT NOT NULL,batch_id INTEGER NOT NULL,"
            "batch_code TEXT NOT NULL,batch_group TEXT NOT NULL,blood_batch TEXT NOT NULL,created_at TEXT NOT NULL,"
            "changed_at TEXT NOT NULL,"
            "method TEXT NOT NULL,x_transform TEXT NOT NULL,y_transform TEXT NOT NULL,"
            "concentration_unit TEXT NOT NULL,result_unit TEXT NOT NULL,unit_ratio REAL NOT NULL CHECK(unit_ratio>0),"
            "source_id INTEGER NOT NULL DEFAULT 0,visible INTEGER NOT NULL DEFAULT 1 CHECK(visible IN(0,1)),"
            "is_checked INTEGER NOT NULL DEFAULT 0 CHECK(is_checked=0),"
            "is_applied INTEGER NOT NULL DEFAULT 0 CHECK(is_applied=0))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS calibration_points("
            "record_id INTEGER NOT NULL REFERENCES calibration_records(id) ON DELETE RESTRICT,"
            "point_index INTEGER NOT NULL CHECK(point_index BETWEEN 1 AND 32),sample_rate TEXT NOT NULL,"
            "concentration REAL NOT NULL CHECK(concentration BETWEEN 0 AND 99999),measurements TEXT NOT NULL,"
            "PRIMARY KEY(record_id,point_index),UNIQUE(record_id,concentration))"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS calibration_project ON calibration_records(project_id,visible,id)")
    };
    for (const QString &sql : schema)
    {
        if (!execute(m_database, sql, {}, &message))
        {
            m_database.rollback();
            fail(0, operation, message);
            release();
            return;
        }
    }
    if (!execute(m_database, QStringLiteral("INSERT OR IGNORE INTO calibration_metadata VALUES('schema',?)"),
                 {QString::fromLatin1(kSchema)}, &message) || !m_database.commit())
    {
        if (message.isEmpty()) message = m_database.lastError().text();
        m_database.rollback();
        fail(0, operation, message);
        release();
        return;
    }
    // 对本轮已标记v3库只追加引用协议元数据，不改任何历史点或标签。
    // 全部可见/隐藏身份预约成功后才发ready；缺失身份或锁失败保留文件并闭锁。
    if (!initializeReferenceProtection(catalogDatabasePath, &message))
    {
        fail(0, operation, QStringLiteral("校准引用保护未就绪，未开放写入；历史数据保留：") + message);
        release();
        return;
    }
    m_ready = true;
    emit ready(databasePath);
}

bool CalibrationStore::beginWrite(QString *message)
{
    // Bounded lock wait in the worker, never a GUI wait or an automatic retry.
    return execute(m_database, QStringLiteral("BEGIN IMMEDIATE"), {}, message);
}

std::unique_ptr<QLockFile> CalibrationStore::lockDatasetOperation(QString *message)
{
    // BEGIN IMMEDIATE可能被SQLITE RAISE(ROLLBACK)提前解除。文件操作锁覆盖两库
    // 提交及失败回收，不能把“SQLite事务还在”当作所有故障下的串行保证。
    std::unique_ptr<QLockFile> lock(new QLockFile(normalizedPath(m_database.databaseName())
                                                 + QStringLiteral(".operation.lock")));
    lock->setStaleLockTime(0);
    if (!lock->tryLock(250))
    {
        *message = QStringLiteral("校准数据集操作保护锁正被另一操作占用或不可写；本次未写入，请稍后显式重试。");
        return {};
    }
    return lock;
}

bool CalibrationStore::checkDatasetPathBinding(QString *message)
{
    // 同一UUID只能对应同一个规范文件。不同物理副本各自持SQLite/文件锁，
    // 因此在同一catalog事务中持久绑定路径，禁止副本复用另一文件的预约键。
    // 恢复须保留原规范路径；移位恢复需显式核对数据集/预约后另行迁移绑定。
    const QString key = QStringLiteral("calibration_dataset_path:") + m_datasetId;
    QSqlQuery existing(m_referenceCatalog);
    existing.prepare(QStringLiteral("SELECT value FROM catalog_metadata WHERE key=?"));
    existing.addBindValue(key);
    if (!existing.exec()) { *message = existing.lastError().text(); return false; }
    if (existing.next())
    {
        if (existing.value(0).toString() == m_datasetPath) return true;
        *message = QStringLiteral("该校准数据集UUID已绑定另一规范文件路径；拒绝同UUID副本。请按原路径恢复或显式核对恢复范围，未换UUID、未释放预约。");
        return false;
    }
    return execute(m_referenceCatalog, QStringLiteral("INSERT INTO catalog_metadata(key,value) VALUES(?,?)"),
                   {key,m_datasetPath}, message);
}

QString CalibrationStore::referenceKey(const CalibrationRecord &record) const
{
    return QStringLiteral("dataset:%1:project:%2:batch:%3")
        .arg(m_datasetId).arg(record.projectId).arg(record.batchId);
}

bool CalibrationStore::initializeReferenceProtection(QString catalogPath, QString *message)
{
    const auto operationLock = lockDatasetOperation(message);
    if (!operationLock) return false;
    m_referenceCatalogConnectionName = QStringLiteral("calibration-reservations-") + QUuid::createUuid().toString();
    m_referenceCatalog = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_referenceCatalogConnectionName);
    m_referenceCatalog.setDatabaseName(catalogPath);
    m_referenceCatalog.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=250"));
    if (!m_referenceCatalog.open())
    {
        *message = QStringLiteral("无法打开引用预约连接：") + m_referenceCatalog.lastError().text();
        return false;
    }
    // 不建立或升级共用表；项目/试剂库须已由其所属模块完成初始化。
    if (!execute(m_referenceCatalog, QStringLiteral("PRAGMA foreign_keys=ON"), {}, message)
        || !execute(m_referenceCatalog, QStringLiteral("SELECT owner_module,reference_key,project_id FROM catalog_project_reference LIMIT 0"), {}, message)
        || !execute(m_referenceCatalog, QStringLiteral("SELECT owner_module,reference_key,batch_id FROM catalog_batch_reference LIMIT 0"), {}, message))
        return false;

    if (!beginWrite(message)) return false;
    QString dataset, protocol;
    {
        QSqlQuery metadata(m_database);
        if (!metadata.exec(QStringLiteral("SELECT key,value FROM calibration_metadata WHERE key IN('dataset_uuid','reference_protocol')")))
        {
            *message = metadata.lastError().text(); m_database.rollback(); return false;
        }
        while (metadata.next())
        {
            if (metadata.value(0).toString() == QStringLiteral("dataset_uuid")) dataset = metadata.value(1).toString();
            else protocol = metadata.value(1).toString();
        }
    }
    if (dataset.isEmpty() && protocol.isEmpty())
    {
        // 本轮未接引用协议的v3库仅在此显式追加一次，随后对包括隐藏记录在内的身份做保护。
        dataset = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!execute(m_database, QStringLiteral("INSERT INTO calibration_metadata VALUES('dataset_uuid',?)"), {dataset}, message)
            || !execute(m_database, QStringLiteral("INSERT INTO calibration_metadata VALUES('reference_protocol','catalog-reservations-v1')"), {}, message))
        { m_database.rollback(); return false; }
        protocol = QStringLiteral("catalog-reservations-v1");
    }
    if (protocol != QStringLiteral("catalog-reservations-v1") || QUuid(dataset).isNull())
    {
        *message = QStringLiteral("数据集身份/引用协议不完整，不能猜测已有预约归属；需先核对恢复范围。");
        m_database.rollback(); return false;
    }
    if (!m_database.commit())
    {
        *message = m_database.lastError().text(); m_database.rollback(); return false;
    }
    m_datasetId = dataset;
    m_datasetPath = normalizedPath(m_database.databaseName());

    // 与save/copy使用同一锁顺序，防止启动对账把另一写者的在途预约误判为残留。
    if (!beginWrite(message)) return false;
    CalibrationRecords identities;
    {
        // 只读取身份，不受2048个可见页面行的限制，不把隐藏历史当成可释放记录。
        QSqlQuery records(m_database);
        if (!records.exec(QStringLiteral("SELECT DISTINCT project_id,project_name,batch_id,batch_code,batch_group,concentration_unit,result_unit,unit_ratio FROM calibration_records ORDER BY project_id,batch_id")))
        {
            *message = records.lastError().text(); m_database.rollback(); return false;
        }
        while (records.next())
        {
            CalibrationRecord record;
            record.projectId = records.value(0).toLongLong(); record.projectName = records.value(1).toString();
            record.batchId = records.value(2).toLongLong(); record.batchCode = records.value(3).toString();
            record.batchGroup = records.value(4).toString(); record.concentrationUnit = records.value(5).toString();
            record.resultUnit = records.value(6).toString(); record.unitRatio = records.value(7).toDouble(); identities.append(record);
        }
    }
    QStringList newProjects, newBatches;
    if (!reserveReferences(&identities, true, &newProjects, &newBatches, message, true))
    { m_database.rollback(); return false; }
    if (!m_database.commit())
    {
        // 预约已保护真实历史，即使本地读事务收尾失败也保留预约，不能按新增失败释放。
        *message = m_database.lastError().text(); m_database.rollback(); return false;
    }
    return true;
}

bool CalibrationStore::reserveReferences(CalibrationRecords *records, bool historicalIdentity,
                                         QStringList *newProjects, QStringList *newBatches, QString *message,
                                         bool allowDisabledHistory)
{
    newProjects->clear(); newBatches->clear();
    if (!m_referenceCatalog.isOpen() || m_datasetId.isEmpty())
    { *message = QStringLiteral("引用预约尚未就绪，禁止写入本地记录。"); return false; }
    if (!execute(m_referenceCatalog, QStringLiteral("BEGIN IMMEDIATE"), {}, message)) return false;
    if (!checkDatasetPathBinding(message))
    { m_referenceCatalog.rollback(); return false; }
    const auto hold = [this, message](const QString &table, const QString &column, const QString &key,
                                     qint64 id, QStringList *added)
    {
        QSqlQuery existing(m_referenceCatalog);
        existing.prepare(QStringLiteral("SELECT ") + column + QStringLiteral(" FROM ") + table
                         + QStringLiteral(" WHERE owner_module='calibration' AND reference_key=?"));
        existing.addBindValue(key);
        if (!existing.exec()) { *message = existing.lastError().text(); return false; }
        if (existing.next())
        {
            if (existing.value(0).toLongLong() == id) return true;
            *message = QStringLiteral("该数据集组合键预约了不同身份，需人工核对，未覆盖预约。"); return false;
        }
        if (!execute(m_referenceCatalog, QStringLiteral("INSERT INTO ") + table
                     + QStringLiteral("(owner_module,reference_key,") + column
                     + QStringLiteral(") VALUES('calibration',?,?)"), {key,id}, message)) return false;
        added->append(key); return true;
    };
    bool success = true;
    for (CalibrationRecord &record : *records)
    {
        if (record.projectId <= 0 || record.batchId < 0)
        {
            *message = QStringLiteral("历史项目/批次稳定编号无效（project=%1,batch=%2），未猜测替代身份。")
                .arg(record.projectId).arg(record.batchId);
            success = false; break;
        }
        if (!checkCatalog(&record, historicalIdentity, message, m_referenceCatalog, allowDisabledHistory))
        { success = false; break; }
        const QString key = referenceKey(record);
        if (!hold(QStringLiteral("catalog_project_reference"), QStringLiteral("project_id"), key,
                  record.projectId, newProjects)
            || (record.batchId > 0 && !hold(QStringLiteral("catalog_batch_reference"), QStringLiteral("batch_id"),
                                           key, record.batchId, newBatches)))
        { success = false; break; }
    }
    if (success && allowDisabledHistory)
    {
        QSet<QString> projectKeys, batchKeys;
        for (const CalibrationRecord &record : *records)
        {
            projectKeys.insert(referenceKey(record));
            if (record.batchId > 0) batchKeys.insert(referenceKey(record));
        }
        const auto checkOrphans = [this, message](const QString &table, const QSet<QString> &expected)
        {
            QSqlQuery query(m_referenceCatalog);
            query.prepare(QStringLiteral("SELECT reference_key FROM ") + table
                + QStringLiteral(" WHERE owner_module='calibration' AND reference_key LIKE ?"));
            query.addBindValue(QStringLiteral("dataset:") + m_datasetId + QStringLiteral(":%"));
            if (!query.exec()) { *message = query.lastError().text(); return false; }
            while (query.next())
            {
                const QString key = query.value(0).toString();
                if (!expected.contains(key))
                {
                    *message = QStringLiteral("发现本数据集无对应本地记录的保护预约：%1；已保留，须核对崩溃/恢复范围后显式释放。").arg(key);
                    return false;
                }
            }
            return true;
        };
        success = checkOrphans(QStringLiteral("catalog_project_reference"), projectKeys)
            && checkOrphans(QStringLiteral("catalog_batch_reference"), batchKeys);
    }
    if (!success || !m_referenceCatalog.commit())
    {
        if (success) *message = m_referenceCatalog.lastError().text();
        m_referenceCatalog.rollback(); newProjects->clear(); newBatches->clear(); return false;
    }
    return true;
}

bool CalibrationStore::releaseNewReferences(const QStringList &projects, const QStringList &batches, QString *message)
{
    if (projects.isEmpty() && batches.isEmpty()) return true;
    if (!execute(m_referenceCatalog, QStringLiteral("BEGIN IMMEDIATE"), {}, message)) return false;
    bool success = true;
    for (const QString &key : batches)
        if (!(success = execute(m_referenceCatalog, QStringLiteral("DELETE FROM catalog_batch_reference WHERE owner_module='calibration' AND reference_key=?"), {key}, message))) break;
    for (const QString &key : projects)
    {
        if (!success) break;
        success = execute(m_referenceCatalog, QStringLiteral("DELETE FROM catalog_project_reference WHERE owner_module='calibration' AND reference_key=?"), {key}, message);
    }
    if (!success || !m_referenceCatalog.commit())
    {
        if (success) *message = m_referenceCatalog.lastError().text();
        m_referenceCatalog.rollback(); return false;
    }
    return true;
}

void CalibrationStore::cleanupFailedReferences(const QStringList &projects, const QStringList &batches, QString *message)
{
    // SQL错误可能已自动回滚。先结束旧状态，再在仍持有数据集操作锁时重拿
    // 本地写锁核对已提交身份；不依赖旧事务，不释放已被真实记录使用的预约。
    m_database.rollback();
    if (projects.isEmpty() && batches.isEmpty()) return;
    QString cleanup;
    if (!beginWrite(&cleanup))
    {
        *message += QStringLiteral("；无法重建本地回收写锁，本次新增保护预约保留，需显式核对后释放：") + cleanup;
        return;
    }
    QSet<QString> needed;
    {
        QSqlQuery records(m_database);
        if (!records.exec(QStringLiteral("SELECT DISTINCT project_id,batch_id FROM calibration_records")))
        {
            cleanup = records.lastError().text();
            m_database.rollback();
            *message += QStringLiteral("；本地身份回读失败，本次新增保护预约保留，需显式核对后释放：") + cleanup;
            return;
        }
        while (records.next())
        {
            CalibrationRecord identity;
            identity.projectId = records.value(0).toLongLong(); identity.batchId = records.value(1).toLongLong();
            needed.insert(referenceKey(identity));
        }
    }
    QStringList unusedProjects, unusedBatches;
    for (const QString &key : projects) if (!needed.contains(key)) unusedProjects.append(key);
    for (const QString &key : batches) if (!needed.contains(key)) unusedBatches.append(key);
    if (!releaseNewReferences(unusedProjects, unusedBatches, &cleanup))
        *message += QStringLiteral("；本次新增保护预约保留，需显式核对后释放：") + cleanup;
    m_database.rollback();
}

bool CalibrationStore::read(CalibrationRecords *records, QString *message, bool includeHidden)
{
    records->clear();
    QSqlQuery headers(m_database);
    const QString sql = QStringLiteral("SELECT id,revision,project_id,project_name,batch_id,batch_code,blood_batch,"
        "created_at,method,x_transform,y_transform,concentration_unit,result_unit,unit_ratio,source_id,visible,"
        "is_checked,is_applied,batch_group,changed_at FROM calibration_records")
        + (includeHidden ? QString() : QStringLiteral(" WHERE visible=1")) + QStringLiteral(" ORDER BY id");
    if (!headers.exec(sql))
    {
        *message = headers.lastError().text();
        return false;
    }
    while (headers.next())
    {
        if (records->size() >= kMaximumVisibleRecords)
        {
            *message = QStringLiteral("本地校准记录超过单次读取上限2048，未返回不完整列表。");
            return false;
        }
        CalibrationRecord record;
        record.id = headers.value(0).toLongLong();
        record.revision = headers.value(1).toLongLong();
        record.projectId = headers.value(2).toLongLong();
        record.projectName = headers.value(3).toString();
        record.batchId = headers.value(4).toLongLong();
        record.batchCode = headers.value(5).toString();
        record.bloodBatch = headers.value(6).toString();
        record.createdAt = headers.value(7).toString();
        record.method = headers.value(8).toString();
        record.xTransform = headers.value(9).toString();
        record.yTransform = headers.value(10).toString();
        record.concentrationUnit = headers.value(11).toString();
        record.resultUnit = headers.value(12).toString();
        record.unitRatio = headers.value(13).toDouble();
        record.sourceId = headers.value(14).toLongLong();
        record.visible = headers.value(15).toBool();
        record.batchGroup = headers.value(18).toString();
        record.changedAt = headers.value(19).toString();
        if (headers.value(16).toInt() != 0 || headers.value(17).toInt() != 0)
        {
            *message = QStringLiteral("本地库出现不支持的审核/应用状态，拒绝将其当作有效校准。");
            return false;
        }
        QSqlQuery points(m_database);
        points.prepare(QStringLiteral("SELECT point_index,sample_rate,concentration,measurements "
                                      "FROM calibration_points WHERE record_id=? ORDER BY point_index"));
        points.addBindValue(record.id);
        if (!points.exec())
        {
            *message = points.lastError().text();
            return false;
        }
        while (points.next())
        {
            CalibrationPoint point;
            point.index = points.value(0).toInt();
            point.sampleRate = points.value(1).toString();
            point.concentration = points.value(2).toDouble();
            const QByteArray encoded = points.value(3).toByteArray();
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(encoded, &parseError);
            if (encoded.size() > 4096 || parseError.error != QJsonParseError::NoError || !document.isArray())
            {
                *message = QStringLiteral("原始测试点格式损坏，未返回部分数据。");
                return false;
            }
            for (const QJsonValue &value : document.array())
            {
                if (!value.isDouble())
                {
                    *message = QStringLiteral("原始测试值不是数值，未返回部分数据。");
                    return false;
                }
                point.measurements.append(value.toDouble());
            }
            record.points.append(point);
        }
        const bool wasVisible = record.visible;
        record.visible = true;
        *message = validate(record);
        record.visible = wasVisible;
        if (!message->isEmpty())
        {
            *message = QStringLiteral("校准记录内容无效：") + *message;
            return false;
        }
        records->append(record);
    }
    return true;
}

void CalibrationStore::load(qint64 requestId)
{
    const QString operation = QStringLiteral("load");
    if (!check(requestId, operation)) return;
    CalibrationRecords records;
    QString message;
    // Header and point rows must belong to one read snapshot.
    if (!m_database.transaction())
    {
        fail(requestId, operation, m_database.lastError().text());
        return;
    }
    if (!read(&records, &message) || !m_database.commit())
    {
        if (message.isEmpty()) message = m_database.lastError().text();
        m_database.rollback();
        fail(requestId, operation, message);
        return;
    }
    emit snapshotReady(requestId, records);
}

void CalibrationStore::loadCatalog(qint64 requestId)
{
    const QString operation = QStringLiteral("catalog");
    if (!check(requestId, operation)) return;
    ProjectRecords projects;
    ProjectUnits units;
    ReagentSnapshot reagents;
    if (!m_catalog.transaction())
    {
        fail(requestId, operation, m_catalog.lastError().text());
        return;
    }
    QString message;
    const auto readQuery = [this, &message](QSqlQuery &query, const QString &sql)
    {
        if (!query.exec(sql))
        {
            message = query.lastError().text();
            return false;
        }
        return true;
    };
    bool success = true;
    {
        QSqlQuery query(m_catalog);
        success = readQuery(query, QStringLiteral("SELECT unit,ratio FROM option_unit ORDER BY unit"));
        while (success && query.next())
        {
            units.append({query.value(0).toString(), query.value(1).toDouble()});
        }
    }
    if (success)
    {
        QSqlQuery query(m_catalog);
        success = readQuery(query, QStringLiteral("SELECT id,project_id,project_en_name,project_cn_name,"
            "project_lis_code,is_used,fixed,change_time FROM project_base ORDER BY id"));
        while (success && query.next())
        {
            if (projects.size() >= 1024)
            {
                message = QStringLiteral("项目数量超过1024，未返回不完整选择列表。");
                success = false;
                break;
            }
            ProjectRecord project;
            project.id = query.value(0).toLongLong();
            project.instrumentProjectId = query.value(1).toString();
            project.name = query.value(2).toString();
            project.fullName = query.value(3).toString();
            project.lisCode = query.value(4).toString();
            project.enabled = query.value(5).toBool();
            project.fixed = query.value(6).toBool();
            project.changedAt = query.value(7).toString();
            QSqlQuery results(m_catalog);
            results.prepare(QStringLiteral("SELECT result_type,result_name,decimal_point,test_low_range,"
                "test_up_range,normal_low_range,normal_up_range,lis_code,unit_name,main_result "
                "FROM project_result WHERE project_id=? ORDER BY result_type"));
            results.addBindValue(project.id);
            if (!results.exec())
            {
                message = results.lastError().text();
                success = false;
                break;
            }
            while (results.next())
            {
                ProjectResult result;
                result.type = results.value(0).toString();
                result.name = results.value(1).toString();
                result.decimals = results.value(2).toInt();
                result.testLow = results.value(3).toDouble();
                result.testHigh = results.value(4).toDouble();
                result.normalLow = results.value(5).toDouble();
                result.normalHigh = results.value(6).toDouble();
                result.lisCode = results.value(7).toString();
                result.unit = results.value(8).toString();
                result.mainResult = results.value(9).toBool();
                project.results.append(result);
            }
            projects.append(project);
        }
    }
    if (success)
    {
        QSqlQuery query(m_catalog);
        success = readQuery(query, QStringLiteral("SELECT id,project_id,batch_code,batch_group "
                                                 "FROM reagent_batch_group ORDER BY id"));
        while (success && query.next())
        {
            if (reagents.batches.size() >= 4096)
            {
                message = QStringLiteral("批次数量超过4096，未返回不完整选择列表。");
                success = false;
                break;
            }
            ReagentBatch batch;
            batch.id = query.value(0).toLongLong();
            batch.projectId = query.value(1).toLongLong();
            batch.code = query.value(2).toString();
            batch.batchGroup = query.value(3).toString();
            reagents.batches.append(batch);
        }
    }
    if (!success || !m_catalog.commit())
    {
        if (message.isEmpty()) message = m_catalog.lastError().text();
        m_catalog.rollback();
        fail(requestId, operation, message);
        return;
    }
    emit catalogReady(requestId, projects, units, reagents);
}

bool CalibrationStore::checkCatalog(CalibrationRecord *recordPointer, bool existingIdentity, QString *message,
                                    QSqlDatabase catalog, bool allowDisabledHistory)
{
    CalibrationRecord &record = *recordPointer;
    if (!catalog.isValid()) catalog = m_catalog;
    QSqlQuery project(catalog);
    project.prepare(QStringLiteral("SELECT project_en_name,is_used FROM project_base WHERE id=?"));
    project.addBindValue(record.projectId);
    if (!project.exec())
    {
        *message = project.lastError().text();
        return false;
    }
    if (!project.next() || project.value(0).toString() != record.projectName
        || (!allowDisabledHistory && !project.value(1).toBool()))
    {
        *message = QStringLiteral("项目已改名、删除或停用，请刷新项目后重新建立本地记录；历史记录仍保留原标签。");
        return false;
    }
    // 已保存记录及副本可保留原始单位/倍率；不能将结果类型缺失或单位被删视为合法变更。
    // 当前启用门槛独立于历史单位保留，仅启动对账允许停用项目的历史身份。
    QSqlQuery unit(catalog);
    unit.prepare(QStringLiteral("SELECT r.unit_name,u.ratio FROM project_result r JOIN option_unit u "
        "ON r.unit_name=u.unit WHERE r.project_id=? AND r.result_type='Calibration'"));
    unit.addBindValue(record.projectId);
    if (!unit.exec())
    {
        *message = unit.lastError().text();
        return false;
    }
    if (!unit.next() || (!existingIdentity && (unit.value(0).toString() != record.concentrationUnit
        || unit.value(1).toDouble() != record.unitRatio)))
    {
        *message = QStringLiteral("项目的浓度单位或倍率已变化，请刷新后重新建立记录，不能自动改变历史点单位。");
        return false;
    }
    QSqlQuery originalUnit(catalog);
    originalUnit.prepare(QStringLiteral("SELECT r.unit_name FROM project_result r JOIN option_unit u "
        "ON r.unit_name=u.unit WHERE r.project_id=? AND r.result_type='Original'"));
    originalUnit.addBindValue(record.projectId);
    if (!originalUnit.exec())
    {
        *message = originalUnit.lastError().text();
        return false;
    }
    if (!originalUnit.next() || (!existingIdentity && originalUnit.value(0).toString() != record.resultUnit))
    {
        *message = QStringLiteral("项目的原始测试单位已变化或缺失，请刷新后重新建立记录；历史单位不自动转换。");
        return false;
    }
    if (existingIdentity)
    {
        QSqlQuery historicalUnits(catalog);
        historicalUnits.prepare(QStringLiteral("SELECT COUNT(*) FROM option_unit WHERE unit IN(?,?)"));
        historicalUnits.addBindValue(record.concentrationUnit); historicalUnits.addBindValue(record.resultUnit);
        const int expected = record.concentrationUnit == record.resultUnit ? 1 : 2;
        if (!historicalUnits.exec() || !historicalUnits.next() || historicalUnits.value(0).toInt() != expected)
        {
            *message = QStringLiteral("历史单位已从字典删除或检查失败，保留历史数值，禁止写入/复制，需先核对恢复。");
            return false;
        }
    }
    if (record.batchId > 0)
    {
        QSqlQuery batch(catalog);
        batch.prepare(QStringLiteral("SELECT project_id,batch_code,batch_group FROM reagent_batch_group WHERE id=?"));
        batch.addBindValue(record.batchId);
        if (!batch.exec())
        {
            *message = batch.lastError().text();
            return false;
        }
        if (!batch.next() || batch.value(0).toLongLong() != record.projectId
            || batch.value(1).toString() != record.batchCode)
        {
            *message = QStringLiteral("试剂批次已删除、改号或改属项目，请刷新后重新选择。");
            return false;
        }
        const QString currentGroup = batch.value(2).toString();
        if (!boundedText(currentGroup, 256) || ((existingIdentity || !record.batchGroup.isEmpty())
                                              && record.batchGroup != currentGroup))
        {
            *message = QStringLiteral("试剂组分批号已变化，不能重用旧记录身份；请刷新后新增本地记录。");
            return false;
        }
        // New records retain the current committed component-group identity.
        record.batchGroup = currentGroup;
    }
    return true;
}

bool CalibrationStore::writeRecord(CalibrationRecord record, qint64 *id, QString *message)
{
    const QString changedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (record.id > 0)
    {
        QSqlQuery identity(m_database);
        identity.prepare(QStringLiteral("SELECT project_id,project_name,batch_id,batch_code,batch_group,"
            "blood_batch,method,x_transform,y_transform,concentration_unit,result_unit,unit_ratio,source_id "
            "FROM calibration_records WHERE id=? AND revision=? AND visible=1"));
        identity.addBindValue(record.id);
        identity.addBindValue(record.revision);
        if (!identity.exec())
        {
            *message = identity.lastError().text();
            return false;
        }
        if (!identity.next())
        {
            *message = QStringLiteral("记录已被修改或删除；本次未覆盖，请重载后再试。");
            return false;
        }
        if (identity.value(0).toLongLong() != record.projectId || identity.value(1).toString() != record.projectName
            || identity.value(2).toLongLong() != record.batchId || identity.value(3).toString() != record.batchCode
            || identity.value(4).toString() != record.batchGroup || identity.value(5).toString() != record.bloodBatch
            || identity.value(6).toString() != record.method || identity.value(7).toString() != record.xTransform
            || identity.value(8).toString() != record.yTransform || identity.value(9).toString() != record.concentrationUnit
            || identity.value(10).toString() != record.resultUnit || identity.value(11).toDouble() != record.unitRatio
            || identity.value(12).toLongLong() != record.sourceId)
        {
            *message = QStringLiteral("已保存记录的项目、批次、校准品、单位及方法身份不可改写；请新增本地记录。");
            return false;
        }
    }
    if (!checkCatalog(&record, record.id > 0 || record.sourceId > 0, message)) return false;
    QVariantList values = {record.projectId, record.projectName, record.batchId,
        record.batchCode.isNull() ? QStringLiteral("") : record.batchCode,
        record.bloodBatch, record.method, record.xTransform, record.yTransform,
        record.concentrationUnit, record.resultUnit, record.unitRatio};
    if (record.id == 0)
    {
        QSqlQuery capacity(m_database);
        if (!capacity.exec(QStringLiteral("SELECT count(*) FROM calibration_records WHERE visible=1"))
            || !capacity.next())
        {
            *message = capacity.lastError().text();
            return false;
        }
        if (capacity.value(0).toInt() >= kMaximumVisibleRecords)
        {
            *message = QStringLiteral("本地校准记录已达2048条，不能继续新增。");
            return false;
        }
        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral("INSERT INTO calibration_records(project_id,project_name,batch_id,batch_code,"
            "blood_batch,method,x_transform,y_transform,concentration_unit,result_unit,unit_ratio,"
            "revision,source_id,created_at,batch_group,changed_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,1,?,?,?,?)"));
        for (const QVariant &value : values) insert.addBindValue(value);
        insert.addBindValue(record.sourceId);
        insert.addBindValue(changedAt);
        insert.addBindValue(record.batchGroup.isNull() ? QStringLiteral("") : record.batchGroup);
        insert.addBindValue(changedAt);
        if (!insert.exec())
        {
            *message = insert.lastError().text();
            return false;
        }
        record.id = insert.lastInsertId().toLongLong();
    }
    else
    {
        QSqlQuery update(m_database);
        update.prepare(QStringLiteral("UPDATE calibration_records SET project_id=?,project_name=?,batch_id=?,"
            "batch_code=?,blood_batch=?,method=?,x_transform=?,y_transform=?,concentration_unit=?,result_unit=?,"
            "unit_ratio=?,changed_at=?,revision=revision+1 WHERE id=? AND revision=? AND visible=1"));
        for (const QVariant &value : values) update.addBindValue(value);
        update.addBindValue(changedAt);
        update.addBindValue(record.id);
        update.addBindValue(record.revision);
        if (!update.exec())
        {
            *message = update.lastError().text();
            return false;
        }
        if (update.numRowsAffected() != 1)
        {
            *message = QStringLiteral("记录已被修改或删除；本次未覆盖，请重载后再试。");
            return false;
        }
        if (!execute(m_database, QStringLiteral("DELETE FROM calibration_points WHERE record_id=?"), {record.id}, message))
            return false;
    }
    for (const CalibrationPoint &point : record.points)
    {
        QJsonArray measurements;
        for (double value : point.measurements) measurements.append(value);
        if (!execute(m_database, QStringLiteral("INSERT INTO calibration_points VALUES(?,?,?,?,?)"),
            {record.id, point.index, point.sampleRate, point.concentration,
             QString::fromUtf8(QJsonDocument(measurements).toJson(QJsonDocument::Compact))}, message))
            return false;
    }
    *id = record.id;
    return true;
}

void CalibrationStore::save(qint64 requestId, CalibrationRecord record)
{
    const QString operation = QStringLiteral("save");
    if (!check(requestId, operation, true)) return;
    QString message = validate(record);
    if (!message.isEmpty())
    {
        fail(requestId, operation, message);
        return;
    }
    // Caller cannot fabricate copy provenance or inject an already-used new id.
    if ((record.id == 0 && (record.revision != 0 || record.sourceId != 0))
        || (record.id > 0 && record.revision <= 0))
    {
        fail(requestId, operation, QStringLiteral("新增/更新记录版本不一致。"));
        return;
    }
    const auto operationLock = lockDatasetOperation(&message);
    if (!operationLock || !beginWrite(&message))
    {
        fail(requestId, operation, message);
        return;
    }
    CalibrationRecords reservation{record};
    QStringList newProjects, newBatches;
    if (!reserveReferences(&reservation, record.id > 0, &newProjects, &newBatches, &message))
    {
        m_database.rollback();
        fail(requestId, operation, QStringLiteral("项目/批次保护预约失败，本次未保存：") + message);
        return;
    }
    record = reservation.first();
    qint64 id = 0;
    if (!writeRecord(record, &id, &message) || !m_database.commit())
    {
        if (message.isEmpty()) message = m_database.lastError().text();
        // 操作锁仍在；失败回收会重新建立SQLite写锁并核对实际已提交身份。
        cleanupFailedReferences(newProjects, newBatches, &message);
        m_database.rollback();
        fail(requestId, operation, QStringLiteral("本地校准未保存，原记录保留：") + message);
        return;
    }
    emit succeeded(requestId, operation, id);
}

void CalibrationStore::copy(qint64 requestId, qint64 sourceId, qint64 sourceRevision)
{
    const QString operation = QStringLiteral("copy");
    if (!check(requestId, operation, true)) return;
    QString message;
    const auto operationLock = lockDatasetOperation(&message);
    if (!operationLock || !beginWrite(&message))
    {
        fail(requestId, operation, message);
        return;
    }
    CalibrationRecords records;
    if (!read(&records, &message))
    {
        m_database.rollback();
        fail(requestId, operation, message);
        return;
    }
    CalibrationRecord record;
    for (const CalibrationRecord &candidate : records)
    {
        if (candidate.id == sourceId && candidate.revision == sourceRevision)
        {
            record = candidate;
            break;
        }
    }
    if (record.id == 0)
    {
        m_database.rollback();
        fail(requestId, operation, QStringLiteral("复制来源已改变或删除，请重载；不会复制未保存草稿。"));
        return;
    }
    record.sourceId = record.id;
    record.id = 0;
    record.revision = 0;
    CalibrationRecords reservation{record};
    QStringList newProjects, newBatches;
    if (!reserveReferences(&reservation, true, &newProjects, &newBatches, &message))
    {
        m_database.rollback();
        fail(requestId, operation, QStringLiteral("复制保护预约失败：") + message);
        return;
    }
    record = reservation.first();
    qint64 id = 0;
    if (!writeRecord(record, &id, &message) || !m_database.commit())
    {
        if (message.isEmpty()) message = m_database.lastError().text();
        cleanupFailedReferences(newProjects, newBatches, &message);
        m_database.rollback();
        fail(requestId, operation, QStringLiteral("复制未保存：") + message);
        return;
    }
    emit succeeded(requestId, operation, id);
}

void CalibrationStore::remove(qint64 requestId, qint64 id, qint64 revision)
{
    const QString operation = QStringLiteral("remove");
    if (!check(requestId, operation, true)) return;
    QString message;
    const auto operationLock = lockDatasetOperation(&message);
    if (!operationLock || !beginWrite(&message))
    {
        fail(requestId, operation, message);
        return;
    }
    QSqlQuery identity(m_database);
    identity.prepare(QStringLiteral("SELECT project_id FROM calibration_records WHERE id=? AND revision=? AND visible=1"));
    identity.addBindValue(id); identity.addBindValue(revision);
    if (!identity.exec() || !identity.next())
    {
        message = identity.lastError().isValid() ? identity.lastError().text() : QStringLiteral("记录已修改或删除；未删除。");
        identity.finish(); m_database.rollback(); fail(requestId,operation,message); return;
    }
    const qint64 projectId = identity.value(0).toLongLong();
    identity.finish();
    QSqlQuery project(m_catalog);
    project.prepare(QStringLiteral("SELECT is_used FROM project_base WHERE id=?"));
    project.addBindValue(projectId);
    if (!project.exec() || !project.next() || !project.value(0).toBool())
    {
        message = project.lastError().isValid() ? project.lastError().text()
            : QStringLiteral("项目已停用或缺失，仅可读取历史和启动对账，不能隐藏删除校准记录。");
        project.finish(); m_database.rollback(); fail(requestId,operation,message); return;
    }
    project.finish();
    QSqlQuery update(m_database);
    update.prepare(QStringLiteral("UPDATE calibration_records SET visible=0,revision=revision+1 "
                                  "WHERE id=? AND revision=? AND visible=1 AND is_checked=0 AND is_applied=0"));
    update.addBindValue(id);
    update.addBindValue(revision);
    if (!update.exec() || update.numRowsAffected() != 1)
    {
        message = update.lastError().isValid() ? update.lastError().text()
            : QStringLiteral("记录已修改、已删除或状态不允许；未删除。");
        update.finish();
        m_database.rollback();
        fail(requestId, operation, message);
        return;
    }
    update.finish();
    if (!m_database.commit())
    {
        message = m_database.lastError().text();
        m_database.rollback();
        fail(requestId, operation, message);
        return;
    }
    emit succeeded(requestId, operation, id);
}

void CalibrationStore::release()
{
    m_ready = false;
    m_datasetId.clear();
    m_datasetPath.clear();
    if (m_referenceCatalog.isValid()) m_referenceCatalog.close();
    m_referenceCatalog = QSqlDatabase();
    if (!m_referenceCatalogConnectionName.isEmpty()) QSqlDatabase::removeDatabase(m_referenceCatalogConnectionName);
    m_referenceCatalogConnectionName.clear();
    if (m_database.isValid()) m_database.close();
    m_database = QSqlDatabase();
    if (!m_connectionName.isEmpty()) QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
    if (m_catalog.isValid()) m_catalog.close();
    m_catalog = QSqlDatabase();
    if (!m_catalogConnectionName.isEmpty()) QSqlDatabase::removeDatabase(m_catalogConnectionName);
    m_catalogConnectionName.clear();
}

void CalibrationStore::close()
{
    if (QThread::currentThread() != thread())
    {
        fail(0, QStringLiteral("close"), QStringLiteral("关闭必须在校准工作线程执行。"));
        return;
    }
    release();
    emit closed();
}
