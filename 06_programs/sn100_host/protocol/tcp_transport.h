#ifndef SN100_READ_ONLY_TCP_TRANSPORT_H
#define SN100_READ_ONLY_TCP_TRANSPORT_H

#include "frame_codec.h"
#include "read_only_messages.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQueue>

class QTcpSocket;
class QTimer;

namespace Sn100Protocol
{

// 本轮仅连接本进程测试服务的127.0.0.1临时端口。调用者须在本对象所属
// 事件线程调用这些方法；socket/timer均为子对象，不创建工作线程。
// 本层不关联请求、不自动重连/重试，也不把socket写入当成仪器应答。
class ReadOnlyTcpTransport : public QObject
{
    Q_OBJECT

public:
    // 保存1..65536字节的本地队列预算，不把小预算抬高；小于13字节时，
    // 当前13字节只读查询帧会被queueQuery整帧拒收，不写任何前缀。
    explicit ReadOnlyTcpTransport(QObject *parent = nullptr, int maxQueuedBytes = 32768,
                                  int writeQuantum = 4096, int connectTimeoutMs = 1000);
    ~ReadOnlyTcpTransport() override;

    bool connectLoopback(quint16 port, QString *error = nullptr);
    void close();
    bool queueQuery(ReadOnlyQuery query, QString *error = nullptr);
    bool isConnected() const;
    qint64 pendingWriteBytes() const;
    quint64 generation() const;
    int bufferedBytes() const;
    int assemblyBytes() const;
    // 专用诊断默认关闭。接收槽只采集，不在事件线程写盘或改变连接。
    void setTimingEnabled(bool enabled);

protected:
    // 唯一测试注入点：默认创建真正QTcpSocket。测试子类可提供受控socket，
    // 覆写Qt5虚函数writeData/connectToHost以覆盖短写/0/-1/黑洞超时。
    // 返回值须为新建、未连接、与本对象同事件线程且parent为参数的socket；
    // 注入结果不能据此称为操作系统实际发生短写。正式CLI不提供故障开关。
    virtual QTcpSocket *createSocket(QObject *parent);

signals:
    void connected(quint64 generation);
    void disconnected(quint64 generation);
    void messageReceived(QByteArray message, quint64 generation);
    void wireReceived(QByteArray bytes, quint64 generation);
    // 完整帧已经原子进入本层有界队列；这是planned TX，尚不证明写入Qt或对端。
    void wireQueued(QByteArray frame, quint64 generation);
    // 来自QTcpSocket::bytesWritten，仅说明socket写出进展，不是对端ACK。
    void bytesDelivered(qint64 bytes, quint64 generation);
    // elapsedNs来自本transport自建单调时钟；不是peer时钟或仪器ACK。
    void timingEvent(QString stage, qint64 elapsedNs, quint64 generation,
                     qint64 value, qint64 extra, qint64 applicationPending, qint64 socketPending);
    void errorOccurred(QString message, quint64 generation);

private:
    bool current(QTcpSocket *socket, quint64 generation) const;
    quint64 clearConnection(const QString &error, bool notify);
    void failSocket(QTcpSocket *socket, quint64 generation, const QString &message);
    void beginRemoteRead(QTcpSocket *socket, quint64 generation, const QString &message);
    void publishBatch(const DecodeBatch &batch, QTcpSocket *socket, quint64 generation);
    void scheduleRead(QTcpSocket *socket, quint64 generation);
    void readAvailable(QTcpSocket *socket, quint64 generation);
    void scheduleWrite(QTcpSocket *socket, quint64 generation);
    void pumpWrite(QTcpSocket *socket, quint64 generation);
    void tick(QTcpSocket *socket, quint64 generation);
    void traceTiming(const char *stage, qint64 value = 0, qint64 extra = 0);

    QPointer<QTcpSocket> m_socket;
    QTimer *m_tick = nullptr;
    QElapsedTimer m_clock;
    FrameDecoder m_decoder;
    QQueue<QByteArray> m_frames;
    qint64 m_applicationPendingBytes = 0;
    int m_frameOffset = 0;
    int m_maxQueuedBytes;
    int m_writeQuantum;
    int m_connectTimeoutMs;
    qint64 m_connectStartedMs = -1;
    qint64 m_writeProgressMs = -1;
    QString m_peerCloseError;
    quint64 m_generation = 0;
    bool m_active = false;
    bool m_peerEof = false;
    bool m_readScheduled = false;
    bool m_readDraining = false;
    bool m_writeScheduled = false;
    bool m_writeDraining = false;
    bool m_timingEnabled = false;
};

} // namespace Sn100Protocol

#endif
