#include "maintenance_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QVariant>

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

bool verifySchema(QSqlDatabase &database, QString *message)
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
            while (query.next())
            {
                const QString date = query.value(0).toString();
                const QDate parsed = QDate::fromString(date, Qt::ISODate);
                if (++rows > month.daysInMonth() || !parsed.isValid()
                        || parsed.toString(Qt::ISODate) != date || parsed < first || parsed > last)
                {
                    message = QStringLiteral("维护日期或行数无效，未呈现不完整的记录");
                    return false;
                }
                for (int index = 0; index < table.actions.size(); ++index)
                {
                    const QString value = query.value(index + 1).toString();
                    if (value == QStringLiteral("1"))
                        records.append({table.dateColumn, date, table.actions.at(index)});
                    else if (!value.isEmpty())
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
    if (!m_session.authenticated || m_session.role < 1 || m_session.role > 5)
    {
        emit error(operation, QStringLiteral("请先选择有效的脱机操作员身份，再登记人工维护记录"));
        return;
    }
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
            // Only whitelisted identifiers enter SQL; dates/values are bound.
            // The date is unique, so repeating an action remains one completion.
            query.prepare(QStringLiteral("INSERT OR IGNORE INTO %1 (%2) VALUES (?)")
                          .arg(selected.table, selected.dateColumn));
            query.addBindValue(date.toString(Qt::ISODate));
            success = query.exec();
            if (success)
            {
                query.prepare(QStringLiteral("UPDATE %1 SET %2=? WHERE %3=?")
                              .arg(selected.table, action, selected.dateColumn));
                query.addBindValue(QStringLiteral("1"));
                query.addBindValue(date.toString(Qt::ISODate));
                success = query.exec();
            }
            if (!success)
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
