#include "frmmain.h"
#include "task_reagent_context.h"
#include "instrument/device_control.h"
#include "reagents/reagents_page.h"
#include "reagents/reagent_store.h"
#include "reagents/original_reagent_level_worker.h"
#include "samples/sample_page.h"
#include "samples/sample_store.h"
#include "projects/project_page.h"
#include <QCryptographicHash>
#include <QDir>
#include "projects/original_project_repository.h"
#include "original_files/original_ini.h"
#include <QDataStream>
#include <QDebug>
#include <QMetaObject>
#include <QTimer>
#include <limits>

namespace TaskReagentRouting
{
bool sameTaskSession(const AccountSession &a,const AccountSession &b)
{
    return a.name==b.name && a.role==b.role && a.authenticated==b.authenticated && a.source==b.source &&
        a.originalPolicySource==b.originalPolicySource && a.grantedPowerIds==b.grantedPowerIds;
}
QByteArray taskBasisBytes(const OriginalTaskReagents::RuntimeBasis &b)
{
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out<<OriginalTaskReagents::taskOriginBytes(b);
    out<<b.available<<b.actionId<<b.arrivalPacketId<<b.connection<<b.basisRevision<<b.permissionRevision
       <<b.root<<b.payloadSha256<<b.basisToken<<b.protocolToken<<b.actualModel<<b.configuredModel<<b.themeFingerprint
       <<b.capacityRelativeFile<<b.capacityFingerprint<<b.capacityFileState<<b.configuredPositions<<b.reportedPositions
       <<b.mainStatus<<b.capacityCalibrationActive<<b.versions<<b.unavailableReason<<b.holes.size();
    for (auto h=b.holes.cbegin();h!=b.holes.cend();++h)
        out<<h.key()<<h->available<<h->rowId<<h->hole<<h->originalIdentity<<h->revision<<h->maximumSource
           <<h->maximumMicrolitres<<h->amountMicrolitres<<h->usedMicrolitres<<h->state<<h->unavailableReason;
    return bytes;
}
QByteArray taskCommitSignature(const OriginalTaskReagents::Commit &r)
{
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out<<r.receiptId<<int(r.action)<<taskBasisBytes(r.basis)<<r.root<<r.payloadSha256<<r.originDigest<<r.settingTable
       <<int(r.transaction)<<r.committed<<r.reagentSqlWritten<<r.postCommitVerified<<r.basisMatchesDatabaseBefore
       <<r.beforeVersions<<r.afterVersions<<r.proofMessage;
    for (const auto *plate : {&r.beforePlate,&r.afterPlate})
    {
        out<<plate->complete<<plate->table<<plate->columns<<plate->rows.size();
        for (const auto &row : plate->rows) out<<row.values<<row.sqliteTypes;
    }
    out<<r.sqlWrites.size();
    for (const auto &row : r.sqlWrites) out<<row.rowId<<row.hole<<row.predicateUsed<<row.writtenUsed<<row.columns;
    out<<r.refreshHoles.size();
    for (const auto &h : r.refreshHoles)
        out<<h.rowId<<h.hole<<h.storedUsed<<h.sqliteType<<h.trustedBasis<<h.displayConversionVerified<<h.usedMicrolitres<<h.message;
    out<<r.tasks.size();
    for (const auto &task : r.tasks)
        out<<task.taskId<<task.reportRow<<task.projectId<<task.calibrationId<<task.rack<<task.type<<task.projectName
           <<task.projectFile<<task.projectFingerprint<<task.hole<<task.repeatIndex<<task.calibrationPoint
           <<task.reagentSlots<<task.reservedByHole<<task.returnedByHole<<task.skippedAdded;
    out<<r.alreadyHandledTaskIds<<r.skippedAddedTaskIds<<r.refusedUncertainTaskIds;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
}

namespace
{
using namespace TaskReagentRouting;
using OriginalTaskReagents::TransactionState;
QString digest(const QByteArray &payload)
{ return QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()); }

bool hasLiveWork(const Bridge &bridge)
{
    for (const auto &action : bridge.actions)
        if (action.dispatched || action.stage==Stage::WaitingForN) return true;
    return false;
}
void foldTransaction(Bridge &bridge,const Action &action)
{
    if (!action.transactionUnknown) return;
    // 凭据编号可能本身有冲突；只用本窗口认领的原动作编号归档，不能覆盖另一动作。
    const QString key=QStringLiteral("%1:%2:%3:%4").arg(action.epoch).arg(action.id).arg(action.packet).arg(action.connection);
    if (!bridge.transactions.contains(key) && bridge.transactions.size()>=32)
    { bridge.transactionOverflow=true; return; }
    TransactionFact fact;
    fact.actionId=action.id; fact.packet=action.packet; fact.connection=action.connection; fact.epoch=action.epoch;
    fact.source=action.seed.source; fact.userOperation=action.seed.userOperation; fact.originDigest=action.seed.originDigest;
    fact.root=action.root; fact.receiptId=action.receipt ? action.receipt->receiptId : QString(); fact.payloadSha256=action.payloadSha256;
    if (action.receipt)
    { fact.receiptAvailable=true; fact.transaction=action.receipt->transaction; fact.committed=action.receipt->committed; }
    // 未决事务可能改过原本没有可信显示量的孔；前版可信孔不能限定实际SQL影响范围。
    // 尚无完整影响范围证明时整盘待核，后续Ur或手工来源也不能缩小这笔事务的遮罩。
    fact.unscoped=true;
    bridge.transactions.insert(key,fact);
}
void foldDisplay(Bridge &bridge,const Action &action)
{
    if (!action.displayUnknown) return;
    if (!bridge.displayUnknown.contains(action.root) && bridge.displayUnknown.size()>=8)
    {
        bridge.globalDisplayUnknown=true;
        bridge.globalAfterProcessedSerial=qMax(bridge.globalAfterProcessedSerial,action.processedSerial);
        bridge.independentFullIdentity=0;
        return;
    }
    auto &latch=bridge.displayUnknown[action.root]; latch.root=action.root;
    if (bridge.displayGeneration!=std::numeric_limits<quint64>::max()) ++bridge.displayGeneration;
    latch.generation=bridge.displayGeneration;
    latch.afterProcessedSerial=qMax(latch.afterProcessedSerial,action.processedSerial);
    QMap<QString,qint64> required;
    for (auto hole=action.basis.holes.cbegin();hole!=action.basis.holes.cend();++hole)
    {
        if (!hole->available) continue;
        bool applied=false;
        for (const auto &result : action.adoption.holes)
            if (result.hole==hole.key() && result.rowId==hole->rowId) applied=result.runtimeApplied;
        if (!applied) required.insert(hole.key(),hole->rowId);
    }
    if (action.unscoped || required.isEmpty()) { latch.unscoped=true; latch.holes.clear(); }
    if (!latch.unscoped)
        for (auto hole=required.cbegin();hole!=required.cend();++hole)
        {
            if ((latch.holes.contains(hole.key()) && latch.holes.value(hole.key())!=hole.value()) || latch.holes.size()>=128)
            { latch.unscoped=true; latch.holes.clear(); break; }
            latch.holes.insert(hole.key(),hole.value());
        }
}

QString visibleMessage(const QString &diagnostic,const Bridge *bridge)
{
    if (bridge && (bridge->transactionOverflow || !bridge->transactions.isEmpty()))
        return QStringLiteral("任务提交结果或记录归属尚未确认，已暂停新的任务请求，原记录继续保留。");
    if (bridge && bridge->closing)
        return QStringLiteral("正在等待原任务和资料读取结束，处理完成后再关闭。");
    const QStringList internal{"Root","Page","COMMIT","ACK","basis","Qj","裸N","弱刷新","processed",
        "Tt","TR","Ur","Provider","revision","SQL","NULL","BLOB","used","after","三库","指纹","票据","上下文","准入"};
    bool technical=false;
    for (const auto &word : internal) if (diagnostic.contains(word)) { technical=true; break; }
    if (!technical) return diagnostic;
    if (diagnostic.contains(QStringLiteral("只恢复已核范围")))
        return QStringLiteral("本次只核实了部分孔位，其余试剂剩余量仍待核对。");
    if (diagnostic.contains(QStringLiteral("原来源已按本次孔位")))
        return QStringLiteral("已核对本次返回或设置的孔位，其他待核记录保持原状态。");
    if (bridge && !bridge->actions.isEmpty())
        return QStringLiteral("正在处理任务并核对试剂剩余量，请等待当前处理完成。");
    return QStringLiteral("试剂资料或剩余量尚未核实，已保留原记录，未自动重试或退回用量。");
}
}

void frmMain::reportTaskReagent(const QString &message)
{
    const QString shown=visibleMessage(message,taskReagentBridge_.get());
    setProperty("taskReagentMessage",shown);
    qInfo().noquote()<<"TASK_REAGENT"<<message; // 原诊断保留；页面使用通俗说明。
    if (reagents_) reagents_->notifyOriginalLevel(shown);
}

QString frmMain::taskReagentTransactionBlocker() const
{
    if (taskReagentBridge_ && (taskReagentBridge_->transactionOverflow || !taskReagentBridge_->transactions.isEmpty()))
        return tr("任务提交结果或记录归属尚未确认，已暂停新的任务请求。");
    if (taskReagentBridge_)
    {
        if (!taskReagentBridge_->nReaders.isEmpty())
            return tr("正在等候液面结果读取结束，暂不能开始新的任务。");
        for (const auto &action : taskReagentBridge_->actions)
            if (!action.processed)
                return tr("正在等待当前任务处理结果，暂不能开始新的任务。");
    }
    return QString();
}

void frmMain::syncTaskReagentDisplayUnknown()
{
    if (!taskReagentBridge_ || !reagents_) return;
    const auto &b=*taskReagentBridge_;
    QStringList holes;
    bool unscoped=false;
    const auto known=b.displayUnknown.constFind(b.identity.root);
    if (known!=b.displayUnknown.cend()) { unscoped=known->unscoped; holes=known->holes.keys(); }
    if (b.globalDisplayUnknown && (b.independentFullIdentity!=b.identity.revision || b.independentFullSerial<b.globalAfterProcessedSerial))
        unscoped=true;
    if (b.transactionOverflow) unscoped=true;
    for (const auto &fact : b.transactions)
    {
        if (!fact.root.isEmpty() && fact.root!=b.identity.root) continue;
        if (fact.unscoped || fact.holes.isEmpty()) unscoped=true;
        for (auto hole=fact.holes.cbegin();hole!=fact.holes.cend();++hole)
            if (!holes.contains(hole.key())) holes.append(hole.key());
    }
    for (const auto &a : b.actions)
    {
        if (!a.transactionUnknown || a.root!=b.identity.root) continue;
        bool any=false;
        for (auto h=a.basis.holes.cbegin();h!=a.basis.holes.cend();++h)
            if (h->available) { any=true; if (!holes.contains(h.key())) holes.append(h.key()); }
        if (!any || a.receiptConflict) unscoped=true;
    }
    reagents_->setOriginalTaskReagentDisplayUnknown(b.identity.revision,b.identity.root,holes,unscoped,
        tr("本次任务的试剂剩余量尚未核实，相关数量和可测次数待核。"));
}

void frmMain::ensureTaskReagentFence()
{
    if (!taskReagentBridge_ || !reagentLevelWorker_ || !deviceControl_) return;
    auto &bridge=*taskReagentBridge_;
    const quint64 connection=deviceControl_->connectionGeneration();
    bridge.awaitFreshQj=true;
    if (!connection || (bridge.fenceClosed && bridge.fenceConnection==connection)) return;
    if (bridge.fenceToken==std::numeric_limits<quint64>::max())
    { bridge.transactionOverflow=true; reportTaskReagent(tr("任务准入门编号耗尽，未回绕旧令牌，后续事务待核。")); return; }
    const quint64 token=++bridge.fenceToken;
    QString message;
    if (!reagentLevelWorker_->setTaskAdmissionFence(connection,token,true,&message))
    { bridge.transactionOverflow=true; reportTaskReagent(message); return; }
    bridge.fenceClosed=true; bridge.fenceConnection=connection;
}

QString frmMain::taskReagentNBlocker() const
{
    if (!taskReagentBridge_) return QString();
    const auto &b=*taskReagentBridge_;
    if (b.closing) return tr("原任务正在收尾，处理完成后再关闭，暂不保存新的液面结果。");
    if (b.transactionOverflow || !b.transactions.isEmpty()) return tr("任务提交结果尚未确认，暂不保存新的液面结果。");
    if (hasLiveWork(b) || !b.nReaders.isEmpty()) return tr("正在处理任务或核对试剂剩余量，暂不保存新的液面结果。");
    if (b.displayUnknown.contains(b.identity.root)) return tr("试剂剩余量仍待核对，暂不保存新的液面结果。");
    if (b.globalDisplayUnknown && (b.independentFullIdentity!=b.identity.revision || b.independentFullSerial<b.globalAfterProcessedSerial))
        return tr("当前试剂盘尚未完成全部孔位核对，暂不保存新的液面结果。");
    return b.awaitFreshQj ? tr("任务已收尾，仍在等待设备的新状态，暂不保存新的液面结果。") : QString();
}

bool frmMain::taskReagentObservationOnly(quint64 connection,bool validQj)
{
    if (!taskReagentBridge_) return false;
    auto &b=*taskReagentBridge_;
    const bool blocked=b.closing || hasLiveWork(b) || b.transactionOverflow || !b.transactions.isEmpty() ||
        b.displayUnknown.contains(b.identity.root) || (b.globalDisplayUnknown &&
        (b.independentFullIdentity!=b.identity.revision || b.independentFullSerial<b.globalAfterProcessedSerial));
    if (blocked || b.awaitFreshQj) ensureTaskReagentFence();
    if (validQj && !blocked && b.fenceClosed && b.fenceConnection==connection && reagentLevelWorker_)
    {
        QString message;
        if (reagentLevelWorker_->setTaskAdmissionFence(connection,b.fenceToken,false,&message))
        { b.fenceClosed=false; b.awaitFreshQj=false; return false; }
        reportTaskReagent(message);
    }
    return b.fenceClosed || blocked;
}

quint64 frmMain::taskReagentFenceToken() const
{ return taskReagentBridge_ ? taskReagentBridge_->fenceToken : 0; }

int frmMain::taskReagentRetainedPackets() const
{
    int count=0;
    if (taskReagentBridge_) for (const auto &a : taskReagentBridge_->actions)
        if (a.seed.source==OriginalTaskReagents::OperationSource::UserOperation || (a.processed && !devicePackets_.contains(a.packet))) ++count;
    return count;
}
qint64 frmMain::taskReagentRetainedBytes() const
{
    qint64 bytes=0;
    if (taskReagentBridge_) for (const auto &a : taskReagentBridge_->actions)
        if (a.seed.source==OriginalTaskReagents::OperationSource::UserOperation || (a.processed && !devicePackets_.contains(a.packet))) bytes+=a.payloadBytes;
    return bytes;
}

void frmMain::attachTaskReagents()
{
    if (!deviceTaskStore_ || !reagents_ || !reagents_->taskStore() || !deviceControl_) return;
    if (!taskReagentBridge_) taskReagentBridge_=std::make_shared<Bridge>();
    auto &bridge=*taskReagentBridge_;
    if (bridge.attached) return;
    bridge.attached=true;
    bridge.actionDeadlineTimer=new QTimer(this);
    bridge.actionDeadlineTimer->setInterval(500);
    bridge.actionDeadlineTimer->setTimerType(Qt::PreciseTimer);
    connect(bridge.actionDeadlineTimer,&QTimer::timeout,this,[this]()
    {
        if (!taskReagentBridge_) return;
        auto &b=*taskReagentBridge_;
        bool needTimer=false, warned=false;
        const auto now=Clock::now();
        for (auto &action : b.actions)
        {
            if (action.processed || action.waitWarned) continue;
            if (now-action.arrived>=std::chrono::seconds(10))
            {
                action.waitWarned=true; warned=true;
                if (!action.dispatched) action.basisEligible=false;
                qInfo()<<"TASK_REAGENT_WAIT_TIMEOUT"<<action.id<<action.packet<<action.dispatched;
            }
            else needTimer=true;
        }
        if (!needTimer) b.actionDeadlineTimer->stop();
        if (warned) reportTaskReagent(tr("任务处理时间较长，仍在等待原处理结束；没有自动重试或退回试剂用量。"));
    });
    connect(deviceTaskStore_,&SampleStore::taskReagentCommitObserved,this,&frmMain::observeTaskReagentCommit,Qt::QueuedConnection);
    connect(deviceTaskStore_,&SampleStore::userTaskOperationFinished,this,&frmMain::finishUserTaskOperation,Qt::QueuedConnection);
    connect(reagents_,&ReagentsPage::originalTaskReagentAdopted,this,&frmMain::receiveTaskReagentAdoption);
    connect(reagents_->taskStore(),&ReagentStore::originalTaskReagentReadback,this,
        [this](const OriginalTaskReagents::Commit &receipt,bool,const ReagentSnapshot &,const ReagentProjects &,const QString &)
        { taskReagentReadbackFinished(receipt); },Qt::QueuedConnection);
    connect(deviceControl_,&DeviceControl::changed,this,[this](const DeviceControlState &)
    { taskReagentContextChanged(); advanceTaskReagents(); });
    connect(reagents_,&ReagentsPage::snapshotChanged,this,[this](const ReagentSnapshot &)
    { taskReagentContextChanged(); advanceTaskReagents(); });
    connect(reagents_,&ReagentsPage::busyChanged,this,[this](bool busy)
    { if (!busy) advanceTaskReagents(); });
    if (reagentLevelSource_) bridge.verifiedSource=std::make_shared<const OriginalLevels::Context>(*reagentLevelSource_);
    taskReagentContextChanged();
}

bool frmMain::routeTaskReagentPacket(quint64 packet,quint64 connection,const QByteArray &payload)
{
    const QByteArray command=payload.left(2);
    if (command!="Tt" && command!="TR") return false;
    if (!taskReagentBridge_) attachTaskReagents();
    if (!taskReagentBridge_) return false;
    auto &b=*taskReagentBridge_;
    if (b.closing || b.transactionOverflow || !b.transactions.isEmpty())
    {
        const QByteArray removed=devicePackets_.take(packet); devicePacketBytes_-=removed.size();
        reportTaskReagent(tr("原事务尚未核清或正在关闭，本次Tt/TR未进入后台；未伪造回应、重发或退款。"));
        return true;
    }
    if (b.nextAction==std::numeric_limits<quint64>::max())
    { b.transactionOverflow=true; ensureTaskReagentFence(); reportTaskReagent(tr("任务动作编号耗尽，本包未排事务。"));
      const auto removed=devicePackets_.take(packet); devicePacketBytes_-=removed.size(); return true; }
    Action a;
    a.id=++b.nextAction; a.packet=packet; a.connection=connection; a.epoch=b.epoch;
    a.expected=command=="Tt" ? OriginalTaskReagents::Action::RackReservation : OriginalTaskReagents::Action::UnusedReservationReturn;
    a.root=samples_ ? samples_->property("originalRoot").toString() : QString();
    a.payloadSha256=digest(payload); a.payloadBytes=payload.size();
    a.basisEligible=b.actions.isEmpty() && !b.displayUnknown.contains(a.root) &&
        !(b.globalDisplayUnknown && (b.independentFullIdentity!=b.identity.revision ||
            b.independentFullSerial<b.globalAfterProcessedSerial));
    const auto &identity=b.identity;
    a.seed.actionId=a.id; a.seed.arrivalPacketId=packet; a.seed.connection=connection; a.seed.root=a.root;
    a.seed.payloadSha256=a.payloadSha256; a.seed.permissionRevision=identity.revision;
    a.seed.basisToken=QStringLiteral("task:%1:%2:%3:%4").arg(b.epoch).arg(a.id).arg(packet).arg(connection);
    a.seed.actualModel=identity.actualModel; a.seed.configuredModel=identity.configuredModel;
    a.seed.protocolToken=identity.protocolToken; a.seed.themeFingerprint=identity.themeFingerprint;
    a.seed.capacityRelativeFile=identity.capacityRelativeFile; a.seed.capacityFingerprint=identity.capacityFingerprint;
    a.seed.capacityFileState=identity.capacityFileState; a.seed.configuredPositions=identity.configuredPositions;
    a.seed.reportedPositions=identity.reportedPositions; a.seed.capacityCalibrationActive=identity.capacityCalibrationActive;
    a.seed.mainStatus=deviceControl_->state().status.mainStatus; // 原到达值，后续Qj不重贴。
    a.seed.originDigest=a.payloadSha256; a.basis=a.seed;
    b.actions.insert(a.id,a); b.waiting.enqueue(a.id);
    if (b.actionDeadlineTimer && !b.actionDeadlineTimer->isActive()) b.actionDeadlineTimer->start();
    ensureTaskReagentFence();
    updateDeviceControlActions();
    qInfo().noquote()<<"TASK_REAGENT_ARRIVED"<<a.id<<packet<<connection<<a.epoch<<int(a.expected)<<a.payloadSha256;
    advanceTaskReagents();
    return true;
}

void frmMain::observeTaskReagentCommit(const OriginalTaskReagents::Commit &receipt)
{
    if (!taskReagentBridge_) return;
    auto &b=*taskReagentBridge_;
    auto it=b.actions.find(receipt.basis.actionId);
    const QByteArray signature=taskCommitSignature(receipt);
    if (it==b.actions.end())
    {
        const auto old=b.terminal.constFind(receipt.basis.actionId);
        if (old!=b.terminal.cend() && old->receiptSignature==signature) return;
        if (old!=b.terminal.cend() && old->id==receipt.basis.actionId && old->connection==receipt.basis.connection &&
            old->expected==receipt.action && old->root==receipt.root && old->payloadSha256==receipt.payloadSha256 &&
            old->source==receipt.basis.source && old->originDigest==receipt.originDigest && old->packet==receipt.basis.arrivalPacketId &&
            old->basisToken==receipt.basis.basisToken && old->basisSignature==QCryptographicHash::hash(taskBasisBytes(receipt.basis),QCryptographicHash::Sha256))
        {
            // 本应先于processed的事实迟到：保原事实和归属未知，不借已回收槽位采用或撤新来源。
            Action late; late.id=old->id; late.packet=old->packet; late.connection=old->connection; late.epoch=old->epoch;
            late.seed.source=old->source; late.seed.userOperation=old->userOperation; late.seed.originDigest=old->originDigest;
            late.root=old->root; late.payloadSha256=old->payloadSha256; late.transactionUnknown=true;
            late.displayUnknown=true; late.unscoped=true; late.processedSerial=b.processedSerial;
            late.receipt=std::make_shared<const OriginalTaskReagents::Commit>(receipt);
            foldTransaction(b,late); foldDisplay(b,late); ensureTaskReagentFence();
            reportTaskReagent(tr("已结束动作收到新的迟到事务事实，保留提交/未确认原值并停止新事务；未重放或套新页面。"));
            advanceTaskReagents(); return;
        }
        reportTaskReagent(tr("收到不属于活跃动作的提交事实，未借其清未知或覆盖当前来源。")); return;
    }
    auto &a=it.value();
    if (!a.dispatched || receipt.action!=a.expected || receipt.basis.actionId!=a.id || receipt.basis.connection!=a.connection ||
        receipt.root!=a.root || receipt.payloadSha256!=a.payloadSha256 || receipt.originDigest!=a.seed.originDigest ||
        receipt.basis.arrivalPacketId!=a.packet || !OriginalTaskReagents::taskOriginAllows(receipt.basis,receipt.action) ||
        taskBasisBytes(receipt.basis)!=taskBasisBytes(a.basis))
    {
        if (!a.receipt) { a.receipt=std::make_shared<const OriginalTaskReagents::Commit>(receipt); a.receiptSignature=signature; }
        a.observed=true; a.receiptConflict=true; a.transactionUnknown=true; a.displayUnknown=true; a.unscoped=true;
        a.message=tr("后台事务事实的原动作/类型/连接/根或完整basis归属无法核清。");
        qInfo().noquote()<<"TASK_REAGENT_UNBOUND_FACT"<<receipt.receiptId<<int(receipt.transaction)<<receipt.committed;
        foldTransaction(b,a); ensureTaskReagentFence(); syncTaskReagentDisplayUnknown(); reportTaskReagent(a.message); advanceTaskReagents(); return;
    }
    if (a.observed)
    {
        if (a.receiptSignature!=signature)
        { a.transactionUnknown=true; a.receiptConflict=true; a.displayUnknown=true; a.unscoped=true;
          foldTransaction(b,a); syncTaskReagentDisplayUnknown(); reportTaskReagent(tr("同一动作提交凭据冲突，未用新值替换原事实。")); advanceTaskReagents(); }
        return;
    }
    a.observed=true; a.receiptAuthorized=true; a.receipt=std::make_shared<const OriginalTaskReagents::Commit>(receipt); a.receiptSignature=signature;
    a.transactionUnknown=receipt.transaction==TransactionState::CommitUnconfirmed ||
        (receipt.committed!=(receipt.transaction==TransactionState::Committed));
    foldTransaction(b,a);
    if (a.transactionUnknown)
    {
        a.displayUnknown=true; a.unscoped=!a.basis.available;
        syncTaskReagentDisplayUnknown(); reportTaskReagent(tr("后台报告原事务提交无法确认；原committed=false保留，旧量不得当本次已核结果。"));
    }
    qInfo().noquote()<<"TASK_REAGENT_OBSERVED"<<a.id<<a.packet<<a.connection<<receipt.receiptId
        <<int(receipt.transaction)<<receipt.committed<<receipt.reagentSqlWritten<<receipt.postCommitVerified;
    // 这里只保存事实，processed之前绝不排Page重读去抢本事务锁。
    ensureTaskReagentFence(); advanceTaskReagents();
}

void frmMain::finishTaskReagentPacket(quint64 packet,quint64 connection,const QByteArray &command,bool success,const QString &message)
{
    if (!taskReagentBridge_) return;
    auto &b=*taskReagentBridge_;
    auto it=b.actions.begin();
    while (it!=b.actions.end() && (it->seed.source!=OriginalTaskReagents::OperationSource::DevicePacket || it->packet!=packet)) ++it;
    if (it==b.actions.end()) return;
    auto &a=it.value();
    const QByteArray expected=a.expected==OriginalTaskReagents::Action::RackReservation ? QByteArray("Tt") : QByteArray("TR");
    if (!a.dispatched || a.connection!=connection || command!=expected || a.processed)
    { reportTaskReagent(tr("processed不属于该原活跃动作，未假报释放事务资格。")); return; }
    a.processed=true;
    if (b.processedSerial!=std::numeric_limits<quint64>::max()) ++b.processedSerial;
    else b.transactionOverflow=true;
    a.processedSerial=b.processedSerial; a.message=message;
    if (!a.observed && a.expected==OriginalTaskReagents::Action::UnusedReservationReturn && success)
    {
        a.displayUnknown=true; a.unscoped=true;
        reportTaskReagent(tr("本次TR没有新COMMIT；原无提交弱刷新仍未接，不制造提交或运行采用成功。"));
    }
    qInfo().noquote()<<"TASK_REAGENT_PROCESSED"<<a.id<<packet<<connection<<success<<a.observed<<message;
    advanceTaskReagents(); // 事件队列继续，原processed槽先按原逻辑退包和发送错误。
}

void frmMain::receiveTaskReagentAdoption(const OriginalTaskReagents::RuntimeAdoption &result)
{
    if (!taskReagentBridge_) return;
    auto &b=*taskReagentBridge_;
    auto it=b.actions.find(result.actionId);
    if (it==b.actions.end()) return; // 有界last/tombstone不授权清旧B/C。
    auto &a=it.value();
    if (!a.pageRequested || !a.receipt || result.actionId!=a.id || result.connection!=a.connection || result.root!=a.root ||
        result.payloadSha256!=a.payloadSha256 || result.arrivalPacketId!=a.packet || result.source!=a.seed.source ||
        result.userRequestId!=a.seed.userOperation.requestId || result.originDigest!=a.seed.originDigest || result.basisToken!=a.basis.basisToken ||
        result.permissionRevision!=a.basis.permissionRevision || result.receiptId!=a.receipt->receiptId ||
        result.transaction!=a.receipt->transaction || result.committed!=a.receipt->committed)
    { reportTaskReagent(tr("Page回执不属于原动作，未借另一ACK清任务或来源未知。")); return; }
    a.ackSeen=true; a.adoption=result;
    a.displayUnknown=a.receiptConflict || !result.snapshotAccepted || !result.runtimeApplied;
    a.unscoped=a.receiptConflict || !a.basis.available || a.basis.holes.isEmpty();
    qInfo().noquote()<<"TASK_REAGENT_DISPLAY"<<a.id<<a.packet<<result.receiptId<<result.committed
        <<result.snapshotAccepted<<result.runtimeApplied<<result.duplicate<<result.message;
    for (const auto &hole : result.holes)
        qInfo().noquote()<<"TASK_REAGENT_HOLE"<<a.id<<hole.rowId<<hole.hole<<hole.runtimeApplied<<hole.available<<hole.revision<<hole.reason;
    advanceTaskReagents();
}

void frmMain::taskReagentReadbackFinished(const OriginalTaskReagents::Commit &receipt)
{
    if (!taskReagentBridge_) return;
    auto it=taskReagentBridge_->actions.find(receipt.basis.actionId);
    if (it==taskReagentBridge_->actions.end() || !it->pageRequested || it->receiptSignature!=taskCommitSignature(receipt)) return;
    it->readerPending=false;
    advanceTaskReagents();
}

void frmMain::completeTaskReagentAction(quint64 actionId)
{
    if (!taskReagentBridge_) return;
    auto &b=*taskReagentBridge_;
    auto it=b.actions.find(actionId);
    if (it==b.actions.end()) return;
    const Action a=it.value();
    if (a.dispatched && (!a.processed || a.readerPending || a.enqueueing || (a.pageRequested && !a.ackSeen))) return;
    if (reagents_ && !a.basis.basisToken.isEmpty()) reagents_->cancelOriginalTaskReagentBasis(a.basis.basisToken,tr("原动作已真实收尾。"));
    foldTransaction(b,a); foldDisplay(b,a);
    Terminal terminal;
    terminal.id=a.id; terminal.packet=a.packet; terminal.connection=a.connection; terminal.epoch=a.epoch;
    terminal.source=a.seed.source; terminal.userOperation=a.seed.userOperation; terminal.originDigest=a.seed.originDigest;
    terminal.root=a.root; terminal.payloadSha256=a.payloadSha256; terminal.basisToken=a.basis.basisToken;
    terminal.receiptId=a.receipt ? a.receipt->receiptId : QString(); terminal.receiptSignature=a.receiptSignature;
    terminal.expected=a.expected; terminal.basisSignature=QCryptographicHash::hash(taskBasisBytes(a.basis),QCryptographicHash::Sha256);
    terminal.committed=a.receipt && a.receipt->committed; terminal.snapshotAccepted=a.adoption.snapshotAccepted;
    terminal.runtimeApplied=a.adoption.runtimeApplied;
    b.terminal.insert(actionId,terminal);
    while (b.terminal.size()>32) b.terminal.erase(b.terminal.begin());
    b.actions.erase(it); // B只留有界scope，typed大行和原包字节不成为第二个历史队列。
    if (b.actions.isEmpty() && b.actionDeadlineTimer) b.actionDeadlineTimer->stop();
    const auto raw=a.packet ? devicePackets_.take(a.packet) : QByteArray(); devicePacketBytes_-=raw.size();
    qInfo()<<"TASK_REAGENT_RETIRED"<<a.id<<a.packet<<a.displayUnknown<<a.transactionUnknown;
    checkTaskReagentSource();
    if (a.seed.source==OriginalTaskReagents::OperationSource::UserOperation)
    {
        QString message=a.message.isEmpty() ? tr("本次用户准备未完成或已取消，未发送开始。") : a.message;
        const bool current=!a.cancelled && a.epoch==b.epoch && userTaskOriginCurrent(a.seed,&message);
        const bool success=a.userSuccess && current && !a.transactionUnknown && !a.receiptConflict;
        if (!success && deviceTaskStore_) QMetaObject::invokeMethod(deviceTaskStore_,"cancelDeviceTaskPreparation",Qt::QueuedConnection);
        finishDeviceTaskPreparation(a.seed.userOperation.requestId,success,message);
    }
}

void frmMain::advanceTaskReagents()
{
    if (!taskReagentBridge_ || taskReagentBridge_->pumpQueued) return;
    taskReagentBridge_->pumpQueued=true;
    QTimer::singleShot(0,this,[this]()
    {
        if (!taskReagentBridge_) return;
        auto &b=*taskReagentBridge_; b.pumpQueued=false;
        for (const auto actionId : b.actions.keys())
        {
            auto it=b.actions.find(actionId);
            if (it==b.actions.end() || !it->processed) continue;
            auto &a=it.value();
            const bool committed=a.receiptAuthorized && a.receipt && a.receipt->committed && a.receipt->transaction==TransactionState::Committed;
            const bool unconfirmed=a.receiptAuthorized && a.receipt && !a.receipt->committed && a.receipt->transaction==TransactionState::CommitUnconfirmed;
            const bool needsPage=committed || unconfirmed;
            if (needsPage && !a.pageRequested && reagents_)
            {
                a.stage=Stage::Page; a.pageRequested=true; a.enqueueing=true;
                QString message;
                const auto receipt=*a.receipt;
                const bool queued=reagents_->enqueueOriginalTaskReagentCommit(receipt,&message);
                it=b.actions.find(actionId);
                if (it==b.actions.end()) continue;
                it->enqueueing=false; it->readerPending=queued && !it->ackSeen;
                if (!queued && !it->ackSeen)
                { it->ackSeen=true; it->displayUnknown=true; it->unscoped=!it->basis.available; it->message=message; }
            }
            it=b.actions.find(actionId);
            if (it!=b.actions.end() && (!needsPage || (it->ackSeen && !it->readerPending))) completeTaskReagentAction(actionId);
        }
        bool active=false;
        for (const auto &a : b.actions) if (a.dispatched) active=true;
        if (!active)
        {
            while (!b.waiting.isEmpty() && !b.actions.contains(b.waiting.head())) b.waiting.dequeue();
            if (!b.waiting.isEmpty())
            {
                const quint64 actionId=b.waiting.head();
                auto &a=b.actions[actionId];
                if (b.closing || b.transactionOverflow || !b.transactions.isEmpty() || a.cancelled)
                {
                    b.waiting.dequeue();
                    reportTaskReagent(tr("尚未进入后台的原任务已停止等待；原事务或身份待核，未伪造processed、回应或重放。"));
                    completeTaskReagentAction(actionId); advanceTaskReagents();
                }
                else if (!reagentLevelBusy() && b.nReaders.isEmpty() && reagents_)
                {
                    b.waiting.dequeue();
                    if (a.epoch!=b.epoch) a.cancelled=true;
                    if (a.cancelled) { completeTaskReagentAction(actionId); advanceTaskReagents(); return; }
                    QString message;
                    if (a.basisEligible && !reagents_->isBusy() && !reagents_->hasUnconfirmedOriginalLevelRuntime() &&
                        a.seed.permissionRevision==b.identity.revision && a.seed.connection==b.identity.connection)
                        a.basis=reagents_->freezeOriginalTaskReagentBasis(a.seed,&message);
                    else
                    { a.basis=a.seed; a.basis.available=false; a.basis.unavailableReason=tr("原到达时没有独立可冻结前版，未把前一动作after贴给旧包。"); }
                    if (a.seed.source==OriginalTaskReagents::OperationSource::UserOperation)
                    {
                        QString reason;
                        if (!userTaskOriginCurrent(a.seed,&reason))
                        { a.cancelled=true; a.message=reason; completeTaskReagentAction(actionId); advanceTaskReagents(); return; }
                        a.dispatched=true; a.stage=Stage::Backend;
                        const bool linuxNew=a.seed.protocolToken==QStringLiteral("LinuxNew");
                        if (!QMetaObject::invokeMethod(deviceTaskStore_,"prepareUserDeviceTasks",Qt::QueuedConnection,
                            Q_ARG(OriginalTaskReagents::RuntimeBasis,a.basis),Q_ARG(bool,linuxNew)))
                        { a.dispatched=false; a.cancelled=true; a.message=tr("用户准备请求未能进入后台，未伪造处理结束或提交。 "); completeTaskReagentAction(actionId); }
                        advanceTaskReagents(); return;
                    }
                    const QByteArray payload=devicePackets_.value(a.packet);
                    if (payload.isEmpty() || digest(payload)!=a.payloadSha256)
                    { a.cancelled=true; completeTaskReagentAction(actionId); reportTaskReagent(tr("原任务包已不在唯一预算队列，未补造或重发。")); return; }
                    a.dispatched=true; a.stage=Stage::Backend;
                    if (!QMetaObject::invokeMethod(deviceTaskStore_,"receiveDevicePacketWithTaskBasis",Qt::QueuedConnection,
                        Q_ARG(QByteArray,payload),Q_ARG(quint64,a.connection),Q_ARG(quint64,a.packet),
                        Q_ARG(OriginalTaskReagents::RuntimeBasis,a.basis)))
                    {
                        a.dispatched=false; a.cancelled=true;
                        reportTaskReagent(tr("原任务未能排入后台，已取消冻结依据；不假报设备回应或数据库提交。"));
                        completeTaskReagentAction(actionId);
                    }
                }
            }
        }
        updateDeviceControlActions();
        setProperty("taskReagentNBlocker",taskReagentNBlocker());
        syncTaskReagentDisplayUnknown();
        if (b.readerCloseIntent && !b.readerCloseRetryPosted)
        {
            bool readerBusy=!b.nReaders.isEmpty();
            for (const auto &a : b.actions)
                if (a.readerPending || (a.pageRequested && !a.ackSeen)) readerBusy=true;
            if (!readerBusy)
            { b.readerCloseRetryPosted=true; QTimer::singleShot(0,this,[this](){window()->close();}); }
        }
        if (b.closing)
        {
            bool waiting=!b.nReaders.isEmpty();
            for (const auto &a : b.actions) if (a.dispatched || a.readerPending) waiting=true;
            if (!waiting && b.closeRetryNeeded)
            { b.closeRetryNeeded=false; QTimer::singleShot(0,this,[this](){window()->close();}); }
        }
    });
}

bool frmMain::userTaskOriginCurrent(const OriginalTaskReagents::RuntimeBasis &basis,QString *message) const
{
    const auto reject=[&](const QString &reason) { if (message) *message=reason; return false; };
    if (!OriginalTaskReagents::validTaskOrigin(basis) || basis.source!=OriginalTaskReagents::OperationSource::UserOperation ||
        !deviceControl_ || basis.connection!=deviceControl_->connectionGeneration() || !deviceControl_->state().connected ||
        !samples_ || samples_->property("originalRoot").toString()!=basis.root || !projects_)
        return reject(tr("用户准备请求不再属于原连接或资料，未转交新上下文。"));
    const auto &u=basis.userOperation;
    if (OriginalTaskReagents::sampleTaskInputDigest(samples_->snapshot())!=u.sampleSnapshotDigest)
        return reject(tr("用户准备期间已保存的样本任务输入改变，未沿用旧确认。"));
    if (u.accountName!=session_.name || u.role!=session_.role || u.authenticated!=session_.authenticated ||
        u.sessionSource!=int(session_.source) || u.policyRoot!=session_.originalPolicySource || u.grantedPowerIds!=session_.grantedPowerIds)
        return reject(tr("用户准备请求的原账户或权限已经改变。"));
    for (auto file=u.configurationVersions.cbegin();file!=u.configurationVersions.cend();++file)
        if (OriginalIni::fingerprint(QDir(basis.root).filePath(file.key()),message)!=file.value())
            return reject(tr("用户准备期间原配置版本改变，未发送开始。"));
    QMap<QString,QString> versions;
    for (const auto &project : projects_->projects())
    {
        if (project.originalFileName.isEmpty() || project.originalFingerprint.isEmpty() || versions.contains(project.originalFileName))
            return reject(tr("当前项目资料身份不完整，用户准备停止。"));
        versions.insert(project.originalFileName,project.originalFingerprint);
    }
    if (versions!=u.projectVersions) return reject(tr("用户准备期间项目资料已经改变。"));
    for (auto project=versions.cbegin();project!=versions.cend();++project)
        if (OriginalProjectRepository::databaseFingerprint(QDir(basis.root).filePath(QStringLiteral("project/")+project.key()),message)!=project.value())
            return reject(tr("用户准备期间项目文件已经改变，未沿用页面旧版本。"));
    if (message) message->clear();
    return true;
}

bool frmMain::queueUserTaskPreparation(quint64 request,quint64 connection,bool linuxNew,QString *message)
{
    Q_UNUSED(linuxNew); // 协议选择绑定在原seed；后台仍按同一原协议处理。
    if (!taskReagentBridge_) attachTaskReagents();
    if (!taskReagentBridge_ || !samples_ || !projects_ || !deviceControl_) return false;
    auto &b=*taskReagentBridge_;
    if (!request || request<=b.lastUserRequest || b.closing || b.transactionOverflow || !b.transactions.isEmpty() ||
        !b.actions.isEmpty() || b.nextAction==std::numeric_limits<quint64>::max())
    { if (message) *message=tr("上一任务或用户请求仍未收尾，未重复登记。 "); return false; }
    Action a; a.id=++b.nextAction; a.connection=connection; a.epoch=b.epoch;
    a.expected=OriginalTaskReagents::Action::None; a.root=samples_->property("originalRoot").toString();
    auto &seed=a.seed; seed.source=OriginalTaskReagents::OperationSource::UserOperation;
    seed.actionId=a.id; seed.connection=connection; seed.root=a.root;
    seed.permissionRevision=b.identity.revision; seed.mainStatus=deviceControl_->state().status.mainStatus;
    seed.basisToken=QStringLiteral("user-task:%1:%2:%3").arg(b.epoch).arg(a.id).arg(request);
    auto &u=seed.userOperation; u.requestId=request; u.connection=connection; u.root=a.root;
    u.allowedAction=OriginalTaskReagents::Action::None;
    u.sampleSnapshotDigest=OriginalTaskReagents::sampleTaskInputDigest(samples_->snapshot());
    u.accountName=session_.name; u.role=session_.role; u.authenticated=session_.authenticated;
    u.sessionSource=int(session_.source); u.policyRoot=session_.originalPolicySource; u.grantedPowerIds=session_.grantedPowerIds;
    for (const auto &file : {QStringLiteral("config.ini"),QStringLiteral("config_Debug.ini"),QStringLiteral("theme_config.ini")})
        u.configurationVersions.insert(file,OriginalIni::fingerprint(QDir(a.root).filePath(file),message));
    for (const auto &project : projects_->projects())
    {
        if (u.projectVersions.contains(project.originalFileName)) return false;
        u.projectVersions.insert(project.originalFileName,project.originalFingerprint);
    }
    const auto &identity=b.identity;
    seed.actualModel=identity.actualModel; seed.configuredModel=identity.configuredModel; seed.protocolToken=identity.protocolToken;
    seed.themeFingerprint=identity.themeFingerprint; seed.capacityRelativeFile=identity.capacityRelativeFile;
    seed.capacityFingerprint=identity.capacityFingerprint; seed.capacityFileState=identity.capacityFileState;
    seed.configuredPositions=identity.configuredPositions; seed.reportedPositions=identity.reportedPositions;
    seed.capacityCalibrationActive=identity.capacityCalibrationActive;
    seed.originDigest=OriginalTaskReagents::userOperationDigest(u);
    if (!userTaskOriginCurrent(seed,message)) return false;
    a.payloadBytes=OriginalTaskReagents::userOperationBytes(u).size();
    if (a.payloadBytes>128000 || devicePackets_.size()+taskReagentRetainedPackets()>=32 ||
        devicePacketBytes_+taskReagentRetainedBytes()+a.payloadBytes>1048576)
    { if (message) *message=tr("用户请求资料超过任务队列预算，未登记或发送。 "); return false; }
    a.basisEligible=!b.displayUnknown.contains(a.root) && !b.globalDisplayUnknown;
    a.basis=seed; b.lastUserRequest=request;
    b.actions.insert(a.id,a); b.waiting.enqueue(a.id);
    if (b.actionDeadlineTimer && !b.actionDeadlineTimer->isActive()) b.actionDeadlineTimer->start();
    ensureTaskReagentFence(); advanceTaskReagents();
    return true;
}

void frmMain::cancelUserTaskPreparation(quint64 request,const QString &message)
{
    if (!taskReagentBridge_) return;
    for (auto &a : taskReagentBridge_->actions)
        if (a.seed.source==OriginalTaskReagents::OperationSource::UserOperation && a.seed.userOperation.requestId==request)
        {
            a.cancelled=true; a.message=message;
            if (reagents_ && !a.basis.basisToken.isEmpty()) reagents_->cancelOriginalTaskReagentBasis(a.basis.basisToken,message);
        }
    advanceTaskReagents();
}

void frmMain::finishUserTaskOperation(const OriginalTaskReagents::Commit &receipt,bool success,const QString &message)
{
    if (!taskReagentBridge_) return;
    auto &b=*taskReagentBridge_;
    auto it=b.actions.find(receipt.basis.actionId);
    if (it==b.actions.end()) { observeTaskReagentCommit(receipt); return; }
    const auto &expected=it.value();
    const bool belongs=expected.seed.source==OriginalTaskReagents::OperationSource::UserOperation && expected.dispatched &&
        receipt.basis.source==expected.seed.source && receipt.action==expected.expected &&
        receipt.basis.actionId==expected.id && receipt.basis.arrivalPacketId==expected.packet &&
        receipt.basis.connection==expected.connection && receipt.root==expected.root &&
        receipt.payloadSha256==expected.payloadSha256 && receipt.originDigest==expected.seed.originDigest &&
        OriginalTaskReagents::taskOriginAllows(receipt.basis,receipt.action) && taskBasisBytes(receipt.basis)==taskBasisBytes(expected.basis);
    if (!belongs)
    {
        // 可疑事实仍进原未知保护，但它不是当前worker真正结束的证明。
        observeTaskReagentCommit(receipt);
        reportTaskReagent(tr("用户完成回执不属于原请求，仍等待对应后台真实结束。"));
        return;
    }
    if (expected.processed) { observeTaskReagentCommit(receipt); return; }
    observeTaskReagentCommit(receipt);
    it=b.actions.find(receipt.basis.actionId);
    if (it==b.actions.end()) return;
    auto &a=it.value();
    const bool noWrite=receipt.action==OriginalTaskReagents::Action::None && !receipt.committed &&
        receipt.transaction==OriginalTaskReagents::TransactionState::NotStarted && !receipt.reagentSqlWritten &&
        receipt.sqlWrites.isEmpty() && receipt.tasks.isEmpty() && receipt.beforeVersions.isEmpty() && receipt.afterVersions.isEmpty() &&
        !receipt.beforePlate.complete && !receipt.afterPlate.complete && receipt.refreshHoles.isEmpty() &&
        receipt.beforePlate.rows.isEmpty() && receipt.afterPlate.rows.isEmpty() &&
        receipt.alreadyHandledTaskIds.isEmpty() && receipt.skippedAddedTaskIds.isEmpty() && receipt.refusedUncertainTaskIds.isEmpty() &&
        !receipt.postCommitVerified && !receipt.basisMatchesDatabaseBefore;
    if (!noWrite || !a.receiptAuthorized || a.receiptConflict)
    { a.transactionUnknown=true; a.displayUnknown=true; a.unscoped=true; foldTransaction(b,a); ensureTaskReagentFence(); }
    a.processed=true; a.userSuccess=success && noWrite && a.receiptAuthorized && !a.receiptConflict;
    a.message=message;
    if (b.processedSerial!=std::numeric_limits<quint64>::max()) ++b.processedSerial; else b.transactionOverflow=true;
    a.processedSerial=b.processedSerial;
    advanceTaskReagents(); // 只有真实worker返回后，才允许Page或无写退休。
}

void frmMain::enterTaskReagentCloseAttempt()
{
    if (!taskReagentBridge_) return;
    taskReagentBridge_->readerCloseIntent=false; taskReagentBridge_->readerCloseRetryPosted=false;
}

void frmMain::deferTaskReagentCatalogClose()
{
    // 只记“本任务专读忙”导致的拒绝；草稿/其他页面忙仍按原规则，不自动重试其用户选择。
    if (!taskReagentBridge_ || !reagents_ || !reagents_->isBusy() || reagents_->hasUnsavedChanges() ||
        (projects_ && (projects_->isBusy() || projects_->hasUnsavedChanges())) ||
        (samples_ && (samples_->isBusy() || samples_->hasUnsavedChanges()))) return;
    if (!taskReagentBridge_->nReaders.isEmpty())
    { taskReagentBridge_->readerCloseIntent=true; return; }
    for (const auto &a : taskReagentBridge_->actions)
        if (a.pageRequested && (a.readerPending || !a.ackSeen))
        { taskReagentBridge_->readerCloseIntent=true; return; }
}

void frmMain::cancelTaskReagentContexts(const QString &reason)
{
    if (!taskReagentBridge_) return;
    auto &b=*taskReagentBridge_;
    if (b.epoch!=std::numeric_limits<quint64>::max()) ++b.epoch; else b.transactionOverflow=true;
    b.sourceTicket.reset(); b.confirmedSources.clear(); b.independentFullIdentity=0;
    for (auto &a : b.actions)
    {
        a.cancelled=true; a.message=reason;
        if (reagents_ && !a.basis.basisToken.isEmpty()) reagents_->cancelOriginalTaskReagentBasis(a.basis.basisToken,reason);
    }
    ensureTaskReagentFence(); advanceTaskReagents();
}

bool frmMain::requestTaskReagentShutdown()
{
    if (!taskReagentBridge_) return true;
    auto &b=*taskReagentBridge_;
    if (!b.closing) { b.closing=true; cancelTaskReagentContexts(tr("程序退出，旧任务展示依据已取消；仍等真实后台收尾。")); }
    bool waiting=!b.nReaders.isEmpty();
    for (const auto &a : b.actions) if (a.dispatched || a.readerPending || !a.processed) waiting=true;
    if (waiting)
    { b.closeRetryNeeded=true; reportTaskReagent(tr("等待原任务事务/专用回读真实结束，不终止线程、不假定已回滚。")); advanceTaskReagents(); return false; }
    qInfo()<<"TASK_REAGENT_SHUTDOWN_READY";
    return true;
}
