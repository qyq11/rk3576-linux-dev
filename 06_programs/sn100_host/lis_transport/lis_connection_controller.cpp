#include "lis_connection_controller.h"
#include "lis_tcp_transport.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QThread>
#include <limits>

namespace Sn100Lis
{
LisConnectionController::LisConnectionController(LisSettingsStore *store, bool reconnect,
    int reconnectMilliseconds, QObject *parent) : QObject(parent),
    transport_(new LisTcpTransport(this)), reconnect_(reconnect),
    reconnectMilliseconds_(reconnectMilliseconds)
{
    qRegisterMetaType<ConfigurationIdentity>("Sn100Lis::ConfigurationIdentity");
    qRegisterMetaType<ConnectionState>("Sn100Lis::ConnectionState");
    qRegisterMetaType<ReceivedMessage>("Sn100Lis::ReceivedMessage");
    qRegisterMetaType<SendOutcome>("Sn100Lis::SendOutcome");
    transport_->setObjectName(QStringLiteral("originalLisTcpTransport"));
    state_.state = QStringLiteral("Unconfigured");
    connect(store, &LisSettingsStore::loaded, this, [this](LisSettings settings, QStringList)
    { receiveCommitted(settings, false); });
    connect(store, &LisSettingsStore::saved, this, [this](LisSettings settings, QStringList)
    { receiveCommitted(settings, true); });
    connect(store, &LisSettingsStore::error, this, &LisConnectionController::settingsError);
    connect(store, &LisSettingsStore::closed, this, &LisConnectionController::shutdown);
    connect(transport_, &LisTcpTransport::stateChanged, this,
        [this](QString state, quint64 generation, int peers)
    {
        // configure内部先结清旧会话。新Configured状态须等它返回后才赋新身份。
        if (!applying_) publish(state, generation, peers);
    });
    connect(transport_, &LisTcpTransport::messageReceived, this,
        [this](quint64 generation, quint64 peer, QByteArray body, QString text)
    {
        ReceivedMessage message;
        message.configuration = configuration_;
        message.generation = generation; message.peer = peer;
        message.body = body; message.text = text;
        // 断线前已收齐的正文仍带旧generation；业务须按自己的请求身份判断。
        emit messageReceived(message);
    });
    connect(transport_, &LisTcpTransport::sendFinished, this, &LisConnectionController::receiveSendResult);
    connect(transport_, &LisTcpTransport::wireBytes, this,
        [this](QString direction, quint64 generation, quint64 peer, QByteArray bytes)
    { emit wireBytes(configuration_, direction, generation, peer, bytes); });
    connect(transport_, &LisTcpTransport::messageDiscarded, this,
        [this](quint64 generation, quint64 peer, int count, QString reason)
    { emit messageDiscarded(configuration_, generation, peer, count, reason); });
    connect(transport_, &LisTcpTransport::businessTimeoutTick, this,
        [this](quint64 generation, qint64 elapsed)
    { emit businessTimeoutTick(configuration_, generation, elapsed); });
    connect(transport_, &LisTcpTransport::error, this,
        [this](QString operation, quint64 generation, quint64 peer, QString detail)
    { emit error(configuration_, operation, generation, peer, detail); });
}

LisConnectionController::~LisConnectionController()
{
    // 正常退出先shutdown；析构兜底不再通知正在析构的宿主。
    transport_->disconnect(this);
    transport_->stop(tr("LIS连接控制对象关闭。"));
}

bool LisConnectionController::checkThread(QString *detail) const
{
    if (QThread::currentThread() == thread()) return true;
    if (detail) *detail = tr("LIS连接控制必须在创建它的线程调用。");
    return false;
}

QString LisConnectionController::effectiveKey(const LisSettings &settings)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    // 不用仅比较九个可见项的operator==，也不因医院等无关键的文件指纹变化换代。
    stream << settings.originalProfile << settings.originalRoot << settings.originalValues
           << settings.type << settings.ip << settings.port << settings.commName << settings.baudrate
           << settings.sampleMode << settings.autoGet << settings.autoSend << settings.link
           << settings.tcpType << settings.sequenceNumber << settings.serverId << settings.linkIntervalMs
           << settings.dataBits << settings.parity << settings.stopBits << settings.flowControl
           << settings.serialFramingKnown << settings.simulation << settings.uploadForHole
           << settings.utf16 << settings.sampleCodeCanRepeatTest;
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

void LisConnectionController::receiveCommitted(const LisSettings &settings, bool saved)
{
    if (!checkThread(nullptr) || closing_ || applying_ || stopping_ || opening_) return;
    const bool first = !initialized_;
    initialized_ = true;
    latestReadFingerprint_ = settings.originalFingerprint;
    const QString key = effectiveKey(settings);
    if (!saved && !first && trusted_ && key == effectiveKey_)
        return; // 普通同值重读不会恢复已明确停止的连接，也不重贴来源指纹。

    const quint64 operation = ++operation_;
    if (first) openingRequested_ = settings.link;
    if (saved) openingRequested_ = true;
    applying_ = true;
    trusted_ = false;
    QString detail;
    const bool configured = transport_->configure(settings, reconnect_, reconnectMilliseconds_, &detail);
    applying_ = false;
    if (closing_ || operation != operation_)
    {
        stop(tr("配置处理期间已停止，未继续开启。"));
        return;
    }
    configuration_ = ConfigurationIdentity();
    configuration_.revision = ++nextRevision_;
    configuration_.effectiveKey = key;
    configuration_.transportToken = transport_->configurationToken();
    configuration_.sourceFingerprint = settings.originalFingerprint;
    effectiveKey_ = key;
    if (!configured)
    {
        publish(settings.type == QStringLiteral("tcp") ? QStringLiteral("Unconfigured")
                    : QStringLiteral("SerialPending"), transport_->generation(), 0, detail);
        if (operation == operation_ && !closing_)
            emit error(configuration_, QStringLiteral("configure"), transport_->generation(), 0, detail);
        return;
    }
    // 独立settings/lis.ini仍是本地设置。只对已确认来源的原config.ini恢复原开启策略。
    if (!settings.originalProfile)
    {
        publish(QStringLiteral("LocalSettingsOnly"), transport_->generation(), 0,
                tr("当前是独立本地设置，未启用原资料通信。"));
        return;
    }
    trusted_ = true;
    publish(QStringLiteral("Configured"), transport_->generation(), 0);
    if (closing_ || operation != operation_) return;
    // 原link仍称应答模式：只决定首次启动；明确保存成功不论link都重新开启。
    if (openingRequested_)
    {
        if (!openConfirmed(configuration_.revision, configuration_.transportToken, &detail)
                && operation == operation_ && !closing_)
            emit error(configuration_, QStringLiteral("open"), transport_->generation(), 0, detail);
    }
}

bool LisConnectionController::openConfirmed(quint64 revision, const QString &token, QString *detail)
{
    if (detail) detail->clear();
    if (!checkThread(detail)) return false;
    if (closing_ || applying_ || stopping_ || opening_ || !trusted_ || revision != configuration_.revision
            || token != configuration_.transportToken)
    {
        if (detail) *detail = tr("LIS配置尚待核对、身份已改变或正在关闭，未打开连接。");
        return false;
    }
    const quint64 current = ++operation_;
    openingRequested_ = true;
    opening_ = true;
    const bool opened = transport_->openConfirmed(token, detail);
    opening_ = false;
    if (closing_ || current != operation_)
    {
        stop(tr("开启期间已停止或关闭。"));
        if (detail) *detail = tr("开启期间已停止或关闭，未继续使用旧配置。");
        return false;
    }
    return opened;
}

bool LisConnectionController::sendMessage(quint64 revision, quint64 generation,
    const QString &body, quint64 *request, QString *detail)
{
    if (detail) detail->clear();
    if (request) *request = 0;
    if (!checkThread(detail)) return false;
    if (closing_ || applying_ || stopping_ || opening_ || !trusted_ || revision != configuration_.revision
            || generation != transport_->generation() || !request
            || nextRequest_ == std::numeric_limits<quint64>::max())
    {
        if (detail) *detail = tr("LIS发送身份已失效、编号不可用或正在关闭，未排队。");
        return false;
    }
    // 编号贯穿控制对象生命周期；停止或socket重连不归零。
    const quint64 id = ++nextRequest_;
    QSet<quint64> peers;
    if (!transport_->sendMessage(id, generation, body, detail, &peers)) return false;
    PendingSend pending;
    pending.configuration = configuration_; pending.generation = generation; pending.peers = peers;
    pending_.insert(id, pending);
    *request = id;
    return true;
}

void LisConnectionController::receiveSendResult(quint64 request, quint64 generation, quint64 peer,
    const QString &outcome, const QString &detail)
{
    auto found = pending_.find(request);
    if (found == pending_.end() || found->generation != generation || !found->peers.remove(peer))
    {
        emit error(configuration_, QStringLiteral("send-result"), generation, peer,
                   tr("发送回执没有对应的本地请求身份，未登记业务成功。"));
        return;
    }
    SendOutcome result;
    result.configuration = found->configuration; result.request = request;
    result.generation = generation; result.peer = peer; result.outcome = outcome; result.detail = detail;
    if (found->peers.isEmpty()) pending_.erase(found);
    ++sendNotificationDepth_;
    emit sendFinished(result);
    --sendNotificationDepth_;
    // losePeer可能先摘下整批NotSent再逐个通知；最后一项返回后重新核关闭。
    reportStoppedIfDrained();
}

void LisConnectionController::settingsError(const QString &operation, const QString &detail)
{
    if (closing_) return;
    // 原Store同一error(save)可能是写入前失败，也可能是已提交但回读失败。
    // 未能证实磁盘配置时统一停止，成功重读后才重新接受发送，不拿旧连接冒充一致。
    trusted_ = false;
    const quint64 current = ++operation_;
    stopping_ = true;
    transport_->stop(tr("LIS设置操作失败，连接已停止等待重新读取。"));
    stopping_ = false;
    reportStoppedIfDrained();
    if (closing_ || current != operation_) return;
    publish(QStringLiteral("SettingsUnconfirmed"), transport_->generation(), 0, detail);
    if (!closing_ && current == operation_)
        emit error(configuration_, operation, transport_->generation(), 0, detail);
}

void LisConnectionController::stop(const QString &reason)
{
    if (!checkThread(nullptr)) return;
    openingRequested_ = false;
    ++operation_;
    // 信号的直接消费者可能在结清旧发送时要求停止；由最外层转换收尾。
    if (applying_ || opening_ || stopping_) return;
    stopping_ = true;
    transport_->stop(reason);
    stopping_ = false;
    reportStoppedIfDrained();
}

void LisConnectionController::shutdown()
{
    if (!checkThread(nullptr)) return;
    if (closing_)
    {
        reportStoppedIfDrained();
        return;
    }
    closing_ = true; trusted_ = false; openingRequested_ = false;
    ++operation_;
    if (applying_ || opening_ || stopping_) return;
    stop(tr("LIS正在关闭，停止接收新业务。"));
}

void LisConnectionController::reportStoppedIfDrained()
{
    if (closing_ && !applying_ && !opening_ && !stopping_ && sendNotificationDepth_ == 0
            && !transport_->isActive() && pending_.isEmpty() && !stoppedReported_)
    {
        stoppedReported_ = true;
        emit stopped();
    }
}

void LisConnectionController::publish(const QString &state, quint64 generation, int peers, const QString &detail)
{
    state_.configuration = configuration_; state_.generation = generation;
    state_.connectedPeers = peers; state_.state = state; state_.detail = detail;
    state_.closing = closing_;
    emit stateChanged(state_);
}
}
