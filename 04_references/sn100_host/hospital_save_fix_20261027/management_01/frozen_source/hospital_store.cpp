#include "hospital_store.h"
#include "original_files/original_business_database.h"
#include "original_files/original_ini.h"
#include <QCryptographicHash>
#include <QLockFile>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QThread>
#include <QUuid>
#include <QVariant>

namespace
{
const int kBusyTimeoutMs = 200;
const int kHospitalNameLimit = 32767; // Original QLineEdit default maximum.
const QByteArray kOfflineDataTag("SN100_OFFLINE_O01_V1\n");

QString sqlFailure(const QString &context, const QSqlQuery &query)
{
    return context + QStringLiteral(": ") + query.lastError().text();
}

bool beginWriteTransaction(QSqlDatabase &database, QString *message)
{
    // Acquire the writer reservation before reading IDs. A deferred read-to-
    // write upgrade may fail immediately under another writer despite the busy
    // timeout, whereas BEGIN IMMEDIATE waits within the configured 200 ms.
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE")))
    {
        *message = query.lastError().text();
        return false;
    }
    return true;
}

bool prepareOfflineDirectory(const QFileInfo &databaseInfo, const QFileInfo &iniInfo,
                             QString *message)
{
    const QString directory = databaseInfo.absolutePath();
    // Resolve existing ancestors before creating anything, so a missing child
    // below a reference-directory symlink is rejected without even mkdir().
    QString existing = directory;
    QStringList missingParts;
    while (!QFileInfo::exists(existing))
    {
        const QFileInfo info(existing);
        const QString parent = info.absolutePath();
        if (parent == existing || info.isSymLink())
        {
            *message = QStringLiteral("无法解析所选数据目录的真实父路径");
            return false;
        }
        missingParts.prepend(info.fileName());
        existing = parent;
    }
    const QFileInfo existingInfo(existing);
    if (!existingInfo.isDir() || existingInfo.canonicalFilePath().isEmpty())
    {
        *message = QStringLiteral("所选数据路径的父级不是可解析的目录");
        return false;
    }
    QString canonicalDirectory = existingInfo.canonicalFilePath();
    for (const QString &part : missingParts)
    {
        canonicalDirectory = QDir(canonicalDirectory).filePath(part);
    }
    const QStringList segments = QDir::fromNativeSeparators(canonicalDirectory)
            .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive))
    {
        *message = QStringLiteral("禁止在只读原参考工程目录中创建或打开脱机数据");
        return false;
    }

    const QString tagPath = QDir(directory).filePath(QStringLiteral("offline-data.tag"));
    const QFileInfo tagInfo(tagPath);
    if (tagInfo.isSymLink())
    {
        *message = QStringLiteral("脱机实验目录标记不能是符号链接");
        return false;
    }
    if (tagInfo.exists())
    {
        QFile tag(tagPath);
        if (!tagInfo.isFile() || tagInfo.size() != kOfflineDataTag.size()
                || !tag.open(QIODevice::ReadOnly) || tag.readAll() != kOfflineDataTag
                || tag.error() != QFile::NoError)
        {
            *message = QStringLiteral("offline-data.tag 无效；未打开或修改已有数据库与 INI");
            return false;
        }
        return true;
    }
    if (databaseInfo.exists() || iniInfo.exists())
    {
        *message = QStringLiteral("已有数据库或 INI 缺少有效 offline-data.tag，拒绝将其当作脱机实验数据；未修改原文件");
        return false;
    }
    if (!QDir().mkpath(directory))
    {
        *message = QStringLiteral("无法创建所选数据目录: ") + directory;
        return false;
    }
    QSaveFile tag(tagPath);
    tag.setDirectWriteFallback(false);
    if (!tag.open(QIODevice::WriteOnly) || tag.write(kOfflineDataTag) != kOfflineDataTag.size()
            || !tag.commit())
    {
        *message = QStringLiteral("创建脱机实验目录标记失败: ") + tag.errorString();
        return false;
    }
    return true;
}

bool verifyHospitalSchema(QSqlDatabase &database, QString *message, bool original = false)
{
    const QStringList names = {QStringLiteral("id"), QStringLiteral("column_one"),
                               QStringLiteral("column_two"), QStringLiteral("column_three"),
                               QStringLiteral("type")};
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(hospital_information)")))
    {
        *message = sqlFailure(QStringLiteral("读取医院信息表结构失败"), query);
        return false;
    }
    int column = 0;
    while (query.next())
    {
        if (original)
        {
            const QString name = query.value(1).toString();
            if (!names.contains(name)) continue;
            const int expected = names.indexOf(name);
            const QString type = expected == 0 ? QStringLiteral("INTEGER") : QStringLiteral("VARCHAR");
            if (query.value(2).toString().toUpper() != type || query.value(5).toInt() != (expected == 0 ? 1 : 0))
            { *message = QStringLiteral("原医院表已知字段类型不符，未改表。"); return false; }
            ++column; continue;
        }
        const QString expectedType = column == 0 ? QStringLiteral("INTEGER") : QStringLiteral("VARCHAR");
        if (column >= names.size() || query.value(1).toString() != names.at(column)
                || query.value(2).toString().toUpper() != expectedType
                || query.value(5).toInt() != (column == 0 ? 1 : 0))
        {
            *message = QStringLiteral("医院信息表结构与本模块不匹配；未修改或转换已有数据");
            return false;
        }
        ++column;
    }
    if (column != names.size())
    {
        *message = QStringLiteral("医院信息表字段不完整；未修改或转换已有数据");
        return false;
    }
    return true;
}
}

HospitalStore::HospitalStore(QObject *parent) : QObject(parent)
{
    registerTypes();
}

HospitalStore::~HospitalStore()
{
    // The owner must arrange deletion in this object's worker thread.
    Q_ASSERT(!m_database.isValid() || QThread::currentThread() == thread());
    releaseDatabase();
}

void HospitalStore::registerTypes()
{
    qRegisterMetaType<HospitalRow>("HospitalRow");
    qRegisterMetaType<HospitalRows>("HospitalRows");
    registerHospitalDictionaryTypes();
}

bool HospitalStore::isAllowedType(const QString &type)
{
    return type == QStringLiteral("keshi") || type == QStringLiteral("bingren")
            || type == QStringLiteral("songjian") || type == QStringLiteral("jianyan")
            || type == QStringLiteral("shenhe");
}

bool HospitalStore::checkThread(const QString &operation)
{
    if (QThread::currentThread() != thread())
    {
        emit error(operation, QStringLiteral("数据库请求必须通过排队信号发送至 HospitalStore 工作线程"));
        return false;
    }
    return true;
}

bool HospitalStore::checkReady(const QString &operation)
{
    if (!checkThread(operation))
    {
        return false;
    }
    if (!m_ready || !m_database.isOpen())
    {
        emit error(operation, QStringLiteral("数据库尚未成功初始化或已经关闭"));
        return false;
    }
    return true;
}

void HospitalStore::initialize(QString databasePath, QString iniPath)
{
    const QString operation = QStringLiteral("initialize");
    if (!checkThread(operation))
    {
        return;
    }
    if (QCoreApplication::instance()
            && QThread::currentThread() == QCoreApplication::instance()->thread())
    {
        emit error(operation, QStringLiteral("请先将 HospitalStore 移入工作线程，再初始化数据库"));
        return;
    }
    if (m_ready)
    {
        emit error(operation, QStringLiteral("数据库已经初始化；请先关闭再重新初始化"));
        return;
    }
    if (!QDir::isAbsolutePath(databasePath) || !QDir::isAbsolutePath(iniPath))
    {
        emit error(operation, QStringLiteral("数据库和 INI 必须使用显式数据目录内的绝对路径"));
        return;
    }
    databasePath = QDir::cleanPath(databasePath);
    iniPath = QDir::cleanPath(iniPath);
    const QFileInfo databaseInfo(databasePath);
    const QFileInfo iniInfo(iniPath);
    if (databasePath == iniPath || databaseInfo.absolutePath() != iniInfo.absolutePath()
            || databaseInfo.isSymLink() || iniInfo.isSymLink())
    {
        emit error(operation, QStringLiteral("数据库和 INI 应为同一显式数据目录中的不同普通文件，不能使用符号链接"));
        return;
    }
    QString message;
    if (!prepareOfflineDirectory(databaseInfo, iniInfo, &message))
    {
        emit error(operation, message);
        return;
    }

    m_connectionName = QStringLiteral("sn100-hospital-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(databasePath);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(kBusyTimeoutMs));
    m_iniPath = iniPath;

    // Keep every query in this scope, before closing/removing a failed connection.
    const bool initialized = [&]()
    {
        if (!m_database.open())
        {
            message = QStringLiteral("打开 SQLite 失败: ") + m_database.lastError().text();
            return false;
        }
        QSqlQuery query(m_database);
        const QStringList pragmas = {
            QStringLiteral("PRAGMA busy_timeout=%1").arg(kBusyTimeoutMs),
            QStringLiteral("PRAGMA journal_mode=DELETE"),
            QStringLiteral("PRAGMA synchronous=FULL")
        };
        for (const QString &pragma : pragmas)
        {
            if (!query.exec(pragma))
            {
                message = sqlFailure(QStringLiteral("配置 SQLite 持久性失败"), query);
                return false;
            }
            query.finish();
        }
        if (!m_database.transaction())
        {
            message = QStringLiteral("初始化事务失败: ") + m_database.lastError().text();
            return false;
        }
        if (!query.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS hospital_information ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,column_one VARCHAR,"
                "column_two VARCHAR,column_three VARCHAR,type VARCHAR)"))
                || !verifyHospitalSchema(m_database, &message))
        {
            if (message.isEmpty())
            {
                message = sqlFailure(QStringLiteral("创建医院信息表失败"), query);
            }
            m_database.rollback();
            return false;
        }
        if (!query.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS app_settings (key TEXT PRIMARY KEY,value TEXT NOT NULL)")))
        {
            message = sqlFailure(QStringLiteral("创建医院名称设置表失败"), query);
            m_database.rollback();
            return false;
        }
        query.prepare(QStringLiteral("INSERT OR IGNORE INTO app_settings(key,value) VALUES(:key,:value)"));
        query.bindValue(QStringLiteral(":key"), QStringLiteral("hospital/name"));
        query.bindValue(QStringLiteral(":value"), QStringLiteral(""));
        if (!query.exec())
        {
            message = sqlFailure(QStringLiteral("初始化空医院名称失败"), query);
            m_database.rollback();
            return false;
        }
        query.finish();
        if (!m_database.commit())
        {
            message = QStringLiteral("初始化提交失败: ") + m_database.lastError().text();
            m_database.rollback();
            return false;
        }
        return true;
    }();
    if (!initialized)
    {
        releaseDatabase();
        emit error(operation, message);
        return;
    }

    m_ready = true;
    QString hospitalName;
    HospitalRows rows;
    if (!readSnapshot(QString(), &hospitalName, &rows, &message))
    {
        releaseDatabase();
        emit error(operation, message);
        return;
    }
    if (!exportIni(hospitalName, &message))
    {
        emit warning(QStringLiteral("SQLite 已正常打开；医院名称 INI 导出失败，下次启动或保存时重试: ") + message);
    }
    emit ready();
}

bool HospitalStore::readSnapshot(const QString &type, QString *hospitalName,
                                 HospitalRows *rows, QString *message)
{
    if (!m_originalRoot.isEmpty())
        *hospitalName = m_originalHospitalName;
    else
    {
        QSqlQuery settingsQuery(m_database);
        settingsQuery.prepare(QStringLiteral("SELECT value FROM app_settings WHERE key=:key"));
        settingsQuery.bindValue(QStringLiteral(":key"), QStringLiteral("hospital/name"));
        if (!settingsQuery.exec())
        {
            *message = sqlFailure(QStringLiteral("读取医院名称失败"), settingsQuery);
            return false;
        }
        if (!settingsQuery.next())
        {
            *message = QStringLiteral("SQLite 权威设置中缺少 hospital/name");
            return false;
        }
        *hospitalName = settingsQuery.value(0).toString();
        settingsQuery.finish();
    }
    QSqlQuery query(m_database);
    QString sql = QStringLiteral("SELECT id,column_one,column_two,column_three,type FROM hospital_information");
    if (!type.isEmpty())
    {
        sql += QStringLiteral(" WHERE type=:type");
    }
    sql += QStringLiteral(" ORDER BY id");
    query.prepare(sql);
    if (!type.isEmpty())
    {
        query.bindValue(QStringLiteral(":type"), type);
    }
    if (!query.exec())
    {
        *message = sqlFailure(QStringLiteral("读取医院信息失败"), query);
        return false;
    }
    rows->clear();
    while (query.next())
    {
        HospitalRow row;
        row.id = query.value(0).toLongLong();
        row.columnOne = query.value(1).toString();
        row.columnTwo = query.value(2).toString();
        row.columnThree = query.value(3).toString();
        row.type = query.value(4).toString();
        rows->append(row);
    }
    if (query.lastError().isValid())
    {
        *message = sqlFailure(QStringLiteral("读取医院信息未完成"), query);
        return false;
    }
    return true;
}

void HospitalStore::queryCommittedDictionary(HospitalDictionaryRequest request)
{
    HospitalDictionaryReply reply;
    reply.request = request;
    // The editor's uncorrelated error/loaded signals must not end this query,
    // and a picker failure must not end an unrelated editor operation.
    const auto readCommitted = [this, &reply, &request]() -> bool
    {
        if (QThread::currentThread() != thread())
        {
            reply.error = QStringLiteral("字典读取线程不匹配，未执行查询");
            return false;
        }
        if (!m_ready || !m_database.isOpen())
        {
            reply.error = QStringLiteral("医院字典尚未准备好或已经关闭");
            return false;
        }
        if (request.requestId.isNull() || !isAllowedType(request.type))
        {
            reply.error = QStringLiteral("字典查询已失效，请重新打开选择器");
            return false;
        }
        if (request.filter.size() > kHospitalDictionaryFilterLimit)
        {
            reply.error = QStringLiteral("筛选内容过长，请缩短后重试");
            return false;
        }
        QString sql = QStringLiteral("SELECT id,column_one,column_two FROM hospital_information WHERE type=:type");
        if (!request.filter.isEmpty())
            sql += QStringLiteral(" AND (column_one LIKE :pattern OR column_two LIKE :pattern)");
        // The old query left ordering unspecified; use persistent id, not the
        // displayed row number, to make the committed snapshot deterministic.
        sql += QStringLiteral(" ORDER BY id ASC");
        QSqlQuery query(m_database);
        if (!query.prepare(sql))
        {
            reply.error = sqlFailure(QStringLiteral("准备字典查询失败"), query);
            return false;
        }
        query.bindValue(QStringLiteral(":type"), request.type);
        if (!request.filter.isEmpty())
            query.bindValue(QStringLiteral(":pattern"), QLatin1Char('%') + request.filter + QLatin1Char('%'));
        if (!query.exec())
        {
            reply.error = sqlFailure(QStringLiteral("读取医院字典失败"), query);
            return false;
        }
        while (query.next())
        {
            bool validId = false;
            HospitalDictionaryEntry entry;
            entry.id = query.value(0).toLongLong(&validId);
            if (!validId || entry.id <= 0)
            {
                reply.error = QStringLiteral("医院字典记录标识无效，未返回不完整结果");
                return false;
            }
            entry.name = query.value(1).toString();
            entry.mnemonic = query.value(2).toString();
            reply.entries.append(entry);
        }
        if (query.lastError().isValid())
        {
            reply.error = sqlFailure(QStringLiteral("读取医院字典未完成"), query);
            return false;
        }
        query.finish();
        return true;
    };
    reply.succeeded = readCommitted();
    if (!reply.succeeded)
        reply.entries.clear();
    // Destroy the SQL cursor before notifying receivers, including a receiver
    // that may synchronously close the store. One executed request, one reply.
    emit dictionaryQueryCompleted(reply);
}

void HospitalStore::load(QString type)
{
    const QString operation = QStringLiteral("load");
    if (!checkReady(operation))
    {
        return;
    }
    if (!isAllowedType(type))
    {
        emit error(operation, QStringLiteral("不支持的医院信息类别: ") + type);
        return;
    }
    if (!m_originalRoot.isEmpty())
    {
        QString message;
        if (!refreshOriginalHospitalName(&message)) { emit error(operation, message); return; }
    }
    QString hospitalName;
    HospitalRows rows;
    QString message;
    if (!m_database.transaction())
    {
        emit error(operation, QStringLiteral("读取事务失败: ") + m_database.lastError().text());
        return;
    }
    if (!readSnapshot(type, &hospitalName, &rows, &message))
    {
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    if (!m_database.commit())
    {
        message = QStringLiteral("结束读取事务失败: ") + m_database.lastError().text();
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    emit loaded(type, hospitalName, rows);
}

void HospitalStore::readAll()
{
    const QString operation = QStringLiteral("readAll");
    if (!checkReady(operation))
    {
        return;
    }
    if (!m_originalRoot.isEmpty())
    {
        QString message;
        if (!refreshOriginalHospitalName(&message)) { emit error(operation, message); return; }
    }
    QString hospitalName;
    HospitalRows rows;
    QString message;
    if (!m_database.transaction())
    {
        emit error(operation, QStringLiteral("全量读取事务失败: ") + m_database.lastError().text());
        return;
    }
    if (!readSnapshot(QString(), &hospitalName, &rows, &message))
    {
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    if (!m_database.commit())
    {
        message = QStringLiteral("结束全量读取事务失败: ") + m_database.lastError().text();
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    emit allLoaded(hospitalName, rows);
}

bool HospitalStore::validateSave(const QString &type, const QString &hospitalName,
                                 const HospitalRows &rows, QString *message) const
{
    if (!isAllowedType(type))
    {
        *message = QStringLiteral("不支持的医院信息类别: ") + type;
        return false;
    }
    if (hospitalName.isEmpty() || hospitalName.size() > kHospitalNameLimit
            || hospitalName.contains(QChar::Null))
    {
        *message = QStringLiteral("医院名称不能为空，且应不超过 32767 个字符、不能含空字符");
        return false;
    }
    static const QRegularExpression namePattern(QStringLiteral("\\A[0-9A-Za-z\\x{4e00}-\\x{9fa5}]{1,6}\\z"));
    static const QRegularExpression employeePattern(QStringLiteral("\\A[0-9A-Za-z]{0,6}\\z"));
    QSet<qint64> seenIds;
    for (int index = 0; index < rows.size(); ++index)
    {
        const HospitalRow &row = rows.at(index);
        const QString prefix = QStringLiteral("第 %1 行: ").arg(index + 1);
        if (row.type != type || row.id < 0 || (row.id > 0 && seenIds.contains(row.id)))
        {
            *message = prefix + QStringLiteral("类别不匹配、ID 为负或 ID 重复");
            return false;
        }
        if (row.id > 0)
        {
            seenIds.insert(row.id);
        }
        if (!namePattern.match(row.columnOne).hasMatch())
        {
            *message = prefix + QStringLiteral("名称必须为 1 至 6 个中文、英文字母或数字");
            return false;
        }
        if (row.columnTwo.size() > 6 || row.columnTwo.contains(QChar::Null))
        {
            *message = prefix + QStringLiteral("助记符应不超过 6 个字符且不能含空字符");
            return false;
        }
        if (type == QStringLiteral("keshi") || type == QStringLiteral("bingren"))
        {
            if (row.columnThree.size() > 32 || row.columnThree.contains(QChar::Null))
            {
                *message = prefix + QStringLiteral("备注应不超过 32 个字符且不能含空字符");
                return false;
            }
        }
        else if (!employeePattern.match(row.columnThree).hasMatch())
        {
            *message = prefix + QStringLiteral("工号应为不超过 6 个英文字母或数字");
            return false;
        }
    }
    return true;
}

void HospitalStore::save(QString type, QString hospitalName, HospitalRows rows)
{
    const QString operation = QStringLiteral("save");
    if (!checkReady(operation))
    {
        return;
    }
    if (!m_originalRoot.isEmpty()) { saveOriginal(type, hospitalName, rows); return; }
    QString message;
    if (!validateSave(type, hospitalName, rows, &message))
    {
        emit error(operation, message);
        return;
    }
    if (!beginWriteTransaction(m_database, &message))
    {
        emit error(operation, QStringLiteral("开始保存事务失败: ") + message);
        return;
    }
    QString savedName;
    HospitalRows savedRows;
    const bool written = [&]()
    {
        // Validate every existing ID before making any change in the transaction.
        QSqlQuery existing(m_database);
        existing.prepare(QStringLiteral("SELECT type FROM hospital_information WHERE id=:id"));
        for (const HospitalRow &row : rows)
        {
            if (row.id == 0)
            {
                continue;
            }
            existing.bindValue(QStringLiteral(":id"), row.id);
            if (!existing.exec())
            {
                message = sqlFailure(QStringLiteral("核对已有 ID 失败"), existing);
                return false;
            }
            if (!existing.next() || existing.value(0).toString() != type)
            {
                message = QStringLiteral("ID %1 不存在或不属于当前类别；没有保存任何修改").arg(row.id);
                return false;
            }
            existing.finish();
        }
        QSqlQuery writeQuery(m_database);
        for (const HospitalRow &row : rows)
        {
            const QString sql = row.id == 0
                    ? QStringLiteral("INSERT INTO hospital_information(column_one,column_two,column_three,type) "
                                     "VALUES(:one,:two,:three,:type)")
                    : QStringLiteral("UPDATE hospital_information SET column_one=:one,column_two=:two,"
                                     "column_three=:three WHERE id=:id AND type=:type");
            if (!writeQuery.prepare(sql))
            {
                message = sqlFailure(QStringLiteral("准备医院信息保存失败"), writeQuery);
                return false;
            }
            writeQuery.bindValue(QStringLiteral(":one"), row.columnOne);
            writeQuery.bindValue(QStringLiteral(":two"), row.columnTwo);
            writeQuery.bindValue(QStringLiteral(":three"), row.columnThree);
            writeQuery.bindValue(QStringLiteral(":type"), type);
            if (row.id != 0)
            {
                writeQuery.bindValue(QStringLiteral(":id"), row.id);
            }
            if (!writeQuery.exec() || writeQuery.numRowsAffected() != 1)
            {
                message = sqlFailure(QStringLiteral("保存医院信息失败"), writeQuery);
                return false;
            }
            writeQuery.finish();
        }
        QSqlQuery nameQuery(m_database);
        nameQuery.prepare(QStringLiteral("UPDATE app_settings SET value=:value WHERE key=:key"));
        nameQuery.bindValue(QStringLiteral(":value"), hospitalName);
        nameQuery.bindValue(QStringLiteral(":key"), QStringLiteral("hospital/name"));
        if (!nameQuery.exec() || nameQuery.numRowsAffected() != 1)
        {
            message = sqlFailure(QStringLiteral("保存医院名称失败"), nameQuery);
            return false;
        }
        nameQuery.finish();
        return readSnapshot(type, &savedName, &savedRows, &message);
    }();
    if (!written)
    {
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    if (!m_database.commit())
    {
        message = QStringLiteral("保存事务提交失败: ") + m_database.lastError().text();
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    if (!exportIni(savedName, &message))
    {
        emit warning(QStringLiteral("数据已提交到 SQLite；医院名称 INI 导出失败，下次启动或保存时重试: ") + message);
    }
    emit saved(type, savedName, savedRows);
}

void HospitalStore::remove(QString type, qint64 id)
{
    const QString operation = QStringLiteral("remove");
    if (!checkReady(operation))
    {
        return;
    }
    if (!isAllowedType(type) || id <= 0)
    {
        emit error(operation, QStringLiteral("删除需要受支持的类别和大于零的已有 ID"));
        return;
    }
    QString message;
    if (!beginWriteTransaction(m_database, &message))
    {
        emit error(operation, QStringLiteral("开始删除事务失败: ") + message);
        return;
    }
    const bool deleted = [&]()
    {
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("DELETE FROM hospital_information WHERE id=:id AND type=:type"));
        query.bindValue(QStringLiteral(":id"), id);
        query.bindValue(QStringLiteral(":type"), type);
        if (!query.exec())
        {
            message = sqlFailure(QStringLiteral("删除医院信息失败"), query);
            return false;
        }
        if (query.numRowsAffected() != 1)
        {
            message = QStringLiteral("ID %1 不存在或不属于当前类别；没有删除任何记录").arg(id);
            return false;
        }
        return true;
    }();
    if (!deleted)
    {
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    if (!m_database.commit())
    {
        message = QStringLiteral("删除事务提交失败: ") + m_database.lastError().text();
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    emit removed(type, id);
}

bool HospitalStore::exportIni(const QString &hospitalName, QString *message)
{
    // QSettings performs the original Qt5 UTF-8 INI escaping. QSaveFile then
    // replaces the selected export atomically without risking a partial file.
    // Preserve other keys in this experiment's own INI, as the original page
    // only changes hospital/name. Invalid/unreadable exports are never replaced.
    QByteArray existingContents;
    if (QFileInfo::exists(m_iniPath))
    {
        QFile existing(m_iniPath);
        if (!existing.open(QIODevice::ReadOnly))
        {
            *message = QStringLiteral("无法读取已有 INI，保留原文件: ") + existing.errorString();
            return false;
        }
        existingContents = existing.readAll();
        if (existing.error() != QFile::NoError)
        {
            *message = QStringLiteral("读取已有 INI 失败，保留原文件: ") + existing.errorString();
            return false;
        }
    }
    QTemporaryFile temporary(QFileInfo(m_iniPath).absolutePath()
                             + QStringLiteral("/.hospital-settings-XXXXXX.ini"));
    if (!temporary.open())
    {
        *message = temporary.errorString();
        return false;
    }
    const QString temporaryPath = temporary.fileName();
    if (temporary.write(existingContents) != existingContents.size() || !temporary.flush())
    {
        *message = QStringLiteral("无法准备 INI 导出临时文件: ") + temporary.errorString();
        return false;
    }
    temporary.close();
    {
        QSettings settings(temporaryPath, QSettings::IniFormat);
        settings.setIniCodec("UTF-8");
        settings.allKeys(); // Force parsing before changing any value.
        if (settings.status() != QSettings::NoError)
        {
            *message = QStringLiteral("已有 INI 格式无效，保留原文件，QSettings status=%1").arg(int(settings.status()));
            return false;
        }
        settings.setValue(QStringLiteral("hospital/name"), hospitalName);
        settings.sync();
        if (settings.status() != QSettings::NoError)
        {
            *message = QStringLiteral("UTF-8 INI 序列化失败，QSettings status=%1").arg(int(settings.status()));
            return false;
        }
    }
    QFile serialized(temporaryPath);
    if (!serialized.open(QIODevice::ReadOnly))
    {
        *message = serialized.errorString();
        return false;
    }
    const QByteArray bytes = serialized.readAll();
    if (serialized.error() != QFile::NoError)
    {
        *message = serialized.errorString();
        return false;
    }
    serialized.close();
    QSaveFile target(m_iniPath);
    target.setDirectWriteFallback(false);
    if (!target.open(QIODevice::WriteOnly))
    {
        *message = target.errorString();
        return false;
    }
    if (target.write(bytes) != bytes.size())
    {
        *message = target.errorString();
        target.cancelWriting();
        return false;
    }
    if (!target.commit())
    {
        *message = target.errorString();
        return false;
    }
    return true;
}

void HospitalStore::releaseDatabase()
{
    m_ready = false;
    if (m_database.isValid())
    {
        m_database.close();
    }
    m_database = QSqlDatabase();
    m_originalDatabase.reset();
    m_originalRoot.clear();
    m_originalHospitalName.clear();
    m_originalIniFingerprint.clear();
    if (!m_connectionName.isEmpty())
    {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_connectionName.clear();
    m_iniPath.clear();
}

void HospitalStore::close()
{
    if (!checkThread(QStringLiteral("close")))
    {
        return;
    }
    releaseDatabase();
    emit closed();
}

namespace
{
QString hospitalIniFingerprint(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool readHospitalIniBytes(const QString &path, QByteArray *bytes, QString *message)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() > 262144)
    { *message = QStringLiteral("原医院配置须为不超过256KiB的现有普通文件。"); return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { *message = QStringLiteral("原医院配置无法读取。"); return false; }
    *bytes = file.readAll();
    if (file.error() != QFile::NoError) { *message = QStringLiteral("原医院配置读取未完成。"); return false; }
    return true;
}

bool publishHospitalIniBytes(const QString &path, const QByteArray &bytes, QString *message)
{
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(QFileInfo(path).permissions()) ||
        output.write(bytes) != bytes.size() || !output.commit())
    { *message = QStringLiteral("原医院名称配置未能完整提交，请检查目录权限或空间。"); return false; }
    return true;
}

bool hospitalNameBytes(const QString &path, const QByteArray &original, const QString &name,
                       QByteArray *rewritten, QString *message)
{
    // 让Qt只序列化医院名称本值；原配置本身不经QSettings重写，避免丢掉注释和未知键。
    QTemporaryFile valueFile(QFileInfo(path).absolutePath() + QStringLiteral("/.hospital-value-XXXXXX.ini"));
    if (!valueFile.open()) { *message = QStringLiteral("无法准备医院名称读取/保存副本。"); return false; }
    valueFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QString temporaryPath = valueFile.fileName();
    valueFile.close();
    {
        QSettings settings(temporaryPath, QSettings::IniFormat);
        settings.setIniCodec("UTF-8");
        settings.setValue(QStringLiteral("hospital/name"), name);
        settings.sync();
        if (settings.status() != QSettings::NoError) { *message = QStringLiteral("医院名称无法按原INI格式序列化。"); return false; }
    }
    QFile encoded(temporaryPath);
    if (!encoded.open(QIODevice::ReadOnly)) { *message = QStringLiteral("无法读取医院名称序列化副本。"); return false; }
    QByteArray encodedValue;
    bool foundEncoded = false;
    for (const QByteArray &line : encoded.readAll().split('\n'))
    {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith("name=")) { encodedValue = trimmed.mid(5); foundEncoded = true; }
    }
    encoded.close();
    if (!foundEncoded) { *message = QStringLiteral("医院名称序列化结果缺少本键。"); return false; }
    QList<QByteArray> lines = original.split('\n');
    int nameIndex = -1, hospitalHeader = -1, groupEnd = lines.size();
    QByteArray group;
    for (int index = 0; index < lines.size(); ++index)
    {
        QByteArray trimmed = lines[index].trimmed();
        if (index == 0 && trimmed.startsWith("\xef\xbb\xbf")) trimmed.remove(0, 3);
        if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith(';')) continue;
        if (trimmed.startsWith('[') && trimmed.endsWith(']'))
        {
            const QByteArray next = trimmed.mid(1, trimmed.size() - 2).trimmed();
            if (group == "hospital" && next != "hospital" && groupEnd == lines.size()) groupEnd = index;
            group = next;
            if (group == "hospital")
            {
                if (hospitalHeader >= 0) { *message = QStringLiteral("医院配置含重复分组，未猜测改写位置。"); return false; }
                hospitalHeader = index;
            }
            continue;
        }
        const int equal = lines[index].indexOf('=');
        if (group == "hospital" && equal >= 0 && lines[index].left(equal).trimmed() == "name")
        {
            if (nameIndex >= 0) { *message = QStringLiteral("医院名称键重复，未选择其中一份覆盖。"); return false; }
            nameIndex = index;
        }
    }
    const bool crlf = original.contains("\r\n");
    if (nameIndex >= 0)
    {
        const QByteArray oldLine = lines[nameIndex];
        lines[nameIndex] = oldLine.left(oldLine.indexOf('=') + 1) + encodedValue + (oldLine.endsWith('\r') ? "\r" : "");
        *rewritten = lines.join('\n');
    }
    else if (hospitalHeader >= 0)
    {
        if (groupEnd == lines.size() && !lines.isEmpty() && lines.last().isEmpty()) --groupEnd;
        lines.insert(groupEnd, QByteArray("name=") + encodedValue + (crlf ? "\r" : ""));
        *rewritten = lines.join('\n');
    }
    else
    {
        const QByteArray newline = crlf ? QByteArray("\r\n") : QByteArray("\n");
        *rewritten = original;
        if (!rewritten->isEmpty() && !rewritten->endsWith('\n')) rewritten->append(newline);
        rewritten->append(QByteArray("[hospital]") + newline + "name=" + encodedValue + newline);
    }
    OriginalIni::Settings before, after;
    if (!OriginalIni::read(path, &before, message)) return false;
    if (!valueFile.open() || !valueFile.resize(0) || valueFile.write(*rewritten) != rewritten->size() || !valueFile.flush())
    { *message = QStringLiteral("医院配置保存前的回读副本准备失败。"); return false; }
    valueFile.close();
    if (!OriginalIni::read(temporaryPath, &after, message) || after.values.value(QStringLiteral("hospital/name")).toString() != name)
    { *message = QStringLiteral("医院名称保存前回读不一致，未改原配置。"); return false; }
    before.values.remove(QStringLiteral("hospital/name")); after.values.remove(QStringLiteral("hospital/name"));
    if (before.values != after.values)
    { *message = QStringLiteral("医院名称以外的原配置会发生变化，未保存。"); return false; }
    return true;
}
}

bool HospitalStore::refreshOriginalHospitalName(QString *message)
{
    QByteArray before, after;
    if (!readHospitalIniBytes(m_iniPath, &before, message)) return false;
    OriginalIni::Settings configuration;
    if (!OriginalIni::read(m_iniPath, &configuration, message)) return false;
    QString name;
    if (configuration.values.contains(QStringLiteral("hospital/name")))
        name = configuration.values.value(QStringLiteral("hospital/name")).toString();
    else
    {
        OriginalIni::Settings theme;
        const QString themePath = QDir(m_originalRoot).filePath(QStringLiteral("theme_config.ini"));
        QByteArray themeBefore, themeAfter;
        if (!readHospitalIniBytes(themePath, &themeBefore, message) || !OriginalIni::read(themePath, &theme, message) ||
            !readHospitalIniBytes(themePath, &themeAfter, message) || themeBefore != themeAfter)
        { if (message->isEmpty()) *message = QStringLiteral("读取时原主题配置变化，请重新读取。"); return false; }
        name = theme.values.value(QStringLiteral("hospital/name")).toString();
    }
    if (!readHospitalIniBytes(m_iniPath, &after, message) || before != after)
    { *message = QStringLiteral("读取时原全局配置变化，请重新读取。"); return false; }
    m_originalHospitalName = name;
    m_originalIniFingerprint = hospitalIniFingerprint(after);
    return true;
}

void HospitalStore::initializeOriginal(QString root)
{
    const QString operation = QStringLiteral("initializeOriginal");
    if (!checkThread(operation)) return;
    if ((QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread()) || m_database.isValid())
    { emit error(operation, QStringLiteral("请在医院存储工作线程且旧连接关闭时接入原库。")); return; }
    QString message;
    std::unique_ptr<OriginalBusiness::Database> database(new OriginalBusiness::Database);
    if (!database->open(root, QStringLiteral("db/userSet/hospital_Information.db"), true, &message) ||
        !verifyHospitalSchema(database->db, &message, true))
    { emit error(operation, message); return; }
    m_originalRoot = QFileInfo(root).canonicalFilePath();
    m_iniPath = QDir(m_originalRoot).filePath(QStringLiteral("config.ini"));
    if (!refreshOriginalHospitalName(&message))
    { m_originalRoot.clear(); m_iniPath.clear(); emit error(operation, message); return; }
    m_database = database->db;
    m_originalDatabase = std::move(database);
    m_ready = true;
    emit ready();
}

void HospitalStore::saveOriginal(QString type, QString hospitalName, HospitalRows rows)
{
    const QString operation = QStringLiteral("save");
    QString message;
    if (!OriginalBusiness::workingRoot(m_originalRoot, &message) || !validateSave(type, hospitalName, rows, &message))
    { emit error(operation, message); return; }
    QLockFile lock(m_iniPath + QStringLiteral(".sn100-original-config.lock"));
    if (!lock.tryLock(0)) { emit error(operation, QStringLiteral("原配置正在由另一操作维护，请稍后再试。")); return; }
    QByteArray originalIni, changedIni;
    if (!readHospitalIniBytes(m_iniPath, &originalIni, &message)) { emit error(operation, message); return; }
    if (hospitalIniFingerprint(originalIni) != m_originalIniFingerprint)
    { emit error(operation, QStringLiteral("原全局配置已变化，请保留输入并重新读取后保存。")); return; }
    const bool nameChanged = hospitalName != m_originalHospitalName;
    if (nameChanged && !hospitalNameBytes(m_iniPath, originalIni, hospitalName, &changedIni, &message))
    { emit error(operation, message); return; }
    if (!beginWriteTransaction(m_database, &message)) { emit error(operation, message); return; }
    HospitalRows savedRows; QString previousName;
    const bool written = [&]()
    {
        for (const HospitalRow &row : rows)
        {
            if (row.id > 0)
            {
                QSqlQuery existing(m_database);
                existing.prepare(QStringLiteral("SELECT column_one,column_two,column_three,type FROM hospital_information WHERE id=?")); existing.addBindValue(row.id);
                if (!existing.exec() || !existing.next() || existing.value(3).toString() != type)
                { message = QStringLiteral("原医院字典行已不存在或类别变化，未覆盖其他记录。"); return false; }
                if (existing.value(0).toString() == row.columnOne && existing.value(1).toString() == row.columnTwo && existing.value(2).toString() == row.columnThree) continue;
            }
            QSqlQuery query(m_database);
            query.prepare(row.id == 0 ? QStringLiteral("INSERT INTO hospital_information(column_one,column_two,column_three,type) VALUES(?,?,?,?)") :
                QStringLiteral("UPDATE hospital_information SET column_one=?,column_two=?,column_three=? WHERE type=? AND id=?"));
            query.addBindValue(row.columnOne);
            query.addBindValue(row.columnTwo.isNull() ? QStringLiteral("") : row.columnTwo);
            query.addBindValue(row.columnThree.isNull() ? QStringLiteral("") : row.columnThree);
            query.addBindValue(type);
            if (row.id > 0) query.addBindValue(row.id);
            if (!query.exec() || query.numRowsAffected() != 1) { message = QStringLiteral("原医院字典保存失败。"); return false; }
        }
        return readSnapshot(type, &previousName, &savedRows, &message);
    }();
    if (!written) { m_database.rollback(); emit error(operation, message); return; }
    if (nameChanged)
    {
        QByteArray stillCurrent;
        if (!readHospitalIniBytes(m_iniPath, &stillCurrent, &message) || stillCurrent != originalIni || !publishHospitalIniBytes(m_iniPath, changedIni, &message))
        { m_database.rollback(); emit error(operation, message.isEmpty() ? QStringLiteral("保存前配置再次变化，未覆盖。") : message); return; }
    }
    if (!m_database.commit())
    {
        const bool rolledBack = m_database.rollback();
        bool restored = true;
        if (nameChanged)
        {
            QByteArray current;
            restored = readHospitalIniBytes(m_iniPath, &current, &message) && current == changedIni && publishHospitalIniBytes(m_iniPath, originalIni, &message);
        }
        if (!rolledBack || !restored) m_ready = false;
        emit error(operation, rolledBack && restored ? QStringLiteral("原医院保存提交失败，字典和名称已回退，请重试。") :
            QStringLiteral("原医院保存失败且回退未得到完整确认，已停止写入；请关闭后重新打开核对。"));
        return;
    }
    if (nameChanged)
    {
        m_originalHospitalName = hospitalName;
        m_originalIniFingerprint = hospitalIniFingerprint(changedIni);
    }
    emit saved(type, m_originalHospitalName, savedRows);
}
