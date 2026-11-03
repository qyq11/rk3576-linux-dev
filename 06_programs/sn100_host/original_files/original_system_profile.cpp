#include "original_system_profile.h"
#include "original_business_database.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextCodec>
#include <QRegularExpression>

namespace OriginalRuntime
{
namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
bool integer(const QVariant &value, qint64 minimum, qint64 maximum, qint64 *result)
{
    bool ok = false;
    const QString text = value.toString();
    const qint64 parsed = text.toLongLong(&ok);
    if (!ok || parsed < minimum || parsed > maximum) return false;
    *result = parsed;
    return true;
}
bool boolean(const QVariant &value, bool *result)
{
    if (value.toString() != "0" && value.toString() != "1") return false;
    *result = value.toString() == "1";
    return true;
}
bool digest(const QString &path, QString *result, QString *message)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return fail(message, QStringLiteral("无法读取原系统资料指纹。"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input)) return fail(message, QStringLiteral("系统资料指纹读取未完成。"));
    const QByteArray content = hash.result();
    // 同字节的另一工作根也不是同一编辑上下文，旧页面不能跨根提交。
    *result = QString::fromLatin1(QCryptographicHash::hash(QFileInfo(path).canonicalFilePath().toUtf8() + '\n' + content,
                                                         QCryptographicHash::Sha256).toHex());
    return true;
}
bool loadChannels(const QString &root, Channels *result, QString *message)
{
    OriginalBusiness::Database database;
    if (!database.open(root, QStringLiteral("db/systemInfo/channel.db"), false, message)) return false;
    const QStringList tables{"test_channel", "warm_channel", "cache_channel"};
    for (int index = 0; index < tables.size(); ++index)
    {
        QStringList fields{"id", "channel_num", "is_open"};
        if (index == 0) fields << "warmup_only";
        OriginalBusiness::Rows rows;
        if (!database.rows(tables[index], fields, &rows, message)) return false;
        QSet<QString> numbers;
        QVector<Channel> channels;
        for (const auto &row : rows)
        {
            Channel channel;
            if (!integer(row.value("id"), 1, 1000000000, &channel.id) ||
                !boolean(row.value("is_open"), &channel.enabled) ||
                (index == 0 && !boolean(row.value("warmup_only"), &channel.warmupOnly)))
                return fail(message, QStringLiteral("原通道编号或开关值无效，未猜测修正。"));
            channel.number = row.value("channel_num").toString();
            if (channel.number.isEmpty() || numbers.contains(channel.number))
                return fail(message, QStringLiteral("原通道名称为空或重复。"));
            numbers.insert(channel.number);
            channels.append(channel);
        }
        if (index == 0) result->test = channels;
        else if (index == 1) result->warm = channels;
        else result->cache = channels;
    }
    return digest(QDir(root).filePath("db/systemInfo/channel.db"), &result->fingerprint, message);
}
bool loadChecks(const QString &root, QVector<ResultCheckRule> *result, QString *message)
{
    OriginalBusiness::Database database;
    OriginalBusiness::Rows rows;
    if (!database.open(root, "db/systemInfo/result_Check.db", false, message) ||
        !database.rows("result_check", {"id", "describe", "type", "result_message_cn", "result_message_en", "bit_index"},
                       &rows, message)) return false;
    QSet<qint64> identifiers;
    for (const auto &row : rows)
    {
        ResultCheckRule rule;
        qint64 bit = 0;
        if (!integer(row.value("id"), 1, 1000000000, &rule.id) || identifiers.contains(rule.id) ||
            !integer(row.value("bit_index"), 0, 4095, &bit))
            return fail(message, QStringLiteral("原结果标记编号或位序无效。"));
        identifiers.insert(rule.id);
        rule.bitIndex = int(bit);
        rule.description = row.value("describe").toString();
        rule.type = row.value("type").toString();
        rule.chineseMessage = row.value("result_message_cn").toString();
        rule.englishMessage = row.value("result_message_en").toString();
        result->append(rule);
    }
    return true;
}
bool csvFields(const QString &line, QStringList *fields)
{
    QString current;
    bool quoted = false;
    for (int index = 0; index < line.size(); ++index)
    {
        const QChar ch = line[index];
        if (ch == '"')
        {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') { current += '"'; ++index; }
            else quoted = !quoted;
        }
        else if (ch == ',' && !quoted) { fields->append(current.trimmed()); current.clear(); }
        else current += ch;
    }
    if (quoted) return false;
    fields->append(current.trimmed());
    return true;
}
bool loadAlarms(const QString &root, QVector<AlarmRule> *result, QString *message)
{
    const QString path = QDir(root).filePath("db/systemInfo/abnormal_Config.csv");
    for (QFileInfo entry(path); ; entry.setFile(entry.absolutePath()))
    {
        if (entry.isSymLink()) return fail(message, QStringLiteral("异常规则路径不能经过符号链接。"));
        if (entry.absoluteFilePath() == entry.absolutePath()) break;
    }
    QFile input(path);
    if (QFileInfo(path).isSymLink() || !input.open(QIODevice::ReadOnly) || input.size() > 4 * 1024 * 1024)
        return fail(message, QStringLiteral("原异常规则文件无法读取或超过4MB。"));
    const QByteArray bytes = input.readAll();
    QTextCodec::ConverterState utf8;
    QString text = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &utf8);
    if (utf8.invalidChars)
    {
        // 参考CSV确认为Windows中文编码；严格往返校验，不能使用RK默认UTF-8产生乱码。
        QTextCodec *codec = QTextCodec::codecForName("GB18030");
        QTextCodec::ConverterState legacy;
        text = codec->toUnicode(bytes.constData(), bytes.size(), &legacy);
        if (legacy.invalidChars || codec->fromUnicode(text) != bytes)
            return fail(message, QStringLiteral("原异常规则中文编码无法无损读取。"));
    }
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    const QStringList lines = text.split('\n');
    if (lines.isEmpty() || !lines.first().trimmed().startsWith("id,error_code,describe_cn,measure,"))
        return fail(message, QStringLiteral("原异常规则表头不匹配。"));
    for (int index = 1; index < lines.size(); ++index)
    {
        if (lines[index].trimmed().isEmpty()) continue;
        QStringList fields;
        if (!csvFields(lines[index].trimmed(), &fields) || fields.size() < 9)
            return fail(message, QStringLiteral("原异常规则第%1行字段不完整，未静默丢弃。").arg(index + 1));
        AlarmRule rule;
        rule.originalColumns = fields;
        rule.id = fields[0]; rule.code = fields[1]; rule.description = fields[2]; rule.measure = fields[3];
        rule.idleAction = fields[4]; rule.testingAction = fields[5]; rule.testStatus = fields[6]; rule.parameter = fields[8];
        const QString enabled = fields[7].toLower();
        if ((enabled != "true" && enabled != "false") || rule.code.isEmpty())
            return fail(message, QStringLiteral("原异常规则启用值或错误码无效。"));
        rule.enabled = enabled == "true";
        result->append(rule);
    }
    return true;
}
}

bool UserDictionary::allows(const QString &powerId, int roleId) const
{
    return roles.contains(roleId) && powers.contains(powerId) && powers.value(powerId).roleIds.contains(roleId);
}
QStringList UserDictionary::grantedPowers(int roleId) const
{
    QStringList result;
    for (auto it = powers.cbegin(); it != powers.cend(); ++it) if (allows(it.key(), roleId)) result << it.key();
    return result;
}
bool loadUserDictionary(const QString &root, UserDictionary *result, QString *message)
{
    if (!result) return fail(message, QStringLiteral("角色字典输出为空。"));
    UserDictionary candidate;
    OriginalBusiness::Database database;
    OriginalBusiness::Rows roles, powers;
    if (!database.open(root, "db/systemInfo/user.db", false, message) ||
        !database.rows("user_role", {"id", "role"}, &roles, message) ||
        !database.rows("user_powers", {"id", "describe", "can_role_id"}, &powers, message)) return false;
    for (const auto &row : roles)
    {
        qint64 id = 0;
        if (!integer(row.value("id"), 0, 1000000000, &id) || candidate.roles.contains(int(id)))
            return fail(message, QStringLiteral("原角色编号无效或重复。"));
        candidate.roles.insert(int(id), row.value("role").toString());
    }
    for (const auto &row : powers)
    {
        const QString id = row.value("id").toString();
        qint64 numericId = 0;
        if (!integer(row.value("id"), 1, 1000000000, &numericId) || candidate.powers.contains(id))
            return fail(message, QStringLiteral("原权限编号无效或重复。"));
        PermissionRule rule;
        rule.description = row.value("describe").toString();
        for (const QString &role : row.value("can_role_id").toString().split(';', Qt::SkipEmptyParts))
        {
            qint64 roleId = 0;
            if (!integer(role, 0, 1000000000, &roleId) || !candidate.roles.contains(int(roleId)))
                return fail(message, QStringLiteral("原权限引用了不存在的角色。"));
            rule.roleIds.insert(int(roleId));
        }
        candidate.powers.insert(id, rule);
    }
    if (candidate.roles.isEmpty()) return fail(message, QStringLiteral("原角色字典为空。"));
    *result = candidate;
    return true;
}
QStringList Channels::activeTestNumbers(bool includeWarmupOnly) const
{
    QStringList result;
    for (const auto &channel : test)
        if (channel.enabled && (includeWarmupOnly || !channel.warmupOnly)) result << channel.number;
    return result;
}
QStringList SystemProfile::messagesForBits(const QVector<bool> &bits, bool english) const
{
    QStringList result;
    for (const auto &rule : resultChecks)
        if (rule.bitIndex < bits.size() && bits[rule.bitIndex]) result << (english ? rule.englishMessage : rule.chineseMessage);
    return result;
}
bool SystemProfile::mayStartWithAlarms(const QStringList &codes, QString *reason) const
{
    // 对应原MaskDlgAbnormal::canStartTest，按测试过程动作规则阻止启动，不发动作。
    for (const QString &code : codes)
        for (const auto &rule : alarms)
            if (rule.code == code && rule.testingAction != "warning" && rule.testingAction != "none" && !rule.testingAction.isEmpty())
                return fail(reason, QStringLiteral("当前异常%1阻止开始：%2").arg(code, rule.description));
    if (reason) reason->clear();
    return true;
}
bool loadSystemProfile(const QString &root, SystemProfile *result, QString *message)
{
    if (!result || !OriginalBusiness::workingRoot(root, message)) return false;
    SystemProfile candidate;
    if (!loadChannels(root, &candidate.channels, message) || !loadUserDictionary(root, &candidate.users, message) ||
        !loadChecks(root, &candidate.resultChecks, message) || !loadAlarms(root, &candidate.alarms, message)) return false;
    *result = candidate;
    return true;
}
bool setTestChannelEnabled(const QString &root, const QString &number, bool enabled,
                           const QString &expectedFingerprint, QString *message)
{
    if (!OriginalBusiness::workingRoot(root, message)) return false;
    const QString path = QDir(root).filePath("db/systemInfo/channel.db");
    QLockFile lock(path + ".sn100-write.lock");
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0)) return fail(message, QStringLiteral("原通道资料正在写入，请稍后重读。"));
    QString actual;
    if (!digest(path, &actual, message) || expectedFingerprint.size() != 64 || actual != expectedFingerprint)
        return fail(message, QStringLiteral("原通道资料已变化，请重读后修改。"));
    OriginalBusiness::Database database;
    OriginalBusiness::Rows rows;
    if (!database.open(root, "db/systemInfo/channel.db", true, message) ||
        !database.rows("test_channel", {"id", "channel_num", "is_open"}, &rows, message)) return false;
    int count = 0;
    bool old = false;
    for (const auto &row : rows) if (row.value("channel_num").toString() == number)
    {
        ++count;
        if (!boolean(row.value("is_open"), &old)) return fail(message, QStringLiteral("原通道开关无效。"));
    }
    if (count != 1) return fail(message, QStringLiteral("通道不存在或名称重复，未修改。"));
    if (old == enabled) return true;
    if (!database.begin(message)) return false;
    QString again;
    if (!digest(path, &again, message) || again != actual ||
        !database.execute("UPDATE test_channel SET is_open=? WHERE channel_num=? AND is_open=?",
                          {enabled ? 1 : 0, number, old ? 1 : 0}, message, 1) || !database.commit(message))
    { database.rollback(); return false; }
    return true;
}

void ChannelCommitPermit::revoke() noexcept
{
    state_.exchange(State::Revoked, std::memory_order_acq_rel);
}

bool ChannelCommitPermit::tryBeginCommit() noexcept
{
    State expected = State::Active;
    return state_.compare_exchange_strong(expected, State::Committing,
                                         std::memory_order_acq_rel, std::memory_order_acquire);
}

ChannelCommitPermit::State ChannelCommitPermit::state() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

void ChannelCommitPermit::markCommitted() noexcept
{
    // 撤销已发生时保留Revoked，供页面拒收旧结果；实际提交事实由返回值单独保存。
    State expected = State::Committing;
    state_.compare_exchange_strong(expected, State::Committed,
                                  std::memory_order_acq_rel, std::memory_order_acquire);
}

namespace
{
constexpr qint64 MaximumChannelDatabaseBytes = 16 * 1024 * 1024;
constexpr int MaximumChannelSnapshotRows = 1024;
constexpr int MaximumChannelReadbackUpdates = 1024;

bool channelFileReady(const QString &path, QString *message)
{
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isFile() || info.size() < 100 || info.size() > MaximumChannelDatabaseBytes)
        return fail(message, QStringLiteral("通道库不存在、格式不完整或超过本次16MiB本地预算。"));
    for (QFileInfo current(info.absoluteFilePath()); ; current.setFile(current.absolutePath()))
    {
        if (current.isSymLink()) return fail(message, QStringLiteral("通道库路径不能经过符号链接。"));
        if (current.absoluteFilePath() == current.absolutePath()) break;
    }
    // 文件hash只代表主库，不能拿它覆盖WAL内容或未经确认的遗留事务。
    for (const QString &suffix : {QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")})
    {
        const QFileInfo sidecar(path + suffix);
        if (sidecar.exists() || sidecar.isSymLink())
            return fail(message, QStringLiteral("通道库存在WAL或回滚日志文件，暂不支持本次回读保存；未清理或转换原库。"));
    }
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return fail(message, QStringLiteral("无法读取通道库格式。"));
    const QByteArray header = input.read(100);
    if (header.size() != 100 || header.left(16) != QByteArray("SQLite format 3\0", 16) ||
        header.at(18) != char(1) || header.at(19) != char(1))
        return fail(message, QStringLiteral("通道回读只支持原SQLite回滚日志格式，未启用WAL或转换数据库。"));
    return true;
}

bool deleteJournalMode(OriginalBusiness::Database &database, QString *message)
{
    QSqlQuery query(database.db);
    if (!query.exec(QStringLiteral("PRAGMA journal_mode")) || !query.next())
        return fail(message, QStringLiteral("无法核对通道库日志模式：") + query.lastError().text());
    const QString mode = query.value(0).toString();
    if (mode.compare(QStringLiteral("delete"), Qt::CaseInsensitive) != 0)
        return fail(message, QStringLiteral("通道回读仅支持DELETE日志模式，未改写原模式：") + mode);
    return true;
}

bool boundedChannelDigest(const QString &path, QString *result, QString *message)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return fail(message, QStringLiteral("无法读取通道资料指纹。"));
    const qint64 size = input.size();
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    if (size < 100 || size > MaximumChannelDatabaseBytes || canonicalPath.isEmpty())
        return fail(message, QStringLiteral("通道库指纹输入超过16MiB本地预算或路径无效。"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 readBytes = 0;
    while (readBytes < size)
    {
        const QByteArray block = input.read(qMin(qint64(65536), size - readBytes));
        if (block.isEmpty()) return fail(message, QStringLiteral("通道资料指纹读取未完成。"));
        hash.addData(block);
        readBytes += block.size();
    }
    if (!input.atEnd() || input.error() != QFileDevice::NoError || input.size() != size ||
        QFileInfo(path).canonicalFilePath() != canonicalPath)
        return fail(message, QStringLiteral("读取期间通道资料大小或路径已变化，未采用不一致指纹。"));
    *result = QString::fromLatin1(QCryptographicHash::hash(canonicalPath.toUtf8() + '\n' + hash.result(),
                                                         QCryptographicHash::Sha256).toHex());
    return true;
}

bool boundedChannelRows(OriginalBusiness::Database &database, Channels *result, QString *message)
{
    Channels candidate;
    const QStringList tables{QStringLiteral("test_channel"), QStringLiteral("warm_channel"), QStringLiteral("cache_channel")};
    for (int table = 0; table < tables.size(); ++table)
    {
        // 只读原普通表；不执行视图或虚拟表的自定义读取/更新逻辑。
        QSqlQuery schema(database.db);
        if (!schema.prepare(QStringLiteral("SELECT type,sql FROM sqlite_master WHERE name=? COLLATE NOCASE")))
            return fail(message, schema.lastError().text());
        schema.addBindValue(tables.at(table));
        if (!schema.exec() || !schema.next() || schema.value(0).toString() != QStringLiteral("table") ||
            !schema.value(1).toString().simplified().startsWith(QStringLiteral("CREATE TABLE "), Qt::CaseInsensitive))
            return fail(message, QStringLiteral("通道资料缺少支持的原普通表：") + tables.at(table));
        // 原建表无COLLATE，文字键按BINARY匹配；历史自定义比较规则暂不猜测兼容。
        if (table == 0 && schema.value(1).toString().contains(QRegularExpression(
                QStringLiteral("\\bCOLLATE\\b"),QRegularExpression::CaseInsensitiveOption)))
            return fail(message, QStringLiteral("检测通道表含COLLATE声明，本次回读暂不支持；未改比较规则。"));
        schema.finish();

        QSqlQuery query(database.db);
        query.setForwardOnly(true);
        const QString fields = table == 0 ? QStringLiteral("id,channel_num,is_open,warmup_only")
                                         : QStringLiteral("id,channel_num,is_open");
        if (!query.exec(QStringLiteral("SELECT %1 FROM %2 ORDER BY id LIMIT 1025").arg(fields, tables.at(table))))
            return fail(message, QStringLiteral("无法读取原通道字段：") + query.lastError().text());
        QSet<QString> names;
        QVector<Channel> channels;
        while (query.next())
        {
            if (channels.size() >= MaximumChannelSnapshotRows)
                return fail(message, QStringLiteral("原通道表超过1024行本地预算，未截断或发布部分资料。"));
            Channel channel;
            if (!integer(query.value(0), 1, 1000000000, &channel.id) ||
                !boolean(query.value(2), &channel.enabled) ||
                (table == 0 && !boolean(query.value(3), &channel.warmupOnly)))
                return fail(message, QStringLiteral("原通道编号、开关或仅预热值无效，未猜测修正。"));
            channel.number = query.value(1).toString();
            if (channel.number.isEmpty() || names.contains(channel.number))
                return fail(message, QStringLiteral("原通道名称为空或重复，未应用回读。"));
            names.insert(channel.number);
            channels.append(channel);
        }
        if (query.lastError().isValid()) return fail(message, query.lastError().text());
        if (table == 0) candidate.test = channels;
        else if (table == 1) candidate.warm = channels;
        else candidate.cache = channels;
    }
    *result = candidate;
    return true;
}

bool channelUpdateSchemaReady(OriginalBusiness::Database &database, QString *message)
{
    QSqlQuery columns(database.db);
    if (!columns.exec(QStringLiteral("PRAGMA table_xinfo(test_channel)")))
        return fail(message, QStringLiteral("无法核对检测通道表的生成/隐藏列。"));
    int columnCount = 0;
    while (columns.next())
    {
        bool valid = false;
        const int hidden = columns.value(6).toInt(&valid);
        if (++columnCount > 256 || !valid || hidden != 0)
            return fail(message, QStringLiteral("检测通道表存在生成/隐藏列或超过256列本地预算，未执行联动更新。"));
    }
    if (!columnCount || columns.lastError().isValid())
        return fail(message, QStringLiteral("检测通道列资料无效或当前SQLite不支持核对，未猜测放行。"));
    columns.finish();
    // 只改is_open不能触发旧库中的其他联动；不通过关闭PRAGMA或删触发器放行。
    QSqlQuery foreignKeys(database.db);
    if (!foreignKeys.exec(QStringLiteral("PRAGMA foreign_keys")) || !foreignKeys.next())
        return fail(message, QStringLiteral("无法核对通道库的外键联动设置。"));
    if (foreignKeys.value(0).toInt() != 0)
        return fail(message, QStringLiteral("本次通道回读暂不支持启用外键联动的连接，原设置未改。"));
    foreignKeys.finish();
    QSqlQuery triggers(database.db);
    if (!triggers.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='trigger' AND tbl_name='test_channel' COLLATE NOCASE LIMIT 1")))
        return fail(message, triggers.lastError().text());
    if (triggers.next()) return fail(message, QStringLiteral("原检测通道表存在触发器，未执行可能改变其他字段/表的更新。"));
    if (triggers.lastError().isValid()) return fail(message, triggers.lastError().text());
    return true;
}

bool rollbackChannels(OriginalBusiness::Database &database, QString *message)
{
    if (database.db.rollback()) return true;
    if (message) *message += QStringLiteral("；通道事务回滚未完成：") + database.db.lastError().text();
    return false;
}

bool channelReadLockReady(OriginalBusiness::Database &database, QString *message)
{
    QSqlQuery query(database.db);
    if (!query.exec(QStringLiteral("SELECT 1 FROM sqlite_master LIMIT 1")) || !query.next())
        return fail(message, QStringLiteral("无法取得通道库读取快照：") + query.lastError().text());
    return true;
}

bool channelPageBudgetReady(OriginalBusiness::Database &database, QString *message)
{
    QSqlQuery pages(database.db);
    QSqlQuery pageSize(database.db);
    if (!pages.exec(QStringLiteral("PRAGMA page_count")) || !pages.next() ||
        !pageSize.exec(QStringLiteral("PRAGMA page_size")) || !pageSize.next())
        return fail(message, QStringLiteral("无法核对提交后的通道库大小预算。"));
    qint64 count = 0;
    qint64 bytes = 0;
    if (!integer(pages.value(0), 1, MaximumChannelDatabaseBytes, &count) ||
        !integer(pageSize.value(0), 512, 65536, &bytes) || count > MaximumChannelDatabaseBytes / bytes)
        return fail(message, QStringLiteral("通道更新将超过16MiB本地文件预算，事务未提交。"));
    return true;
}

bool channelReadTransaction(OriginalBusiness::Database &database, const QString &path,
                            Channels *result, QString *message)
{
    if (!deleteJournalMode(database, message) || !database.execute(QStringLiteral("BEGIN"), {}, message)) return false;
    Channels candidate;
    // SELECT取得并保持SQLite读锁，直到指纹完成，防止合法并发提交与主库hash错版。
    if (!channelReadLockReady(database, message) || !channelFileReady(path, message) ||
        !deleteJournalMode(database, message) || !boundedChannelRows(database, &candidate, message) ||
        !boundedChannelDigest(path, &candidate.fingerprint, message) ||
        !database.commit(message))
    {
        rollbackChannels(database, message);
        return false;
    }
    *result = candidate;
    return true;
}
}

bool readChannelsSnapshot(const QString &root, Channels *result, QString *message)
{
    if (message) message->clear();
    if (!result) return fail(message, QStringLiteral("通道快照输出对象为空。"));
    if (!OriginalBusiness::workingRoot(root, message)) return false;
    const QString path = QDir(root).filePath(QStringLiteral("db/systemInfo/channel.db"));
    QLockFile lock(path + QStringLiteral(".sn100-write.lock"));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0)) return fail(message, QStringLiteral("原通道资料正在处理，请稍后重读。"));
    if (!channelFileReady(path, message)) return false;
    OriginalBusiness::Database database;
    if (!database.open(root, QStringLiteral("db/systemInfo/channel.db"), false, message)) return false;
    return channelReadTransaction(database, path, result, message);
}

ChannelApplyResult applyTestChannelReadback(const QString &root, const QVector<ChannelSwitchUpdate> &updates,
                                           const QString &expectedFingerprint, const ChannelCommitPermitPtr &permit)
{
    ChannelApplyResult result;
    if (!permit)
    {
        result.message = QStringLiteral("通道回读缺少提交许可，未修改。返回值不能作为设备完成确认。");
        return result;
    }
    const auto cancelled = [&result]()
    {
        result.outcome = ChannelApplyOutcome::Cancelled;
        result.message = QStringLiteral("通道回读提交前已撤销，事务未提交。");
        return result;
    };
    const auto initialPermit = permit->state();
    if (initialPermit == ChannelCommitPermit::State::Revoked) return cancelled();
    if (initialPermit != ChannelCommitPermit::State::Active)
    {
        result.message = QStringLiteral("通道回读提交许可已被使用，未重复提交。");
        return result;
    }
    if (updates.size() > MaximumChannelReadbackUpdates || expectedFingerprint.size() != 64)
    {
        result.message = QStringLiteral("通道回读超过1024项本地预算，或预期资料指纹无效。");
        return result;
    }
    if (!OriginalBusiness::workingRoot(root, &result.message)) return result;
    const QString path = QDir(root).filePath(QStringLiteral("db/systemInfo/channel.db"));
    QLockFile lock(path + QStringLiteral(".sn100-write.lock"));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0))
    {
        result.message = QStringLiteral("原通道资料正在写入，回读未排队覆盖。");
        return result;
    }
    if (!channelFileReady(path, &result.message)) return result;
    OriginalBusiness::Database database;
    if (!database.open(root, QStringLiteral("db/systemInfo/channel.db"), true, &result.message) ||
        !deleteJournalMode(database, &result.message) || !database.begin(&result.message)) return result;
    Channels before;
    if (!channelFileReady(path, &result.message) || !deleteJournalMode(database, &result.message) ||
        !boundedChannelRows(database, &before, &result.message) || !boundedChannelDigest(path, &before.fingerprint, &result.message) ||
        !channelUpdateSchemaReady(database, &result.message))
    {
        rollbackChannels(database, &result.message);
        return result;
    }
    if (before.fingerprint != expectedFingerprint)
    {
        result.message = QStringLiteral("原通道资料指纹已变化，回读未覆盖其他修改。");
        rollbackChannels(database, &result.message);
        return result;
    }
    QMap<QString, int> known;
    for (int index = 0; index < before.test.size(); ++index) known.insert(before.test.at(index).number, index);
    int changedRows = 0;
    int unknownRows = 0;
    for (const ChannelSwitchUpdate &update : updates)
    {
        if (permit->state() == ChannelCommitPermit::State::Revoked)
        {
            if (!rollbackChannels(database, &result.message)) return result;
            return cancelled();
        }
        const auto target = known.constFind(update.number);
        if (target == known.constEnd())
        {
            ++unknownRows;
            continue;
        }
        Channel &channel = before.test[target.value()];
        if (channel.enabled == update.enabled) continue;
        if (!database.execute(QStringLiteral("UPDATE OR ABORT test_channel SET is_open=? WHERE id=? AND channel_num=? AND is_open=?"),
                              {update.enabled ? 1 : 0, channel.id, channel.number, channel.enabled ? 1 : 0}, &result.message, 1))
        {
            rollbackChannels(database, &result.message);
            return result;
        }
        channel.enabled = update.enabled;
        ++changedRows;
    }
    if (!channelPageBudgetReady(database, &result.message))
    {
        rollbackChannels(database, &result.message);
        return result;
    }
    // CAS与COMMIT相邻：撤销先取得状态则回滚；提交票先取得后，不再把成功误报取消。
    if (!permit->tryBeginCommit())
    {
        if (!rollbackChannels(database, &result.message)) return result;
        if (permit->state() == ChannelCommitPermit::State::Revoked) return cancelled();
        result.message = QStringLiteral("通道回读提交许可已被占用，事务未提交。");
        return result;
    }
    if (!database.commit(&result.message))
    {
        rollbackChannels(database, &result.message);
        return result;
    }
    permit->markCommitted();
    result.changedRows = changedRows;
    result.unknownRows = unknownRows;
    // 提交后必须重读；失败时保持空快照，不把提交前资料伪装成新状态。
    if (!channelFileReady(path, &result.message) || !channelReadTransaction(database, path, &result.channels, &result.message))
    {
        result.outcome = ChannelApplyOutcome::CommittedReloadFailed;
        result.message = QStringLiteral("通道开关已提交，但重读新快照失败：") + result.message;
        return result;
    }
    result.outcome = ChannelApplyOutcome::Committed;
    result.message = QStringLiteral("通道回读已提交并重读；实际改变%1次，跳过未知项%2次。")
        .arg(changedRows).arg(unknownRows);
    return result;
}
}
