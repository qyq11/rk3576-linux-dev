#include "maintenance_store.h"
#include "original_files/original_business_database.h"
#include "original_files/original_ini.h"

#include <QDir>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QHostAddress>
#include <QLockFile>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QVariant>
#include <QRegularExpression>
#include <QSet>

namespace
{
const QByteArray kOfflineTag("SN100_INSTRUMENT_OFFLINE_V1\n");
const int kBusyTimeoutMs = 200;

struct MaintenanceTable
{
    QString table;
    QString dateColumn;
    QStringList actions;
    QString actionType;
};

QList<MaintenanceTable> maintenanceTables()
{
    // Keep the supplier's table/column contract, including clean_dirver's spelling.
    return {
        {QStringLiteral("day_maintain"), QStringLiteral("day"),
         {QStringLiteral("long_clean"), QStringLiteral("waste_cup"),
          QStringLiteral("waste_water"), QStringLiteral("waste_cup_box"),
          QStringLiteral("shut_down")}, QStringLiteral("TEXT")},
        {QStringLiteral("week_maintain"), QStringLiteral("week"),
         {QStringLiteral("clean_dirver"), QStringLiteral("clean_pool")}, QStringLiteral("VARCHAR")},
        {QStringLiteral("month_maintain"), QStringLiteral("month"),
         {QStringLiteral("clean_needle")}, QStringLiteral("VARCHAR")}
    };
}

bool prepareOfflinePath(QString directory, QString *databasePath, bool *exists, QString *message)
{
    if (!QDir::isAbsolutePath(directory))
    {
        *message = QStringLiteral("仪器维护必须显式指定绝对脱机数据目录");
        return false;
    }
    directory = QDir::cleanPath(directory);
    QString ancestor = directory;
    QStringList missing;
    while (!QFileInfo::exists(ancestor))
    {
        const QFileInfo info(ancestor);
        if (info.isSymLink() || info.absolutePath() == ancestor)
        {
            *message = QStringLiteral("维护数据目录的父路径无法解析");
            return false;
        }
        missing.prepend(info.fileName());
        ancestor = info.absolutePath();
    }
    const QFileInfo ancestorInfo(ancestor);
    QString canonical = ancestorInfo.canonicalFilePath();
    if (!ancestorInfo.isDir() || canonical.isEmpty())
    {
        *message = QStringLiteral("维护数据父路径不是可解析目录");
        return false;
    }
    for (const QString &part : missing)
        canonical = QDir(canonical).filePath(part);
    if (QDir::fromNativeSeparators(canonical).split(QLatin1Char('/'), Qt::SkipEmptyParts)
            .contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive))
    {
        *message = QStringLiteral("禁止在只读参考工程中创建或打开维护数据");
        return false;
    }
    *databasePath = QDir(directory).filePath(QStringLiteral("maintenance.db"));
    const QFileInfo databaseInfo(*databasePath);
    const QString tagPath = QDir(directory).filePath(QStringLiteral("offline-data.tag"));
    const QFileInfo tagInfo(tagPath);
    *exists = databaseInfo.exists();
    if (databaseInfo.isSymLink() || tagInfo.isSymLink()
            || (*exists && !databaseInfo.isFile()))
    {
        *message = QStringLiteral("维护数据库和脱机标记必须为普通文件，不能使用符号链接");
        return false;
    }
    if (tagInfo.exists())
    {
        QFile tag(tagPath);
        if (!tagInfo.isFile() || tagInfo.size() != kOfflineTag.size()
                || !tag.open(QIODevice::ReadOnly) || tag.readAll() != kOfflineTag
                || tag.error() != QFile::NoError)
        {
            *message = QStringLiteral("维护脱机标记无效；未打开或修改已有数据库");
            return false;
        }
        return true;
    }
    if (*exists)
    {
        *message = QStringLiteral("已有维护数据库缺少有效脱机标记，拒绝导入或修改");
        return false;
    }
    if (!QDir().mkpath(directory))
    {
        *message = QStringLiteral("无法创建维护脱机数据目录");
        return false;
    }
    QSaveFile tag(tagPath);
    tag.setDirectWriteFallback(false);
    if (!tag.open(QIODevice::WriteOnly) || tag.write(kOfflineTag) != kOfflineTag.size() || !tag.commit())
    {
        *message = QStringLiteral("创建维护脱机标记失败: ") + tag.errorString();
        return false;
    }
    return true;
}

bool verifySchema(QSqlDatabase &database, QString *message, bool original = false)
{
    for (const MaintenanceTable &table : maintenanceTables())
    {
        QStringList names{QStringLiteral("id"), table.dateColumn};
        names.append(table.actions);
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table.table)))
        {
            *message = QStringLiteral("读取维护表结构失败: ") + query.lastError().text();
            return false;
        }
        int index = 0;
        while (query.next())
        {
            if (original)
            {
                const QString name = query.value(1).toString();
                if (!names.contains(name)) continue;
                const int knownIndex = names.indexOf(name);
                const QString expectedType = knownIndex == 0 ? QStringLiteral("INTEGER") : knownIndex == 1 ? QStringLiteral("DATE") : table.actionType;
                if (query.value(2).toString().toUpper() != expectedType || query.value(5).toInt() != (knownIndex == 0 ? 1 : 0))
                { *message = QStringLiteral("原维护表已知字段不兼容，未改表。"); return false; }
                ++index; continue;
            }
            const QString type = index == 0 ? QStringLiteral("INTEGER")
                               : index == 1 ? QStringLiteral("DATE") : table.actionType;
            if (index >= names.size() || query.value(1).toString() != names.at(index)
                    || query.value(2).toString().toUpper() != type
                    || query.value(5).toInt() != (index == 0 ? 1 : 0))
            {
                *message = QStringLiteral("维护表 %1 结构不匹配；未迁移或转换已有数据").arg(table.table);
                return false;
            }
            ++index;
        }
        if (index != names.size())
        {
            *message = QStringLiteral("维护表 %1 缺失或字段不完整").arg(table.table);
            return false;
        }
    }
    return true;
}

QString connectionFileFingerprint(const QString &path, QString *message)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    const QStringList parts = QDir::fromNativeSeparators(canonical)
                                  .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!info.isAbsolute() || !info.isFile() || info.isSymLink() || canonical.isEmpty() ||
        info.size() > 262144 || parts.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive) ||
        parts.contains(QStringLiteral("baseline"), Qt::CaseInsensitive))
    {
        *message = QStringLiteral("请选择工作副本中不超过256KiB的普通配置文件，不能修改参考原件或基线副本。");
        return {};
    }
    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly))
    {
        *message = QStringLiteral("无法读取连接配置：") + file.errorString();
        return {};
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError)
    {
        *message = QStringLiteral("读取连接配置失败：") + file.errorString();
        return {};
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString validateConnectionAddress(const QString &address, const QString &port)
{
    QHostAddress parsed;
    static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]{0,4}$"));
    bool ok = false;
    const uint number = port.toUInt(&ok);
    if (address != address.trimmed() || !parsed.setAddress(address) ||
        parsed.protocol() != QAbstractSocket::IPv4Protocol)
        return QStringLiteral("主控地址须为有效IPv4地址，例如192.168.100.123。");
    if (!digits.match(port).hasMatch() || !ok || number < 1 || number > 65535)
        return QStringLiteral("主控端口须为1至65535的整数，不能留空。");
    return {};
}
}

MaintenanceStore::MaintenanceStore(QObject *parent) : QObject(parent)
{
    registerTypes();
}

MaintenanceStore::~MaintenanceStore()
{
    Q_ASSERT(!m_database.isValid() || QThread::currentThread() == thread());
    releaseDatabase();
}

void MaintenanceStore::loadConnectionSettings(quint64 requestId, QString configurationFile)
{
    if (QThread::currentThread() != thread() || !requestId || requestId <= m_lastConnectionRequest)
    {
        emit connectionSettingsError(requestId, QStringLiteral("连接配置读取请求重复、过期或线程不正确。"), false);
        return;
    }
    m_lastConnectionRequest = requestId;
    QString message;
    const QString fingerprint = connectionFileFingerprint(configurationFile, &message);
    if (fingerprint.isEmpty())
    {
        emit connectionSettingsError(requestId, message, false);
        return;
    }
    OriginalIni::Settings settings;
    if (!OriginalIni::read(configurationFile, &settings, &message))
    {
        emit connectionSettingsError(requestId, message, false);
        return;
    }
    const QString address = settings.values.value(QStringLiteral("tcpmain/ip")).toString();
    const QString port = settings.values.value(QStringLiteral("tcpmain/port")).toString();
    message = validateConnectionAddress(address, port);
    if (!message.isEmpty() ||
        fingerprint != connectionFileFingerprint(configurationFile, &message))
    {
        emit connectionSettingsError(requestId, message.isEmpty()
            ? QStringLiteral("配置格式错误或读取期间文件已变化，请重新读取。") : message, false);
        return;
    }
    // 原MaskDlgAbnormal::canStartTest依据CSV第6列决定是否阻止开始，warning/none不阻止。
    QStringList blockingCodes;
    QFile alarms(QFileInfo(configurationFile).dir().filePath(QStringLiteral("db/systemInfo/abnormal_Config.csv")));
    bool alarmAvailable = alarms.size() > 0 && alarms.size() <= 1048576 && alarms.open(QIODevice::ReadOnly);
    if (alarmAvailable)
    {
        const QList<QByteArray> rows = alarms.readAll().split('\n');
        int validRows = 0;
        for (int i = 1; i < rows.size(); ++i)
        {
            if (rows[i].trimmed().isEmpty()) continue;
            const auto fields = rows[i].split(',');
            if (fields.size() < 9) { alarmAvailable = false; break; }
            ++validRows;
            const QByteArray action = fields[5].trimmed();
            if (!action.isEmpty() && action != "warning" && action != "none")
                blockingCodes.append(QString::fromLatin1(fields[1].trimmed()));
        }
        alarmAvailable = alarmAvailable && validRows > 0 && alarms.error() == QFile::NoError;
    }
    emit connectionAlarmPolicyReady(requestId, blockingCodes, alarmAvailable);
    emit connectionSettingsReady(requestId, address, port, fingerprint, false);
}

void MaintenanceStore::saveConnectionSettings(quint64 requestId, QString configurationFile,
                                              QString expectedFingerprint, QString address, QString port)
{
    if (QThread::currentThread() != thread() || !requestId || requestId <= m_lastConnectionRequest)
    {
        emit connectionSettingsError(requestId, QStringLiteral("连接配置保存请求重复、过期或线程不正确。"), false);
        return;
    }
    m_lastConnectionRequest = requestId;
    if (!m_session.authenticated || m_session.role != 5)
    {
        emit connectionSettingsError(requestId, QStringLiteral("与原版一致，仅管理员可修改主控连接配置。"), false);
        return;
    }
    QString message = validateConnectionAddress(address, port);
    if (!message.isEmpty())
    {
        emit connectionSettingsError(requestId, message, false);
        return;
    }
    const QString current = connectionFileFingerprint(configurationFile, &message);
    if (current.isEmpty() || expectedFingerprint.isEmpty() || current != expectedFingerprint)
    {
        emit connectionSettingsError(requestId, message.isEmpty()
            ? QStringLiteral("连接配置已被其他操作修改，请保留草稿并重新读取后再保存。") : message, false);
        return;
    }
    QLockFile lock(configurationFile + QStringLiteral(".sn100-original-config.lock"));
    if (!lock.tryLock(0))
    {
        emit connectionSettingsError(requestId, QStringLiteral("另一个连接配置操作正在进行，请稍后重试。"), false);
        return;
    }
    if (current != connectionFileFingerprint(configurationFile, &message))
    {
        emit connectionSettingsError(requestId, QStringLiteral("连接配置在保存前已变化，请重新读取。"), false);
        return;
    }
    OriginalIni::Settings settings;
    if (!OriginalIni::read(configurationFile, &settings, &message))
    {
        emit connectionSettingsError(requestId, message, false);
        return;
    }
    // 只替换原版三个键的值，保留#注释、未知键、换行和其余原始字节。
    QFile source(configurationFile);
    if (!source.open(QIODevice::ReadOnly))
    {
        emit connectionSettingsError(requestId, source.errorString(), false);
        return;
    }
    QByteArray original = source.readAll();
    const bool readFailed = source.error() != QFile::NoError;
    source.close();
    const QMap<QByteArray, QByteArray> replacements = {
        {"tcpmain/ip", address.toUtf8()}, {"tcpmain/port", port.toUtf8()},
        {"tcplog/ip", address.toUtf8()}};
    QSet<QByteArray> seen;
    QByteArray group, rewritten;
    const QList<QByteArray> lines = original.split('\n');
    for (int i = 0; i < lines.size(); ++i)
    {
        QByteArray line = lines[i];
        QByteArray trimmed = line.trimmed();
        if (i == 0 && trimmed.startsWith("\xef\xbb\xbf")) trimmed.remove(0, 3);
        if (trimmed.startsWith('[') && trimmed.endsWith(']')) group = trimmed.mid(1, trimmed.size() - 2);
        else if (!trimmed.startsWith('#') && !trimmed.startsWith(';'))
        {
            const int equal = line.indexOf('=');
            const QByteArray key = group + '/' + line.left(equal).trimmed();
            if (equal >= 0 && replacements.contains(key))
            {
                if (seen.contains(key))
                {
                    emit connectionSettingsError(requestId, QStringLiteral("连接配置有重复键，未保存，请先核对原文件。"), false);
                    return;
                }
                seen.insert(key);
                line = line.left(equal + 1) + replacements.value(key) + (line.endsWith('\r') ? "\r" : "");
            }
        }
        rewritten += line;
        if (i + 1 < lines.size()) rewritten += '\n';
    }
    // tcplog原键若不存在，增加同名组不会改变其原端口；不补造主连接的必需键。
    if (!seen.contains("tcplog/ip")) rewritten += "\n[tcplog]\nip=" + address.toUtf8() + '\n';
    QSaveFile output(configurationFile);
    if (readFailed || !seen.contains("tcpmain/ip") || !seen.contains("tcpmain/port") ||
        current != connectionFileFingerprint(configurationFile, &message) ||
        !output.open(QIODevice::WriteOnly) || output.write(rewritten) != rewritten.size() || !output.commit())
    {
        emit connectionSettingsError(requestId, QStringLiteral("连接配置未能原子保存，请核对文件后重试：") + output.errorString(), false);
        return;
    }
    const QString fingerprint = connectionFileFingerprint(configurationFile, &message);
    if (fingerprint.isEmpty())
    {
        // 写入结局不明时先要求重读，不能让重复保存覆盖可能已提交的配置。
        emit connectionSettingsError(requestId, QStringLiteral("连接配置写入或回读失败，请重新读取确认实际文件。"), true);
        return;
    }
    OriginalIni::Settings verified;
    if (!OriginalIni::read(configurationFile, &verified, &message) ||
        verified.values.value(QStringLiteral("tcpmain/ip")).toString() != address ||
        verified.values.value(QStringLiteral("tcpmain/port")).toString() != port ||
        verified.values.value(QStringLiteral("tcplog/ip")).toString() != address)
    {
        emit connectionSettingsError(requestId, QStringLiteral("配置已写入但回读不一致，请重新读取确认后再修改。"), true);
        return;
    }
    emit connectionSettingsReady(requestId, address, port, fingerprint, true);
}

void MaintenanceStore::registerTypes()
{
    qRegisterMetaType<MaintenanceRecord>("MaintenanceRecord");
    qRegisterMetaType<MaintenanceRecords>("MaintenanceRecords");
    qRegisterMetaType<AccountSession>("AccountSession");
}

bool MaintenanceStore::checkThread(const QString &operation)
{
    if (QThread::currentThread() == thread())
        return true;
    emit error(operation, QStringLiteral("维护存储只能在所属工作线程使用"));
    return false;
}

bool MaintenanceStore::checkReady(const QString &operation)
{
    if (!checkThread(operation))
        return false;
    if (m_ready && m_database.isOpen())
        return true;
    emit error(operation, QStringLiteral("维护数据尚未就绪，请等待初始化或重新打开程序"));
    return false;
}

void MaintenanceStore::initialize(QString absoluteDataDir)
{
    const QString operation = QStringLiteral("initialize");
    if (!checkThread(operation))
        return;
    if (m_database.isValid())
    {
        emit error(operation, QStringLiteral("维护存储已初始化；请先关闭后再打开"));
        return;
    }
    QString path, message;
    bool existing = false;
    if (!prepareOfflinePath(absoluteDataDir, &path, &existing, &message))
    {
        emit error(operation, message);
        return;
    }
    m_connectionName = QStringLiteral("sn100-maintenance-") + QUuid::createUuid().toString();
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(path);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(kBusyTimeoutMs));
    if (!m_database.open())
    {
        message = QStringLiteral("打开维护数据库失败: ") + m_database.lastError().text();
        releaseDatabase();
        emit error(operation, message);
        return;
    }
    bool success = true;
    {
        QSqlQuery query(m_database);
        if (existing)
        {
            // A failed first schema transaction can leave our tagged, empty
            // database behind. Retry only that empty state; never repair or
            // convert a database which already contains any user schema.
            success = query.exec(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'"))
                    && query.next();
            if (!success)
                message = QStringLiteral("读取维护数据库结构失败: ") + query.lastError().text();
            else if (query.value(0).toInt() == 0)
                existing = false;
            else
                success = verifySchema(m_database, &message);
            query.finish();
        }
        if (success && !query.exec(QStringLiteral("PRAGMA busy_timeout=200")))
        {
            success = false;
            message = QStringLiteral("设置维护数据库锁等待失败: ") + query.lastError().text();
        }
        if (success && !query.exec(QStringLiteral("PRAGMA synchronous=FULL")))
        {
            success = false;
            message = QStringLiteral("设置维护数据库持久化失败: ") + query.lastError().text();
        }
        if (success && !existing)
        {
            success = m_database.transaction();
            if (!success)
                message = QStringLiteral("开始维护建表事务失败: ") + m_database.lastError().text();
            for (const MaintenanceTable &table : maintenanceTables())
            {
                if (!success)
                    break;
                QStringList columns{QStringLiteral("id INTEGER PRIMARY KEY AUTOINCREMENT"),
                                    table.dateColumn + QStringLiteral(" DATE UNIQUE")};
                for (const QString &action : table.actions)
                    columns.append(action + QLatin1Char(' ') + table.actionType);
                success = query.exec(QStringLiteral("CREATE TABLE %1 (%2)")
                                     .arg(table.table, columns.join(QLatin1Char(','))));
                if (!success)
                    message = QStringLiteral("创建维护表失败: ") + query.lastError().text();
            }
            if (success && !m_database.commit())
            {
                success = false;
                message = QStringLiteral("提交维护建表事务失败: ") + m_database.lastError().text();
            }
            if (!success)
                m_database.rollback();
        }
    }
    if (!success)
    {
        releaseDatabase();
        emit error(operation, message);
        return;
    }
    m_ready = true;
    emit ready();
}

void MaintenanceStore::initializeOriginal(QString root)
{
    const QString operation = QStringLiteral("initializeOriginal");
    if (!checkThread(operation)) return;
    if ((QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread()) || m_database.isValid())
    { emit error(operation, QStringLiteral("请在维护存储工作线程且原连接关闭时打开原库。")); return; }
    QString message;
    std::unique_ptr<OriginalBusiness::Database> database(new OriginalBusiness::Database);
    if (!database->open(root, QStringLiteral("db/userSet/maintain.db"), true, &message) || !verifySchema(database->db, &message, true))
    { emit error(operation, message); return; }
    m_originalRoot = QFileInfo(root).canonicalFilePath();
    m_database = database->db;
    m_originalDatabase = std::move(database);
    m_ready = true;
    emit ready();
}

void MaintenanceStore::setSession(AccountSession session)
{
    if (checkThread(QStringLiteral("setSession")))
        m_session = session;
}

void MaintenanceStore::loadMonth(QDate month)
{
    const QString operation = QStringLiteral("loadMonth");
    if (!checkReady(operation))
        return;
    if (!month.isValid() || month.year() < 1 || month.year() > 9999)
    {
        emit error(operation, QStringLiteral("维护查询月份无效"));
        return;
    }
    const QDate first(month.year(), month.month(), 1);
    const QDate last(month.year(), month.month(), month.daysInMonth());
    MaintenanceRecords records;
    QString message;
    // All three original tables describe one calendar view. Keep one SQLite
    // read snapshot even if another application connection commits meanwhile.
    if (!m_database.transaction())
    {
        emit error(operation, QStringLiteral("开始维护查询快照失败: ") + m_database.lastError().text());
        return;
    }
    const bool success = [&]()
    {
        for (const MaintenanceTable &table : maintenanceTables())
        {
            QSqlQuery query(m_database);
            query.prepare(QStringLiteral("SELECT %1,%2 FROM %3 WHERE %1 BETWEEN ? AND ? ORDER BY %1,id")
                          .arg(table.dateColumn, table.actions.join(QLatin1Char(',')), table.table));
            query.addBindValue(first.toString(Qt::ISODate));
            query.addBindValue(last.toString(Qt::ISODate));
            if (!query.exec())
            {
                message = QStringLiteral("查询维护记录失败: ") + query.lastError().text();
                return false;
            }
            int rows = 0;
            QSet<QString> seenDates;
            while (query.next())
            {
                const QString date = query.value(0).toString();
                const QDate parsed = QDate::fromString(date, Qt::ISODate);
                if (++rows > (m_originalRoot.isEmpty() ? month.daysInMonth() : 100000) || !parsed.isValid()
                        || parsed.toString(Qt::ISODate) != date || parsed < first || parsed > last)
                {
                    message = QStringLiteral("维护日期或行数无效，未呈现不完整的记录");
                    return false;
                }
                // 原日历同日只取查询到的首行；保留重复原行，但不能合并后伪造完成项。
                if (!m_originalRoot.isEmpty() && seenDates.contains(date)) continue;
                seenDates.insert(date);
                for (int index = 0; index < table.actions.size(); ++index)
                {
                    const QString value = query.value(index + 1).toString();
                    if (m_originalRoot.isEmpty() ? value == QStringLiteral("1") : value.toInt() > 0)
                        records.append({table.dateColumn, date, table.actions.at(index)});
                    else if (!value.isEmpty() && m_originalRoot.isEmpty())
                    {
                        message = QStringLiteral("维护完成标记无效，未呈现不完整的记录");
                        return false;
                    }
                }
            }
            if (query.lastError().isValid())
            {
                message = QStringLiteral("读取维护记录失败: ") + query.lastError().text();
                return false;
            }
        }
        return true;
    }();
    if (!success)
    {
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    if (!m_database.commit())
    {
        message = QStringLiteral("结束维护查询快照失败: ") + m_database.lastError().text();
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    emit loaded(records);
}

void MaintenanceStore::record(QString action, QDate date)
{
    const QString operation = QStringLiteral("record");
    if (!checkReady(operation))
        return;
    if (!m_session.authenticated || m_session.role < (m_originalRoot.isEmpty() ? 1 : 0) || m_session.role > 5)
    {
        emit error(operation, QStringLiteral("请先建立有效操作员会话，再登记人工维护记录"));
        return;
    }
    if (!m_originalRoot.isEmpty() && m_session.originalPolicySource != m_originalRoot)
    { emit error(operation, QStringLiteral("当前身份不属于此原资料根，请重新登录。")); return; }
    // The supplier records today's date even while the calendar browses another
    // month. Do not let an offline completion fabricate past/future maintenance.
    if (!date.isValid() || date != QDate::currentDate())
    {
        emit error(operation, QStringLiteral("人工维护只能登记当天实际完成的操作"));
        return;
    }
    if (action == QStringLiteral("long_clean") || action == QStringLiteral("shut_down"))
    {
        emit error(operation, QStringLiteral("洗针和关机须由真实设备流程确认，离线不能登记完成"));
        return;
    }
    MaintenanceTable selected;
    for (const MaintenanceTable &table : maintenanceTables())
    {
        if (table.actions.contains(action))
            selected = table;
    }
    if (selected.table.isEmpty())
    {
        emit error(operation, QStringLiteral("未知的人工维护项目"));
        return;
    }
    QString message;
    bool success = false;
    {
        QSqlQuery query(m_database);
        if (!query.exec(QStringLiteral("BEGIN IMMEDIATE")))
            message = QStringLiteral("维护数据库正忙或无法开始事务: ") + query.lastError().text();
        else
        {
            if (!m_originalRoot.isEmpty())
            {
                // 原表没有日期唯一约束；先按原规则查当天，不能靠INSERT OR IGNORE去重。
                query.prepare(QStringLiteral("SELECT id,%1 FROM %2 WHERE %3=? ORDER BY id").arg(action, selected.table, selected.dateColumn));
                query.addBindValue(date.toString(Qt::ISODate));
                success = query.exec();
                bool exists = false; qint64 rowId = 0; QString previous;
                if (success && query.next()) { exists = true; rowId = query.value(0).toLongLong(); previous = query.value(1).toString(); }
                if (success && query.next())
                { success = false; message = QStringLiteral("原维护库同一天有多条记录，未猜测覆盖哪一条，请核对原资料。"); }
                if (query.lastError().isValid()) success = false;
                query.finish();
                if (success && exists && previous != QStringLiteral("1"))
                {
                    query.prepare(QStringLiteral("UPDATE %1 SET %2=? WHERE id=?").arg(selected.table, action));
                    query.addBindValue(QStringLiteral("1")); query.addBindValue(rowId);
                    success = query.exec() && query.numRowsAffected() == 1;
                }
                else if (success && !exists)
                {
                    // 原DAL给同表其他维护项绑定空文本；省略列会变NULL，不能改变原记录含义。
                    QStringList placeholders;
                    for (int column = 0; column <= selected.actions.size(); ++column) placeholders.append(QStringLiteral("?"));
                    query.prepare(QStringLiteral("INSERT INTO %1(%2,%3) VALUES(%4)").arg(selected.table,
                        selected.dateColumn, selected.actions.join(QLatin1Char(',')), placeholders.join(QLatin1Char(','))));
                    query.addBindValue(date.toString(Qt::ISODate));
                    for (const QString &knownAction : selected.actions)
                        query.addBindValue(knownAction == action ? QStringLiteral("1") : QStringLiteral(""));
                    success = query.exec() && query.numRowsAffected() == 1;
                }
            }
            else
            {
                query.prepare(QStringLiteral("INSERT OR IGNORE INTO %1 (%2) VALUES (?)").arg(selected.table, selected.dateColumn));
                query.addBindValue(date.toString(Qt::ISODate)); success = query.exec();
                if (success)
                {
                    query.prepare(QStringLiteral("UPDATE %1 SET %2=? WHERE %3=?").arg(selected.table, action, selected.dateColumn));
                    query.addBindValue(QStringLiteral("1")); query.addBindValue(date.toString(Qt::ISODate)); success = query.exec();
                }
            }
            if (!success && message.isEmpty())
                message = QStringLiteral("保存维护记录失败: ") + query.lastError().text();
            else if (!m_database.commit())
            {
                success = false;
                message = QStringLiteral("提交维护记录失败: ") + m_database.lastError().text();
            }
            if (!success)
                m_database.rollback();
        }
    }
    if (success)
        emit succeeded(action);
    else
        emit error(operation, message);
}

void MaintenanceStore::releaseDatabase()
{
    m_ready = false;
    if (m_database.isValid())
        m_database.close();
    m_database = QSqlDatabase();
    m_originalDatabase.reset();
    m_originalRoot.clear();
    if (!m_connectionName.isEmpty())
        QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
    m_session = AccountSession();
}

void MaintenanceStore::close()
{
    if (!checkThread(QStringLiteral("close")))
        return;
    releaseDatabase();
    emit closed();
}
