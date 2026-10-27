#include "status_session.h"

#include <QJsonArray>
#include <limits>

namespace Sn100Protocol
{
namespace
{
bool decimal(const QString &text, int minimum, int maximum, int *value)
{
    if (text.isEmpty() || text.size() > 10)
    {
        return false;
    }
    for (QChar character : text)
    {
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
        {
            return false;
        }
    }
    bool ok = false;
    const qlonglong converted = text.toLongLong(&ok, 10);
    if (!ok || converted < minimum || converted > maximum)
    {
        return false;
    }
    *value = int(converted);
    return true;
}

bool fail(QString *error, const QString &message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}
}

QJsonObject StatusSnapshot::toJson() const
{
    static const char *const names[] = {
        "model", "main_status", "led_status", "error_work_type", "error_codes",
        "door_normal", "door_open_ss", "temperature_normal", "versions", "serial",
        "emergency_stop", "no_water_test", "no_cup_test", "version_check",
        "start_key", "plate_key", "sample_scanner", "reagent_scanner",
        "main_period_ms", "protocol_marker", "reagent_channels", "test_channels",
        "emergency_holes", "sample_holes", "dual_needle_working"
    };
    static const char *const mainNames[] = {
        "idle", "warming", "ready", "preparing", "prepared", "testing", "paused"
    };
    QJsonObject fields;
    for (int index = 0; index < qMin(generalFields.size(), 25); ++index)
    {
        fields.insert(QString::fromLatin1(names[index]), generalFields.at(index));
    }
    QJsonArray blocks;
    for (const SubsystemStatus &block : subsystems)
    {
        blocks.append(QJsonObject{{QStringLiteral("id"), block.id},
                      {QStringLiteral("raw_fields"), QJsonArray::fromStringList(block.fields)}});
    }
    return {{QStringLiteral("evidence"), QStringLiteral("simulated-source-derived")},
            {QStringLiteral("model"), model},
            {QStringLiteral("main_status"), mainStatus},
            {QStringLiteral("main_status_name"), mainStatus >= 0 && mainStatus < 7
                 ? QString::fromLatin1(mainNames[mainStatus]) : QStringLiteral("unknown")},
            {QStringLiteral("led_status"), ledStatus},
            {QStringLiteral("error_work_type"), errorWorkType},
            {QStringLiteral("declared_general_count"), generalFields.size()},
            {QStringLiteral("general"), fields},
            {QStringLiteral("subsystems_raw"), blocks}};
}

bool parseStatus(const QByteArray &message, StatusSnapshot *status, QString *error)
{
    if (error)
    {
        error->clear();
    }
    if (!status || !message.startsWith("Qj"))
    {
        return fail(error, QStringLiteral("status.command: 当前仅支持参考工程 Qj 状态消息"));
    }
    QStringList fields;
    int cursor = 2;
    while (cursor < message.size())
    {
        if (message.at(cursor++) != '[')
        {
            return fail(error, QStringLiteral("status.syntax: 字段必须完整放在方括号内"));
        }
        const int start = cursor;
        while (cursor < message.size() && message.at(cursor) != ']')
        {
            const unsigned char character = static_cast<unsigned char>(message.at(cursor++));
            if (character < 0x20 || character > 0x7e || character == '[' || cursor - start > 1024)
            {
                return fail(error, QStringLiteral("status.syntax: 非法字符或字段超过本地1024字节预算"));
            }
        }
        if (cursor == message.size() || fields.size() >= 4096)
        {
            return fail(error, QStringLiteral("status.syntax: 字段未结束或超过本地字段预算"));
        }
        fields.append(QString::fromLatin1(message.constData() + start, cursor - start));
        ++cursor;
    }
    int count = 0;
    if (fields.isEmpty() || !decimal(fields.first(), 4, 25, &count) || fields.size() < count + 2)
    {
        return fail(error, QStringLiteral("status.count: 需4至25个已知主字段及明确的子系统计数"));
    }
    StatusSnapshot parsed;
    parsed.generalFields = fields.mid(1, count);
    parsed.model = parsed.generalFields.at(0);
    if (parsed.model.isEmpty() ||
        !decimal(parsed.generalFields.at(1), 0, 6, &parsed.mainStatus) ||
        !decimal(parsed.generalFields.at(2), 0, 7, &parsed.ledStatus) ||
        !decimal(parsed.generalFields.at(3), 0, 7, &parsed.errorWorkType))
    {
        return fail(error, QStringLiteral("status.enum: 型号为空或主状态/灯状态/错误类型不在参考枚举范围"));
    }
    // 原版这些字段用 toInt()>0；保留正整数真值语义，拒绝把非法文本默认为0。
    for (int index : {5, 6, 7, 10, 11, 12, 14, 15, 16, 17, 20, 21, 22, 23, 24})
    {
        int number = 0;
        if (index < count && !decimal(parsed.generalFields.at(index), 0,
                                      std::numeric_limits<int>::max(), &number))
        {
            return fail(error, QStringLiteral("status.number: 第%1个主字段不是合法非负整数").arg(index + 1));
        }
    }
    cursor = count + 1;
    int subsystemCount = 0;
    if (!decimal(fields.at(cursor++), 0, 64, &subsystemCount))
    {
        return fail(error, QStringLiteral("status.subsystems: 子系统计数超过本地64块预算或非法"));
    }
    for (int blockIndex = 0; blockIndex < subsystemCount; ++blockIndex)
    {
        SubsystemStatus block;
        int fieldCount = 0;
        if (cursor + 2 > fields.size() || !decimal(fields.at(cursor), 0, 65535, &block.id) ||
            !decimal(fields.at(cursor + 1), 0, 256, &fieldCount))
        {
            return fail(error, QStringLiteral("status.subsystems: 子系统头缺失或超过本地字段预算"));
        }
        cursor += 2;
        if (cursor + fieldCount > fields.size())
        {
            return fail(error, QStringLiteral("status.subsystems: 子系统字段少于声明数量"));
        }
        block.fields = fields.mid(cursor, fieldCount);
        parsed.subsystems.append(block);
        cursor += fieldCount;
    }
    if (cursor != fields.size())
    {
        return fail(error, QStringLiteral("status.count: 声明数量之外还有未归属字段"));
    }
    *status = parsed;
    return true;
}

StatusSession::StatusSession(int requestTimeoutMs, int maximumRetries)
    : m_requestTimeoutMs(qBound(1, requestTimeoutMs, 60000)),
      m_maximumRetries(qBound(0, maximumRetries, 3))
{
}

void StatusSession::clearConnection()
{
    m_decoder.reset();
    m_connected = false;
    m_pending = false;
    m_fresh = false;
    m_timedOut = false;
    m_deadline = -1;
    m_retries = 0;
    m_lastValidStatusAt = -1;
    m_previousStatus.clear();
}

bool StatusSession::acceptTime(qint64 nowMs, SessionEvents *events)
{
    if (nowMs < 0 || nowMs < m_now || nowMs > std::numeric_limits<qint64>::max() - 200001)
    {
        clearConnection();
        events->notices.append(QStringLiteral("session.time: 时间倒退/溢出，连接与旧状态已清空"));
        return false;
    }
    m_now = nowMs;
    return true;
}

SessionEvents StatusSession::open(qint64 nowMs)
{
    SessionEvents events;
    if (!acceptTime(nowMs, &events))
    {
        return events;
    }
    clearConnection();
    m_connected = true;
    m_connectedAt = nowMs;
    events.notices.append(QStringLiteral("session.connected: 模拟连接已建立，仪器状态未知"));
    return events;
}

void StatusSession::sendQuery(qint64 nowMs, SessionEvents *events)
{
    QString error;
    events->transmitted.append(encodeMessage(QByteArray("Qj"), Direction::HostToDevice, &error));
    if (!error.isEmpty())
    {
        events->notices.append(error);
        return;
    }
    m_pending = true;
    m_timedOut = false;
    m_deadline = nowMs + m_requestTimeoutMs;
}

SessionEvents StatusSession::queryStatus(qint64 nowMs)
{
    SessionEvents events = advance(nowMs);
    if (!m_connected || m_pending)
    {
        events.notices.append(QStringLiteral("session.query_rejected: 未连接或已有Qj在途请求"));
        return events;
    }
    m_retries = 0;
    sendQuery(nowMs, &events);
    return events;
}

SessionEvents StatusSession::receive(const QByteArray &bytes, qint64 nowMs)
{
    SessionEvents events = advance(nowMs);
    if (!m_connected)
    {
        events.notices.append(QStringLiteral("session.rx_rejected: 模拟断线期间不接收旧报文"));
        return events;
    }
    const DecodeBatch batch = m_decoder.feed(bytes, nowMs);
    events.notices.append(batch.errors);
    if (batch.duplicatePackets)
    {
        events.notices.append(QStringLiteral("packet.duplicate: 跳过%1个相同在途分片").arg(batch.duplicatePackets));
    }
    for (const QByteArray &message : batch.messages)
    {
        StatusSnapshot snapshot;
        QString error;
        if (!parseStatus(message, &snapshot, &error))
        {
            events.notices.append(error);
            continue;
        }
        if (message == m_previousStatus)
        {
            events.notices.append(QStringLiteral("status.repeated: 同值完整状态仍刷新；线上无消息ID，不能推定重传"));
        }
        if (!m_pending || nowMs >= m_deadline)
        {
            events.notices.append(QStringLiteral("status.uncorrelated: 作为最新观测值接收，不能证明属于哪次Qj请求"));
        }
        m_previousStatus = message;
        m_pending = false;
        m_fresh = true;
        m_timedOut = false;
        m_lastValidStatusAt = nowMs;
        events.statuses.append(snapshot);
    }
    return events;
}

SessionEvents StatusSession::advance(qint64 nowMs)
{
    SessionEvents events;
    if (!acceptTime(nowMs, &events) || !m_connected)
    {
        return events;
    }
    events.notices.append(m_decoder.expire(nowMs).errors);
    const qint64 lastObservation = m_lastValidStatusAt >= 0 ? m_lastValidStatusAt : m_connectedAt;
    if (nowMs - lastObservation >= 200000)
    {
        clearConnection();
        events.notices.append(QStringLiteral("session.silence: 200000ms无有效状态，模拟断线并清空旧状态"));
        return events;
    }
    if (m_pending && nowMs >= m_deadline)
    {
        if (m_retries < m_maximumRetries)
        {
            ++m_retries;
            events.notices.append(QStringLiteral("session.retry: 仅模拟重试只读Qj，第%1次").arg(m_retries));
            sendQuery(nowMs, &events);
        }
        else
        {
            m_pending = false;
            m_fresh = false;
            m_timedOut = true;
            events.notices.append(QStringLiteral("session.timeout: Qj模拟重试已耗尽，状态失效"));
        }
    }
    return events;
}

SessionEvents StatusSession::disconnect(qint64 nowMs)
{
    SessionEvents events;
    if (!acceptTime(nowMs, &events))
    {
        return events;
    }
    clearConnection();
    events.notices.append(QStringLiteral("session.disconnected: 已清空半帧、组包、在途请求与旧状态"));
    return events;
}

QString StatusSession::linkState() const
{
    if (!m_connected) return QStringLiteral("disconnected");
    if (m_pending) return QStringLiteral("awaiting-status");
    if (m_timedOut) return QStringLiteral("status-timeout");
    return m_fresh ? QStringLiteral("has-simulated-status") : QStringLiteral("connected-status-unknown");
}

bool StatusSession::hasFreshStatus() const { return m_fresh; }
int StatusSession::bufferedBytes() const { return m_decoder.bufferedBytes(); }
int StatusSession::assemblyBytes() const { return m_decoder.assemblyBytes(); }
}
