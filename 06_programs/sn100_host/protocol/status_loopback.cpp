#include "status_loopback.h"
#include "read_only_client.h"
#include <QDebug>
#include <QHostAddress>
#include <QMutexLocker>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace Sn100Protocol
{
namespace
{
constexpr int kWireEvents = 128;
constexpr int kWireBytes = 65536;
constexpr int kBatchEvents = 16;
constexpr int kBatchBytes = 16384;
constexpr int kReadBytes = 4096;
} // namespace

StatusLoopbackWorker::StatusLoopbackWorker(quint64 runId, QObject *parent) : QObject(parent)
{
    state_.runId = runId;
}

void StatusLoopbackWorker::registerTypes()
{
    qRegisterMetaType<LoopbackStatusState>("Sn100Protocol::LoopbackStatusState");
    qRegisterMetaType<LoopbackWireEvent>("Sn100Protocol::LoopbackWireEvent");
    qRegisterMetaType<LoopbackWireBatch>("Sn100Protocol::LoopbackWireBatch");
}

bool StatusLoopbackWorker::onWorkerThread()
{
    if (QThread::currentThread() == thread())
    {
        return true;
    }
    emit error(QStringLiteral("loopback.thread: 操作必须在worker线程执行"));
    return false;
}

void StatusLoopbackWorker::start()
{
    if (!onWorkerThread())
    {
        return;
    }
    if (state_.running || stopping_)
    {
        emit error(QStringLiteral("loopback.start: worker已经运行或已关闭"));
        return;
    }
    clock_.start();
    state_.running = true;
    state_.detail = QStringLiteral("本机模拟；真实仪器未知，设备动作禁用");
    server_ = new QTcpServer(this);
    server_->setProxy(QNetworkProxy::NoProxy);
    server_->setMaxPendingConnections(1);
    connect(server_, &QTcpServer::newConnection, this, &StatusLoopbackWorker::acceptPeer);
    if (!server_->listen(QHostAddress::LocalHost, 0))
    {
        fail(
            QStringLiteral("loopback.listen: 无法创建本轮127.0.0.1临时端点：%1").arg(server_->errorString()));
        return;
    }
    state_.port = server_->serverPort();
    client_ = new ReadOnlyClient(this, 120, 1);
    poll_ = new QTimer(this);
    poll_->setInterval(1000); // 沿用既有soak只读查询节拍，不排队叠加请求。
    connect(poll_, &QTimer::timeout, this, &StatusLoopbackWorker::queryStatus);
    connect(client_, &ReadOnlyClient::connected, this,
            [this](quint64 generation)
            {
                if (stopping_ || generation != client_->generation())
                {
                    return;
                }
                state_.connectionGeneration = generation;
                state_.connected = true;
                state_.detail = QStringLiteral("本机模拟已连接；尚非仪器状态或ACK");
                poll_->start();
                queryStatus();
                if (!startedReported_)
                {
                    startedReported_ = true;
                    emit started();
                }
            });
    connect(client_, &ReadOnlyClient::disconnected, this,
            [this](quint64 generation)
            {
                if (generation != state_.connectionGeneration)
                {
                    return;
                }
                lastDisconnected_ = generation;
                poll_->stop();
                clearStatus();
                syncClientState();
                state_.connectionGeneration = client_->generation();
                publishState();
            });
    connect(client_, &ReadOnlyClient::queryQueued, this,
            [this](ReadOnlyQuery query, int attempt, quint64 generation)
            {
                if (query != ReadOnlyQuery::Status || generation != client_->generation() || stopping_)
                {
                    return;
                }
                state_.connectionGeneration = generation;
                state_.attempt = attempt;
                ++state_.queriesQueued;
                syncClientState();
                publishState();
            });
    connect(client_, &ReadOnlyClient::requestFinished, this,
            [this](ReadOnlyQuery query, bool success, const QString &detail, quint64 generation)
            {
                // close()先发旧代次的取消终态，再发disconnected；即使停止中也保留该结果。
                if (query != ReadOnlyQuery::Status || generation != state_.connectionGeneration)
                {
                    return;
                }
                state_.hasRequestResult = true;
                state_.lastRequestSuccess = success;
                state_.detail = detail;
                if (success)
                {
                    ++state_.requestsSucceeded;
                }
                else
                {
                    ++state_.requestsFailed;
                }
                state_.attempt = 0;
                if (!client_->hasFreshStatus())
                {
                    clearStatus();
                }
                syncClientState();
                publishState();
            });
    connect(client_, &ReadOnlyClient::notice, this,
            [this](const QString &message, quint64 generation)
            {
                if (stopping_ || (generation != state_.connectionGeneration &&
                                  (state_.connected || generation != lastDisconnected_)))
                {
                    return;
                }
                state_.detail = message; // 只留最后一条，不无限积累诊断列表。
                syncClientState();
                publishState();
            });
    auto *transport = client_->findChild<ReadOnlyTcpTransport *>();
    if (!transport)
    {
        fail(QStringLiteral("loopback.client: 既有client缺少所属Transport"));
        return;
    }
    connect(transport, &ReadOnlyTcpTransport::wireReceived, this,
            [this](const QByteArray &bytes, quint64 generation)
            {
                if (!stopping_ && generation == client_->generation())
                {
                    recordWire("RX", bytes, generation);
                }
            });
    // client先处理该完整消息。再次复用同一纯解析器，只为保留原Qj字节和typed值，
    // 不另造字段规则，也不把晚到观测改成旧请求成功。
    connect(transport, &ReadOnlyTcpTransport::messageReceived, this,
            [this](const QByteArray &message, quint64 generation)
            {
                if (stopping_ || generation != client_->generation() || !client_->hasFreshStatus())
                {
                    return;
                }
                StatusSnapshot snapshot;
                QString error;
                if (!message.startsWith("Qj") || !parseStatus(message, &snapshot, &error))
                {
                    return;
                }
                state_.snapshot = snapshot;
                state_.rawQj = message;
                state_.hasSnapshot = true;
                state_.statusAtMs = clock_.elapsed();
                ++state_.observations;
                syncClientState();
                publishState();
            });
    openConnection();
}

void StatusLoopbackWorker::clearStatus()
{
    state_.fresh = false;
    state_.hasSnapshot = false;
    state_.snapshot = StatusSnapshot();
    state_.rawQj.clear();
    state_.statusAtMs = -1;
}

void StatusLoopbackWorker::syncClientState()
{
    if (!client_)
    {
        return;
    }
    state_.connected = client_->linkState() != QStringLiteral("disconnected") &&
                       client_->linkState() != QStringLiteral("connecting");
    state_.pending = client_->requestPending();
    state_.fresh = state_.hasSnapshot && client_->hasFreshStatus();
    state_.linkState = client_->linkState();
}

void StatusLoopbackWorker::openConnection()
{
    if (stopping_ || !state_.running || !server_ || !server_->isListening())
    {
        return;
    }
    if (peer_)
    {
        QObject::disconnect(peer_, nullptr, this, nullptr);
        peer_->abort();
        peer_->deleteLater();
        peer_.clear();
    }
    peerDecoder_.reset();
    clearStatus();
    state_.error.clear();
    QString error;
    const bool accepted = client_->connectLoopback(server_->serverPort(), &error);
    state_.connectionGeneration = client_->generation();
    lastDisconnected_ = 0;
    syncClientState();
    if (!accepted)
    {
        fail(error);
    }
    else
    {
        publishState();
    }
}

void StatusLoopbackWorker::reconnect()
{
    if (!onWorkerThread())
    {
        return;
    }
    if (!state_.running || stopping_)
    {
        emit error(QStringLiteral("loopback.reconnect: 本轮端点未运行"));
        return;
    }
    openConnection(); // 仅重连仍由本worker持有的serverPort，不接受外部端点。
}

void StatusLoopbackWorker::acceptPeer()
{
    while (server_->hasPendingConnections())
    {
        QTcpSocket *socket = server_->nextPendingConnection();
        if (!socket)
        {
            continue;
        }
        if (stopping_ || peer_)
        {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        peer_ = socket;
        socket->setProxy(QNetworkProxy::NoProxy);
        socket->setReadBufferSize(8192);
        const quint64 generation = client_->generation();
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket, generation]() { readPeer(socket, generation); });
        connect(socket, &QTcpSocket::disconnected, this,
                [this, socket]()
                {
                    if (peer_ == socket)
                    {
                        peer_.clear();
                    }
                    socket->deleteLater();
                });
    }
}

void StatusLoopbackWorker::readPeer(QTcpSocket *peer, quint64 generation)
{
    if (stopping_ || peer_ != peer || generation != client_->generation())
    {
        return;
    }
    const QByteArray bytes = peer->read(kReadBytes);
    if (bytes.isEmpty())
    {
        return;
    }
    recordWire("TX", bytes, generation); // 本机对端实际收到，区别于wireQueued计划TX。
    if (stopping_)
    {
        return;
    }
    const DecodeBatch batch = peerDecoder_.feed(bytes, clock_.elapsed());
    if (!batch.errors.isEmpty())
    {
        fail(QStringLiteral("loopback.peer_decode: %1").arg(batch.errors.join(';')));
        return;
    }
    for (const QByteArray &message : batch.messages)
    {
        if (message != "Qj")
        {
            fail(QStringLiteral("loopback.command: 自建模拟端只接受Qj"));
            return;
        }
        replyStatus(peer, generation);
        if (stopping_ || peer_ != peer || generation != client_->generation())
        {
            return;
        }
    }
    if (peer->bytesAvailable() > 0)
    {
        QPointer<QTcpSocket> guarded(peer);
        QTimer::singleShot(0, this,
                           [this, guarded, generation]()
                           {
                               if (guarded)
                               {
                                   readPeer(guarded, generation);
                               }
                           });
    }
}

bool StatusLoopbackWorker::writePeer(QTcpSocket *peer, quint64 generation, const QByteArray &bytes)
{
    if (stopping_ || peer_ != peer || generation != client_->generation())
    {
        return false;
    }
    if (bytes.size() > kReadBytes || peer->bytesToWrite() + bytes.size() > 8192)
    {
        fail(QStringLiteral("loopback.peer_write: 本地模拟写队列超过8192字节预算"));
        return false;
    }
    if (peer->write(bytes) != bytes.size())
    {
        fail(QStringLiteral("loopback.peer_write: 本地模拟应答入队失败"));
        return false;
    }
    return true;
}

void StatusLoopbackWorker::replyStatus(QTcpSocket *peer, quint64 generation)
{
    // 与既有匿名client金样本同一业务值；SIMULATED绝不是现场型号或就绪证明。
    for (const QByteArray &frame : encodeMessage("Qj[4][SIMULATED][2][1][0][0]", Direction::DeviceToHost))
    {
        if (!writePeer(peer, generation, frame))
        {
            return;
        }
    }
}

void StatusLoopbackWorker::queryStatus()
{
    if (stopping_ || !client_ || !state_.running || client_->requestPending())
    {
        return;
    }
    if (client_->linkState() == "disconnected" || client_->linkState() == "connecting")
    {
        return;
    }
    QString error;
    if (!client_->request(ReadOnlyQuery::Status, &error))
    {
        state_.detail = error;
        syncClientState();
        publishState();
    }
}

void StatusLoopbackWorker::publishState()
{
    if (stopping_)
    {
        return;
    }
    stateDirty_ = true;
    if (stateScheduled_ || statePending_)
    {
        return;
    }
    stateScheduled_ = true;
    QTimer::singleShot(0, this,
                       [this]()
                       {
                           stateScheduled_ = false;
                           if (stopping_ || statePending_ || !stateDirty_)
                           {
                               return;
                           }
                           stateDirty_ = false;
                           statePending_ = true;
                           state_.elapsedMs = clock_.elapsed();
                           pendingStateRevision_ = ++state_.revision;
                           emit stateReady(state_, pendingStateRevision_);
                       });
}

void StatusLoopbackWorker::acknowledgeState(quint64 runId, quint64 revision)
{
    if (!onWorkerThread() || stopping_ || runId != state_.runId || revision != pendingStateRevision_)
    {
        return;
    }
    statePending_ = false;
    if (stateDirty_)
    {
        publishState();
    }
}

void StatusLoopbackWorker::recordWire(const QString &direction, const QByteArray &bytes, quint64 generation)
{
    if (bytes.isEmpty())
    {
        return;
    }
    LoopbackWireEvent event;
    event.runId = state_.runId;
    event.connectionGeneration = generation;
    event.sequence = ++state_.wireEvents;
    event.elapsedMs = clock_.elapsed();
    event.direction = direction;
    event.bytes = bytes;
    state_.wireBytes += quint64(bytes.size());
    // 每条<=4096字节。先留下实际wire证据：exec退出后GUI不再消费ack，
    // shutdownAndWait仍可直接闭合worker，不能把未转发的GUI批次伪称完整日志。
    qInfo().noquote() << "STATUS_LOOPBACK_WIRE run=" << event.runId << "generation=" << generation
                      << "sequence=" << event.sequence << "elapsed_ms=" << event.elapsedMs << direction
                      << "HEX=" << bytes.toHex(' ');
    if (wireQueue_.size() >= kWireEvents || queuedWireBytes_ + bytes.size() > kWireBytes)
    {
        state_.traceOverflow = true;
        fail(QStringLiteral("loopback.trace_budget: GUI未消费的wire超过128条/65536字节，已停止模拟"));
        return;
    }
    wireQueue_.enqueue(event);
    queuedWireBytes_ += bytes.size();
    flushWire();
}

void StatusLoopbackWorker::flushWire()
{
    if (wirePending_ || wireQueue_.isEmpty())
    {
        return;
    }
    LoopbackWireBatch batch;
    int bytes = 0;
    while (!wireQueue_.isEmpty() && batch.size() < kBatchEvents &&
           bytes + wireQueue_.head().bytes.size() <= kBatchBytes)
    {
        LoopbackWireEvent event = wireQueue_.dequeue();
        bytes += event.bytes.size();
        queuedWireBytes_ -= event.bytes.size();
        batch.append(event);
    }
    wirePending_ = true;
    emit wireBatchReady(state_.runId, ++wireBatchId_, batch);
}

void StatusLoopbackWorker::acknowledgeWire(quint64 runId, quint64 batchId)
{
    if (!onWorkerThread() || stopping_ || runId != state_.runId || batchId != wireBatchId_)
    {
        return;
    }
    wirePending_ = false;
    flushWire();
}

void StatusLoopbackWorker::fail(const QString &message)
{
    if (stopping_)
    {
        return;
    }
    state_.error = message;
    state_.detail = message;
    emit error(message);
    stop();
}

void StatusLoopbackWorker::stop()
{
    if (!onWorkerThread() || stopping_)
    {
        return;
    }
    stopping_ = true; // 先停止新请求；client.close保留取消终态，不等GUI ack。
    if (poll_)
    {
        poll_->stop();
    }
    if (client_)
    {
        client_->close();
    }
    if (peer_)
    {
        QObject::disconnect(peer_, nullptr, this, nullptr);
        peer_->abort();
        peer_->deleteLater();
        peer_.clear();
    }
    if (server_)
    {
        server_->close();
    }
    peerDecoder_.reset();
    // 最多剩128条/64KiB，停止时一次性有界交付；原始实际字节此前已逐条qInfo。
    if (!wireQueue_.isEmpty())
    {
        LoopbackWireBatch last;
        while (!wireQueue_.isEmpty())
        {
            last.append(wireQueue_.dequeue());
        }
        emit wireBatchReady(state_.runId, 0, last);
    }
    queuedWireBytes_ = 0;
    clearStatus();
    state_.running = state_.connected = state_.pending = false;
    state_.attempt = 0;
    state_.linkState = state_.error.isEmpty() ? QStringLiteral("stopped") : QStringLiteral("error");
    if (!state_.error.isEmpty())
    {
        state_.detail = state_.error;
    }
    state_.elapsedMs = clock_.isValid() ? clock_.elapsed() : 0;
    ++state_.revision;
    emit stateReady(state_, 0);
    qInfo() << "STATUS_LOOPBACK_WORKER_STOPPED" << state_.runId << "wire_events=" << state_.wireEvents
            << "wire_bytes=" << state_.wireBytes;
    emit stopped();
}

StatusLoopbackController::StatusLoopbackController(QObject *parent) : QObject(parent)
{
    StatusLoopbackWorker::registerTypes();
    thread_.setObjectName(QStringLiteral("O09LoopbackStatus"));
}

StatusLoopbackController::~StatusLoopbackController()
{
    shutdownAndWait();
}
LoopbackStatusState StatusLoopbackController::current() const
{
    return state_;
}
bool StatusLoopbackController::isRunning() const
{
    return thread_.isRunning();
}
bool StatusLoopbackController::isStopping() const
{
    return stopping_;
}

void StatusLoopbackController::start()
{
    if (QThread::currentThread() != thread())
    {
        emit error(QStringLiteral("loopback.controller_thread: start必须在控制器线程执行"));
        return;
    }
    if (thread_.isRunning() || stopping_)
    {
        emit error(QStringLiteral("loopback.busy: 本机模拟已经运行或正在停止"));
        return;
    }
    const quint64 run = ++runId_;
    state_ = LoopbackStatusState();
    state_.runId = run;
    state_.running = true;
    state_.linkState = QStringLiteral("starting");
    {
        QMutexLocker lock(&finalMutex_);
        finalState_ = LoopbackStatusState();
    }
    auto *worker = new StatusLoopbackWorker(run);
    worker_ = worker;
    worker->moveToThread(&thread_);
    connect(&thread_, &QThread::started, worker, &StatusLoopbackWorker::start);
    connect(&thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &StatusLoopbackWorker::stateReady, this,
            [this, worker, run](LoopbackStatusState state, quint64 revision)
            {
                if (run != runId_ || state.revision < state_.revision)
                {
                    return;
                }
                state_ = state;
                emit snapshotChanged(state_);
                if (revision && worker_ == worker)
                {
                    QMetaObject::invokeMethod(worker, "acknowledgeState", Qt::QueuedConnection,
                                              Q_ARG(quint64, run), Q_ARG(quint64, revision));
                }
            });
    connect(worker, &StatusLoopbackWorker::wireBatchReady, this,
            [this, worker, run](quint64, quint64 batchId, const LoopbackWireBatch &batch)
            {
                if (run != runId_)
                {
                    return;
                }
                for (const LoopbackWireEvent &event : batch)
                {
                    emit wireObserved(event);
                }
                if (batchId && worker_ == worker)
                {
                    QMetaObject::invokeMethod(worker, "acknowledgeWire", Qt::QueuedConnection,
                                              Q_ARG(quint64, run), Q_ARG(quint64, batchId));
                }
            });
    connect(worker, &StatusLoopbackWorker::started, this,
            [this, run]()
            {
                if (run == runId_)
                {
                    emit started();
                }
            });
    connect(worker, &StatusLoopbackWorker::error, this,
            [this, run](const QString &message)
            {
                if (run == runId_)
                {
                    emit error(message);
                }
            });
    // quit是线程安全入口，不能依赖已经退出的GUI事件循环消费queued quit。
    connect(worker, &StatusLoopbackWorker::stopped, &thread_, &QThread::quit, Qt::DirectConnection);
    connect(
        worker, &StatusLoopbackWorker::stateReady, this,
        [this](LoopbackStatusState state, quint64 revision)
        {
            if (revision != 0)
            {
                return;
            }
            // 仅复制受锁保护的终态值，不触GUI；供exec返回后的同步回收读取。
            QMutexLocker lock(&finalMutex_);
            finalState_ = state;
        },
        Qt::DirectConnection);
    QObject::disconnect(finishedConnection_);
    finishedConnection_ = connect(&thread_, &QThread::finished, this,
                                  [this, run]()
                                  {
                                      if (run != runId_)
                                      {
                                          return;
                                      }
                                      worker_.clear();
                                      stopping_ = false;
                                      emit stopped();
                                  });
    thread_.start();
    emit snapshotChanged(state_);
}

void StatusLoopbackController::reconnect()
{
    if (!thread_.isRunning() || !worker_ || stopping_)
    {
        emit error(QStringLiteral("loopback.reconnect: 本机模拟未运行"));
        return;
    }
    QMetaObject::invokeMethod(worker_, "reconnect", Qt::QueuedConnection);
}

void StatusLoopbackController::stop()
{
    if (!thread_.isRunning() || !worker_ || stopping_)
    {
        return;
    }
    stopping_ = true;
    QMetaObject::invokeMethod(worker_, "stop", Qt::QueuedConnection);
}

bool StatusLoopbackController::shutdownAndWait()
{
    if (QThread::currentThread() == &thread_)
    {
        return false;
    }
    if (thread_.isRunning())
    {
        if (worker_)
        {
            QMetaObject::invokeMethod(worker_, "stop", Qt::QueuedConnection);
        }
        thread_.wait();
    }
    worker_.clear();
    ++runId_; // exec结束后的旧queued批次/快照不能在稍后恢复事件循环时复活状态。
    stopping_ = false;
    {
        QMutexLocker lock(&finalMutex_);
        if (finalState_.runId == state_.runId)
        {
            state_ = finalState_;
        }
    }
    state_.running = state_.connected = state_.pending = state_.fresh = state_.hasSnapshot = false;
    state_.rawQj.clear();
    state_.snapshot = StatusSnapshot();
    state_.linkState = state_.error.isEmpty() ? QStringLiteral("stopped") : QStringLiteral("error");
    return !thread_.isRunning();
}
} // namespace Sn100Protocol
