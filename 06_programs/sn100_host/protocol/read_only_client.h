#ifndef SN100_READ_ONLY_CLIENT_H
#define SN100_READ_ONLY_CLIENT_H

#include "tcp_transport.h"

#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>

class QTimer;

namespace Sn100Protocol
{

// 所有方法在本对象事件线程调用。仅一个只读请求在途，连接后不自动查询。
// success只表示本地等待到了同类型合法观测，不是仪器ACK或事务关联证明。
class ReadOnlyClient : public QObject
{
    Q_OBJECT

public:
    explicit ReadOnlyClient(QObject *parent = nullptr, int timeoutMs = 1000,
                            int maxRetries = 1, int maxQueuedBytes = 32768);
    ~ReadOnlyClient() override;

    bool connectLoopback(quint16 port, QString *error = nullptr);
    void close();
    bool request(ReadOnlyQuery query, QString *error = nullptr);
    bool requestPending() const;
    bool hasFreshStatus() const;
    QString linkState() const;
    quint64 generation() const;

signals:
    void connected(quint64 generation);
    void disconnected(quint64 generation);
    void messageObserved(ReadOnlyMessage message, quint64 generation);
    void requestFinished(ReadOnlyQuery query, bool success, QString detail, quint64 generation);
    void notice(QString message, quint64 generation);
    // planned入队，第1次attempt=1；不表示socket已写出或模拟对端已收到。
    void queryQueued(ReadOnlyQuery query, int attempt, quint64 generation);

private:
    bool current(quint64 generation) const;
    void resetState();
    void onConnected(quint64 generation);
    void onDisconnected(quint64 generation);
    void observe(const QByteArray &message, quint64 generation);
    void tick();
    void finishRequest(bool success, const QString &detail, quint64 generation);

    ReadOnlyTcpTransport *m_transport = nullptr;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    int m_timeoutMs;
    int m_maxRetries;
    int m_attempt = 0;
    ReadOnlyQuery m_query = ReadOnlyQuery::Status;
    qint64 m_deadline = -1;
    qint64 m_connectedAt = -1;
    qint64 m_lastStatusAt = -1;
    quint64 m_connectionGeneration = 0;
    quint64 m_operation = 0;
    quint64 m_requestRevision = 0;
    bool m_connected = false;
    bool m_connecting = false;
    bool m_enqueueBusy = false;
    bool m_silenceClosing = false;
    bool m_pending = false;
    bool m_fresh = false;
    bool m_requestTimedOut = false;
};

} // namespace Sn100Protocol

Q_DECLARE_METATYPE(Sn100Protocol::ReadOnlyQuery)
Q_DECLARE_METATYPE(Sn100Protocol::ReadOnlyMessage)

#endif
