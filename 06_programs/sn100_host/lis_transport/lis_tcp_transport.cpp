#include "lis_tcp_transport.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

namespace Sn100Lis
{
namespace
{
// 本地资源预算，不冒作LIS协议上限；超限明确失败，不丢包后继续报成功。
constexpr int MaximumPeers = 16, MaximumQueuedMessages = 64, MaximumReceivedMessages = 256;
constexpr qint64 MaximumQueueBytes = 4 * 1048576;
}
struct LisTcpTransport::Peer
{
    quint64 id = 0, generation = 0, writeId = 0;
    QTcpSocket *socket = nullptr;
    MllpCodec codec;
    bool client = false;
    QByteArray writing;
    qint64 submitted = 0, confirmed = 0, writeStarted = 0;
};

LisTcpTransport::LisTcpTransport(QObject *parent) : QObject(parent), server_(new QTcpServer(this))
{
    clock_.start();
    server_->setMaxPendingConnections(MaximumPeers);
    cycle_.setInterval(200);
    reconnectTimer_.setSingleShot(true);
    connectDeadline_.setSingleShot(true);
    reconnectTimer_.setTimerType(Qt::PreciseTimer);
    connectDeadline_.setTimerType(Qt::PreciseTimer);
    connect(&cycle_,&QTimer::timeout,this,&LisTcpTransport::cycle);
    connect(&reconnectTimer_,&QTimer::timeout,this,&LisTcpTransport::beginClient);
    connect(server_,&QTcpServer::newConnection,this,&LisTcpTransport::acceptClients);
    connect(&connectDeadline_,&QTimer::timeout,this,[this]()
    {
        const auto ids = peers_.keys();
        for (quint64 id : ids)
            if (peers_.contains(id) && peers_.value(id)->client
                && peers_.value(id)->socket->state()!=QAbstractSocket::ConnectedState)
                losePeer(id,tr("TCP连接在1500毫秒内尚未建立。"));
    });
}

LisTcpTransport::~LisTcpTransport()
{
    destroying_=true;
    stop(tr("LIS传输对象关闭。"));
}

bool LisTcpTransport::checkThread(QString *error) const
{
    if (QThread::currentThread()==thread()) return true;
    if (error) *error=tr("LIS传输必须在创建它的线程调用。 ");
    return false;
}

bool LisTcpTransport::configure(const LisSettings &settings, bool reconnect,
    int reconnectMilliseconds, QString *detail)
{
    if (detail) detail->clear();
    if (!checkThread(detail)) return false;
    if (stopping_ || changing_ || destroying_)
    {
        if (detail) *detail=tr("LIS正在停止或更换配置，请等待当前操作结束。 ");
        return false;
    }
    changing_=true;
    configurationToken_.clear();
    stop(tr("已提交设置改变；旧连接和未完成发送已结束，等待重新核对端点。"));
    const auto reject = [this,detail](const QString &message)
    {
        if (detail) *detail=message;
        changing_=false;
        state_=QStringLiteral("Unconfigured"); publish();
        return false;
    };
    if (settings.type!=QStringLiteral("tcp"))
        return reject(tr("当前不是TCP配置；串口通信仍待单独接入。"));
    if (settings.simulation)
        return reject(tr("原配置要求模拟通信；本TCP入口不启动模拟心跳或模拟结果。"));
    bool portValid=false;
    const uint port=settings.port.toUInt(&portValid);
    if (!portValid || !QRegularExpression(QStringLiteral("^[0-9]{1,5}$")).match(settings.port).hasMatch()
        || port<1 || port>65535 || reconnectMilliseconds<0)
        return reject(tr("TCP端口须为1至65535，重连间隔须为非负毫秒数；原设置未改写。"));
    // 原openComm只有client选择客户端，其他值（包括空值）选择服务端。
    const QString mode=settings.tcpType==QStringLiteral("client") ? QStringLiteral("client") : QStringLiteral("server");
    if (mode==QStringLiteral("client") && (settings.ip.isEmpty() || settings.ip.size()>1024
        || settings.ip.contains(QChar::Null) || settings.ip.contains(QChar('\n')) || settings.ip.contains(QChar('\r'))))
        return reject(tr("TCP客户端地址为空或格式无效；未选择替代地址。"));
    QString code=settings.utf16 ? QStringLiteral("true") : QStringLiteral("false");
    if (settings.originalProfile)
    {
        code=QStringLiteral("false");
        int matches=0;
        for (auto it=settings.originalValues.cbegin();it!=settings.originalValues.cend();++it)
            if (it.key().compare(QStringLiteral("lis/lisCode"),Qt::CaseInsensitive)==0)
            { code=it.value().toString(); ++matches; }
        if (matches>1) return reject(tr("原LIS编码键重复，未猜测采用其中一项。"));
    }
    utf16_=code!=QStringLiteral("false");
    host_=settings.ip; port_=quint16(port); mode_=mode;
    reconnect_=reconnect;
    // 原main把小于2000的重连间隔钳到2000；仅用于socket重连，不重发业务消息。
    reconnectMilliseconds_=qMax(2000,reconnectMilliseconds);
    QByteArray identity;
    QDataStream stream(&identity,QIODevice::WriteOnly);
    stream << settings.type << settings.tcpType << host_ << settings.port << code
           << settings.originalFingerprint << reconnect_ << reconnectMilliseconds_ << settings.link;
    configurationToken_=QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex());
    changing_=false;
    state_=QStringLiteral("Configured"); publish();
    return true;
}

bool LisTcpTransport::openConfirmed(const QString &expected, QString *detail)
{
    if (detail) detail->clear();
    if (!checkThread(detail)) return false;
    if (stopping_ || changing_ || destroying_ || configurationToken_.isEmpty() || expected!=configurationToken_ || armed_)
    {
        if (detail) *detail=tr("端点尚未配置、配置已改变或连接已启动；未打开旧LIS地址。 ");
        return false;
    }
    armed_=true; phase_=0; cycle_.start();
    if (mode_==QStringLiteral("client")) beginClient();
    else
    {
        ++generation_;
        if (!server_->listen(QHostAddress::Any,port_))
        {
            const QString reason=server_->errorString();
            const quint64 failedGeneration=generation_;
            armed_=false; cycle_.stop(); state_=QStringLiteral("Failed"); publish();
            if (detail) *detail=reason;
            emit error(QStringLiteral("listen"),failedGeneration,0,reason);
            return false;
        }
        publish();
    }
    return true; // 只表示异步连接已发起或正在监听；Connected由真实socket信号报告。
}

void LisTcpTransport::publish()
{
    int connected=0;
    for (const auto &peer : peers_)
        if (peer->socket->state()==QAbstractSocket::ConnectedState) ++connected;
    if (armed_)
        state_=connected ? QStringLiteral("Connected") : mode_==QStringLiteral("server")
            ? QStringLiteral("Listening") : reconnectTimer_.isActive()
                ? QStringLiteral("ReconnectWaiting") : peers_.isEmpty()
                    ? QStringLiteral("Disconnected") : QStringLiteral("Connecting");
    emit stateChanged(state_,generation_,connected);
}

void LisTcpTransport::beginClient()
{
    if (!armed_ || mode_!=QStringLiteral("client") || !peers_.isEmpty()) return;
    ++generation_;
    auto *socket=new QTcpSocket(this);
    attachPeer(socket,true);
    connectDeadline_.start(1500);
    socket->connectToHost(host_,port_);
    publish();
}

void LisTcpTransport::retryClient()
{
    if (!armed_ || mode_!=QStringLiteral("client")) return;
    if (reconnect_) reconnectTimer_.start(reconnectMilliseconds_);
    else state_=QStringLiteral("Disconnected");
    publish();
}

void LisTcpTransport::acceptClients()
{
    const quint64 operationGeneration=generation_;
    while (server_->hasPendingConnections())
    {
        QTcpSocket *socket=server_->nextPendingConnection();
        if (!socket) continue;
        if (!armed_ || peers_.size()>=MaximumPeers)
        {
            emit error(QStringLiteral("accept"),generation_,0,tr("LIS连接数量超过本地16个上限，未合并其他客户端的数据。"));
            socket->abort(); socket->deleteLater();
            if (!armed_ || generation_!=operationGeneration) return;
            continue;
        }
        attachPeer(socket,false);
        publish();
        if (!armed_ || generation_!=operationGeneration) return;
    }
}

void LisTcpTransport::attachPeer(QTcpSocket *socket, bool client)
{
    auto peer=std::make_shared<Peer>();
    peer->id=++nextPeer_; peer->generation=generation_; peer->socket=socket; peer->client=client;
    peer->codec.reset(utf16_);
    const quint64 id=peer->id;
    peers_.insert(id,peer);
    socket->setReadBufferSize(65536);
    connect(socket,&QTcpSocket::connected,this,[this,id]()
    {
        if (!peers_.contains(id)) return;
        connectDeadline_.stop(); reconnectTimer_.stop(); publish();
    });
    connect(socket,&QTcpSocket::readyRead,this,[this,id]() { readPeer(id); });
    connect(socket,&QTcpSocket::bytesWritten,this,[this,id](qint64 bytes) { written(id,bytes); });
    connect(socket,&QTcpSocket::disconnected,this,[this,id]() { losePeer(id,tr("TCP对端已断开。")); });
    connect(socket,QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),this,
        [this,id](QAbstractSocket::SocketError)
    {
        if (peers_.contains(id)) losePeer(id,peers_.value(id)->socket->errorString());
    });
}

void LisTcpTransport::readPeer(quint64 id)
{
    if (!armed_ || !peers_.contains(id)) return;
    const auto peer=peers_.value(id);
    const QByteArray bytes=peer->socket->read(65536);
    if (bytes.isEmpty()) return;
    emit wireBytes(QStringLiteral("RX"),peer->generation,id,bytes);
    if (!armed_ || !peers_.contains(id) || peers_.value(id)!=peer) return;
    QList<QByteArray> bodies;
    QString detail;
    if (!peer->codec.feed(bytes,&bodies,&detail)) { losePeer(id,detail); return; }
    qint64 added=0;
    for (const auto &body : bodies) added+=body.size();
    if (received_.size()+bodies.size()>MaximumReceivedMessages || receivedBytes_+added>MaximumQueueBytes)
    {
        emit messageDiscarded(peer->generation,id,bodies.size(),tr("完整接收消息超过256条或4MiB本地预算，停止此连接。"));
        losePeer(id,tr("LIS接收队列超限，未静默丢弃后继续报成功。")); return;
    }
    for (const auto &body : bodies) received_.enqueue({peer->generation,id,body});
    receivedBytes_+=added;
}

bool LisTcpTransport::sendMessage(quint64 id, quint64 expectedGeneration, const QString &body, QString *detail,
    QSet<quint64> *acceptedPeers)
{
    if (detail) detail->clear();
    if (!checkThread(detail)) return false;
    const auto reject=[detail](const QString &reason) { if (detail) *detail=reason; return false; };
    if (!armed_ || generation_!=expectedGeneration || !id || id<=lastMessage_)
        return reject(tr("LIS未启动、连接代次已改变或发送请求编号重复；未自动重发。"));
    PendingSend message; message.id=id; message.generation=generation_;
    for (const auto &peer : peers_)
        if (peer->socket->state()==QAbstractSocket::ConnectedState) message.targets.insert(peer->id);
    if (message.targets.isEmpty()) return reject(tr("LIS没有已连接对端，未排队等待以后转发到另一连接。"));
    if (body.size()>MllpCodec::MaximumFrameBytes)
        return reject(tr("LIS消息文字超过本地单帧预算，未分配更大的编码缓冲。"));
    message.frame=MllpCodec::encode(body,utf16_);
    if (message.frame.size()>MllpCodec::MaximumFrameBytes || sends_.size()>=MaximumQueuedMessages
        || queuedBytes_+message.frame.size()>MaximumQueueBytes)
        return reject(tr("LIS发送超过单帧1MiB、64条或4MiB本地预算，未截断或覆盖旧消息。"));
    lastMessage_=id; queuedBytes_+=message.frame.size(); sends_.enqueue(message);
    // 回传本次真正固定的广播目标，供业务逐对端结清本地发送；不改变线上路由。
    if (acceptedPeers) *acceptedPeers=message.targets;
    return true; // 仅排队；每个对端之后分别报告WrittenToSocket/NotSent/OutcomeUnknown。
}

void LisTcpTransport::dispatchOne()
{
    if (sends_.isEmpty()) return;
    for (quint64 id : sends_.head().targets)
        if (peers_.contains(id) && peers_.value(id)->writeId) return;
    const PendingSend message=sends_.dequeue(); queuedBytes_-=message.frame.size();
    QList<std::shared_ptr<Peer>> targets;
    // 广播先登记所有目标，再开始写。首个目标的通知可能立即停止连接，
    // stop仍须能为尚未轮到的目标报告NotSent，不能让它们从队列中消失。
    for (quint64 id : message.targets)
    {
        if (!peers_.contains(id)) continue; // losePeer已明确回报NotSent并移除此目标。
        const auto peer=peers_.value(id);
        peer->writeId=message.id; peer->writing=message.frame;
        peer->submitted=0; peer->confirmed=0; peer->writeStarted=clock_.elapsed();
        targets.append(peer);
    }
    for (const auto &peer : targets)
    {
        if (!armed_ || generation_!=message.generation) return;
        if (peers_.value(peer->id)!=peer) continue;
        advanceWrite(peer);
    }
}

void LisTcpTransport::advanceWrite(const std::shared_ptr<Peer> &peer)
{
    if (!peer->writeId || peer->submitted>=peer->writing.size()) return;
    const QByteArray remaining=peer->writing.mid(int(peer->submitted));
    const qint64 accepted=peer->socket->write(remaining);
    if (accepted<0) { losePeer(peer->id,peer->socket->errorString()); return; }
    if (accepted>0)
    {
        peer->submitted+=accepted;
        emit wireBytes(QStringLiteral("TX_SOCKET"),peer->generation,peer->id,remaining.left(int(accepted)));
    }
}

void LisTcpTransport::written(quint64 id, qint64 bytes)
{
    if (!peers_.contains(id) || !peers_.value(id)->writeId) return;
    const auto peer=peers_.value(id);
    peer->confirmed+=bytes;
    if (peer->confirmed>=peer->writing.size() && peer->submitted==peer->writing.size())
    {
        const quint64 request=peer->writeId;
        peer->writeId=0; peer->writing.clear();
        emit sendFinished(request,peer->generation,id,QStringLiteral("WrittenToSocket"),
            tr("本地socket已写完；尚未收到LIS业务确认，不能据此标记结果已上传。"));
    }
    else advanceWrite(peer);
}

void LisTcpTransport::losePeer(quint64 id, const QString &reason)
{
    const quint64 operationGeneration=generation_;
    const auto peer=peers_.take(id);
    if (!peer) return;
    if (peer->client) connectDeadline_.stop();
    peer->socket->disconnect(this);
    peer->socket->abort(); peer->socket->deleteLater();
    // 先完成队列修改再通知；接收通知的槽可能立即stop/configure，不能再索引旧队列。
    QList<QPair<quint64,quint64>> unsent;
    for (int index=sends_.size()-1;index>=0;--index)
    {
        if (sends_[index].targets.remove(id))
            unsent.append(qMakePair(sends_[index].id,sends_[index].generation));
        if (sends_[index].targets.isEmpty()) { queuedBytes_-=sends_[index].frame.size(); sends_.removeAt(index); }
    }
    if (peer->writeId)
        emit sendFinished(peer->writeId,peer->generation,id,peer->submitted>0
            ? QStringLiteral("OutcomeUnknown") : QStringLiteral("NotSent"),reason);
    for (const auto &request : unsent)
        emit sendFinished(request.first,request.second,id,QStringLiteral("NotSent"),reason);
    if (peer->codec.bufferedBytes())
        emit messageDiscarded(peer->generation,id,1,tr("连接断开时有未完成消息，字节数%1：%2")
            .arg(peer->codec.bufferedBytes()).arg(reason));
    emit error(QStringLiteral("connection"),peer->generation,id,reason);
    // 已收齐的消息仍带原代次/对端交业务，不能和新连接的半帧混拼。
    if (!stopping_ && generation_==operationGeneration)
    {
        if (peer->client) retryClient();
        else publish();
    }
}

void LisTcpTransport::deliverReceived()
{
    const quint64 operationGeneration=generation_;
    const int count=received_.size();
    for (int index=0;index<count && !received_.isEmpty();++index)
    {
        const Received received=received_.dequeue(); receivedBytes_-=received.body.size();
        QString text,detail;
        if (!MllpCodec::decode(received.body,utf16_,&text,&detail))
        {
            emit error(QStringLiteral("decode"),received.generation,received.peer,detail);
            emit messageDiscarded(received.generation,received.peer,1,detail);
        }
        else emit messageReceived(received.generation,received.peer,received.body,text);
        if (!armed_ || generation_!=operationGeneration) return;
    }
}

void LisTcpTransport::cycle()
{
    if (!armed_) return;
    const quint64 operationGeneration=generation_;
    const auto peers=peers_.values();
    for (const auto &peer : peers)
    {
        if (!peers_.contains(peer->id)) continue;
        if (peer->socket->bytesAvailable()>0) readPeer(peer->id);
        if (!armed_ || generation_!=operationGeneration) return;
        if (!peers_.contains(peer->id) || !peer->writeId) continue;
        if (clock_.elapsed()-peer->writeStarted>=1000) losePeer(peer->id,tr("本地TCP写入1000毫秒未完成，结局待核，未自动重发。"));
        else advanceWrite(peer);
        if (!armed_ || generation_!=operationGeneration) return;
    }
    if (!armed_) return;
    // 原LisCommThread的200毫秒三拍顺序：发一条、交完整接收、业务超时检查。
    if (phase_==0) dispatchOne();
    else if (phase_==1) deliverReceived();
    else emit businessTimeoutTick(generation_,clock_.elapsed());
    if (!armed_ || generation_!=operationGeneration) return;
    phase_=(phase_+1)%3;
}

void LisTcpTransport::stop(const QString &reason)
{
    if (!checkThread(nullptr)) return;
    if (stopping_) return;
    stopping_=true;
    armed_=false;
    ++generation_; state_=QStringLiteral("Stopped");
    cycle_.stop(); reconnectTimer_.stop(); connectDeadline_.stop();
    // 未交给业务的旧入站连接也在本轮停止中清掉，不能等重开后归入新代次。
    while (server_->hasPendingConnections())
    {
        QTcpSocket *socket=server_->nextPendingConnection();
        if (socket) { socket->abort(); socket->deleteLater(); }
    }
    server_->close();
    const auto ids=peers_.keys();
    for (quint64 id : ids) losePeer(id,reason.isEmpty() ? tr("LIS已停止。") : reason);
    while (!received_.isEmpty())
    {
        const auto received=received_.dequeue();
        emit messageDiscarded(received.generation,received.peer,1,reason);
    }
    receivedBytes_=0; queuedBytes_=0; sends_.clear();
    stopping_=false;
    publish();
}
}
