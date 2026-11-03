#include "original_channel_receiver.h"
#include "instrument/device_control.h"
#include <QCoreApplication>
#include <QDir>
#include <QScopedValueRollback>
#include <QThread>
#include <limits>
#include <utility>

namespace
{
constexpr int MaximumBatches = 32, MaximumRows = 1024, MaximumPayloadBytes = 128000;
constexpr qint64 MaximumPendingBytes = 2 * 1024 * 1024;
bool revoked(const OriginalRuntime::ChannelCommitPermitPtr &permit)
{
    return !permit || permit->state() == OriginalRuntime::ChannelCommitPermit::State::Revoked;
}
QString outcomeName(OriginalRuntime::ChannelApplyOutcome outcome)
{
    using Outcome = OriginalRuntime::ChannelApplyOutcome;
    if (outcome == Outcome::Committed) return QStringLiteral("Committed");
    if (outcome == Outcome::Cancelled) return QStringLiteral("Cancelled");
    if (outcome == Outcome::CommittedReloadFailed) return QStringLiteral("CommittedReloadFailed");
    return QStringLiteral("Failed");
}
}

OriginalChannelReceiver::OriginalChannelReceiver(OriginalChannelStore *store, DeviceControl *control,
    QString originalRoot, SnapshotConsumer consumer, QObject *parent)
    : QObject(parent), store_(store), control_(control), originalRoot_(QDir::cleanPath(originalRoot)),
      consumer_(std::move(consumer))
{
    OriginalChannelStore::registerTypes();
    if (store)
    {
        connect(this,&OriginalChannelReceiver::requestOpen,store,&OriginalChannelStore::openOriginal,Qt::QueuedConnection);
        connect(this,&OriginalChannelReceiver::requestReload,store,&OriginalChannelStore::reload,Qt::QueuedConnection);
        connect(this,&OriginalChannelReceiver::requestApply,store,&OriginalChannelStore::apply,Qt::QueuedConnection);
        connect(this,&OriginalChannelReceiver::requestClose,store,&OriginalChannelStore::close,Qt::QueuedConnection);
        connect(store,&OriginalChannelStore::opened,this,[this](const QString &root, OriginalRuntime::Channels channels)
        {
            reloadPending_ = false;
            if (QDir::cleanPath(root) != originalRoot_ || channels.fingerprint.size() != 64)
            {
                ready_ = false;
                emit error(tr("通道后台来源不符，未消费快照。"));
                requestStop();
                return;
            }
            fingerprint_ = channels.fingerprint;
            ready_ = true;
            if (stopRequested_) finishStop(); else dispatch();
        },Qt::QueuedConnection);
        connect(store,&OriginalChannelStore::openFailed,this,[this](const QString &message)
        {
            reloadPending_ = false; ready_ = false;
            cancelWaiting(message,QStringLiteral("Failed"));
            emit error(message);
            finishStop();
        },Qt::QueuedConnection);
        connect(store,&OriginalChannelStore::reloaded,this,[this](OriginalRuntime::Channels channels, const QString &message)
        {
            reloadPending_ = false;
            ready_ = message.isEmpty() && channels.fingerprint.size() == 64;
            if (ready_)
            {
                fingerprint_ = channels.fingerprint;
                consumeRecoveredSnapshot(channels);
            }
            else
            {
                cancelWaiting(message,QStringLiteral("Failed"));
                emit error(tr("通道资料重读失败，未沿用旧指纹：") + message);
            }
            if (stopRequested_) finishStop(); else dispatch();
        },Qt::QueuedConnection);
        connect(store,&OriginalChannelStore::applied,this,&OriginalChannelReceiver::finishResult,Qt::QueuedConnection);
        connect(store,&OriginalChannelStore::closed,this,[this]()
        {
            sourceClosed_ = true; ready_ = false; reloadPending_ = false;
            if (!stopRequested_) emit error(tr("通道后台提前关闭，停止接收；在途回执仍按实际结果结清。"));
            requestStop();
        },Qt::QueuedConnection);
        connect(store,&QObject::destroyed,this,[this]()
        {
            store_ = nullptr;
            finishAfterWorkerStopped();
        },Qt::QueuedConnection);
    }
    if (control && control->thread() == thread())
    {
        // 同GUI线程直接撤销，不能等SQL排队后再判旧连接。
        connect(control,&DeviceControl::changed,this,[this](const DeviceControlState &) { synchronizeConnection(); },Qt::DirectConnection);
        connect(control,&DeviceControl::payloadReceivedForConnection,this,&OriginalChannelReceiver::receivePayload,Qt::DirectConnection);
        connect(control,&QObject::destroyed,this,[this]() { control_ = nullptr; requestStop(); },Qt::DirectConnection);
        synchronizeConnection();
    }
}

OriginalChannelReceiver::~OriginalChannelReceiver()
{
    if (inFlight_) inFlight_->permit->revoke();
    if (committedRecovery_) committedRecovery_->permit->revoke();
    for (const Batch &batch : waiting_) batch.permit->revoke();
}

bool OriginalChannelReceiver::start()
{
    if (!QCoreApplication::instance() || thread() != QCoreApplication::instance()->thread()
        || QThread::currentThread() != thread() || started_ || stopRequested_ || !store_ || !control_
        || control_->thread() != thread() || !store_->thread() || !store_->thread()->isRunning()
        || store_->thread() == thread() || !QDir::isAbsolutePath(originalRoot_))
    {
        emit error(tr("Uh接收须在GUI线程使用已运行的独立通道后台及明确原工作根。"));
        return false;
    }
    started_ = true;
    reloadPending_ = true;
    emit requestOpen(originalRoot_);
    return true;
}

bool OriginalChannelReceiver::parse(const QByteArray &payload, OriginalChannelUpdates *updates, QString *message)
{
    const auto reject = [message](const QString &text) { *message = text; return false; };
    if (!payload.startsWith("Uh") || payload.size() > MaximumPayloadBytes)
        return reject(QStringLiteral("Uh消息超过128000字节本地上限或命令不符。"));
    QVector<QString> fields;
    QByteArray field;
    bool inField = false;
    for (int position = 2; position < payload.size(); ++position)
    {
        const char byte = payload.at(position);
        if (!inField)
        {
            if (byte == '[') inField = true;
            continue;
        }
        if (byte != ']') { field.append(byte); continue; }
        // 与原commandChanger的QString(QByteArray)相同：逐字段UTF8，首NUL停止该字段。
        fields.append(QString(field));
        field.clear(); inField = false;
        if (fields.size() > 1 + 2 * MaximumRows)
            return reject(QStringLiteral("Uh字段数量超过本地上限，未截断写库。"));
    }
    // 原扫描忽略字段外字节、保留字段内'['，不追加未闭合尾字段。
    if (fields.size() < 2) return reject(QStringLiteral("Uh不足原入口要求的两个字段，未写库。"));
    // 原转换失败或负数仍交空列表刷新；只安全拒绝正数量缺项和超预算。
    const int count = qMax(0,fields.at(0).toInt());
    if (count > MaximumRows || count > (fields.size()-1)/2)
        return reject(QStringLiteral("Uh超过1024项或正数量字段不足，未写库。"));
    OriginalChannelUpdates result;
    result.reserve(count);
    for (int index = 0; index < count; ++index)
        result.append({fields.at(1+2*index), bool(fields.at(2+2*index).toInt())});
    // 沿原循环只使用count项；完整的额外尾字段不解释为其他通道。
    *updates = result;
    return true;
}

void OriginalChannelReceiver::synchronizeConnection()
{
    if (QThread::currentThread() != thread()) return;
    const quint64 generation = control_ ? control_->connectionGeneration() : 0;
    const bool connected = control_ && control_->state().connected;
    if (generation == generation_ && connected == connected_) return;
    generation_ = generation; connected_ = connected;
    if (inFlight_) inFlight_->permit->revoke();
    if (committedRecovery_) committedRecovery_->permit->revoke();
    {
        QScopedValueRollback<bool> guard(dispatching_,true);
        cancelWaiting(tr("连接已改变，未派发的旧Uh已取消。"));
    }
    dispatch();
}

void OriginalChannelReceiver::receivePayload(QByteArray payload, quint64 generation)
{
    if (!payload.startsWith("Uh")) return;
    if (QThread::currentThread() != thread()) return;
    synchronizeConnection();
    if (!started_ || stopRequested_ || !connected_ || generation != generation_)
    {
        emit error(tr("Uh不属于当前启用连接，未排队写库。"));
        return;
    }
    OriginalChannelUpdates updates;
    QString message;
    if (!parse(payload,&updates,&message)) { emit error(message); return; }
    if (pendingBatchCount() >= MaximumBatches || pendingBytes_+payload.size() > MaximumPendingBytes
        || nextRequest_ == std::numeric_limits<quint64>::max())
    {
        emit error(tr("Uh待处理超过32批/2MiB或请求编号耗尽，本批明确拒绝，未覆盖旧批。"));
        return;
    }
    Batch batch;
    batch.request = ++nextRequest_; batch.generation = generation;
    batch.bytes = payload.size(); batch.updates = updates;
    batch.permit = std::make_shared<OriginalRuntime::ChannelCommitPermit>();
    pendingBytes_ += batch.bytes;
    waiting_.enqueue(batch);
    if (!ready_) reloadIfNeeded();
    dispatch();
}

bool OriginalChannelReceiver::current(const Batch &batch) const
{
    return current(batch.generation,batch.permit);
}

bool OriginalChannelReceiver::current(quint64 generation, const OriginalRuntime::ChannelCommitPermitPtr &permit) const
{
    return !stopRequested_ && connected_ && generation == generation_ && !revoked(permit)
        && control_ && control_->state().connected && control_->connectionGeneration() == generation;
}

void OriginalChannelReceiver::consumeRecoveredSnapshot(const OriginalRuntime::Channels &channels)
{
    if (!committedRecovery_) return;
    QScopedValueRollback<bool> guard(finishing_,true);
    const CommittedRecovery owner = *committedRecovery_;
    committedRecovery_.reset();
    bool displayed = false;
    QString message;
    if (current(owner.generation,owner.permit) && consumer_)
    {
        displayed = consumer_(channels,owner.generation,&message);
        displayed = displayed && current(owner.generation,owner.permit);
        if (displayed) message = tr("已提交通道资料重读恢复，当前原表已刷新并清除旧AD。");
        else message = tr("已提交资料重读恢复，但页面未消费：") + message;
    }
    else message = tr("已提交资料重读恢复，归属连接已失效或正在关闭，未刷新页面。");
    emit snapshotRecovered(owner.request,owner.generation,displayed,message);
}

void OriginalChannelReceiver::dispatch()
{
    if (dispatching_ || finishing_ || inFlight_ || !ready_ || stopRequested_ || !store_) return;
    QScopedValueRollback<bool> guard(dispatching_,true);
    while (!waiting_.isEmpty() && !stopRequested_)
    {
        Batch batch = waiting_.dequeue();
        if (!current(batch))
        {
            batch.permit->revoke(); pendingBytes_ -= batch.bytes;
            emit batchFinished(batch.request,batch.generation,QStringLiteral("Cancelled"),false,
                               tr("旧连接的Uh未派发。"),0,0);
            continue;
        }
        inFlight_.reset(new Batch(batch));
        emit requestApply(batch.request,batch.generation,batch.updates,fingerprint_,batch.permit);
        return;
    }
    finishStop();
}

void OriginalChannelReceiver::reloadIfNeeded()
{
    if (!started_ || ready_ || reloadPending_ || inFlight_ || stopRequested_ || !store_) return;
    reloadPending_ = true;
    emit requestReload();
}

void OriginalChannelReceiver::cancelWaiting(const QString &reason, const QString &outcome)
{
    const auto waiting = waiting_;
    waiting_.clear();
    for (const Batch &batch : waiting) { batch.permit->revoke(); pendingBytes_ -= batch.bytes; }
    for (const Batch &batch : waiting)
        emit batchFinished(batch.request,batch.generation,outcome,false,reason,0,0);
}

void OriginalChannelReceiver::finishResult(quint64 request, quint64 generation, OriginalRuntime::ChannelApplyResult result)
{
    if (!inFlight_ || inFlight_->request != request || inFlight_->generation != generation)
    { emit error(tr("通道后台回执没有对应在途批次，未更新指纹或页面。")); return; }
    {
        QScopedValueRollback<bool> guard(finishing_,true);
        const Batch batch = *inFlight_;
        inFlight_.reset(); pendingBytes_ -= batch.bytes;
        using Outcome = OriginalRuntime::ChannelApplyOutcome;
        bool displayed = false;
        if (result.outcome == Outcome::Committed && result.channels.fingerprint.size() == 64)
        {
            // 即使旧代次已提交，也推进后台版本；显示归属另按当前连接判断。
            fingerprint_ = result.channels.fingerprint; ready_ = true;
            if (current(batch) && consumer_)
            {
                QString problem;
                displayed = consumer_(result.channels,generation,&problem);
                displayed = displayed && current(batch);
                if (!displayed) result.message += tr("；库已提交，但页面未消费：") + problem;
            }
            else result.message += tr("；库已提交，结果属于旧连接或正在关闭，未刷新页面。");
        }
        else
        {
            ready_ = false;
            if (result.outcome == Outcome::Committed)
            {
                result.outcome = Outcome::CommittedReloadFailed;
                result.message += tr("；后台未返回有效的新资料指纹，暂停刷新并重读。");
            }
        }
        if (result.outcome == Outcome::CommittedReloadFailed)
        {
            // 仅保留已提交事实的归属，后续真实重读可补刷新，不保存/重放原更新。
            committedRecovery_.reset(new CommittedRecovery{request,generation,batch.permit});
        }
        emit batchFinished(request,generation,outcomeName(result.outcome),displayed,
                           result.message,result.changedRows,result.unknownRows);
    }
    if (stopRequested_) finishStop();
    else { if (!ready_) reloadIfNeeded(); dispatch(); }
}

void OriginalChannelReceiver::requestStop()
{
    if (QThread::currentThread() != thread() || stopped_) return;
    stopRequested_ = true; ready_ = false;
    if (inFlight_) inFlight_->permit->revoke();
    if (committedRecovery_) committedRecovery_->permit->revoke();
    cancelWaiting(tr("通道接收正在关闭，未派发批次已取消。"));
    finishStop();
}

void OriginalChannelReceiver::finishStop()
{
    if (!stopRequested_ || stopped_ || inFlight_ || finishing_) return;
    if (sourceClosed_)
    {
        stopped_ = true;
        emit stopped();
    }
    else if (!closeRequested_ && store_)
    {
        closeRequested_ = true;
        emit requestClose();
    }
}

void OriginalChannelReceiver::finishAfterWorkerStopped()
{
    if (QThread::currentThread() != thread() || stopped_) return;
    stopRequested_ = true;
    if (committedRecovery_) committedRecovery_->permit->revoke();
    cancelWaiting(tr("通道线程已停止，未派发批次取消。"));
    if (inFlight_)
    {
        const Batch batch = *inFlight_;
        batch.permit->revoke(); inFlight_.reset(); pendingBytes_ -= batch.bytes;
        emit batchFinished(batch.request,batch.generation,QStringLiteral("Unconfirmed"),false,
                           tr("线程结束前未消费回执，写入结果待核；禁止自动重放。"),0,0);
    }
    sourceClosed_ = true;
    finishStop();
}
