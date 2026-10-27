#ifndef SN100_STATUS_LOOPBACK_H
#define SN100_STATUS_LOOPBACK_H

#include "frame_codec.h"
#include "status_session.h"
#include <QElapsedTimer>
#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QThread>
#include <QVector>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace Sn100Protocol
{
class ReadOnlyClient;

struct LoopbackStatusState
{
    quint64 runId = 0, revision = 0, connectionGeneration = 0;
    bool running = false, connected = false, pending = false, fresh = false, hasSnapshot = false;
    bool hasRequestResult = false, lastRequestSuccess = false, traceOverflow = false;
    quint16 port = 0; // 本轮自建127.0.0.1端点，仅用于显示，不接受外部端口。
    int attempt = 0;
    qint64 elapsedMs = 0, statusAtMs = -1;
    quint64 queriesQueued = 0, requestsSucceeded = 0, requestsFailed = 0, observations = 0;
    quint64 wireEvents = 0, wireBytes = 0;
    QString linkState = QStringLiteral("stopped"), detail, error; // error仅表示本轮致命失败。
    StatusSnapshot snapshot;
    QByteArray rawQj; // 已校验的原始Qj业务字节；不含帧头尾。
};

struct LoopbackWireEvent
{
    quint64 runId = 0, connectionGeneration = 0, sequence = 0;
    qint64 elapsedMs = 0;
    QString direction; // TX=自建peer实际read，RX=client实际read；均非仪器ACK。
    QByteArray bytes;
};
using LoopbackWireBatch = QVector<LoopbackWireEvent>;

// 构造时不创建socket/timer；moveToThread后才调用start。正常场景只应答匿名Qj。
class StatusLoopbackWorker : public QObject
{
    Q_OBJECT
  public:
    explicit StatusLoopbackWorker(quint64 runId, QObject *parent = nullptr);
    static void registerTypes();

  public slots:
    void start();
    void reconnect();
    void stop();
    void acknowledgeState(quint64 runId, quint64 revision);
    void acknowledgeWire(quint64 runId, quint64 batchId);

  signals:
    void stateReady(Sn100Protocol::LoopbackStatusState state, quint64 revision);
    void wireBatchReady(quint64 runId, quint64 batchId, Sn100Protocol::LoopbackWireBatch batch);
    void started();
    void stopped();
    void error(QString message);

  protected:
    // 唯一故障注入点供独立测试覆盖坏包/分片/迟到；产品无场景参数或故障UI。
    virtual void replyStatus(QTcpSocket *peer, quint64 generation);
    bool writePeer(QTcpSocket *peer, quint64 generation, const QByteArray &bytes);

  private:
    bool onWorkerThread();
    void openConnection();
    void acceptPeer();
    void readPeer(QTcpSocket *peer, quint64 generation);
    void queryStatus();
    void clearStatus();
    void syncClientState();
    void publishState();
    void recordWire(const QString &direction, const QByteArray &bytes, quint64 generation);
    void flushWire();
    void fail(const QString &message);

    LoopbackStatusState state_;
    ReadOnlyClient *client_ = nullptr;
    QTcpServer *server_ = nullptr;
    QPointer<QTcpSocket> peer_;
    QTimer *poll_ = nullptr;
    QElapsedTimer clock_;
    FrameDecoder peerDecoder_{Direction::HostToDevice};
    QQueue<LoopbackWireEvent> wireQueue_;
    int queuedWireBytes_ = 0;
    quint64 wireBatchId_ = 0, pendingStateRevision_ = 0, lastDisconnected_ = 0;
    bool stopping_ = false, startedReported_ = false, stateScheduled_ = false;
    bool statePending_ = false, stateDirty_ = false, wirePending_ = false;
};

// GUI线程只持有线程/值快照；不提供host、port或任意命令参数。
class StatusLoopbackController : public QObject
{
    Q_OBJECT
  public:
    explicit StatusLoopbackController(QObject *parent = nullptr);
    ~StatusLoopbackController() override;
    LoopbackStatusState current() const;
    bool isRunning() const;
    bool isStopping() const;
    // app.exec返回后的入口清理/析构兜底；交互关闭先用异步stop和stopped。
    bool shutdownAndWait();

  public slots:
    void start();
    void reconnect();
    void stop();

  signals:
    void snapshotChanged(Sn100Protocol::LoopbackStatusState state);
    void wireObserved(Sn100Protocol::LoopbackWireEvent event);
    void started(); // 本机连接成功，不表示已有Qj观测或仪器就绪。
    void stopped(); // 已完成worker线程退出。
    void error(QString message);

  private:
    QThread thread_;
    QPointer<StatusLoopbackWorker> worker_;
    QMetaObject::Connection finishedConnection_;
    LoopbackStatusState state_;
    QMutex finalMutex_;
    LoopbackStatusState finalState_;
    quint64 runId_ = 0;
    bool stopping_ = false;
};
} // namespace Sn100Protocol
Q_DECLARE_METATYPE(Sn100Protocol::LoopbackStatusState)
Q_DECLARE_METATYPE(Sn100Protocol::LoopbackWireEvent)
Q_DECLARE_METATYPE(Sn100Protocol::LoopbackWireBatch)
#endif
