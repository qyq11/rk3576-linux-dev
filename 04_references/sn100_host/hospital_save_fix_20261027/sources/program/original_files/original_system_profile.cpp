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
}
