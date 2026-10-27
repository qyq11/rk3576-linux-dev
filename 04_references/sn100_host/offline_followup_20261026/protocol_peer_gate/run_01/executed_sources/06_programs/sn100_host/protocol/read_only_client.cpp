#include "read_only_client.h"

#include <QTimer>

namespace Sn100Protocol
{
namespace
{
bool reject(QString *error, const QString &message)
{
    if(error) *error = message;
    return false;
}
}

ReadOnlyClient::ReadOnlyClient(QObject *parent, int timeoutMs, int maxRetries, int maxQueuedBytes)
    : QObject(parent), m_timeoutMs(qBound(1, timeoutMs, 60000)),
      m_maxRetries(qBound(0, maxRetries, 3))
{
    qRegisterMetaType<ReadOnlyQuery>("ReadOnlyQuery");
    qRegisterMetaType<ReadOnlyQuery>("Sn100Protocol::ReadOnlyQuery");
    qRegisterMetaType<ReadOnlyMessage>("ReadOnlyMessage");
    qRegisterMetaType<ReadOnlyMessage>("Sn100Protocol::ReadOnlyMessage");
    m_clock.start();
    m_transport = new ReadOnlyTcpTransport(this, maxQueuedBytes);
    m_timer = new QTimer(this);
    m_timer->setInterval(10);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this]() { tick(); });
    connect(m_transport, &ReadOnlyTcpTransport::connected, this, &ReadOnlyClient::onConnected);
    connect(m_transport, &ReadOnlyTcpTransport::disconnected, this, &ReadOnlyClient::onDisconnected);
    connect(m_transport, &ReadOnlyTcpTransport::messageReceived, this, &ReadOnlyClient::observe);
    connect(m_transport, &ReadOnlyTcpTransport::errorOccurred, this,
            [this](const QString &error, quint64 connectionGeneration)
    {
        if(connectionGeneration != m_connectionGeneration) return;
        if(m_transport->generation() != connectionGeneration)
        {
            // 硬错误已使transport失效。先一次性取消，再转告诊断，避免
            // notice槽重连时把旧在途等待无声覆盖；随后的旧disconnected会被过滤。
            const quint64 stoppedGeneration = m_transport->generation();
            onDisconnected(connectionGeneration);
            if(m_transport->generation() == stoppedGeneration)
                emit notice(error, connectionGeneration);
            return;
        }
        emit notice(error, connectionGeneration);
    });
}

ReadOnlyClient::~ReadOnlyClient()
{
    m_timer->stop();
    QObject::disconnect(m_transport, nullptr, this, nullptr);
    m_transport->close();
}

void ReadOnlyClient::setTimingEnabled(bool enabled)
{
    m_timingEnabled = enabled;
}

void ReadOnlyClient::traceTiming(const char *stage)
{
    if(!m_timingEnabled) return;
    emit timingEvent(QString::fromLatin1(stage), m_clock.nsecsElapsed(), m_connectionGeneration,
                     m_requestRevision, m_attempt, m_deadline);
}

bool ReadOnlyClient::current(quint64 connectionGeneration) const
{
    return m_connected && m_connectionGeneration == connectionGeneration
            && m_transport->generation() == connectionGeneration;
}

void ReadOnlyClient::resetState()
{
    m_timer->stop();
    m_connected = false;
    m_connecting = false;
    m_pending = false;
    m_enqueueBusy = false;
    m_silenceClosing = false;
    m_fresh = false;
    m_requestTimedOut = false;
    m_attempt = 0;
    m_deadline = -1;
    m_connectedAt = -1;
    m_lastStatusAt = -1;
    m_connectionGeneration = 0;
    ++m_requestRevision;
}

bool ReadOnlyClient::connectLoopback(quint16 port, QString *error)
{
    if(error) error->clear();
    const quint64 operation = ++m_operation;
    // 先让旧连接完整取消；其requestFinished槽可重入connect，须尊重新操作。
    m_transport->close();
    if(operation != m_operation)
        return reject(error, QStringLiteral("client.cancelled: 清理旧连接期间已有新操作"));
    resetState();
    m_connecting = true;
    m_connectionGeneration = m_transport->generation() + 1;
    const bool accepted = m_transport->connectLoopback(port, error);
    if(operation != m_operation)
        return reject(error, QStringLiteral("client.cancelled: 连接启动期间已有新操作"));
    if(!accepted)
    {
        resetState();
        return false;
    }
    return true;
}

void ReadOnlyClient::close()
{
    const quint64 operation = ++m_operation;
    m_transport->close();
    if(operation == m_operation) resetState();
}

void ReadOnlyClient::onConnected(quint64 connectionGeneration)
{
    if(connectionGeneration != m_connectionGeneration ||
       connectionGeneration != m_transport->generation()) return;
    m_connected = true;
    m_connecting = false;
    m_connectedAt = m_clock.elapsed();
    m_lastStatusAt = -1;
    m_fresh = false;
    m_timer->start();
    emit connected(connectionGeneration);
}

void ReadOnlyClient::onDisconnected(quint64 connectionGeneration)
{
    if(connectionGeneration != m_connectionGeneration) return;
    const bool wasPending = m_pending;
    const ReadOnlyQuery query = m_query;
    const quint64 stoppedGeneration = m_transport->generation();
    resetState();
    if(wasPending)
    {
        emit requestFinished(query, false, QStringLiteral("client.cancelled: 连接关闭，在途只读等待已取消；未确认仪器ACK"),
                             connectionGeneration);
        if(m_transport->generation() != stoppedGeneration) return;
    }
    emit disconnected(connectionGeneration);
}

bool ReadOnlyClient::request(ReadOnlyQuery query, QString *error)
{
    if(error) error->clear();
    if(m_connected) tick();
    if(!m_connected || m_silenceClosing || !m_transport->isConnected())
        return reject(error, QStringLiteral("client.not_connected: 尚未建立可发送的回环连接"));
    if(m_pending || m_enqueueBusy)
        return reject(error, QStringLiteral("client.busy: 已有一个只读请求在途或正在入队"));
    const QByteArray command = queryPayload(query);
    if(command != "Qj" && command != "Qk" && command != "QH")
        return reject(error, QStringLiteral("client.query: 仅允许Qj/Qk/QH只读请求"));
    const quint64 connectionGeneration = m_connectionGeneration;
    const quint64 revision = ++m_requestRevision;
    m_enqueueBusy = true;
    QString queueError;
    const bool accepted = m_transport->queueQuery(query, &queueError);
    if(!current(connectionGeneration) || m_requestRevision != revision)
        return reject(error, QStringLiteral("client.cancelled: 请求入队期间连接/请求代次已改变"));
    m_enqueueBusy = false;
    if(!accepted)
    {
        emit notice(queueError, connectionGeneration);
        return reject(error, queueError);
    }
    // 只有真正整帧入队后，才建立在途状态和单调时钟截止点。
    m_query = query;
    m_pending = true;
    m_attempt = 1;
    m_deadline = m_clock.elapsed() + m_timeoutMs;
    m_requestTimedOut = false;
    traceTiming("deadline.initial");
    emit queryQueued(query, m_attempt, connectionGeneration);
    return true;
}

void ReadOnlyClient::finishRequest(bool success, const QString &detail, quint64 connectionGeneration)
{
    if(!current(connectionGeneration) || !m_pending) return;
    traceTiming(success ? "request.success" : "request.failure");
    const ReadOnlyQuery query = m_query;
    m_pending = false;
    m_attempt = 0;
    m_deadline = -1;
    m_requestTimedOut = !success;
    if(!success && query == ReadOnlyQuery::Status) m_fresh = false;
    ++m_requestRevision;
    // 先清理等待再通知，接收者可立即发起下一请求或关闭/重连。
    emit requestFinished(query, success, detail, connectionGeneration);
}

void ReadOnlyClient::observe(const QByteArray &message, quint64 connectionGeneration)
{
    if(!current(connectionGeneration)) return;
    traceTiming("observe.enter");
    // tick可能超时结束旧请求，其直接槽又发起新请求；当前已收到的旧报文
    // 只能成为观测，不能冒认随后才建立的新等待的回复。
    const quint64 receivedForRevision = m_requestRevision;
    // 事件循环繁忙时timer可能迟到；不能让过了截止点的报文跳过超时策略。
    tick();
    if(!current(connectionGeneration)) return;
    ReadOnlyMessage parsed;
    QString error;
    if(!parseReadOnlyMessage(message, &parsed, &error))
    {
        emit notice(error, connectionGeneration);
        return;
    }
    if(parsed.command == "Qj")
    {
        m_fresh = true;
        m_lastStatusAt = m_clock.elapsed();
        if(!m_pending && m_query == ReadOnlyQuery::Status) m_requestTimedOut = false;
    }
    const bool matchingRequest = m_pending && m_requestRevision == receivedForRevision
            && parsed.command == queryPayload(m_query);
    // 先交付实际观测。接收者可以在此关闭/重连，旧请求应按取消结束，
    // 而不是先收到success=true后才得知用户已拒绝继续使用该连接。
    emit messageObserved(parsed, connectionGeneration);
    if(!current(connectionGeneration) || !matchingRequest || !m_pending ||
       m_requestRevision != receivedForRevision) return;
    if(parsed.command == queryPayload(m_query))
    {
        const QString detail = QStringLiteral("client.observation: 收到最新同类%1有效观测；线上无消息ID，无法证明属于哪次请求/重试；不是仪器ACK")
                .arg(QString::fromLatin1(parsed.command));
        finishRequest(true, detail, connectionGeneration);
    }
}

void ReadOnlyClient::tick()
{
    const quint64 connectionGeneration = m_connectionGeneration;
    if(!current(connectionGeneration) || m_silenceClosing) return;
    const qint64 now = m_clock.elapsed();
    traceTiming("timer.tick");
    const qint64 lastStatus = m_lastStatusAt >= 0 ? m_lastStatusAt : m_connectedAt;
    if(lastStatus >= 0 && now - lastStatus >= 200000)
    {
        m_fresh = false;
        m_silenceClosing = true;
        emit notice(QStringLiteral("client.silence: 200000ms无有效Qj观测，关闭回环连接并清理旧状态"),
                    connectionGeneration);
        if(current(connectionGeneration)) m_transport->close();
        return;
    }
    if(!m_pending || now < m_deadline) return;
    if(m_attempt <= m_maxRetries)
    {
        const ReadOnlyQuery query = m_query;
        const quint64 revision = m_requestRevision;
        QString error;
        const bool accepted = m_transport->queueQuery(query, &error);
        if(!current(connectionGeneration) || !m_pending || revision != m_requestRevision) return;
        if(accepted)
        {
            ++m_attempt;
            m_deadline = m_clock.elapsed() + m_timeoutMs;
            traceTiming("deadline.retry");
            emit queryQueued(query, m_attempt, connectionGeneration);
        }
        else
        {
            const QString detail = QStringLiteral("client.retry_rejected: 只读重试未入队，等待失败：%1").arg(error);
            finishRequest(false, detail, connectionGeneration);
            if(current(connectionGeneration)) emit notice(detail, connectionGeneration);
        }
        return;
    }
    const QString detail = QStringLiteral("client.timeout: 只读等待及%1次重试已耗尽；状态未知，TCP连接未据此假称断开")
            .arg(m_maxRetries);
    finishRequest(false, detail, connectionGeneration);
    if(current(connectionGeneration)) emit notice(detail, connectionGeneration);
}

bool ReadOnlyClient::requestPending() const { return m_pending; }
bool ReadOnlyClient::hasFreshStatus() const { return m_fresh; }
quint64 ReadOnlyClient::generation() const { return m_transport->generation(); }

QString ReadOnlyClient::linkState() const
{
    if(m_connecting) return QStringLiteral("connecting");
    if(!m_connected) return QStringLiteral("disconnected");
    if(m_pending) return QStringLiteral("awaiting-reply");
    if(m_requestTimedOut) return QStringLiteral("request-timeout");
    return m_fresh ? QStringLiteral("has-simulated-status") : QStringLiteral("connected-status-unknown");
}

} // namespace Sn100Protocol
