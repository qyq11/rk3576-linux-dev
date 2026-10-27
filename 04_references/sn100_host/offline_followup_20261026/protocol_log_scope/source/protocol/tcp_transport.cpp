#include "tcp_transport.h"

#include <QHostAddress>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QTimer>
#include <algorithm>

namespace Sn100Protocol
{
namespace
{
constexpr int kReadChunkBytes = 4096;
constexpr int kPerTurnBytes = 16384;
constexpr int kSocketReadBufferBytes = 65536;
constexpr int kTimerIntervalMs = 20;

bool rejected(QString *error, const QString &message)
{
    if(error) *error = message;
    return false;
}
}

ReadOnlyTcpTransport::ReadOnlyTcpTransport(QObject *parent, int maxQueuedBytes,
                                         int writeQuantum, int connectTimeoutMs)
    : QObject(parent), m_maxQueuedBytes(qBound(1, maxQueuedBytes, 65536)),
      m_writeQuantum(qBound(1, writeQuantum, kPerTurnBytes)),
      m_connectTimeoutMs(qBound(1, connectTimeoutMs, 60000))
{
    m_clock.start();
    m_tick = new QTimer(this);
    m_tick->setInterval(kTimerIntervalMs);
    m_tick->setTimerType(Qt::PreciseTimer);
}

ReadOnlyTcpTransport::~ReadOnlyTcpTransport()
{
    clearConnection(QString(), false);
}

void ReadOnlyTcpTransport::setTimingEnabled(bool enabled)
{
    m_timingEnabled = enabled;
}

void ReadOnlyTcpTransport::traceTiming(const char *stage, qint64 value, qint64 extra)
{
    if(!m_timingEnabled) return;
    emit timingEvent(QString::fromLatin1(stage), m_clock.nsecsElapsed(), m_generation,
                     value, extra, m_applicationPendingBytes,
                     m_socket ? m_socket->bytesToWrite() : 0);
}

QTcpSocket *ReadOnlyTcpTransport::createSocket(QObject *parent)
{
    return new QTcpSocket(parent);
}

bool ReadOnlyTcpTransport::current(QTcpSocket *socket, quint64 connectionGeneration) const
{
    return socket && m_active && m_socket.data() == socket
            && m_generation == connectionGeneration;
}

quint64 ReadOnlyTcpTransport::clearConnection(const QString &error, bool notify)
{
    const quint64 oldGeneration = m_generation;
    const quint64 newGeneration = ++m_generation;
    const bool wasActive = m_active;
    QTcpSocket *oldSocket = m_socket.data();
    // 先使代次失效，再中止socket；同步触发或已排队的旧回调均不能碰新连接。
    m_active = false;
    m_socket.clear();
    m_tick->stop();
    QObject::disconnect(m_tick, nullptr, this, nullptr);
    m_decoder.reset();
    m_frames.clear();
    m_applicationPendingBytes = 0;
    m_frameOffset = 0;
    m_connectStartedMs = -1;
    m_writeProgressMs = -1;
    m_peerCloseError.clear();
    m_peerEof = false;
    m_readScheduled = false;
    m_readDraining = false;
    m_writeScheduled = false;
    m_writeDraining = false;
    if(oldSocket)
    {
        QObject::disconnect(oldSocket, nullptr, this, nullptr);
        oldSocket->abort();
        oldSocket->deleteLater();
    }
    if(notify)
    {
        if(!error.isEmpty()) emit errorOccurred(error, oldGeneration);
        if(wasActive) emit disconnected(oldGeneration);
    }
    return newGeneration;
}

bool ReadOnlyTcpTransport::connectLoopback(quint16 port, QString *error)
{
    if(error) error->clear();
    const quint64 connectionGeneration = clearConnection(QString(), true);
    // 公开信号的接收者也可能重入close/connect，不能覆盖其新连接。
    if(connectionGeneration != m_generation)
        return rejected(error, QStringLiteral("transport.reentered: 连接清理期间代次已改变"));
    if(port == 0)
        return rejected(error, QStringLiteral("transport.port: 本进程回环测试端口不能为0"));

    QTcpSocket *socket = createSocket(this);
    // 测试工厂也是用户可覆写调用，允许close/reconnect重入时必须尊重新代次。
    if(connectionGeneration != m_generation)
    {
        if(socket && socket != m_socket.data() && socket->parent() == this &&
           socket->thread() == thread()) socket->deleteLater();
        return rejected(error, QStringLiteral("transport.reentered: socket工厂调用期间代次已改变"));
    }
    if(!socket)
        return rejected(error, QStringLiteral("transport.factory: socket工厂未返回对象，未发起连接"));
    if(socket->parent() != this || socket->thread() != thread())
        return rejected(error, QStringLiteral("transport.factory: socket所有权/事件线程不符合约定，未接管对象"));
    if(socket->state() != QAbstractSocket::UnconnectedState)
    {
        socket->deleteLater();
        return rejected(error, QStringLiteral("transport.factory: socket必须未连接，未发起连接"));
    }
    m_socket = socket;
    m_active = true;
    m_connectStartedMs = m_clock.elapsed();
    socket->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    socket->setReadBufferSize(kSocketReadBufferBytes);
    const QPointer<QTcpSocket> guardedSocket(socket);

    connect(socket, &QAbstractSocket::connected, this,
            [this, guardedSocket, connectionGeneration]()
    {
        if(!current(guardedSocket.data(), connectionGeneration)) return;
        m_connectStartedMs = -1;
        emit connected(connectionGeneration);
    });
    connect(socket, &QAbstractSocket::disconnected, this,
            [this, guardedSocket, connectionGeneration]()
    {
        if(!current(guardedSocket.data(), connectionGeneration)) return;
        beginRemoteRead(guardedSocket.data(), connectionGeneration, QString());
    });
    const auto socketError = [this, guardedSocket, connectionGeneration](QAbstractSocket::SocketError code)
    {
        QTcpSocket *socket = guardedSocket.data();
        if(!current(socket, connectionGeneration)) return;
        if(code == QAbstractSocket::RemoteHostClosedError)
        {
            beginRemoteRead(socket, connectionGeneration,
                            QStringLiteral("transport.peer_closed: 回环对端已关闭，已排空收到的完整帧"));
            return;
        }
        failSocket(socket, connectionGeneration,
                   QStringLiteral("transport.socket: loopback socket错误：%1").arg(socket->errorString()));
    };
    // Qt5.14没有errorOccurred；按本机构建版本选择信号，保持旧Windows兼容。
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket, &QAbstractSocket::errorOccurred, this, socketError);
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, socketError);
#endif
    connect(socket, &QIODevice::readyRead, this,
            [this, guardedSocket, connectionGeneration]()
    {
        if(!current(guardedSocket.data(), connectionGeneration)) return;
        readAvailable(guardedSocket.data(), connectionGeneration);
    });
    connect(socket, &QIODevice::bytesWritten, this,
            [this, guardedSocket, connectionGeneration](qint64 bytes)
    {
        QTcpSocket *socket = guardedSocket.data();
        if(!current(socket, connectionGeneration)) return;
        if(bytes > 0)
        {
            traceTiming("socket.bytesWritten", bytes);
            if(!current(socket, connectionGeneration)) return;
            m_writeProgressMs = pendingWriteBytes() > 0 ? m_clock.elapsed() : -1;
            emit bytesDelivered(bytes, connectionGeneration);
            if(!current(socket, connectionGeneration)) return;
        }
        scheduleWrite(socket, connectionGeneration);
    });
    connect(m_tick, &QTimer::timeout, this, [this, guardedSocket, connectionGeneration]()
    {
        if(!current(guardedSocket.data(), connectionGeneration)) return;
        tick(guardedSocket.data(), connectionGeneration);
    });
    m_tick->start();
    // 数值回环地址、NoProxy；不查DNS、不读配置、不接受外部IP，不自动重连。
    socket->connectToHost(QHostAddress(QHostAddress::LocalHost), port);
    if(!current(socket, connectionGeneration))
        return rejected(error, QStringLiteral("transport.connect: 回环连接启动失败或已取消"));
    return true;
}

void ReadOnlyTcpTransport::close()
{
    clearConnection(QString(), true);
}

bool ReadOnlyTcpTransport::queueQuery(ReadOnlyQuery query, QString *error)
{
    if(error) error->clear();
    if(!isConnected())
        return rejected(error, QStringLiteral("transport.not_connected: 尚未建立回环连接"));

    QByteArray payload;
    switch(query)
    {
    case ReadOnlyQuery::Status:
    case ReadOnlyQuery::Alarm:
    case ReadOnlyQuery::Consumables:
        payload = queryPayload(query);
        break;
    default:
        return rejected(error, QStringLiteral("transport.query: 只允许Status/Alarm/Consumables只读查询"));
    }
    if(payload != "Qj" && payload != "Qk" && payload != "QH")
        return rejected(error, QStringLiteral("transport.query: 查询payload不在本轮只读白名单"));
    QString encodingError;
    const QList<QByteArray> frames = encodeMessage(payload, Direction::HostToDevice, &encodingError);
    if(!encodingError.isEmpty() || frames.isEmpty())
        return rejected(error, QStringLiteral("transport.encode: %1").arg(encodingError));
    qint64 bytes = 0;
    for(const QByteArray &frame : frames) bytes += frame.size();
    const qint64 previousPending = pendingWriteBytes();
    if(previousPending + bytes > m_maxQueuedBytes)
        return rejected(error, QStringLiteral("transport.backpressure: 待写%1字节+新帧%2字节超过本地%3字节预算；整批拒收")
                        .arg(previousPending).arg(bytes).arg(m_maxQueuedBytes));

    QTcpSocket *socket = m_socket.data();
    const quint64 connectionGeneration = m_generation;
    for(const QByteArray &frame : frames) m_frames.enqueue(frame);
    m_applicationPendingBytes += bytes;
    traceTiming("queue.accepted", bytes, frames.size());
    if(!current(socket, connectionGeneration))
        return rejected(error, QStringLiteral("transport.cancelled: 诊断通知期间连接已关闭或改变"));
    if(previousPending == 0) m_writeProgressMs = m_clock.elapsed();
    for(const QByteArray &frame : frames)
    {
        emit wireQueued(frame, connectionGeneration);
        if(!current(socket, connectionGeneration))
            return rejected(error, QStringLiteral("transport.cancelled: 帧入队通知期间连接已关闭或改变"));
    }
    // 不在queueQuery中同步泵送：连续调用也始终看到整批有界的待写数量。
    scheduleWrite(socket, connectionGeneration);
    return true;
}

bool ReadOnlyTcpTransport::isConnected() const
{
    return m_active && !m_peerEof && m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

qint64 ReadOnlyTcpTransport::pendingWriteBytes() const
{
    return m_applicationPendingBytes + (m_socket ? qMax(qint64(0), m_socket->bytesToWrite()) : 0);
}

quint64 ReadOnlyTcpTransport::generation() const { return m_generation; }
int ReadOnlyTcpTransport::bufferedBytes() const { return m_decoder.bufferedBytes(); }
int ReadOnlyTcpTransport::assemblyBytes() const { return m_decoder.assemblyBytes(); }

void ReadOnlyTcpTransport::failSocket(QTcpSocket *socket, quint64 connectionGeneration,
                                      const QString &message)
{
    if(current(socket, connectionGeneration)) clearConnection(message, true);
}

void ReadOnlyTcpTransport::beginRemoteRead(QTcpSocket *socket, quint64 connectionGeneration,
                                         const QString &message)
{
    if(!current(socket, connectionGeneration)) return;
    // EOF不是“Qt缓冲已读完”。立即停发，但保留socket内已收到的字节；
    // 若先emit终止错误，接收者可能close()并丢掉剩余完整帧。
    m_peerEof = true;
    if(!message.isEmpty()) m_peerCloseError = message;
    readAvailable(socket, connectionGeneration);
}

void ReadOnlyTcpTransport::publishBatch(const DecodeBatch &batch, QTcpSocket *socket,
                                      quint64 connectionGeneration)
{
    // 坏帧/半帧过期由共享codec丢弃并允许后续帧恢复；socket I/O错误才终止连接。
    for(const QString &error : batch.errors)
    {
        if(!current(socket, connectionGeneration)) return;
        emit errorOccurred(error, connectionGeneration);
    }
    if(batch.duplicatePackets && current(socket, connectionGeneration))
        emit errorOccurred(QStringLiteral("packet.duplicate: 跳过%1个相同在途分片")
                           .arg(batch.duplicatePackets), connectionGeneration);
    for(const QByteArray &message : batch.messages)
    {
        if(!current(socket, connectionGeneration)) return;
        emit messageReceived(message, connectionGeneration);
    }
}

void ReadOnlyTcpTransport::scheduleRead(QTcpSocket *socket, quint64 connectionGeneration)
{
    if(!current(socket, connectionGeneration) || m_readScheduled) return;
    m_readScheduled = true;
    const QPointer<QTcpSocket> guardedSocket(socket);
    QTimer::singleShot(0, this, [this, guardedSocket, connectionGeneration]()
    {
        if(!current(guardedSocket.data(), connectionGeneration)) return;
        m_readScheduled = false;
        readAvailable(guardedSocket.data(), connectionGeneration);
    });
}

void ReadOnlyTcpTransport::readAvailable(QTcpSocket *socket, quint64 connectionGeneration)
{
    if(!current(socket, connectionGeneration) || m_readDraining) return;
    m_readDraining = true;
    int budget = kPerTurnBytes;
    while(current(socket, connectionGeneration) && budget > 0 && socket->bytesAvailable() > 0)
    {
        const QByteArray bytes = socket->read(qMin(kReadChunkBytes, budget));
        if(bytes.isEmpty()) break;
        budget -= bytes.size();
        traceTiming("read.actual", bytes.size(), socket->bytesAvailable());
        if(!current(socket, connectionGeneration)) return;
        emit wireReceived(bytes, connectionGeneration);
        if(!current(socket, connectionGeneration)) return;
        const DecodeBatch decoded = m_decoder.feed(bytes, m_clock.elapsed());
        publishBatch(decoded, socket, connectionGeneration);
    }
    if(!current(socket, connectionGeneration)) return;
    m_readDraining = false;
    // readyRead不承诺为已留在Qt缓冲区的字节再发一次，主动安排下一轮有界读取。
    if(socket->bytesAvailable() > 0) scheduleRead(socket, connectionGeneration);
    else if(m_peerEof)
    {
        QString reason = m_peerCloseError;
        if(m_decoder.bufferedBytes() || m_decoder.assemblyBytes())
        {
            if(!reason.isEmpty()) reason += QStringLiteral("; ");
            reason += QStringLiteral("transport.eof_incomplete: EOF残留半帧%1字节、未完成组包%2字节，已丢弃而未发布")
                    .arg(m_decoder.bufferedBytes()).arg(m_decoder.assemblyBytes());
        }
        clearConnection(reason, true);
    }
}

void ReadOnlyTcpTransport::scheduleWrite(QTcpSocket *socket, quint64 connectionGeneration)
{
    if(!current(socket, connectionGeneration) || m_peerEof || m_writeScheduled || m_frames.isEmpty()) return;
    m_writeScheduled = true;
    traceTiming("write.scheduled");
    if(!current(socket, connectionGeneration)) return;
    const QPointer<QTcpSocket> guardedSocket(socket);
    QTimer::singleShot(0, this, [this, guardedSocket, connectionGeneration]()
    {
        if(!current(guardedSocket.data(), connectionGeneration)) return;
        m_writeScheduled = false;
        traceTiming("write.callback");
        if(!current(guardedSocket.data(), connectionGeneration)) return;
        pumpWrite(guardedSocket.data(), connectionGeneration);
    });
}

void ReadOnlyTcpTransport::pumpWrite(QTcpSocket *socket, quint64 connectionGeneration)
{
    if(!current(socket, connectionGeneration) || !isConnected() || m_writeDraining) return;
    m_writeDraining = true;
    traceTiming("pump.enter");
    if(!current(socket, connectionGeneration)) return;
    qint64 budget = kPerTurnBytes;
    bool progress = false;
    while(current(socket, connectionGeneration) && !m_frames.isEmpty() && budget > 0)
    {
        const QByteArray &frame = m_frames.head();
        const qint64 remaining = frame.size() - m_frameOffset;
        const qint64 socketRoom = m_maxQueuedBytes - socket->bytesToWrite();
        const qint64 amount = std::min({remaining, qint64(m_writeQuantum), budget, socketRoom});
        if(amount <= 0) break;
        // 本地write接受、bytesWritten进展和peer实际读取分别取证，不能互当ACK。
        traceTiming("write.begin", amount, remaining);
        if(!current(socket, connectionGeneration)) return;
        const qint64 written = socket->write(frame.constData() + m_frameOffset, amount);
        if(!current(socket, connectionGeneration)) return;
        traceTiming("write.returned", written, amount);
        if(!current(socket, connectionGeneration)) return;
        if(m_peerEof)
        {
            m_writeDraining = false;
            return;
        }
        if(written < 0 || written > amount)
        {
            failSocket(socket, connectionGeneration,
                       QStringLiteral("transport.write: 回环socket写入失败：%1").arg(socket->errorString()));
            return;
        }
        // 0/短写不丢弃尾字节。0时等bytesWritten/20ms计时器再尝试，不忙等。
        if(written == 0) break;
        m_frameOffset += int(written);
        m_applicationPendingBytes -= written;
        budget -= written;
        progress = true;
        if(m_frameOffset == frame.size())
        {
            m_frames.dequeue();
            m_frameOffset = 0;
        }
    }
    if(!current(socket, connectionGeneration)) return;
    m_writeDraining = false;
    traceTiming("pump.exit", progress ? 1 : 0);
    if(!current(socket, connectionGeneration)) return;
    if(pendingWriteBytes() == 0) m_writeProgressMs = -1;
    // quantum只是本层有意限制每次write的大小，不冒称操作系统发生了真实短写。
    if(progress && !m_frames.isEmpty()) scheduleWrite(socket, connectionGeneration);
}

void ReadOnlyTcpTransport::tick(QTcpSocket *socket, quint64 connectionGeneration)
{
    if(!current(socket, connectionGeneration)) return;
    const qint64 now = m_clock.elapsed();
    traceTiming("timer.tick", now, m_writeProgressMs);
    if(!current(socket, connectionGeneration)) return;
    if(m_peerEof)
    {
        readAvailable(socket, connectionGeneration);
        return;
    }
    if(m_connectStartedMs >= 0 && now - m_connectStartedMs >= m_connectTimeoutMs)
    {
        failSocket(socket, connectionGeneration,
                   QStringLiteral("transport.connect_timeout: 回环连接超过本地%1ms预算")
                   .arg(m_connectTimeoutMs));
        return;
    }
    if(pendingWriteBytes() > 0 && m_writeProgressMs >= 0 &&
       now - m_writeProgressMs >= m_connectTimeoutMs)
    {
        failSocket(socket, connectionGeneration,
                   QStringLiteral("transport.write_timeout: 待写字节%1ms无socket写出进展，已丢弃队列")
                   .arg(m_connectTimeoutMs));
        return;
    }
    publishBatch(m_decoder.expire(now), socket, connectionGeneration);
    if(!current(socket, connectionGeneration)) return;
    if(!m_frames.isEmpty()) scheduleWrite(socket, connectionGeneration);
}

} // namespace Sn100Protocol
