#include "original_reagent_level_runtime.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QSet>
#include <cmath>

namespace OriginalLevels
{
namespace
{
using namespace OriginalTaskReagents;
const QString information=QStringLiteral("db/userSet/reagent_Information.db");
const QString bottle=QStringLiteral("db/systemInfo/reagent_Bottle_Type.db");
const QString setting=QStringLiteral("db/userSet/reagent_Setting.db");

bool same(const QVariant &a,const QVariant &b)
{ return a.userType()==b.userType() && a.isNull()==b.isNull() && a==b; }
bool sameRow(const QVariantMap &a,const QVariantMap &b)
{
    if (a.keys()!=b.keys()) return false;
    for (auto it=a.cbegin();it!=a.cend();++it) if (!same(it.value(),b.value(it.key()))) return false;
    return true;
}
QString sourceName(MaximumSource source)
{
    switch (source)
    {
    case MaximumSource::InitialBottleByName: return QStringLiteral("InitialBottleByName");
    case MaximumSource::ConfirmedUr: return QStringLiteral("ConfirmedUr");
    case MaximumSource::ManualHoleSave: return QStringLiteral("ManualHoleSave");
    case MaximumSource::FilledHole: return QStringLiteral("FilledHole");
    case MaximumSource::ClearedHole: return QStringLiteral("ClearedHole");
    case MaximumSource::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}
bool snapshotVersions(const ReagentSnapshot &snapshot,QMap<QString,QString> *versions)
{
    if (!snapshot.originalProfile || snapshot.originalRoot.isEmpty()) return false;
    const auto collect=[&](const QString &file,const QString &fingerprint)
    {
        if (fingerprint.isEmpty() || (!versions->value(file).isEmpty() && versions->value(file)!=fingerprint)) return false;
        versions->insert(file,fingerprint); return true;
    };
    for (const auto &row : snapshot.holes) if (!collect(setting,row.originalFingerprint)) return false;
    for (const auto &row : snapshot.bottles) if (!collect(bottle,row.originalFingerprint)) return false;
    for (const auto &row : snapshot.reagents) if (!collect(information,row.originalFingerprint)) return false;
    for (const auto &row : snapshot.batches) if (!collect(information,row.originalFingerprint)) return false;
    for (const auto &row : snapshot.barcodes) if (!collect(information,row.originalFingerprint)) return false;
    return versions->size()==3;
}
bool snapshotMatches(const ReagentSnapshot &snapshot,const TypedPlate &plate)
{
    if (snapshot.holes.size()!=plate.rows.size()) return false;
    QMap<qint64,const TypedRow *> indexed;
    for (const auto &row : plate.rows) indexed.insert(row.values.value(QStringLiteral("id")).toLongLong(),&row);
    QSet<QString> names; QSet<qint64> ids;
    for (const auto &hole : snapshot.holes)
    {
        const auto *stored=indexed.value(hole.id,nullptr);
        if (!stored || names.contains(hole.name) || ids.contains(hole.id) ||
            hole.name!=stored->values.value(QStringLiteral("hole_name")).toString() || hole.originalValues.isEmpty()) return false;
        const QStringList required{"id","hole_name","area","reagent_type","reagent_name","reagent_batch",
            "bottle_type","amount_uL","used_ul","state","priority","datetime","out_date","formula_level",
            "out_ul","warning_ul","max_ul"};
        for (const auto &column : required) if (!hole.originalValues.contains(column)) return false;
        names.insert(hole.name); ids.insert(hole.id);
        for (auto field=hole.originalValues.cbegin();field!=hole.originalValues.cend();++field)
            if (!stored->values.contains(field.key()) || !same(field.value(),stored->values.value(field.key()))) return false;
    }
    return true;
}
RuntimeAdoption adoption(const Commit &receipt)
{
    RuntimeAdoption result;
    const auto &basis=receipt.basis;
    result.actionId=basis.actionId; result.arrivalPacketId=basis.arrivalPacketId;
    result.source=basis.source; result.originDigest=basis.originDigest; result.userRequestId=basis.userOperation.requestId;
    result.connection=basis.connection; result.permissionRevision=basis.permissionRevision;
    result.receiptId=receipt.receiptId; result.basisToken=basis.basisToken;
    result.root=receipt.root; result.payloadSha256=receipt.payloadSha256;
    result.transaction=receipt.transaction; result.committed=receipt.committed;
    return result;
}
QByteArray basisSignature(const RuntimeBasis &b)
{
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out<<OriginalTaskReagents::taskOriginBytes(b);
    out<<b.available<<b.actionId<<b.arrivalPacketId<<b.connection<<b.basisRevision<<b.permissionRevision
       <<b.root<<b.payloadSha256<<b.basisToken<<b.protocolToken<<b.actualModel<<b.configuredModel<<b.themeFingerprint
       <<b.capacityRelativeFile<<b.capacityFingerprint<<b.capacityFileState<<b.configuredPositions<<b.reportedPositions
       <<b.mainStatus<<b.capacityCalibrationActive<<b.versions<<b.unavailableReason<<b.holes.size();
    for (auto hole=b.holes.cbegin();hole!=b.holes.cend();++hole)
        out<<hole.key()<<hole->available<<hole->rowId<<hole->hole<<hole->originalIdentity<<hole->revision
           <<hole->maximumSource<<hole->maximumMicrolitres<<hole->amountMicrolitres<<hole->usedMicrolitres
           <<hole->state<<hole->unavailableReason;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
// 只为进程内重复凭据作等值校验；不代替数据库版本、完整行链或页面身份检查。
QByteArray signature(const Commit &receipt)
{
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    const auto &b=receipt.basis;
    out<<OriginalTaskReagents::taskOriginBytes(b);
    out<<receipt.receiptId<<int(receipt.action)<<receipt.root<<receipt.payloadSha256<<receipt.originDigest<<receipt.settingTable
       <<int(receipt.transaction)<<receipt.committed<<receipt.postCommitVerified<<receipt.basisMatchesDatabaseBefore
       <<receipt.reagentSqlWritten<<receipt.beforeVersions<<receipt.afterVersions<<receipt.proofMessage
       <<b.available<<b.actionId<<b.arrivalPacketId<<b.connection<<b.basisRevision<<b.permissionRevision
       <<b.root<<b.payloadSha256<<b.basisToken<<b.protocolToken<<b.actualModel<<b.configuredModel<<b.themeFingerprint
       <<b.capacityRelativeFile<<b.capacityFingerprint<<b.capacityFileState<<b.configuredPositions<<b.reportedPositions
       <<b.mainStatus<<b.capacityCalibrationActive<<b.versions<<b.unavailableReason<<b.holes.size();
    for (auto hole=b.holes.cbegin();hole!=b.holes.cend();++hole)
        out<<hole.key()<<hole->available<<hole->rowId<<hole->hole<<hole->originalIdentity<<hole->revision
           <<hole->maximumSource<<hole->maximumMicrolitres<<hole->amountMicrolitres<<hole->usedMicrolitres
           <<hole->state<<hole->unavailableReason;
    for (const auto *plate : {&receipt.beforePlate,&receipt.afterPlate})
    {
        out<<plate->complete<<plate->table<<plate->columns<<plate->rows.size();
        for (const auto &row : plate->rows) out<<row.values<<row.sqliteTypes;
    }
    out<<receipt.sqlWrites.size();
    for (const auto &write : receipt.sqlWrites) out<<write.rowId<<write.hole<<write.predicateUsed<<write.writtenUsed<<write.columns;
    out<<receipt.refreshHoles.size();
    for (const auto &hole : receipt.refreshHoles)
        out<<hole.rowId<<hole.hole<<hole.storedUsed<<hole.sqliteType<<hole.trustedBasis
           <<hole.displayConversionVerified<<hole.usedMicrolitres<<hole.message;
    out<<receipt.tasks.size();
    for (const auto &task : receipt.tasks)
        out<<task.taskId<<task.reportRow<<task.projectId<<task.calibrationId<<task.rack<<task.type
           <<task.projectName<<task.projectFile<<task.projectFingerprint<<task.hole<<task.repeatIndex<<task.calibrationPoint
           <<task.reagentSlots<<task.reservedByHole<<task.returnedByHole<<task.skippedAdded;
    out<<receipt.alreadyHandledTaskIds<<receipt.skippedAddedTaskIds<<receipt.refusedUncertainTaskIds;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
}

OriginalTaskReagents::RuntimeBasis RuntimeProvider::freezeTaskBasis(OriginalTaskReagents::RuntimeBasis seed,QString *message,
                                                                  const QStringList &activeHoles) const
{
    seed.available=false; seed.holes.clear(); seed.versions.clear(); seed.basisRevision=0;
    const auto reject=[&](const QString &reason)
    { seed.unavailableReason=reason; if (message) *message=reason; return seed; };
    if (!frozenTaskSignature_.isEmpty())
        return reject(QStringLiteral("上一任务冻结依据尚未收尾，未覆盖原动作。"));
    QSet<QString> active;
    for (const auto &hole : activeHoles)
    {
        if (hole.isEmpty() || active.contains(hole)) return reject(QStringLiteral("当前活动试剂盘孔名为空或重复。"));
        active.insert(hole);
    }
    if (active.isEmpty() || !observed_ || !snapshotValid_ || seed.root!=snapshot_.originalRoot || !seed.actionId ||
        !OriginalTaskReagents::validTaskOrigin(seed) || !seed.connection || !seed.permissionRevision || seed.basisToken.isEmpty() ||
        seed.originDigest.isEmpty() || seed.protocolToken.isEmpty() || !revision_ ||
        !snapshotVersions(snapshot_,&seed.versions))
        return reject(QStringLiteral("任务动作上下文或当前三库运行快照不完整，任务可照常处理，运行量依据待核。"));
    QSet<qint64> ids;
    for (const auto &hole : snapshot_.holes)
    {
        if (hole.id<=0 || ids.contains(hole.id) || seed.holes.contains(hole.name) || hole.originalValues.isEmpty())
            return reject(QStringLiteral("当前孔集合或原身份投影不唯一，未冻结任务运行依据。"));
        ids.insert(hole.id);
        const auto current=runtimes_.value(hole.name);
        OriginalTaskReagents::HoleRuntimeBasis basis;
        basis.rowId=hole.id; basis.hole=hole.name; basis.originalIdentity=hole.originalValues;
        basis.revision=current.revision; basis.maximumSource=sourceName(current.maximumSource);
        basis.maximumMicrolitres=current.maximumMicrolitres; basis.amountMicrolitres=current.amountMicrolitres;
        basis.usedMicrolitres=current.usedMicrolitres; basis.state=current.state;
        basis.available=active.contains(hole.name) && current.available && current.revision && current.root==seed.root && current.hole==hole.name &&
            current.maximumSource!=MaximumSource::Unknown && std::isfinite(current.maximumMicrolitres) &&
            current.maximumMicrolitres>=0 && std::isfinite(current.amountMicrolitres) && std::isfinite(current.usedMicrolitres) &&
            current.settingFingerprint==seed.versions.value(setting) && current.bottleFingerprint==seed.versions.value(bottle) &&
            current.informationFingerprint==seed.versions.value(information);
        if (!basis.available) basis.unavailableReason=active.contains(hole.name) ? diagnostic(hole.name) :
            QStringLiteral("该原库行不属于当前活动试剂盘控件，保留全行证据但无可信GUI运行来源。");
        seed.holes.insert(hole.name,basis);
    }
    seed.available=true; seed.basisRevision=revision_; seed.unavailableReason.clear();
    frozenTaskBasis_=seed; frozenTaskSignature_=basisSignature(seed);
    if (message) *message=QStringLiteral("已冻结任务前全盘运行依据；未知孔未补造最大量来源。");
    return seed;
}

void RuntimeProvider::cancelTaskBasis(const QString &token)
{
    if (!token.isEmpty() && token==frozenTaskBasis_.basisToken)
    {
        // 只留一份取消依据，供稍后真实COMMIT或提交无法确认通知撤旧值；它永远不能授权采用。
        cancelledTaskToken_=token; cancelledTaskSignature_=frozenTaskSignature_;
        frozenTaskBasis_=OriginalTaskReagents::RuntimeBasis(); frozenTaskSignature_.clear();
    }
}

bool RuntimeProvider::taskBasisCurrent(const OriginalTaskReagents::RuntimeBasis &basis,QString *message) const
{
    const auto reject=[&](const QString &reason) { if (message) *message=reason; return false; };
    QMap<QString,QString> versions;
    if (frozenTaskSignature_.isEmpty() || frozenTaskSignature_!=basisSignature(basis) ||
        !snapshotValid_ || revision_!=basis.basisRevision || snapshot_.originalRoot!=basis.root ||
        !snapshotVersions(snapshot_,&versions) || versions!=basis.versions || snapshot_.holes.size()!=basis.holes.size())
        return reject(QStringLiteral("任务冻结动作、三库或Provider全局revision已变化，未重贴旧依据。"));
    for (const auto &hole : snapshot_.holes)
    {
        const auto frozen=basis.holes.constFind(hole.name); const auto now=current(hole.name);
        if (frozen==basis.holes.cend() || frozen->rowId!=hole.id || !sameRow(frozen->originalIdentity,hole.originalValues) ||
            frozen->revision!=now.revision || (frozen->available &&
                (!now.available || frozen->maximumSource!=sourceName(now.maximumSource) ||
                 frozen->maximumMicrolitres!=now.maximumMicrolitres || frozen->amountMicrolitres!=now.amountMicrolitres ||
                 frozen->usedMicrolitres!=now.usedMicrolitres || frozen->state!=now.state)))
            return reject(QStringLiteral("任务冻结孔的当前原身份、revision或独立运行值已变化。"));
    }
    if (message) message->clear();
    return true;
}

OriginalTaskReagents::RuntimeAdoption RuntimeProvider::rejectTaskCommit(const OriginalTaskReagents::Commit &receipt,const QString &reason)
{
    return rejectTaskReceipt(receipt,reason,receipt.committed);
}

OriginalTaskReagents::RuntimeAdoption RuntimeProvider::markTaskUnconfirmed(const OriginalTaskReagents::Commit &receipt,const QString &reason)
{
    // 不要求未知事务提供已提交after；先认真实结果形状，完整原basis仍须由下面的共同入口认证。
    if (receipt.transaction!=OriginalTaskReagents::TransactionState::CommitUnconfirmed || receipt.committed ||
        receipt.receiptId.isEmpty() ||
        (receipt.action!=OriginalTaskReagents::Action::RackReservation && receipt.action!=OriginalTaskReagents::Action::UnusedReservationReturn) ||
        receipt.root.isEmpty() || receipt.root!=receipt.basis.root || !OriginalTaskReagents::taskOriginAllows(receipt.basis,receipt.action) ||
        receipt.originDigest!=receipt.basis.originDigest ||
        receipt.payloadSha256!=receipt.basis.payloadSha256)
    {
        auto result=adoption(receipt);
        result.message=QStringLiteral("不是归属原动作的提交无法确认结果，未撤销运行来源或消费冻结依据。");
        return result;
    }
    return rejectTaskReceipt(receipt,reason.isEmpty() ?
        QStringLiteral("原任务提交结果无法确认，仍属原动作的运行来源待核；未假报提交、回滚或重放。") : reason,true);
}

OriginalTaskReagents::RuntimeAdoption RuntimeProvider::rejectTaskReceipt(const OriginalTaskReagents::Commit &receipt,
                                                                    const QString &reason,bool mayWithdraw)
{
    auto result=adoption(receipt); result.message=reason;
    const QByteArray originalSignature=basisSignature(receipt.basis);
    const bool authenticated=(!frozenTaskSignature_.isEmpty() && frozenTaskSignature_==originalSignature) ||
        (!cancelledTaskSignature_.isEmpty() && cancelledTaskSignature_==originalSignature && cancelledTaskToken_==receipt.basis.basisToken);
    for (auto hole=receipt.basis.holes.cbegin();hole!=receipt.basis.holes.cend();++hole)
    {
        const auto before=current(hole.key()); bool changed=false;
        // 晚到结果不能撤销后来Ur/N/身份产生的新来源；只撤本次冻结后仍未改变的旧revision。
        if (authenticated && mayWithdraw && before.root==receipt.root && before.revision && before.revision==hole->revision)
        { invalidate(hole.key()); changed=true; }
        const auto now=current(hole.key());
        OriginalTaskReagents::HoleRuntimeAdoption item;
        item.rowId=hole->rowId; item.hole=hole.key(); item.available=now.available; item.changed=changed;
        item.revision=now.revision; item.reason=reason; result.holes.append(item);
    }
    if (authenticated)
    {
        cancelTaskBasis(receipt.basis.basisToken);
        if (cancelledTaskToken_==receipt.basis.basisToken && cancelledTaskSignature_==originalSignature)
        { cancelledTaskToken_.clear(); cancelledTaskSignature_.clear(); }
    }
    return result;
}

OriginalTaskReagents::RuntimeAdoption RuntimeProvider::acceptTaskCommit(const OriginalTaskReagents::Commit &receipt,
                                                                     const ReagentSnapshot &persistedSnapshot)
{
    const QByteArray receiptSignature=signature(receipt);
    if (!acceptedTaskReceipt_.isEmpty() && receipt.receiptId==acceptedTaskReceipt_)
    {
        if (receiptSignature!=acceptedTaskSignature_)
        { auto result=adoption(receipt); result.message=QStringLiteral("同一任务凭据标识的内容冲突，未改当前运行量。"); return result; }
        auto result=acceptedTaskAdoption_; result.duplicate=true;
        for (auto &hole : result.holes)
        {
            const auto now=current(hole.hole); hole.changed=false;
            if (now.revision!=hole.revision)
            {
                result.runtimeApplied=false; result.snapshotAccepted=false; hole.runtimeApplied=false;
                hole.reason=QStringLiteral("该凭据已处理，当前孔已有更新来源，未重放旧值。");
            }
            hole.available=now.available; hole.revision=now.revision;
        }
        result.message=QStringLiteral("同一任务凭据已处理，本次未重复推进运行revision。");
        return result;
    }
    QString problem;
    if (frozenTaskSignature_.isEmpty() || frozenTaskSignature_!=basisSignature(receipt.basis))
        return rejectTaskCommit(receipt,QStringLiteral("任务回执不属于Provider登记的原冻结动作，未撤销或覆盖当前来源。"));
    if (!OriginalTaskReagents::verifyDisplayCommit(receipt,&problem)) return rejectTaskCommit(receipt,problem);
    const auto &basis=receipt.basis;
    QMap<QString,QString> beforeVersions,afterVersions;
    if (!snapshotValid_ || revision_!=basis.basisRevision || snapshot_.originalRoot!=receipt.root ||
        persistedSnapshot.originalRoot!=receipt.root || !snapshotVersions(snapshot_,&beforeVersions) ||
        !snapshotVersions(persistedSnapshot,&afterVersions) || beforeVersions!=basis.versions ||
        afterVersions!=receipt.afterVersions || !snapshotMatches(snapshot_,receipt.beforePlate) ||
        !snapshotMatches(persistedSnapshot,receipt.afterPlate) || snapshot_.holes.size()!=basis.holes.size())
        return rejectTaskCommit(receipt,QStringLiteral("任务凭据与Provider当前revision、工作根、三库或完整行链不对应，已提交显示待核。"));
    for (const auto &hole : snapshot_.holes)
    {
        const auto frozen=basis.holes.constFind(hole.name); const auto now=current(hole.name);
        if (frozen==basis.holes.cend() || frozen->rowId!=hole.id || frozen->hole!=hole.name ||
            !sameRow(frozen->originalIdentity,hole.originalValues) || frozen->revision!=now.revision ||
            (frozen->available &&
                (!now.available || frozen->maximumSource!=sourceName(now.maximumSource) || frozen->maximumMicrolitres!=now.maximumMicrolitres ||
                 frozen->amountMicrolitres!=now.amountMicrolitres || frozen->usedMicrolitres!=now.usedMicrolitres || frozen->state!=now.state)))
            return rejectTaskCommit(receipt,QStringLiteral("任务冻结孔与当前独立运行来源或原身份不同，未用新来源替换旧basis。"));
    }
    if (quint64(receipt.refreshHoles.size())>std::numeric_limits<quint64>::max()-revision_)
        return rejectTaskCommit(receipt,QStringLiteral("任务已提交，但全盘运行revision余量不足，未部分发布或回绕。"));
    int trustedCount=0;
    for (const auto &hole : basis.holes) if (hole.available) ++trustedCount;
    if (!trustedCount)
        return rejectTaskCommit(receipt,QStringLiteral("任务已提交，但冻结前没有可信孔；不把空集合当运行采用成功。"));
    auto result=adoption(receipt); result.snapshotAccepted=true; result.runtimeApplied=true;
    QMap<QString,CurrentHoleRuntime> proposed; QMap<QString,QString> reasons;
    for (const auto &refresh : receipt.refreshHoles)
    {
        auto runtime=current(refresh.hole); const auto frozen=basis.holes.value(refresh.hole);
        QString reason;
        const bool applied=frozen.available && refresh.trustedBasis && refresh.displayConversionVerified;
        if (applied)
        {
            // 原slotRefreshRemainder只setUseVol；保留可信max来源/max/amount/state，不夹到amount。
            runtime.usedMicrolitres=refresh.usedMicrolitres;
            reason=QStringLiteral("任务真实提交后的整盘used已采用；保留本孔原最大量来源、液量及状态，不表示设备已加液。")+refresh.message;
        }
        else
        {
            runtime.available=false; runtime.maximumSource=MaximumSource::Unknown;
            reason=frozen.available ? QStringLiteral("任务已提交，本孔used显示转换未核实：")+refresh.message :
                QStringLiteral("本孔冻结前无可信运行来源，任务整盘刷新未从库max补造来源。");
            if (frozen.available) result.runtimeApplied=false;
        }
        runtime.settingFingerprint=afterVersions.value(setting); runtime.bottleFingerprint=afterVersions.value(bottle);
        runtime.informationFingerprint=afterVersions.value(information);
        proposed.insert(refresh.hole,runtime); reasons.insert(refresh.hole,reason);
    }
    // 以上先构造整盘并完成全部校验；发布时没有SQL、回调或事件循环重入。
    for (auto hole=proposed.cbegin();hole!=proposed.cend();++hole)
    {
        put(hole.value(),reasons.value(hole.key())); const auto now=current(hole.key());
        OriginalTaskReagents::HoleRuntimeAdoption item;
        item.rowId=basis.holes.value(hole.key()).rowId; item.hole=hole.key(); item.runtimeApplied=now.available;
        item.available=now.available; item.changed=true; item.revision=now.revision; item.reason=diagnostic(hole.key());
        result.holes.append(item);
    }
    snapshot_=persistedSnapshot; snapshotValid_=true; observed_=true;
    previousValid_=false; previousRuntimes_.clear(); previousTransitionRevisions_.clear();
    result.message=result.runtimeApplied ? QStringLiteral("任务已提交，全盘已有可信前版的孔已按同次after更新剩余量；未知孔仍待核。") :
        QStringLiteral("任务已提交，整盘快照已采用，部分孔运行剩余量待核；未重放、退量或发送设备动作。");
    acceptedTaskReceipt_=receipt.receiptId; acceptedTaskSignature_=receiptSignature; acceptedTaskAdoption_=result;
    cancelTaskBasis(basis.basisToken);
    return result;
}
}
