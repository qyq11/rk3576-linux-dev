#include "status_replay.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>

namespace Sn100Protocol
{
namespace
{
constexpr qint64 kMaximumFileBytes = 1024 * 1024;
constexpr int kMaximumEvents = 256;
constexpr int kMaximumReceiveBytes = 8192;
constexpr int kMaximumTotalReceiveBytes = 128 * 1024;
constexpr qint64 kMaximumRecordedMs = 24 * 60 * 60 * 1000;

bool onlyKeys(const QJsonObject &object, const QSet<QString> &allowed)
{
    for (auto entry = object.begin(); entry != object.end(); ++entry)
        if (!allowed.contains(entry.key())) return false;
    return true;
}

bool recordedTime(const QJsonValue &value, qint64 *time)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > kMaximumRecordedMs || std::floor(number) != number)
        return false;
    *time = qint64(number);
    return true;
}

bool receiveBytes(const QJsonValue &value, QByteArray *bytes)
{
    if (!value.isString()) return false;
    const QString text = value.toString();
    QByteArray hex;
    // 输入文件已有1MiB上限；仍先限单hex字符串，避免无用的大临时分配。
    if (text.size() > kMaximumReceiveBytes * 3) return false;
    for (QChar character : text)
    {
        const ushort code = character.unicode();
        if (code == ' ') continue;
        if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') || (code >= 'A' && code <= 'F')))
            return false;
        hex.append(char(code));
    }
    if (hex.isEmpty() || hex.size() % 2 || hex.size() > kMaximumReceiveBytes * 2) return false;
    *bytes = QByteArray::fromHex(hex);
    return true;
}
}

StatusReplay::StatusReplay(int requestTimeoutMs, int maximumRetries)
    : m_requestTimeoutMs(qBound(1, requestTimeoutMs, 60000)),
      m_maximumRetries(qBound(0, maximumRetries, 3)),
      m_session(m_requestTimeoutMs, m_maximumRetries)
{
}

bool StatusReplay::loadFile(const QString &path, QString *error)
{
    if (error) error->clear();
    m_session = StatusSession(m_requestTimeoutMs, m_maximumRetries);
    m_events.clear();
    m_state = ReplayState();
    const auto reject = [this, error](const QString &description)
    {
        m_state.error = description;
        if (error) *error = description;
        return false;
    };
    const QFileInfo info(path);
    if (!QDir::isAbsolutePath(path) || !info.isFile() || info.isSymLink()
        || path.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
        || info.canonicalFilePath().contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
        || info.size() > kMaximumFileBytes)
        return reject(QStringLiteral("replay.path: 需≤1MiB绝对常规回放文件，禁止参考目录或符号链接"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return reject(QStringLiteral("replay.read: 无法读取显式回放文件"));
    const QByteArray contents = file.read(kMaximumFileBytes + 1);
    if (contents.size() > kMaximumFileBytes || !file.atEnd())
        return reject(QStringLiteral("replay.budget: 文件读取超过1MiB预算"));
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return reject(QStringLiteral("replay.json: 需有效JSON对象"));
    const QJsonObject root = document.object();
    if (!onlyKeys(root, {QStringLiteral("schema"), QStringLiteral("events")})
        || root.value(QStringLiteral("schema")).toString() != QStringLiteral("sn100-status-replay-v1")
        || !root.value(QStringLiteral("events")).isArray())
        return reject(QStringLiteral("replay.schema: 仅允许sn100-status-replay-v1的schema/events字段"));
    const QJsonArray events = root.value(QStringLiteral("events")).toArray();
    if (events.isEmpty() || events.size() > kMaximumEvents)
        return reject(QStringLiteral("replay.budget: 事件数必须为1..256"));
    QVector<Event> parsed;
    parsed.reserve(events.size());
    qint64 previousTime = -1;
    int totalReceiveBytes = 0;
    const QSet<QString> actions{QStringLiteral("connect"), QStringLiteral("query-status"),
        QStringLiteral("rx"), QStringLiteral("advance"), QStringLiteral("disconnect")};
    for (int index = 0; index < events.size(); ++index)
    {
        if (!events.at(index).isObject()) return reject(QStringLiteral("replay.event: 第%1步不是对象").arg(index + 1));
        const QJsonObject object = events.at(index).toObject();
        Event event;
        event.action = object.value(QStringLiteral("action")).toString();
        QSet<QString> keys{QStringLiteral("at_ms"), QStringLiteral("action")};
        if (event.action == QStringLiteral("rx")) keys.insert(QStringLiteral("hex"));
        if (!onlyKeys(object, keys) || !actions.contains(event.action)
            || !recordedTime(object.value(QStringLiteral("at_ms")), &event.atMs) || event.atMs < previousTime)
            return reject(QStringLiteral("replay.event: 第%1步字段/动作非法或时间非整数、倒退、超24h").arg(index + 1));
        if (event.action == QStringLiteral("rx"))
        {
            if (!receiveBytes(object.value(QStringLiteral("hex")), &event.bytes))
                return reject(QStringLiteral("replay.hex: 第%1步需1..8192字节合法偶数hex，可用空格分隔").arg(index + 1));
            totalReceiveBytes += event.bytes.size();
            if (totalReceiveBytes > kMaximumTotalReceiveBytes)
                return reject(QStringLiteral("replay.budget: 总RX超过128KiB"));
        }
        previousTime = event.atMs;
        parsed.append(event);
    }
    m_events = parsed;
    m_state.loaded = true;
    m_state.eventCount = parsed.size();
    return true;
}

void StatusReplay::clearSnapshot()
{
    m_state.fresh = false;
    m_state.hasSnapshot = false;
    m_state.statusTimeMs = -1;
    m_state.snapshot = StatusSnapshot();
}

void StatusReplay::finish(bool stopped)
{
    const SessionEvents events = m_session.disconnect(m_state.sourceTimeMs);
    m_state.notices.append(events.notices);
    m_state.notices.append(stopped ? QStringLiteral("replay.stopped: 回放已停止，旧展示失效，未连接仪器")
                                 : QStringLiteral("replay.finished: 文件回放结束，旧展示失效，未连接仪器"));
    ++m_state.epoch;
    m_state.linkState = m_session.linkState();
    clearSnapshot();
    m_state.finished = true;
    m_state.stopped = stopped;
}

bool StatusReplay::step()
{
    if (!m_state.loaded || m_state.finished) return false;
    const Event &event = m_events.at(m_state.eventIndex);
    m_state.transmitted.clear();
    m_state.received.clear();
    m_state.notices.clear();
    m_state.sourceTimeMs = event.atMs;
    m_state.action = event.action;
    SessionEvents result;
    if (event.action == QStringLiteral("connect"))
    {
        ++m_state.epoch;
        result = m_session.open(event.atMs);
    }
    else if (event.action == QStringLiteral("query-status")) result = m_session.queryStatus(event.atMs);
    else if (event.action == QStringLiteral("rx"))
    {
        m_state.received = event.bytes;
        result = m_session.receive(event.bytes, event.atMs);
    }
    else if (event.action == QStringLiteral("advance")) result = m_session.advance(event.atMs);
    else if (event.action == QStringLiteral("disconnect"))
    {
        ++m_state.epoch;
        result = m_session.disconnect(event.atMs);
    }
    m_state.transmitted = result.transmitted;
    m_state.notices = result.notices;
    if (!result.statuses.isEmpty())
    {
        m_state.snapshot = result.statuses.last();
        m_state.hasSnapshot = true;
        m_state.statusTimeMs = event.atMs;
    }
    m_state.linkState = m_session.linkState();
    m_state.fresh = m_session.hasFreshStatus();
    if (!m_state.fresh) clearSnapshot();
    ++m_state.eventIndex;
    // 最后一步结束即失效，不能停在一张貌似仍然实时的仪器状态上。
    if (m_state.eventIndex == m_state.eventCount) finish(false);
    return true;
}

void StatusReplay::stop()
{
    if (!m_state.loaded || m_state.finished) return;
    m_state.action = QStringLiteral("stop");
    m_state.transmitted.clear();
    m_state.received.clear();
    m_state.notices.clear();
    finish(true);
}

const ReplayState &StatusReplay::current() const { return m_state; }

} // namespace Sn100Protocol
