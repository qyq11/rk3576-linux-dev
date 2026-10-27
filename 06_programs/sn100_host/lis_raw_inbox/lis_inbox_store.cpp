#include "lis_inbox_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QVariant>

namespace
{
const int applicationId = 0x534e4c49;
const QString schema = QStringLiteral("sn100-lis-raw-inbox-v1");
const QString markerName = QStringLiteral(".sn100-lis-raw-inbox-v1");
const QByteArray markerBytes("SN100 LIS RAW INBOX v1\n");
const QString databaseName = QStringLiteral("lis_inbox.sqlite");

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
bool localIdentity(const QString &identity)
{
    const QUuid value(identity);
    return !value.isNull() && value.toString(QUuid::WithoutBraces) == identity;
}
bool execute(QSqlDatabase database, const QString &sql, QString *error)
{
    QSqlQuery query(database);
    if (query.exec(sql)) return true;
    *error = query.lastError().text();
    return false;
}
bool prepared(QSqlQuery *query, const QString &sql, const QVariantList &values, QString *error)
{
    if (!query->prepare(sql)) { *error = query->lastError().text(); return false; }
    for (const auto &value : values)
        query->addBindValue(value.type() == QVariant::String && value.isNull() ? QVariant(QStringLiteral("")) : value);
    if (query->exec()) return true;
    *error = query->lastError().text();
    return false;
}
bool scalar(QSqlDatabase database, const QString &sql, QVariant *value, QString *error)
{
    QSqlQuery query(database);
    if (!query.exec(sql) || !query.next()) { *error = query.lastError().text(); return false; }
    *value = query.value(0);
    if (query.next() || query.lastError().isValid()) { *error = QStringLiteral("单值SQL读回异常。"); return false; }
    return true;
}
class Transaction
{
public:
    explicit Transaction(QSqlDatabase database) : database_(database) {}
    ~Transaction() { rollback(); }
    bool begin(bool write, QString *error)
    {
        active_ = execute(database_, write ? QStringLiteral("BEGIN IMMEDIATE") : QStringLiteral("BEGIN"), error);
        return active_;
    }
    bool commit(QString *error)
    {
        if (!database_.commit()) { *error = database_.lastError().text(); return false; }
        active_ = false;
        return true;
    }
    bool rollback(QString *error = nullptr)
    {
        if (!active_) return true;
        if (!database_.rollback())
        {
            if (error) *error += QStringLiteral("；事务回滚未获确认：") + database_.lastError().text();
            return false;
        }
        active_ = false;
        return true;
    }
private:
    QSqlDatabase database_;
    bool active_ = false;
};
QMap<QString, QString> tables()
{
    return {
        {QStringLiteral("inbox_metadata"), QStringLiteral("CREATE TABLE inbox_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)")},
        {QStringLiteral("inbox_message"), QStringLiteral("CREATE TABLE inbox_message(id INTEGER PRIMARY KEY,reception_identity TEXT NOT NULL UNIQUE,"
            "profile TEXT NOT NULL,control_id TEXT NOT NULL,raw_frame BLOB NOT NULL,message_type TEXT NOT NULL,version_text TEXT NOT NULL,"
            "parse_status TEXT NOT NULL,received_at TEXT NOT NULL,segment_count INTEGER NOT NULL CHECK(segment_count BETWEEN 5 AND 64))")},
        {QStringLiteral("inbox_segment"), QStringLiteral("CREATE TABLE inbox_segment(message_id INTEGER NOT NULL REFERENCES inbox_message(id) ON DELETE RESTRICT,"
            "segment_index INTEGER NOT NULL CHECK(segment_index BETWEEN 0 AND 63),name TEXT NOT NULL,raw_text TEXT NOT NULL,"
            "field_count INTEGER NOT NULL CHECK(field_count BETWEEN 1 AND 128),PRIMARY KEY(message_id,segment_index))")},
        {QStringLiteral("inbox_field"), QStringLiteral("CREATE TABLE inbox_field(message_id INTEGER NOT NULL,segment_index INTEGER NOT NULL,"
            "field_index INTEGER NOT NULL CHECK(field_index BETWEEN 0 AND 127),raw_text TEXT NOT NULL,PRIMARY KEY(message_id,segment_index,field_index),"
            "FOREIGN KEY(message_id,segment_index) REFERENCES inbox_segment(message_id,segment_index) ON DELETE RESTRICT)")}
    };
}

bool validateSchemaObjects(QSqlDatabase database, QString *failure)
{
    const auto expectedTables = tables();
    // createSchema has no explicit user indexes, views or triggers. The four
    // declared PK/UNIQUE constraints each produce one SQLite automatic index.
    const QMap<QString, QStringList> automaticKeys{
        {QStringLiteral("inbox_metadata"), {QStringLiteral("key")}},
        {QStringLiteral("inbox_message"), {QStringLiteral("reception_identity")}},
        {QStringLiteral("inbox_segment"), {QStringLiteral("message_id"), QStringLiteral("segment_index")}},
        {QStringLiteral("inbox_field"), {QStringLiteral("message_id"), QStringLiteral("segment_index"), QStringLiteral("field_index")}}
    };
    const QMap<QString, QString> statisticsTables{
        {QStringLiteral("sqlite_stat1"), QStringLiteral("CREATE TABLE sqlite_stat1(tbl,idx,stat)")},
        {QStringLiteral("sqlite_stat2"), QStringLiteral("CREATE TABLE sqlite_stat2(tbl,idx,sampleno,sample)")},
        {QStringLiteral("sqlite_stat3"), QStringLiteral("CREATE TABLE sqlite_stat3(tbl,idx,neq,nlt,ndlt,sample)")},
        {QStringLiteral("sqlite_stat4"), QStringLiteral("CREATE TABLE sqlite_stat4(tbl,idx,neq,nlt,ndlt,sample)")}
    };
    QMap<QString, QString> actualTables, automaticNames;
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT type,name,tbl_name,sql FROM sqlite_master ORDER BY type,name")))
    { *failure = query.lastError().text(); return false; }
    while (query.next())
    {
        const QString type = query.value(0).toString(), name = query.value(1).toString();
        const QString owner = query.value(2).toString(), sql = query.value(3).toString();
        if (type == QStringLiteral("table") && expectedTables.contains(name))
        {
            if (owner != name || sql != expectedTables.value(name))
            { *failure = QStringLiteral("收件箱表结构不匹配。"); return false; }
            actualTables.insert(name, sql);
        }
        else if (type == QStringLiteral("index") && query.value(3).isNull()
                 && automaticKeys.contains(owner) && !automaticNames.contains(owner)
                 && QRegularExpression(QStringLiteral("^sqlite_autoindex_") + QRegularExpression::escape(owner)
                                       + QStringLiteral("_[1-9][0-9]*$")).match(name).hasMatch())
        {
            automaticNames.insert(owner, name); // Verify actual origin/columns below.
        }
        else if (type == QStringLiteral("table") && owner == name && statisticsTables.contains(name)
                 && !query.value(3).isNull() && sql == statisticsTables.value(name))
        {
            // Reserved SQLite ANALYZE tables are engine metadata, not user schema.
        }
        else
        {
            *failure = QStringLiteral("收件箱含未声明的schema对象：%1 %2").arg(type, name);
            return false;
        }
    }
    if (query.lastError().isValid()) { *failure = query.lastError().text(); return false; }
    query.finish();
    if (actualTables != expectedTables || automaticNames.size() != automaticKeys.size())
    { *failure = QStringLiteral("收件箱预期schema对象缺失。"); return false; }
    for (auto it = automaticKeys.cbegin(); it != automaticKeys.cend(); ++it)
    {
        const QString indexName = automaticNames.value(it.key());
        const QString expectedOrigin = it.key() == QStringLiteral("inbox_message") ? QStringLiteral("u") : QStringLiteral("pk");
        if (!query.exec(QStringLiteral("PRAGMA index_list(\"") + it.key() + QStringLiteral("\")")))
        { *failure = query.lastError().text(); return false; }
        int count = 0;
        while (query.next())
        {
            ++count;
            if (query.value(1).toString() != indexName || query.value(2).toInt() != 1
                    || query.value(3).toString() != expectedOrigin || query.value(4).toInt() != 0)
            { *failure = QStringLiteral("收件箱索引不是声明约束产生的自动索引。"); return false; }
        }
        if (query.lastError().isValid() || count != 1)
        { *failure = QStringLiteral("收件箱自动索引集合不匹配。"); return false; }
        query.finish();
        if (!query.exec(QStringLiteral("PRAGMA index_info(\"") + indexName + QStringLiteral("\")")))
        { *failure = query.lastError().text(); return false; }
        QStringList columns;
        while (query.next())
        {
            if (query.value(0).toInt() != columns.size() || query.value(1).toInt() < 0)
            { *failure = QStringLiteral("收件箱自动索引列序号无效。"); return false; }
            columns.append(query.value(2).toString());
        }
        if (query.lastError().isValid() || columns != it.value())
        { *failure = QStringLiteral("收件箱自动索引约束列不匹配。"); return false; }
        query.finish();
    }
    return true;
}
}

LisInboxStore::LisInboxStore(QObject *parent) : QObject(parent) {}
LisInboxStore::~LisInboxStore() { release(); }
void LisInboxStore::registerTypes()
{
    qRegisterMetaType<AccountSession>();
    qRegisterMetaType<LisDsrRaw::Message>();
    qRegisterMetaType<LisDsrRaw::Summary>();
    qRegisterMetaType<LisDsrRaw::Summaries>();
}
bool LisInboxStore::checkThread(qint64 request, const QString &operation)
{
    if (QThread::currentThread() == thread()) return true;
    emit error(request, operation, QStringLiteral("收件箱只能在所属数据库工作线程调用。"));
    return false;
}
bool LisInboxStore::checkReady(qint64 request, const QString &operation)
{
    if (!checkThread(request, operation)) return false;
    if (!ready_ || !database_.isOpen())
    { emit error(request, operation, QStringLiteral("收件箱尚未就绪或已停止。")); return false; }
    if (symlinkComponent(directory_) || QFileInfo(directory_).canonicalFilePath() != directory_
            || QFileInfo(QDir(directory_).filePath(databaseName)).isSymLink())
    { ready_ = false; emit error(request, operation, QStringLiteral("隔离目录身份已变化，停止访问并保留原库。")); return false; }
    return true;
}
bool LisInboxStore::canIngest(qint64 request, const QString &operation)
{
    if (!checkReady(request, operation)) return false;
    if (session_.authenticated && session_.role >= 1 && session_.role <= 5
            && session_.source != AccountSessionSource::Guest) return true;
    emit error(request, operation, QStringLiteral("原始收件需要显式已认证的1至5角色；Guest只读。"));
    return false;
}
void LisInboxStore::setSession(AccountSession session)
{
    if (checkThread(0, QStringLiteral("setSession"))) session_ = session;
}
void LisInboxStore::initialize(QString directory)
{
    const QString operation = QStringLiteral("initialize");
    if (!checkThread(0, operation)) return;
    if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
    { emit error(0, operation, QStringLiteral("收件箱初始化必须在数据库工作线程。")); return; }
    release();
    // 正式本地合同：目录由调用方显式给出，父目录已存在且规范；只创建本库叶目录。
    // 不依据源码位置猜路径，不迁移私有候选库，不清除标记、UUID或未知数据。
    const QString supplied = QDir::fromNativeSeparators(directory);
    directory = QDir::cleanPath(supplied);
    const QFileInfo requested(directory);
    const QString parent = requested.dir().canonicalPath();
    if (supplied.contains(QChar('\0')) || supplied != directory || !QDir::isAbsolutePath(directory)
            || QDir(directory).isRoot() || parent.isEmpty() || !QFileInfo(parent).isDir()
            || requested.dir().absolutePath() != parent
            || (QFileInfo::exists(directory) && (!requested.isDir() || requested.canonicalFilePath() != directory))
            || directory.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
            || symlinkComponent(directory))
    { emit error(0, operation, QStringLiteral("收件目录必须是显式绝对规范的非根目录，现有父目录须规范且无符号链接；不使用参考工程目录。")); return; }
    QDir folder(directory);
    QString message;
    if (!folder.exists() && !QDir().mkpath(directory))
    { emit error(0, operation, QStringLiteral("不能创建隔离收件目录。")); return; }
    QLockFile lock(folder.filePath(QStringLiteral(".lis-inbox-write.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { emit error(0, operation, QStringLiteral("另一写者正在使用此收件箱。")); return; }
    const QString marker = folder.filePath(markerName);
    const bool marked = QFileInfo::exists(marker);
    if (marked)
    {
        QFile file(marker);
        if (!QFileInfo(marker).isFile() || QFileInfo(marker).isSymLink() || !file.open(QIODevice::ReadOnly)
                || file.size() != markerBytes.size() || file.readAll() != markerBytes)
        { emit error(0, operation, QStringLiteral("收件箱标记不匹配；不打开未知库。")); return; }
    }
    else
    {
        // The write lock is ours; all other pre-existing content is unknown.
        QStringList entries = folder.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        entries.removeAll(QStringLiteral(".lis-inbox-write.lock"));
        if (!entries.isEmpty()) { emit error(0, operation, QStringLiteral("非空未标记目录拒绝初始化，原内容保留。")); return; }
        QSaveFile file(marker);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(markerBytes) != markerBytes.size() || !file.commit())
        { emit error(0, operation, QStringLiteral("收件箱标记写入失败。")); return; }
    }
    const QString path = folder.filePath(databaseName);
    const bool existing = QFileInfo::exists(path);
    if (marked && !existing)
    { emit error(0, operation, QStringLiteral("既有标记对应的数据库缺失，不自动重建空库。")); return; }
    if (QFileInfo(path).isSymLink() || (existing && !QFileInfo(path).isFile()) || symlinkComponent(folder.filePath(QStringLiteral("inputs"))))
    { emit error(0, operation, QStringLiteral("收件箱文件/输入目录类型不安全。")); return; }
    if (existing)
    {
        QFile header(path);
        if (!header.open(QIODevice::ReadOnly) || header.read(16) != QByteArray("SQLite format 3\0", 16))
        { emit error(0, operation, QStringLiteral("既有文件不是SQLite收件箱，不覆盖。")); return; }
    }
    directory_ = QFileInfo(directory).canonicalFilePath();
    connection_ = QStringLiteral("lis-raw-inbox-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_);
    database_.setDatabaseName(path);
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=200"));
    bool okay = database_.open();
    if (!okay) message = database_.lastError().text();
    if (okay) okay = execute(database_, QStringLiteral("PRAGMA foreign_keys=ON"), &message);
    if (okay) okay = existing ? validateSchema(&message) : createSchema(&message);
    if (okay) okay = folder.mkpath(QStringLiteral("inputs"));
    if (!okay)
    {
        if (message.isEmpty()) message = QStringLiteral("输入目录准备失败。");
        release(); emit error(0, operation, message); return;
    }
    ready_ = true;
    emit ready(path);
}
bool LisInboxStore::createSchema(QString *message)
{
    Transaction transaction(database_);
    if (!transaction.begin(true, message)) return false;
    if (!execute(database_, QStringLiteral("PRAGMA application_id=%1").arg(applicationId), message)
            || !execute(database_, QStringLiteral("PRAGMA user_version=1"), message)) return false;
    for (const auto &sql : tables()) if (!execute(database_, sql, message)) return false;
    // This UUID identifies only the local database, never an MSH control ID.
    storeId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QMap<QString, QString> metadata{{"schema", schema}, {"store_id", storeId_}, {"directory", directory_}};
    QSqlQuery query(database_);
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it)
        if (!prepared(&query, QStringLiteral("INSERT INTO inbox_metadata VALUES(?,?)"), {it.key(), it.value()}, message)) return false;
    return transaction.commit(message);
}
bool LisInboxStore::validateSchema(QString *message)
{
    QVariant value;
    if (!scalar(database_, QStringLiteral("PRAGMA application_id"), &value, message) || value.toInt() != applicationId
            || !scalar(database_, QStringLiteral("PRAGMA user_version"), &value, message) || value.toInt() != 1)
    { *message = QStringLiteral("既有库不是本profile/版本，不迁移或覆盖。"); return false; }
    if (!validateSchemaObjects(database_, message)) return false;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT key,value FROM inbox_metadata"))) { *message = query.lastError().text(); return false; }
    QMap<QString, QString> metadata;
    while (query.next()) metadata.insert(query.value(0).toString(), query.value(1).toString());
    if (query.lastError().isValid() || metadata.size() != 3 || metadata.value("schema") != schema
            || metadata.value("directory") != directory_ || !localIdentity(metadata.value("store_id")))
    { *message = QStringLiteral("收件箱身份/路径不符；复制库不会自动重新绑定。"); return false; }
    storeId_ = metadata.value("store_id");
    query.finish();
    if (!scalar(database_, QStringLiteral("PRAGMA quick_check"), &value, message) || value.toString() != QStringLiteral("ok"))
    { *message = QStringLiteral("收件箱完整性核验失败。"); return false; }
    if (!query.exec(QStringLiteral("PRAGMA foreign_key_check")) || query.next() || query.lastError().isValid())
    { *message = QStringLiteral("收件箱字段关联核验失败。"); return false; }
    query.finish();
    if (!scalar(database_, QStringLiteral("SELECT COUNT(*) FROM inbox_message"), &value, message)
            || value.toLongLong() > LisDsrRaw::MaximumMessages)
    { *message = QStringLiteral("收件箱记录数超过本地预算。"); return false; }
    if (!scalar(database_, QStringLiteral("SELECT COUNT(*) FROM inbox_message WHERE typeof(raw_frame)<>'blob' OR length(raw_frame)>1048576 "
                                        "OR length(raw_frame)<4 OR profile<>'source-derived-utf8-dsr-raw-v1' "
                                        "OR parse_status<>'raw-unmapped-not-applied'"), &value, message) || value.toLongLong() != 0)
    { *message = QStringLiteral("收件箱既有原帧/profile/状态超出本地合同。"); return false; }
    return true;
}
bool LisInboxStore::readMessage(qint64 id, LisDsrRaw::Message *message, QString *failure)
{
    QSqlQuery query(database_);
    if (!prepared(&query, QStringLiteral("SELECT reception_identity,profile,control_id,raw_frame,message_type,version_text,parse_status,received_at,segment_count "
                                        "FROM inbox_message WHERE id=?"), {id}, failure)) return false;
    if (!query.next()) { *failure = QStringLiteral("收件记录不存在。"); return false; }
    LisDsrRaw::Message actual;
    actual.id = id; actual.receptionIdentity = query.value(0).toString(); actual.profile = query.value(1).toString();
    actual.controlId = query.value(2).toString(); actual.frame = query.value(3).toByteArray(); actual.messageType = query.value(4).toString();
    actual.version = query.value(5).toString(); actual.status = query.value(6).toString();
    actual.receivedAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODateWithMs);
    const int segmentCount = query.value(8).toInt();
    if (query.lastError().isValid() || !localIdentity(actual.receptionIdentity) || !actual.receivedAt.isValid()
            || segmentCount < 5 || segmentCount > LisDsrRaw::MaximumSegments)
    { *failure = QStringLiteral("持久收件身份/字段计数无效。"); return false; }
    query.finish();
    if (!prepared(&query, QStringLiteral("SELECT segment_index,name,raw_text,field_count FROM inbox_segment WHERE message_id=? ORDER BY segment_index"), {id}, failure)) return false;
    while (query.next())
    {
        if (query.value(0).toInt() != actual.segments.size() || actual.segments.size() >= segmentCount)
        { *failure = QStringLiteral("持久段序号不连续。"); return false; }
        LisDsrRaw::Segment segment;
        segment.name = query.value(1).toString(); segment.rawText = query.value(2).toString();
        const int count = query.value(3).toInt();
        if (count < 1 || count > LisDsrRaw::MaximumFields) { *failure = QStringLiteral("持久字段数无效。"); return false; }
        QSqlQuery fields(database_);
        if (!prepared(&fields, QStringLiteral("SELECT field_index,raw_text FROM inbox_field WHERE message_id=? AND segment_index=? ORDER BY field_index"),
                      {id, actual.segments.size()}, failure)) return false;
        while (fields.next())
        {
            if (fields.value(0).toInt() != segment.fields.size() || segment.fields.size() >= count)
            { *failure = QStringLiteral("持久字段序号不连续。"); return false; }
            segment.fields.append(fields.value(1).toString());
        }
        if (fields.lastError().isValid() || segment.fields.size() != count) { *failure = QStringLiteral("持久字段读回失败。"); return false; }
        actual.segments.append(segment);
    }
    if (query.lastError().isValid() || actual.segments.size() != segmentCount) { *failure = QStringLiteral("持久段读回失败。"); return false; }
    actual.sourceFields = LisDsrRaw::sourceFields(actual.segments);
    LisDsrRaw::Message decoded;
    if (!LisDsrRaw::decode(actual.profile, actual.controlId, actual.frame, &decoded, failure)) return false;
    if (!LisDsrRaw::samePayload(actual, decoded)) { *failure = QStringLiteral("SQL原帧、字段与解析状态不一致，不伪造读回成功。"); return false; }
    *message = actual;
    return true;
}
void LisInboxStore::ingestBytes(qint64 request, QString identity, QString profile, QString expected, QByteArray frame)
{
    saveFrame(request, QStringLiteral("ingestBytes"), identity, profile, expected, frame);
}
void LisInboxStore::ingestFile(qint64 request, QString identity, QString profile, QString expected, QString path)
{
    const QString operation = QStringLiteral("ingestFile");
    if (!canIngest(request, operation)) return;
    path = QDir::cleanPath(path);
    const QFileInfo info(path);
    const QString inputRoot = QDir(directory_).filePath(QStringLiteral("inputs")) + QChar('/');
    if (!QDir::isAbsolutePath(path) || !info.isFile() || symlinkComponent(path)
            || info.canonicalFilePath() != path || !path.startsWith(inputRoot))
    { emit error(request, operation, QStringLiteral("只读取本收件箱inputs下显式、非符号链接的匿名整帧文件。")); return; }
    QFile input(path);
    if (info.size() > LisDsrRaw::MaximumFrameBytes || !input.open(QIODevice::ReadOnly))
    { emit error(request, operation, QStringLiteral("整帧文件超出1MiB或不能读取。")); return; }
    const QByteArray frame = input.read(LisDsrRaw::MaximumFrameBytes + 1);
    if (input.error() != QFile::NoError || !input.atEnd() || frame.size() > LisDsrRaw::MaximumFrameBytes)
    { emit error(request, operation, QStringLiteral("整帧文件读取失败或增长超限，不截断。")); return; }
    saveFrame(request, operation, identity, profile, expected, frame);
}
void LisInboxStore::saveFrame(qint64 request, const QString &operation, const QString &identity,
                            const QString &profile, const QString &expected, const QByteArray &frame)
{
    if (!canIngest(request, operation)) return;
    if (!localIdentity(identity))
    { emit error(request, operation, QStringLiteral("必须显式给非零、规范小写的本地收件UUID；它不替换MSH-10。")); return; }
    QLockFile lock(QDir(directory_).filePath(QStringLiteral(".lis-inbox-write.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { emit error(request, operation, QStringLiteral("另一个写者正在保存收件。")); return; }
    Transaction transaction(database_);
    QString failure;
    auto fail = [&]()
    {
        if (!transaction.rollback(&failure)) ready_ = false;
        emit error(request, operation, failure);
    };
    if (!transaction.begin(true, &failure)) { fail(); return; }
    QSqlQuery query(database_);
    if (!prepared(&query, QStringLiteral("SELECT id FROM inbox_message WHERE reception_identity=?"), {identity}, &failure)) { fail(); return; }
    const bool exists = query.next();
    const qint64 existingId = exists ? query.value(0).toLongLong() : 0;
    if (query.lastError().isValid()) { failure = query.lastError().text(); fail(); return; }
    query.finish();
    if (exists)
    {
        LisDsrRaw::Message actual;
        if (!readMessage(existingId, &actual, &failure)) { fail(); return; }
        if (actual.profile != profile || actual.controlId != expected || actual.frame != frame)
        { failure = QStringLiteral("本地收件身份冲突：profile、控制号或原帧不同，既有记录保留。"); fail(); return; }
        if (!transaction.commit(&failure)) { fail(); return; }
        emit ingested(request, actual, true);
        return;
    }
    LisDsrRaw::Message decoded;
    if (!LisDsrRaw::decode(profile, expected, frame, &decoded, &failure)) { fail(); return; }
    QVariant count;
    if (!scalar(database_, QStringLiteral("SELECT COUNT(*) FROM inbox_message"), &count, &failure)) { fail(); return; }
    if (count.toLongLong() >= LisDsrRaw::MaximumMessages)
    { failure = QStringLiteral("Inbox limit is 1024; existing records are retained."); fail(); return; }
    decoded.receptionIdentity = identity;
    decoded.receivedAt = QDateTime::currentDateTimeUtc();
    if (!prepared(&query, QStringLiteral("INSERT INTO inbox_message(reception_identity,profile,control_id,raw_frame,message_type,version_text,parse_status,received_at,segment_count) "
                                       "VALUES(?,?,?,?,?,?,?,?,?)"),
            {identity, profile, decoded.controlId, frame, decoded.messageType, decoded.version, decoded.status,
             decoded.receivedAt.toString(Qt::ISODateWithMs), decoded.segments.size()}, &failure)) { fail(); return; }
    const qint64 id = query.lastInsertId().toLongLong();
    if (id <= 0) { failure = QStringLiteral("没有实际SQL收件ID。"); fail(); return; }
    for (int index = 0; index < decoded.segments.size(); ++index)
    {
        const auto &segment = decoded.segments.at(index);
        if (!prepared(&query, QStringLiteral("INSERT INTO inbox_segment VALUES(?,?,?,?,?)"), {id, index, segment.name, segment.rawText, segment.fields.size()}, &failure)) { fail(); return; }
        for (int field = 0; field < segment.fields.size(); ++field)
            if (!prepared(&query, QStringLiteral("INSERT INTO inbox_field VALUES(?,?,?,?)"), {id, index, field, segment.fields.at(field)}, &failure)) { fail(); return; }
    }
    LisDsrRaw::Message actual;
    if (!readMessage(id, &actual, &failure)) { fail(); return; }
    if (actual.receptionIdentity != identity || !LisDsrRaw::samePayload(actual, decoded))
    { failure = QStringLiteral("实际SQL读回与输入不同，整单回滚。"); fail(); return; }
    if (!transaction.commit(&failure)) { fail(); return; }
    emit ingested(request, actual, false);
}
void LisInboxStore::query(qint64 request, int offset, int limit)
{
    const QString operation = QStringLiteral("query");
    if (!checkReady(request, operation)) return;
    if (offset < 0 || offset > LisDsrRaw::MaximumMessages || limit < 1 || limit > 100)
    { emit error(request, operation, QStringLiteral("分页需要非负有界offset及1至100条limit。")); return; }
    QString failure;
    Transaction transaction(database_);
    if (!transaction.begin(false, &failure)) { emit error(request, operation, failure); return; }
    QVariant total;
    if (!scalar(database_, QStringLiteral("SELECT COUNT(*) FROM inbox_message"), &total, &failure))
    { emit error(request, operation, failure); return; }
    QSqlQuery query(database_);
    if (!prepared(&query, QStringLiteral("SELECT id,reception_identity,profile,control_id,message_type,parse_status,received_at,length(raw_frame) "
                                       "FROM inbox_message ORDER BY id LIMIT ? OFFSET ?"), {limit, offset}, &failure))
    { emit error(request, operation, failure); return; }
    LisDsrRaw::Summaries rows;
    while (query.next())
    {
        LisDsrRaw::Summary row;
        row.id = query.value(0).toLongLong(); row.receptionIdentity = query.value(1).toString(); row.profile = query.value(2).toString();
        row.controlId = query.value(3).toString(); row.messageType = query.value(4).toString(); row.status = query.value(5).toString();
        row.receivedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs); row.frameBytes = query.value(7).toInt();
        if (!localIdentity(row.receptionIdentity) || row.profile != LisDsrRaw::profileName() || row.status != LisDsrRaw::parseStatus()
                || row.frameBytes < 4 || row.frameBytes > LisDsrRaw::MaximumFrameBytes || !row.receivedAt.isValid())
        { emit error(request, operation, QStringLiteral("列表中的持久元数据不符合收件箱合同。")); return; }
        rows.append(row);
    }
    if (query.lastError().isValid()) { emit error(request, operation, query.lastError().text()); return; }
    query.finish();
    if (!transaction.commit(&failure)) { emit error(request, operation, failure); return; }
    emit listReady(request, rows, total.toLongLong());
}
void LisInboxStore::detail(qint64 request, qint64 id)
{
    const QString operation = QStringLiteral("detail");
    if (!checkReady(request, operation)) return;
    if (id <= 0) { emit error(request, operation, QStringLiteral("需要实际正收件ID。")); return; }
    QString failure;
    Transaction transaction(database_);
    LisDsrRaw::Message actual;
    if (!transaction.begin(false, &failure) || !readMessage(id, &actual, &failure) || !transaction.commit(&failure))
    { emit error(request, operation, failure); return; }
    emit detailReady(request, actual);
}
void LisInboxStore::release()
{
    ready_ = false;
    if (database_.isValid()) database_.close();
    database_ = QSqlDatabase();
    if (!connection_.isEmpty()) QSqlDatabase::removeDatabase(connection_);
    connection_.clear(); directory_.clear(); storeId_.clear();
}
void LisInboxStore::close()
{
    if (!checkThread(0, QStringLiteral("close"))) return;
    release(); session_ = AccountSession();
    emit closed();
}
