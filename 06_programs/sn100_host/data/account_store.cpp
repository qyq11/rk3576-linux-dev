#include "account_store.h"

#include <QCoreApplication>
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
const int kBusyTimeoutMs = 200;
const int kLineEditLimit = 32767;
const QByteArray kOfflineTag("SN100_OFFLINE_O01_V1\n");

bool validLineEditText(const QString &text)
{
    // The original slots check emptiness separately and accept Intermediate
    // username-validator input. Do not invent a new 16-character policy here.
    return text.size() <= kLineEditLimit && !text.contains(QChar::Null);
}

QString loginDatabaseFailure(const QString &action, const QSqlError &error)
{
    // 不把驱动的SQL/参数文本转给界面，避免认证输入进入日志或证据。
    bool numeric = false;
    const int code = error.nativeErrorCode().toInt(&numeric);
    return numeric ? action + QStringLiteral("（SQLite代码%1）").arg(code) : action;
}

bool prepareAccountPath(QString directory, QString *databasePath,
                        bool *existingDatabase, QString *message)
{
    if (!QDir::isAbsolutePath(directory))
    {
        *message = QStringLiteral("账户实验必须显式指定绝对数据目录");
        return false;
    }
    directory = QDir::cleanPath(directory);
    QString ancestor = directory;
    QStringList missingParts;
    while (!QFileInfo::exists(ancestor))
    {
        const QFileInfo info(ancestor);
        const QString parent = info.absolutePath();
        if (parent == ancestor || info.isSymLink())
        {
            *message = QStringLiteral("无法解析账户数据目录的真实父路径");
            return false;
        }
        missingParts.prepend(info.fileName());
        ancestor = parent;
    }
    const QFileInfo ancestorInfo(ancestor);
    if (!ancestorInfo.isDir() || ancestorInfo.canonicalFilePath().isEmpty())
    {
        *message = QStringLiteral("账户数据父路径不是可解析的目录");
        return false;
    }
    QString canonical = ancestorInfo.canonicalFilePath();
    for (const QString &part : missingParts)
    {
        canonical = QDir(canonical).filePath(part);
    }
    if (QDir::fromNativeSeparators(canonical).split(QLatin1Char('/'), Qt::SkipEmptyParts)
            .contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive))
    {
        *message = QStringLiteral("禁止在只读原参考工程目录中创建或打开账户实验数据");
        return false;
    }

    *databasePath = QDir(directory).filePath(QStringLiteral("user_Information.db"));
    const QFileInfo databaseInfo(*databasePath);
    const QString tagPath = QDir(directory).filePath(QStringLiteral("offline-data.tag"));
    const QFileInfo tagInfo(tagPath);
    if (databaseInfo.isSymLink() || tagInfo.isSymLink())
    {
        *message = QStringLiteral("账户数据库和实验目录标记不能使用符号链接");
        return false;
    }
    *existingDatabase = databaseInfo.exists();
    if (*existingDatabase && !databaseInfo.isFile())
    {
        *message = QStringLiteral("账户数据库路径不是普通文件");
        return false;
    }
    if (tagInfo.exists())
    {
        QFile tag(tagPath);
        if (!tagInfo.isFile() || tagInfo.size() != kOfflineTag.size()
                || !tag.open(QIODevice::ReadOnly) || tag.readAll() != kOfflineTag
                || tag.error() != QFile::NoError)
        {
            *message = QStringLiteral("offline-data.tag 无效；未打开或修改已有账户数据库");
            return false;
        }
        return true;
    }
    // An unmarked existing O01 database/configuration is also not ours to adopt.
    if (*existingDatabase || QFileInfo::exists(QDir(directory).filePath(QStringLiteral("hospital_Information.db")))
            || QFileInfo::exists(QDir(directory).filePath(QStringLiteral("config.ini"))))
    {
        *message = QStringLiteral("已有数据缺少有效 offline-data.tag，拒绝作为账户实验目录；未修改原文件");
        return false;
    }
    if (!QDir().mkpath(directory))
    {
        *message = QStringLiteral("无法创建账户实验数据目录");
        return false;
    }
    QSaveFile tag(tagPath);
    tag.setDirectWriteFallback(false);
    if (!tag.open(QIODevice::WriteOnly) || tag.write(kOfflineTag) != kOfflineTag.size()
            || !tag.commit())
    {
        *message = QStringLiteral("无法创建账户实验目录标记: ") + tag.errorString();
        return false;
    }
    return true;
}

bool verifyAccountSchema(QSqlDatabase &database, QString *message)
{
    const QStringList names = {QStringLiteral("id"), QStringLiteral("name"),
                              QStringLiteral("role"), QStringLiteral("pwd"),
                              QStringLiteral("isdefault")};
    const QStringList types = {QStringLiteral("INTEGER"), QStringLiteral("VARCHAR"),
                              QStringLiteral("INTEGER"), QStringLiteral("VARCHAR"),
                              QStringLiteral("INTEGER")};
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(user_information)")))
    {
        *message = QStringLiteral("读取账户表结构失败: ") + query.lastError().text();
        return false;
    }
    int index = 0;
    while (query.next())
    {
        if (index >= names.size() || query.value(1).toString() != names.at(index)
                || query.value(2).toString().toUpper() != types.at(index)
                || query.value(5).toInt() != (index == 0 ? 1 : 0))
        {
            *message = QStringLiteral("账户表结构不匹配；不迁移或转换已有账户数据");
            return false;
        }
        ++index;
    }
    if (index != names.size())
    {
        *message = QStringLiteral("账户表缺失或字段不完整；不导入已有账户数据");
        return false;
    }
    return true;
}

AccountRows anonymousAccounts()
{
    // Public fields mirror tests/fixtures/accounts.json. The bootstrap secret
    // is supplied only at runtime and is not a source/fixture/default password.
    const QStringList names = {QStringLiteral("offlineSend"), QStringLiteral("offlineTest"),
                               QStringLiteral("offlineCheck"), QStringLiteral("offlineEngineer"),
                               QStringLiteral("offlineAdmin")};
    AccountRows rows;
    for (int index = 0; index < names.size(); ++index)
    {
        AccountRow row;
        row.name = names.at(index);
        row.role = index + 1;
        rows.append(row);
    }
    return rows;
}
}

AccountStore::AccountStore(QObject *parent) : QObject(parent)
{
    registerTypes();
}

AccountStore::~AccountStore()
{
    Q_ASSERT(!m_database.isValid() || QThread::currentThread() == thread());
    releaseDatabase();
}

void AccountStore::registerTypes()
{
    qRegisterMetaType<AccountRow>("AccountRow");
    qRegisterMetaType<AccountRows>("AccountRows");
    qRegisterMetaType<AccountSession>("AccountSession");
    qRegisterMetaType<AccountSessionSource>("AccountSessionSource");
}

QString AccountStore::roleName(int role)
{
    // ID 0 has no source-verified role-table row. Match getRoleById()'s
    // no-match result rather than inventing a visible Guest translation.
    const QStringList names = {QString(), QStringLiteral("送检医师"),
                               QStringLiteral("检验医师"), QStringLiteral("审核医师"),
                               QStringLiteral("工程师"), QStringLiteral("管理员")};
    return role >= 0 && role < names.size() ? names.at(role) : QString();
}

bool AccountStore::checkThread(const QString &operation)
{
    if (QThread::currentThread() != thread())
    {
        emit error(operation, QStringLiteral("账户数据请求必须排队发送至所属工作线程"));
        return false;
    }
    return true;
}

bool AccountStore::checkReady(const QString &operation)
{
    if (!checkThread(operation))
    {
        return false;
    }
    if (!m_ready || !m_database.isOpen())
    {
        emit error(operation, QStringLiteral("账户实验数据库尚未初始化或已经关闭"));
        return false;
    }
    return true;
}

bool AccountStore::beginWrite(QString *message)
{
    // Reserve the writer before reading identity/target rows, avoiding a
    // deferred read-to-write upgrade that can bypass SQLite's busy timeout.
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE")))
    {
        *message = QStringLiteral("开始账户事务失败: ") + query.lastError().text();
        return false;
    }
    return true;
}

void AccountStore::initialize(QString absoluteDataDir, QString bootstrapPassword)
{
    const QString operation = QStringLiteral("initialize");
    if (!checkThread(operation))
    {
        return;
    }
    if (QCoreApplication::instance()
            && QThread::currentThread() == QCoreApplication::instance()->thread())
    {
        emit error(operation, QStringLiteral("请将 AccountStore 移入工作线程后再初始化"));
        return;
    }
    if (m_ready)
    {
        emit error(operation, QStringLiteral("账户数据库已经初始化；请先关闭再打开"));
        return;
    }
    if (!validLineEditText(bootstrapPassword))
    {
        emit error(operation, QStringLiteral("临时实验密码超出输入范围或含空字符"));
        return;
    }
    QString message;
    QString databasePath;
    bool existed = false;
    if (!prepareAccountPath(absoluteDataDir, &databasePath, &existed, &message))
    {
        emit error(operation, message);
        return;
    }
    if ((existed && !bootstrapPassword.isEmpty()) || (!existed && bootstrapPassword.isEmpty()))
    {
        emit error(operation, existed
            ? QStringLiteral("已有账户实验库禁止重新播种或重置密码，请不带初始密码打开")
            : QStringLiteral("首次新建账户实验库需要显式传入临时实验密码"));
        return;
    }

    m_connectionName = QStringLiteral("sn100-account-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(databasePath);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(kBusyTimeoutMs));
    const bool opened = [&]()
    {
        if (!m_database.open())
        {
            message = QStringLiteral("打开账户实验库失败: ") + m_database.lastError().text();
            return false;
        }
        if (existed && !verifyAccountSchema(m_database, &message))
        {
            return false;
        }
        QSqlQuery query(m_database);
        const QStringList pragmas = {QStringLiteral("PRAGMA busy_timeout=%1").arg(kBusyTimeoutMs),
                                     QStringLiteral("PRAGMA journal_mode=DELETE"),
                                     QStringLiteral("PRAGMA synchronous=FULL")};
        for (const QString &pragma : pragmas)
        {
            if (!query.exec(pragma))
            {
                message = QStringLiteral("配置账户数据库持久性失败: ") + query.lastError().text();
                return false;
            }
            query.finish();
        }
        if (existed)
        {
            return true;
        }
        if (!beginWrite(&message))
        {
            return false;
        }
        if (!query.exec(QStringLiteral("CREATE TABLE user_information ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,name VARCHAR,role INTEGER,"
                "pwd VARCHAR,isdefault INTEGER)")))
        {
            message = QStringLiteral("创建匿名账户表失败: ") + query.lastError().text();
            m_database.rollback();
            return false;
        }
        query.prepare(QStringLiteral("INSERT INTO user_information(name,role,pwd,isdefault) "
                                     "VALUES(:name,:role,:pwd,:isdefault)"));
        for (const AccountRow &row : anonymousAccounts())
        {
            query.bindValue(QStringLiteral(":name"), row.name);
            query.bindValue(QStringLiteral(":role"), row.role);
            query.bindValue(QStringLiteral(":pwd"), bootstrapPassword);
            query.bindValue(QStringLiteral(":isdefault"), row.isDefault);
            if (!query.exec())
            {
                message = QStringLiteral("建立匿名账户夹具失败: ") + query.lastError().text();
                m_database.rollback();
                return false;
            }
            query.finish();
        }
        if (!m_database.commit())
        {
            message = QStringLiteral("提交匿名账户夹具失败: ") + m_database.lastError().text();
            m_database.rollback();
            return false;
        }
        return true;
    }();
    bootstrapPassword.clear();
    if (!opened)
    {
        releaseDatabase();
        emit error(operation, message);
        return;
    }
    m_ready = true;
    clearSession();
    emit ready();
}

void AccountStore::clearSession()
{
    const bool actionReady = m_session.actionReady;
    m_session = AccountSession();
    m_session.actionReady = actionReady;
    m_sessionId = 0;
    emit sessionChanged(m_session);
}

void AccountStore::setSession(QString name, bool actionReady)
{
    const QString operation = QStringLiteral("session");
    if (!checkReady(operation))
    {
        return;
    }
    m_session.actionReady = actionReady;
    if (name.isEmpty())
    {
        clearSession();
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id,role FROM user_information WHERE name=:name ORDER BY id LIMIT 1"));
    query.bindValue(QStringLiteral(":name"), name);
    if (!query.exec())
    {
        clearSession();
        emit error(operation, QStringLiteral("读取匿名会话身份失败: ") + query.lastError().text());
        return;
    }
    if (!query.next())
    {
        clearSession();
        emit error(operation, QStringLiteral("所选匿名账户不存在，请重新选择"));
        return;
    }
    const int role = query.value(1).toInt();
    if (role < 0 || role > 5)
    {
        clearSession();
        emit error(operation, QStringLiteral("数据库中的账户角色不受支持，不能建立模拟会话"));
        return;
    }
    m_sessionId = query.value(0).toLongLong();
    m_session.name = name;
    m_session.role = role;
    m_session.authenticated = true;
    m_session.source = AccountSessionSource::Simulated;
    emit sessionChanged(m_session);
}

bool AccountStore::loginAvailable(QString *message) const
{
    if (QThread::currentThread() != thread())
    {
        *message = QStringLiteral("登录请求必须排队发送至账户所属工作线程");
        return false;
    }
    if (!m_ready || !m_database.isOpen())
    {
        *message = QStringLiteral("账户数据库尚未初始化或已经关闭");
        return false;
    }
    return true;
}

void AccountStore::loadLoginDefault(quint64 requestId)
{
    QString message;
    const QString guest = QStringLiteral("Guest");
    if (!loginAvailable(&message))
    {
        emit defaultLoginLoaded(requestId, guest, message);
        return;
    }
    QSqlQuery query(m_database);
    // 原界面只预填一条默认用户名；这里与现账户快照一样固定按id顺序。
    if (!query.exec(QStringLiteral("SELECT name FROM user_information "
                                   "WHERE isdefault=1 ORDER BY id LIMIT 1")))
    {
        emit defaultLoginLoaded(requestId, guest,
            loginDatabaseFailure(QStringLiteral("默认用户名读取失败，请稍后重试"), query.lastError()));
        return;
    }
    const QString name = query.next() ? query.value(0).toString() : guest;
    if (query.lastError().isValid())
    {
        emit defaultLoginLoaded(requestId, guest,
            loginDatabaseFailure(QStringLiteral("默认用户名读取未完成"), query.lastError()));
        return;
    }
    emit defaultLoginLoaded(requestId, name, QString());
}

bool AccountStore::saveDefaultLoginName(const QString &name, QString *message)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE")))
    {
        *message = loginDatabaseFailure(QStringLiteral("默认账户保存失败，原默认标记未改变"), query.lastError());
        return false;
    }
    bool saved = query.exec(QStringLiteral("UPDATE user_information SET isdefault=0"));
    if (saved)
    {
        // 原规则按用户名更新全部同名记录，不擅自把旧重名账户规则改成唯一键。
        query.prepare(QStringLiteral("UPDATE user_information SET isdefault=1 WHERE name=:name"));
        query.bindValue(QStringLiteral(":name"), name);
        saved = query.exec() && query.numRowsAffected() > 0;
    }
    if (!saved)
    {
        *message = loginDatabaseFailure(QStringLiteral("默认账户保存失败，原默认标记已回滚"), query.lastError());
        m_database.rollback();
        return false;
    }
    if (!m_database.commit())
    {
        *message = loginDatabaseFailure(QStringLiteral("默认账户提交失败，原默认标记已回滚"), m_database.lastError());
        m_database.rollback();
        return false;
    }
    return true;
}

void AccountStore::authenticate(quint64 requestId, QString name, QString password, bool remember)
{
    QString message;
    if (!loginAvailable(&message))
    {
        password.clear();
        emit loginFinished(requestId, false, false, message, QString());
        return;
    }
    if (password.isEmpty())
    {
        // 原登录界面空密码始终回落Guest，忽略所填用户名和默认账户勾选。
        clearSession();
        emit loginFinished(requestId, true, false, QString(), QString());
        return;
    }
    if (!validLineEditText(name) || !validLineEditText(password))
    {
        password.clear();
        emit loginFinished(requestId, false, false, QStringLiteral("登录输入超出范围或含空字符"), QString());
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id,name,role FROM user_information "
                                 "WHERE name=:name AND pwd=:password LIMIT 2"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":password"), password);
    const bool executed = query.exec();
    password.clear();
    if (!executed || !query.next())
    {
        message = query.lastError().isValid()
            ? loginDatabaseFailure(QStringLiteral("读取登录账户失败，请稍后重试"), query.lastError())
            : QStringLiteral("用户名或密码错误!");
        emit loginFinished(requestId, false, false, message, QString());
        return;
    }
    bool validId = false;
    bool validRole = false;
    const qint64 id = query.value(0).toLongLong(&validId);
    const QString accountName = query.value(1).toString();
    const int role = query.value(2).toInt(&validRole);
    if (query.next())
    {
        // 原查询未规定多条相同凭据的角色选择；不任意取得一个高权限身份。
        emit loginFinished(requestId, false, false,
            QStringLiteral("相同登录凭据对应多条账户，请先处理账户歧义"), QString());
        return;
    }
    if (query.lastError().isValid())
    {
        emit loginFinished(requestId, false, false,
            loginDatabaseFailure(QStringLiteral("登录账户读取未完成"), query.lastError()), QString());
        return;
    }
    query.finish();
    if (!validId || id <= 0 || !validRole || role < 0 || role > 5)
    {
        emit loginFinished(requestId, false, false, QStringLiteral("账户角色或标识无效，不能登录"), QString());
        return;
    }
    QString warning;
    const bool defaultSaved = remember && role <= 4 && saveDefaultLoginName(accountName, &warning);
    m_sessionId = id;
    m_session.name = accountName;
    m_session.role = role;
    m_session.authenticated = true;
    m_session.source = AccountSessionSource::Password;
    // 登录只改变身份，不改变设备动作状态；默认账户写失败也不伪装为认证失败。
    emit sessionChanged(m_session);
    emit loginFinished(requestId, true, defaultSaved, QString(), warning);
}

bool AccountStore::refreshSession(QString *message)
{
    if (!m_session.authenticated || m_sessionId <= 0)
    {
        *message = QStringLiteral("当前没有有效匿名会话，请先选择实验账户");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT name,role FROM user_information WHERE id=:id"));
    query.bindValue(QStringLiteral(":id"), m_sessionId);
    if (!query.exec())
    {
        *message = QStringLiteral("核对当前账户角色失败: ") + query.lastError().text();
        return false;
    }
    if (!query.next() || query.value(0).toString() != m_session.name)
    {
        clearSession();
        *message = QStringLiteral("当前匿名会话身份已经失效，请重新选择实验账户");
        return false;
    }
    const int role = query.value(1).toInt();
    if (role < 0 || role > 5)
    {
        clearSession();
        *message = QStringLiteral("当前账户角色无效，已清除匿名会话");
        return false;
    }
    if (m_session.role != role)
    {
        m_session.role = role;
        emit sessionChanged(m_session);
    }
    return true;
}

void AccountStore::load()
{
    const QString operation = QStringLiteral("load");
    if (!checkReady(operation))
    {
        return;
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT id,name,role,isdefault FROM user_information ORDER BY id")))
    {
        emit error(operation, QStringLiteral("读取账户列表失败: ") + query.lastError().text());
        return;
    }
    AccountRows rows;
    while (query.next())
    {
        AccountRow row;
        row.id = query.value(0).toLongLong();
        row.name = query.value(1).toString();
        row.role = query.value(2).toInt();
        row.isDefault = query.value(3).toInt();
        rows.append(row);
    }
    if (query.lastError().isValid())
    {
        emit error(operation, QStringLiteral("账户列表读取未完成: ") + query.lastError().text());
        return;
    }
    emit loaded(rows);
}

bool AccountStore::readAccount(const QString &name, AccountRecord *record, bool *found,
                               QString *message)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id,name,role,pwd,isdefault FROM user_information "
                                 "WHERE name=:name ORDER BY id LIMIT 1"));
    query.bindValue(QStringLiteral(":name"), name);
    if (!query.exec())
    {
        *message = QStringLiteral("读取目标账户失败: ") + query.lastError().text();
        return false;
    }
    *found = query.next();
    if (!*found)
    {
        if (query.lastError().isValid())
        {
            *message = QStringLiteral("目标账户读取未完成: ") + query.lastError().text();
            return false;
        }
        return true;
    }
    record->row.id = query.value(0).toLongLong();
    record->row.name = query.value(1).toString();
    record->row.role = query.value(2).toInt();
    record->password = query.value(3).toString();
    record->row.isDefault = query.value(4).toInt();
    return true;
}

bool AccountStore::allowedRole(int role, bool update) const
{
    // Original combos contain IDs 1..min(currentRole,4). For Guest/Admin,
    // the update combo has an empty selection, whose original lookup is ID 0.
    if (update && role == 0 && (m_session.role == 0 || m_session.role == 5))
    {
        return true;
    }
    return role >= 1 && role <= 4 && role <= m_session.role;
}

bool AccountStore::commitWrite(const QString &operation, QString *message)
{
    if (!m_database.commit())
    {
        *message = QStringLiteral("账户%1事务提交失败: ").arg(operation) + m_database.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

void AccountStore::add(QString name, QString password, QString again, int role)
{
    const QString operation = QStringLiteral("add");
    if (!checkReady(operation))
    {
        return;
    }
    QString message;
    if (!beginWrite(&message))
    {
        emit error(operation, message);
        return;
    }
    const bool written = [&]()
    {
        if (!refreshSession(&message))
        {
            return false;
        }
        if (m_session.role == 0)
        {
            message = QStringLiteral("增加失败: 无权限!");
            return false;
        }
        if (name.isEmpty() || again.isEmpty())
        {
            message = QStringLiteral("增加失败: 用户名或密码不能为空!");
            return false;
        }
        if (password != again)
        {
            message = QStringLiteral("增加失败: 密码不一致,请重新输入!");
            return false;
        }
        if (!allowedRole(role, false))
        {
            message = QStringLiteral("增加失败: 所选角色超出当前用户可用选项，不能添加管理员!");
            return false;
        }
        if (!validLineEditText(name) || !validLineEditText(password))
        {
            message = QStringLiteral("账户输入超出原输入框范围或含空字符");
            return false;
        }
        AccountRecord existing;
        bool found = false;
        if (!readAccount(name, &existing, &found, &message))
        {
            return false;
        }
        if (found)
        {
            message = QStringLiteral("用户名已存在!");
            return false;
        }
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("INSERT INTO user_information(name,role,pwd,isdefault) "
                                     "VALUES(:name,:role,:pwd,0)"));
        query.bindValue(QStringLiteral(":name"), name);
        query.bindValue(QStringLiteral(":role"), role);
        query.bindValue(QStringLiteral(":pwd"), password);
        if (!query.exec() || query.numRowsAffected() != 1)
        {
            message = QStringLiteral("保存失败: ") + query.lastError().text();
            return false;
        }
        return true;
    }();
    password.clear();
    again.clear();
    if (!written)
    {
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    if (!commitWrite(operation, &message))
    {
        emit error(operation, message);
        return;
    }
    emit succeeded(operation, false);
}

void AccountStore::remove(QString name, QString password)
{
    const QString operation = QStringLiteral("remove");
    if (!checkReady(operation))
    {
        return;
    }
    QString message;
    if (!beginWrite(&message))
    {
        emit error(operation, message);
        return;
    }
    bool loggedOut = false;
    const bool written = [&]()
    {
        if (!refreshSession(&message))
        {
            return false;
        }
        if (name == m_session.name && !m_session.actionReady)
        {
            message = QStringLiteral("设备正在运行，不允许删除正在登陆账户!");
            return false;
        }
        AccountRecord target;
        bool found = false;
        if (!readAccount(name, &target, &found, &message))
        {
            return false;
        }
        if (!found)
        {
            message = QStringLiteral("删除失败：用户名不存在!");
            return false;
        }
        if (target.password != password)
        {
            message = QStringLiteral("删除失败：用户名或密码错误!");
            return false;
        }
        if (m_session.role < target.row.role)
        {
            message = QStringLiteral("删除失败：当前用户权限低于需要删除用户权限!");
            return false;
        }
        if (target.row.role == 5)
        {
            message = QStringLiteral("删除失败：超级用户不允许删除!");
            return false;
        }
        QSqlQuery query(m_database);
        // Preserve deleteUserInfoByName semantics, including all matching rows
        // if the original update path has produced duplicate account names.
        query.prepare(QStringLiteral("DELETE FROM user_information WHERE name=:name"));
        query.bindValue(QStringLiteral(":name"), name);
        if (!query.exec() || query.numRowsAffected() < 1)
        {
            message = QStringLiteral("删除失败: ") + query.lastError().text();
            return false;
        }
        loggedOut = name == m_session.name;
        return true;
    }();
    password.clear();
    if (!written)
    {
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    if (!commitWrite(operation, &message))
    {
        emit error(operation, message);
        return;
    }
    if (loggedOut)
    {
        clearSession();
    }
    emit succeeded(operation, loggedOut);
}

void AccountStore::update(QString oldName, QString oldPassword, QString newName,
                          QString password, QString again, int role)
{
    const QString operation = QStringLiteral("update");
    if (!checkReady(operation))
    {
        return;
    }
    QString message;
    if (!beginWrite(&message))
    {
        emit error(operation, message);
        return;
    }
    const bool written = [&]()
    {
        if (!refreshSession(&message))
        {
            return false;
        }
        if (!allowedRole(role, true))
        {
            message = QStringLiteral("更新失败: 所选角色不属于当前页面可用选项!");
            return false;
        }
        const int selectedIndex = role == 0 ? -1 : role - 1;
        // Preserve the original equality condition, despite its opposite
        // comment: running + SAME role/index is rejected; changed role may pass.
        if (selectedIndex == m_session.role - 1 && !m_session.actionReady)
        {
            message = QStringLiteral("设备正在运行，不允许修改用户类型!");
            return false;
        }
        AccountRecord target;
        bool found = false;
        if (!readAccount(oldName, &target, &found, &message))
        {
            return false;
        }
        if (!found)
        {
            message = QStringLiteral("更新失败: 用户名不存在!");
            return false;
        }
        if (target.password != oldPassword)
        {
            message = QStringLiteral("更新失败: 用户名或密码错误!");
            return false;
        }
        if (password != again)
        {
            message = QStringLiteral("更新失败: 密码不一致,请重新输入!");
            return false;
        }
        if (newName.isEmpty())
        {
            message = QStringLiteral("更新失败: 用户名或密码不能为空!");
            return false;
        }
        if (!validLineEditText(newName) || !validLineEditText(password))
        {
            message = QStringLiteral("账户输入超出原输入框范围或含空字符");
            return false;
        }
        // The reference permits empty new passwords, duplicate new names and
        // a double-click-selected different target. Do not silently repair them.
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("UPDATE user_information SET name=:name,role=:role,"
                                     "pwd=:pwd,isdefault=:isdefault WHERE id=:id"));
        query.bindValue(QStringLiteral(":name"), newName);
        query.bindValue(QStringLiteral(":role"), role);
        query.bindValue(QStringLiteral(":pwd"), password);
        query.bindValue(QStringLiteral(":isdefault"), target.row.isDefault);
        query.bindValue(QStringLiteral(":id"), target.row.id);
        if (!query.exec() || query.numRowsAffected() != 1)
        {
            message = QStringLiteral("更新失败: ") + query.lastError().text();
            return false;
        }
        return true;
    }();
    oldPassword.clear();
    password.clear();
    again.clear();
    if (!written)
    {
        m_database.rollback();
        emit error(operation, message);
        return;
    }
    if (!commitWrite(operation, &message))
    {
        emit error(operation, message);
        return;
    }
    clearSession(); // Original update succeeds -> signalLoginOut, for any target.
    emit succeeded(operation, true);
}

void AccountStore::releaseDatabase()
{
    m_ready = false;
    m_session = AccountSession();
    m_sessionId = 0;
    if (m_database.isValid())
    {
        m_database.close();
    }
    m_database = QSqlDatabase();
    if (!m_connectionName.isEmpty())
    {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_connectionName.clear();
}

void AccountStore::close()
{
    if (!checkThread(QStringLiteral("close")))
    {
        return;
    }
    releaseDatabase();
    emit closed();
}
