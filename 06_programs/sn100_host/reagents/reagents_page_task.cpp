#include "reagents_page.h"
#include "original_reagent_level_runtime.h"
#include "cusreagenthole.h"
#include "ui_frmreagentmaterial.h"
#include <QAbstractButton>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QMetaObject>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <cmath>

namespace
{
using namespace OriginalTaskReagents;

bool sameValue(const QVariant &a,const QVariant &b)
{ return a.userType()==b.userType() && a.isNull()==b.isNull() && a==b; }

bool sameRow(const QVariantMap &a,const QVariantMap &b)
{
    if (a.keys()!=b.keys()) return false;
    for (auto field=a.cbegin();field!=a.cend();++field)
        if (!sameValue(field.value(),b.value(field.key()))) return false;
    return true;
}

bool sameSession(const AccountSession &a,const AccountSession &b)
{
    // actionReady随检测状态变化，不把正常Testing当成换账户；roleLabel只作显示。
    return a.name==b.name && a.role==b.role && a.authenticated==b.authenticated && a.source==b.source &&
        a.originalPolicySource==b.originalPolicySource && a.grantedPowerIds==b.grantedPowerIds;
}

bool sameTaskIdentity(const TaskIdentity &a,const TaskIdentity &b)
{
    return a.available==b.available && a.connection==b.connection && a.revision==b.revision &&
        a.root==b.root && a.actualModel==b.actualModel && a.configuredModel==b.configuredModel &&
        a.protocolToken==b.protocolToken && a.themeFingerprint==b.themeFingerprint &&
        a.capacityRelativeFile==b.capacityRelativeFile && a.capacityFingerprint==b.capacityFingerprint &&
        a.capacityFileState==b.capacityFileState && a.configuredPositions==b.configuredPositions &&
        a.reportedPositions==b.reportedPositions && a.capacityCalibrationActive==b.capacityCalibrationActive;
}

bool hasTaskRuntimeSemantics(const TaskIdentity &identity)
{
    // 首次完整登记前没有旧运行上下文可比较；available/revision不代表物理换算语义。
    return identity.connection && !identity.root.isEmpty() && !identity.actualModel.isEmpty() &&
        !identity.configuredModel.isEmpty() && !identity.protocolToken.isEmpty() && !identity.themeFingerprint.isEmpty() &&
        !identity.capacityRelativeFile.isEmpty() && !identity.capacityFileState.isEmpty() &&
        identity.configuredPositions>0 && identity.reportedPositions>0;
}

bool sameTaskRuntimeSemantics(const TaskIdentity &a,const TaskIdentity &b)
{
    return a.connection==b.connection && a.root==b.root && a.actualModel==b.actualModel &&
        a.configuredModel==b.configuredModel && a.protocolToken==b.protocolToken && a.themeFingerprint==b.themeFingerprint &&
        a.capacityRelativeFile==b.capacityRelativeFile && a.capacityFingerprint==b.capacityFingerprint &&
        a.capacityFileState==b.capacityFileState && a.configuredPositions==b.configuredPositions &&
        a.reportedPositions==b.reportedPositions && a.capacityCalibrationActive==b.capacityCalibrationActive;
}

bool validTaskIdentity(const TaskIdentity &identity)
{
    return identity.available && identity.connection && identity.revision && !identity.root.isEmpty() &&
        !identity.actualModel.isEmpty() && identity.actualModel==identity.configuredModel &&
        !identity.protocolToken.isEmpty() && !identity.themeFingerprint.isEmpty() &&
        !identity.capacityRelativeFile.isEmpty() && !identity.capacityFileState.isEmpty() &&
        (identity.configuredPositions==20 || identity.configuredPositions==42) &&
        identity.configuredPositions==identity.reportedPositions && !identity.capacityCalibrationActive;
}

QByteArray basisBytes(const RuntimeBasis &b)
{
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out<<b.available<<b.actionId<<b.arrivalPacketId<<b.connection<<b.basisRevision<<b.permissionRevision
       <<b.root<<b.payloadSha256<<b.basisToken<<b.protocolToken<<b.actualModel<<b.configuredModel<<b.themeFingerprint
       <<b.capacityRelativeFile<<b.capacityFingerprint<<b.capacityFileState<<b.configuredPositions<<b.reportedPositions
       <<b.mainStatus<<b.capacityCalibrationActive<<b.versions<<b.unavailableReason<<b.holes.size();
    for (auto hole=b.holes.cbegin();hole!=b.holes.cend();++hole)
        out<<hole.key()<<hole->available<<hole->rowId<<hole->hole<<hole->originalIdentity<<hole->revision
           <<hole->maximumSource<<hole->maximumMicrolitres<<hole->amountMicrolitres<<hole->usedMicrolitres
           <<hole->state<<hole->unavailableReason;
    return bytes;
}

QByteArray receiptSignature(const Commit &r)
{
    // 有界单槽重复回执核对：连typed原行、任务明细也核，不只比较receiptId或原包SHA。
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out<<r.receiptId<<int(r.action)<<basisBytes(r.basis)<<r.root<<r.payloadSha256<<r.settingTable
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
    for (const auto &row : r.refreshHoles)
        out<<row.rowId<<row.hole<<row.storedUsed<<row.sqliteType<<row.trustedBasis
           <<row.displayConversionVerified<<row.usedMicrolitres<<row.message;
    out<<r.tasks.size();
    for (const auto &task : r.tasks)
        out<<task.taskId<<task.reportRow<<task.projectId<<task.calibrationId<<task.rack<<task.type
           <<task.projectName<<task.projectFile<<task.projectFingerprint<<task.hole<<task.repeatIndex<<task.calibrationPoint
           <<task.reagentSlots<<task.reservedByHole<<task.returnedByHole<<task.skippedAdded;
    out<<r.alreadyHandledTaskIds<<r.skippedAddedTaskIds<<r.refusedUncertainTaskIds;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}

template<class Rows> bool sameOriginalRows(const Rows &a,const Rows &b)
{
    if (a.size()!=b.size()) return false;
    for (int i=0;i<a.size();++i)
        if (a[i].id!=b[i].id || a[i].originalFingerprint!=b[i].originalFingerprint ||
            !sameRow(a[i].originalValues,b[i].originalValues)) return false;
    return true;
}

bool sameSnapshot(const ReagentSnapshot &a,const ReagentSnapshot &b)
{
    return a.originalProfile && b.originalProfile && a.originalRoot==b.originalRoot &&
        a.originalProjectOrder==b.originalProjectOrder && sameOriginalRows(a.holes,b.holes) &&
        sameOriginalRows(a.bottles,b.bottles) && sameOriginalRows(a.reagents,b.reagents) &&
        sameOriginalRows(a.batches,b.batches) && sameOriginalRows(a.barcodes,b.barcodes);
}

QMap<QString,QString> pageVersions(const ReagentSnapshot &snapshot)
{
    QMap<QString,QString> versions; bool valid=true;
    const auto collect=[&](const QString &file,const QString &fingerprint)
    {
        if (fingerprint.isEmpty() || (versions.contains(file) && versions.value(file)!=fingerprint)) valid=false;
        versions.insert(file,fingerprint);
    };
    for (const auto &row : snapshot.holes) collect(QStringLiteral("db/userSet/reagent_Setting.db"),row.originalFingerprint);
    for (const auto &row : snapshot.bottles) collect(QStringLiteral("db/systemInfo/reagent_Bottle_Type.db"),row.originalFingerprint);
    for (const auto &row : snapshot.reagents) collect(QStringLiteral("db/userSet/reagent_Information.db"),row.originalFingerprint);
    for (const auto &row : snapshot.batches) collect(QStringLiteral("db/userSet/reagent_Information.db"),row.originalFingerprint);
    for (const auto &row : snapshot.barcodes) collect(QStringLiteral("db/userSet/reagent_Information.db"),row.originalFingerprint);
    return valid && versions.size()==3 ? versions : QMap<QString,QString>();
}

bool sameProjects(const ReagentProjects &a,const ReagentProjects &b)
{
    if (a.size()!=b.size()) return false;
    for (int i=0;i<a.size();++i)
        if (a[i].originalRoot!=b[i].originalRoot || a[i].originalFileName!=b[i].originalFileName ||
            a[i].originalFingerprint.isEmpty() || a[i].originalFingerprint!=b[i].originalFingerprint ||
            a[i].name!=b[i].name || a[i].id!=b[i].id || a[i].unit!=b[i].unit || a[i].enabled!=b[i].enabled ||
            a[i].reagentNames!=b[i].reagentNames || a[i].originalGroupEnabled!=b[i].originalGroupEnabled) return false;
    return true;
}

RuntimeAdoption unadopted(const Commit &r,const QString &message)
{
    RuntimeAdoption result;
    result.actionId=r.basis.actionId; result.arrivalPacketId=r.basis.arrivalPacketId;
    result.connection=r.basis.connection; result.permissionRevision=r.basis.permissionRevision;
    result.receiptId=r.receiptId; result.basisToken=r.basis.basisToken; result.root=r.root;
    result.payloadSha256=r.payloadSha256; result.transaction=r.transaction; result.committed=r.committed;
    result.message=message;
    return result;
}

bool viewMatches(const QVariantMap &view,const OriginalLevels::CurrentHoleRuntime &runtime,const RuntimeBasis &basis,
                 bool taskViewCurrent,quint64 nConnection,quint64 nPermission)
{
    const bool scope=view.contains(QStringLiteral("taskIdentityRevision")) ? taskViewCurrent :
        nPermission && nConnection==basis.connection && view.value("connection").toULongLong()==nConnection &&
        view.value("permission").toULongLong()==nPermission;
    return scope && !view.isEmpty() && runtime.available && view.value("connection").toULongLong()==basis.connection &&
        view.value("root").toString()==runtime.root &&
        view.value("reagent").toString()==runtime.reagent && view.value("bottle").toString()==runtime.bottle &&
        view.value("area").toString()==runtime.area && view.value("state").toString()==runtime.state &&
        runtime.amountMicrolitres==view.value("amount").toDouble() && runtime.usedMicrolitres==view.value("used").toDouble() &&
        runtime.maximumMicrolitres==view.value("maximum").toDouble();
}
}

bool ReagentsPage::originalTaskIdentityMatchesBasis(const RuntimeBasis &basis) const
{
    const auto &identity=originalTaskReagentIdentity_;
    return validTaskIdentity(identity) && basis.connection==identity.connection && basis.permissionRevision==identity.revision &&
        basis.root==identity.root && basis.actualModel==identity.actualModel && basis.configuredModel==identity.configuredModel &&
        basis.protocolToken==identity.protocolToken && basis.themeFingerprint==identity.themeFingerprint &&
        basis.capacityRelativeFile==identity.capacityRelativeFile && basis.capacityFingerprint==identity.capacityFingerprint &&
        basis.capacityFileState==identity.capacityFileState && basis.configuredPositions==identity.configuredPositions &&
        basis.reportedPositions==identity.reportedPositions && basis.capacityCalibrationActive==identity.capacityCalibrationActive;
}

bool ReagentsPage::originalTaskViewDataMatches(const QVariantMap &view,const OriginalLevels::CurrentHoleRuntime &runtime) const
{
    for (const auto &key : {QStringLiteral("root"),QStringLiteral("reagent"),QStringLiteral("bottle"),QStringLiteral("area"),
         QStringLiteral("state"),QStringLiteral("amount"),QStringLiteral("used"),QStringLiteral("maximum"),
         QStringLiteral("testProjectVersionsAvailable"),QStringLiteral("testProjectVersions"),QStringLiteral("testProjectOrder")})
        if (!view.contains(key)) return false;
    bool amountOk=false, usedOk=false, maximumOk=false;
    const double amount=view.value(QStringLiteral("amount")).toDouble(&amountOk);
    const double used=view.value(QStringLiteral("used")).toDouble(&usedOk);
    const double maximum=view.value(QStringLiteral("maximum")).toDouble(&maximumOk);
    if (!amountOk || !usedOk || !maximumOk || !std::isfinite(amount) || !std::isfinite(used) || !std::isfinite(maximum)) return false;
    const auto versions=pageVersions(snapshot_);
    if (view.isEmpty() || !runtime.available || runtime.root!=snapshot_.originalRoot || versions.size()!=3 ||
        !std::isfinite(runtime.amountMicrolitres) || !std::isfinite(runtime.usedMicrolitres) ||
        !std::isfinite(runtime.maximumMicrolitres) || runtime.maximumMicrolitres<0 ||
        runtime.settingFingerprint!=versions.value(QStringLiteral("db/userSet/reagent_Setting.db")) ||
        runtime.bottleFingerprint!=versions.value(QStringLiteral("db/systemInfo/reagent_Bottle_Type.db")) ||
        runtime.informationFingerprint!=versions.value(QStringLiteral("db/userSet/reagent_Information.db")) ||
        view.value("root").toString()!=runtime.root || view.value("reagent").toString()!=runtime.reagent ||
        view.value("bottle").toString()!=runtime.bottle || view.value("area").toString()!=runtime.area ||
        view.value("state").toString()!=runtime.state || runtime.amountMicrolitres!=amount ||
        runtime.usedMicrolitres!=used || runtime.maximumMicrolitres!=maximum) return false;
    QVariantMap projects;
    for (const auto &project : projects_)
    {
        if (project.originalRoot!=runtime.root || project.originalFileName.isEmpty() || project.originalFingerprint.isEmpty() ||
            projects.contains(project.originalFileName)) return false;
        projects.insert(project.originalFileName,project.originalFingerprint);
    }
    return view.value(QStringLiteral("testProjectVersionsAvailable")).toBool() &&
        projects==view.value(QStringLiteral("testProjectVersions")).toMap() &&
        snapshot_.originalProjectOrder==view.value(QStringLiteral("testProjectOrder")).toStringList();
}

bool ReagentsPage::originalTaskViewMatches(const QVariantMap &view,const OriginalLevels::CurrentHoleRuntime &runtime) const
{
    const auto &identity=originalTaskReagentIdentity_;
    const bool sourceOnly=view.value(QStringLiteral("taskIdentitySourceView")).toBool();
    const bool origin=sourceOnly ? !view.contains(QStringLiteral("actionId")) && !view.contains(QStringLiteral("receiptId")) :
        view.value(QStringLiteral("actionId")).toULongLong()!=0 && !view.value(QStringLiteral("receiptId")).toString().isEmpty();
    return validTaskIdentity(identity) && sameSession(originalTaskReagentIdentitySession_,session_) && origin &&
        view.value(QStringLiteral("taskIdentityRevision")).toULongLong()==identity.revision &&
        view.value(QStringLiteral("connection")).toULongLong()==identity.connection && runtime.root==identity.root &&
        originalTaskViewDataMatches(view,runtime);
}

QVariantMap ReagentsPage::originalTaskIdentitySourceView(const QVariantMap &view,const OriginalLevels::CurrentHoleRuntime &runtime) const
{
    const auto &identity=originalTaskReagentIdentity_;
    if (!validTaskIdentity(identity) || !sameSession(originalTaskReagentIdentitySession_,session_) ||
        busy_ || dirty_ || pendingOriginalReload_ || hasUnconfirmedOriginalLevelRuntime() ||
        view.contains(QStringLiteral("taskIdentityRevision")) || view.contains(QStringLiteral("actionId")) ||
        view.contains(QStringLiteral("receiptId")) || !originalLevelPermissionRevision_ ||
        identity.connection!=originalLevelConnection_ || identity.root!=runtime.root ||
        view.value(QStringLiteral("connection")).toULongLong()!=originalLevelConnection_ ||
        view.value(QStringLiteral("permission")).toULongLong()!=originalLevelPermissionRevision_ ||
        !originalTaskViewDataMatches(view,runtime)) return QVariantMap();
    auto saved=view;
    saved.insert(QStringLiteral("taskIdentityRevision"),QVariant::fromValue(identity.revision));
    saved.insert(QStringLiteral("taskIdentitySourceView"),true);
    // 仅登记仍可证明的原N显示前像；不制造Task采用号，也不改原N permission。
    return saved;
}

void ReagentsPage::setOriginalTaskReagentIdentity(TaskIdentity identity)
{
    if (QThread::currentThread()!=thread()) return;
    if (sameTaskIdentity(originalTaskReagentIdentity_,identity) && sameSession(originalTaskReagentIdentitySession_,session_)) return;
    const bool runtimeSemanticsChanged=hasTaskRuntimeSemantics(originalTaskReagentIdentity_) &&
        !sameTaskRuntimeSemantics(originalTaskReagentIdentity_,identity);
    if (originalTaskReagentBasis_)
        cancelOriginalTaskReagentBasis(originalTaskReagentBasis_->basisToken,QStringLiteral("当前连接、账户或试剂资料已变化，旧任务显示已停止等待。"));
    if (runtimeSemanticsChanged && originalLevelRuntime_)
        originalLevelRuntime_->invalidate(); // 保留原数值但撤可信资格、推进revision；不从DB重建最大量。
    // 真身份/账户变化时，未标记的旧N前像也属于旧来源，不能仅凭量相等重签给新身份。
    const bool removed=!originalLevelViews_.isEmpty();
    originalLevelViews_.clear();
    originalTaskReagentUnknownIdentityRevision_=0;
    originalTaskReagentUnknownRoot_.clear(); originalTaskReagentUnknownMessage_.clear();
    originalTaskReagentUnknownHoles_.clear(); originalTaskReagentUnknownUnscoped_=false;
    originalTaskReagentIdentity_=identity;
    originalTaskReagentIdentitySession_=session_;
    refreshTestCounts();
    renderOriginalLevelViews();
    if (removed || runtimeSemanticsChanged) emit originalLevelRuntimeChanged();
}

bool ReagentsPage::originalTaskReagentDisplayUnknown(const QString &hole) const
{
    return originalTaskReagentUnknownIdentityRevision_ &&
        originalTaskReagentUnknownIdentityRevision_==originalTaskReagentIdentity_.revision &&
        originalTaskReagentUnknownRoot_==originalTaskReagentIdentity_.root &&
        originalTaskReagentUnknownRoot_==snapshot_.originalRoot &&
        sameSession(originalTaskReagentIdentitySession_,session_) &&
        (originalTaskReagentUnknownUnscoped_ || originalTaskReagentUnknownHoles_.contains(hole));
}

QString ReagentsPage::originalTaskReagentDisplayUnknownReason() const
{
    return originalTaskReagentUnknownMessage_.isEmpty() ?
        QStringLiteral("任务试剂资料或提交状态仍待核，当前量与次数不能作为已确认显示。") : originalTaskReagentUnknownMessage_;
}

void ReagentsPage::setOriginalTaskReagentDisplayUnknown(quint64 identityRevision,QString root,QStringList holes,
                                                       bool unscoped,QString message)
{
    if (QThread::currentThread()!=thread() || !identityRevision || identityRevision!=originalTaskReagentIdentity_.revision ||
        root.isEmpty() || root!=originalTaskReagentIdentity_.root || root!=snapshot_.originalRoot ||
        !sameSession(originalTaskReagentIdentitySession_,session_)) return;
    const auto active=originalTaskReagentActiveHoles();
    QSet<QString> names;
    for (const auto &hole : active) names.insert(hole);
    QStringList bounded;
    if (!unscoped && !holes.isEmpty())
    {
        if (names.isEmpty() || holes.size()>names.size()) unscoped=true;
        else for (const auto &hole : holes)
        {
            if (hole.isEmpty() || !names.contains(hole)) { unscoped=true; break; }
            if (!bounded.contains(hole)) bounded.append(hole);
        }
    }
    if (unscoped) bounded.clear();
    originalTaskReagentUnknownIdentityRevision_=identityRevision;
    originalTaskReagentUnknownRoot_=root;
    originalTaskReagentUnknownHoles_=bounded;
    originalTaskReagentUnknownUnscoped_=unscoped;
    originalTaskReagentUnknownMessage_=(unscoped || !bounded.isEmpty()) ? message : QString();
    // 不发runtimeChanged，避免Root的B下发与来源观察互相触发；也不解除任何写入门。
    refreshTestCounts();
    renderOriginalLevelViews();
}

void ReagentsPage::initializeOriginalTaskReagentDisplay()
{
    // 原次数页入口先重建表格，随后重新遮住仍待核的项目，不能让旧次数重新出现。
    connect(ui->btnReagentCountPage,&QAbstractButton::clicked,this,&ReagentsPage::renderOriginalLevelViews);
    qRegisterMetaType<OriginalTaskReagents::RuntimeBasis>("OriginalTaskReagents::RuntimeBasis");
    qRegisterMetaType<OriginalTaskReagents::Commit>("OriginalTaskReagents::Commit");
    qRegisterMetaType<OriginalTaskReagents::RuntimeAdoption>("OriginalTaskReagents::RuntimeAdoption");
    qRegisterMetaType<OriginalTaskReagents::TaskIdentity>("OriginalTaskReagents::TaskIdentity");
    originalTaskReagentTimer_=new QTimer(this);
    originalTaskReagentTimer_->setSingleShot(true);
    originalTaskReagentTimer_->setTimerType(Qt::PreciseTimer);
    connect(originalTaskReagentTimer_,&QTimer::timeout,this,[this]()
    {
        if (!originalTaskReagentCommit_) return;
        const auto receipt=*originalTaskReagentCommit_;
        finishOriginalTaskReagentDisplay(receipt,rejectOriginalTaskReagentCommit(receipt,
            QStringLiteral("任务数据库提交事实保留；页面专用重读10秒未收尾，显示待核，未重发或退量。")));
    });
    connect(this,&ReagentsPage::requestSession,this,[this](const AccountSession &session)
    {
        if (originalTaskReagentBasis_ && !sameSession(originalTaskReagentSession_,session))
            cancelOriginalTaskReagentBasis(originalTaskReagentBasis_->basisToken,QStringLiteral("账户身份在任务显示期间变化，旧依据不用于新账户。"));
    });
    connect(store_,&ReagentStore::originalTaskReagentReadback,this,
        [this](const Commit &receipt,bool loaded,const ReagentSnapshot &snapshot,const ReagentProjects &projects,const QString &problem)
    {
        if (!originalTaskReagentCommit_ || receiptSignature(receipt)!=receiptSignature(*originalTaskReagentCommit_)) return;
        QString error;
        if (std::chrono::steady_clock::now()>=originalTaskReagentDeadline_)
            error=QStringLiteral("任务已提交，但页面专用重读超过原10秒单调截止时间，未因事件排队延后而续期采用。");
        else if (!loaded) error=QStringLiteral("任务已提交，但专用页面资料重读失败：")+problem;
        else if (!originalTaskReagentContextMatches(&error)) {}
        else if (!sameProjects(originalTaskReagentProjects_,projects))
            error=QStringLiteral("任务后项目文件、版本或原顺序改变，未复用冻结的次数和详情。");
        else if (snapshot.originalProjectOrder!=originalTaskReagentSnapshot_.originalProjectOrder)
            error=QStringLiteral("任务后实际配置的项目显示顺序改变，未以同文件SHA复用旧N次数。");
        if (!error.isEmpty())
        { finishOriginalTaskReagentDisplay(receipt,rejectOriginalTaskReagentCommit(receipt,error)); return; }
        // Provider先核完整typed after和当前revision；不能先普通observe使可信前版失效。
        auto result=originalLevelRuntime_->acceptTaskCommit(receipt,snapshot);
        if (result.snapshotAccepted)
        {
            for (const auto &hole : result.holes)
            {
                if (!hole.changed) continue;
                if (!hole.runtimeApplied) { originalLevelViews_.remove(hole.hole); continue; }
                auto view=originalTaskReagentViews_.value(hole.hole);
                const auto runtime=originalLevelRuntime_->current(hole.hole);
                if (!view.isEmpty())
                {
                    // N量、上限、状态与次数项目版本原样保留；任务整盘刷新只续used。
                    view.insert(QStringLiteral("used"),runtime.usedMicrolitres);
                    view.insert(QStringLiteral("taskIdentityRevision"),QVariant::fromValue(originalTaskReagentIdentity_.revision));
                    view.insert(QStringLiteral("actionId"),QVariant::fromValue(receipt.basis.actionId));
                    view.insert(QStringLiteral("receiptId"),receipt.receiptId);
                    view.remove(QStringLiteral("taskIdentitySourceView"));
                    const QString taskNote=QStringLiteral(" 任务真实提交后已按同次整盘资料刷新运行剩余量，不表示设备已加液。");
                    QString source=view.value("source").toString();
                    if (!source.endsWith(taskNote)) source+=taskNote; // 连续任务只保留一份提示，不能逐次增长。
                    view.insert(QStringLiteral("source"),source);
                    originalLevelViews_.insert(hole.hole,view);
                }
            }
            snapshot_=snapshot; projects_=projects;
            ui->reagentPanel->setSnapshot(snapshot_);
            selectHole(selectedHole_); refreshTestCounts();
        }
        finishOriginalTaskReagentDisplay(receipt,result);
        if (result.snapshotAccepted) emit snapshotChanged(snapshot_);
    },Qt::QueuedConnection);
}

OriginalTaskReagents::RuntimeBasis ReagentsPage::freezeOriginalTaskReagentBasis(RuntimeBasis seed,QString *message)
{
    bool providerRegistered=false;
    const auto reject=[&](const QString &reason)
    { if (providerRegistered && originalLevelRuntime_) originalLevelRuntime_->cancelTaskBasis(seed.basisToken);
      seed.available=false; seed.holes.clear(); seed.versions.clear(); seed.basisRevision=0;
      seed.unavailableReason=reason; if (message) *message=reason; return seed; };
    if (QThread::currentThread()!=thread()) return reject(QStringLiteral("任务页面依据只能在页面所属线程冻结。"));
    if (originalTaskReagentBasis_ || originalTaskReagentCommit_)
        return reject(QStringLiteral("上一动作的页面依据或重读尚未收尾，未覆盖它；本次任务本身仍按原流程处理。"));
    if (!seed.actionId || seed.actionId<=originalTaskReagentActionFloor_ || !seed.arrivalPacketId || seed.basisToken.isEmpty())
        return reject(QStringLiteral("任务动作编号或冻结标识缺失、已使用或过期，未复用旧依据。"));
    if (!session_.authenticated || !originalLevelRuntime_ || !originalTaskReagentProviderToken_ ||
        !originalTaskIdentityMatchesBasis(seed) || !sameSession(originalTaskReagentIdentitySession_,session_) ||
        seed.root!=snapshot_.originalRoot)
        return reject(QStringLiteral("页面没有该动作独立登记的任务身份、会话或工作根依据；不借N观察号补造，任务本身照原流程处理。"));
    if (busy_ || dirty_ || pendingOriginalReload_ || hasUnconfirmedOriginalLevelRuntime())
        return reject(QStringLiteral("页面仍有草稿、重读或未确认来源，不能补抓任务前版；任务本身不由此阻止。"));
    const auto active=originalTaskReagentActiveHoles();
    if (active.isEmpty()) return reject(QStringLiteral("活动面板孔位缺失或重名，未借隐藏的旧20孔子树冻结依据。"));
    seed=originalLevelRuntime_->freezeTaskBasis(seed,message,active);
    if (!seed.available) return seed;
    providerRegistered=true;
    if (seed.versions!=pageVersions(snapshot_)) return reject(QStringLiteral("Provider与页面的三库实际指纹不同，未冻结旧页面。"));
    if (seed.holes.size()!=snapshot_.holes.size()) return reject(QStringLiteral("Provider与页面原孔集合不同，未冻结半盘。"));
    for (const auto &hole : snapshot_.holes)
    {
        const auto frozen=seed.holes.constFind(hole.name);
        if (frozen==seed.holes.cend() || frozen->rowId!=hole.id || !sameRow(frozen->originalIdentity,hole.originalValues))
            return reject(QStringLiteral("Provider原身份行与页面快照不对应，未以同名孔替代原瓶。"));
    }
    originalTaskReagentBasis_=std::make_shared<const RuntimeBasis>(seed);
    originalTaskReagentBasisProviderToken_=originalTaskReagentProviderToken_;
    originalTaskReagentActionFloor_=seed.actionId;
    originalTaskReagentSession_=session_; originalTaskReagentSnapshot_=snapshot_; originalTaskReagentProjects_=projects_;
    originalTaskReagentCancelled_.clear(); originalTaskReagentViews_.clear();
    for (auto view=originalLevelViews_.cbegin();view!=originalLevelViews_.cend();++view)
    {
        const auto runtime=originalLevelRuntime_->current(view.key());
        if (viewMatches(view.value(),runtime,seed,originalTaskViewMatches(view.value(),runtime),
                        originalLevelConnection_,originalLevelPermissionRevision_))
            originalTaskReagentViews_.insert(view.key(),view.value());
    }
    if (message) message->clear();
    return seed;
}

QStringList ReagentsPage::originalTaskReagentActiveHoles() const
{
    QStringList names;
    QSet<QString> seen;
    // 可见性相对面板判断，页面未显示也可冻结；隐藏的旧20孔祖先不会混入。
    // 配置42位的原界面实际含46控件，不按配置数截断。
    for (const auto *hole : ui->reagentPanel->findChildren<cusReagentHole*>())
    {
        if (!hole->isVisibleTo(ui->reagentPanel)) continue;
        const QString name=hole->objectName();
        if (name.isEmpty() || seen.contains(name)) return QStringList();
        seen.insert(name); names.append(name);
    }
    return names;
}

bool ReagentsPage::originalTaskReagentContextMatches(QString *message) const
{
    const auto reject=[&](const QString &reason) { if (message) *message=reason; return false; };
    if (!originalTaskReagentBasis_ || !originalLevelRuntime_) return reject(QStringLiteral("页面冻结依据已撤销，未补用当前新值。"));
    const auto &basis=*originalTaskReagentBasis_;
    if (!originalTaskReagentCancelled_.isEmpty()) return reject(originalTaskReagentCancelled_);
    if (dirty_ || pendingOriginalReload_ || hasUnconfirmedOriginalLevelRuntime())
        return reject(QStringLiteral("任务等待期间出现草稿、普通重读或Ur/手工待核来源，未覆盖原等待。"));
    if (!originalTaskIdentityMatchesBasis(basis) || !sameSession(originalTaskReagentIdentitySession_,session_) ||
        !sameSession(originalTaskReagentSession_,session_) ||
        originalTaskReagentBasisProviderToken_.lock()!=originalTaskReagentProviderToken_)
        return reject(QStringLiteral("任务前后独立任务身份、Provider实例或账户身份不对应。"));
    if (!sameSnapshot(originalTaskReagentSnapshot_,snapshot_) || !sameProjects(originalTaskReagentProjects_,projects_))
        return reject(QStringLiteral("任务等待期间页面原身份、三库或项目资料已变化，未给旧动作换标签。"));
    QString error;
    if (!originalLevelRuntime_->taskBasisCurrent(basis,&error))
        return reject(error.isEmpty() ? QStringLiteral("任务冻结后Provider全局或逐孔revision/来源已经变化。") : error);
    return true;
}

bool ReagentsPage::enqueueOriginalTaskReagentCommit(Commit receipt,QString *message)
{
    if (QThread::currentThread()!=thread())
    { if (message) *message=QStringLiteral("任务页面凭据只能在页面所属线程排入。"); return false; }
    const auto digest=receiptSignature(receipt);
    if (originalTaskReagentLastAdoption_ && receipt.receiptId==originalTaskReagentLastAdoption_->receiptId)
    {
        if (digest!=originalTaskReagentReceiptSignature_)
        {
            const QString error=QStringLiteral("同一任务凭据标识内容冲突，未重复采用或覆盖当前等待。");
            emit originalTaskReagentAdopted(unadopted(receipt,error));
            if (message) *message=error;
            return false;
        }
        auto result=*originalTaskReagentLastAdoption_; result.duplicate=true;
        const bool contextCurrent=originalTaskIdentityMatchesBasis(receipt.basis) &&
            sameTaskIdentity(originalTaskReagentLastIdentity_,originalTaskReagentIdentity_) &&
            sameSession(originalTaskReagentIdentitySession_,session_) && sameSession(originalTaskReagentLastSession_,session_) &&
            originalTaskReagentProviderToken_ && originalTaskReagentLastProviderToken_.lock()==originalTaskReagentProviderToken_;
        if (!contextCurrent) { result.runtimeApplied=false; result.snapshotAccepted=false; }
        for (auto &hole : result.holes)
        {
            const auto runtime=originalLevelRuntime_ ? originalLevelRuntime_->current(hole.hole) : OriginalLevels::CurrentHoleRuntime();
            hole.changed=false;
            if (!contextCurrent || runtime.revision!=hole.revision) { result.runtimeApplied=false; result.snapshotAccepted=false; hole.runtimeApplied=false; }
            hole.available=runtime.available; hole.revision=runtime.revision;
        }
        emit originalTaskReagentAdopted(result);
        if (message) *message=result.message;
        return true;
    }
    if (originalTaskReagentCommit_)
    {
        if (digest==receiptSignature(*originalTaskReagentCommit_)) { if (message) *message=QStringLiteral("相同凭据仍在专用重读中，未重复入队。"); return true; }
        const QString error=QStringLiteral("上一任务页面凭据仍在处理，未覆盖等待或排无界队列。");
        emit originalTaskReagentAdopted(unadopted(receipt,error));
        if (message) *message=error;
        return false;
    }
    QString error;
    const bool registered=originalTaskReagentBasis_ && basisBytes(receipt.basis)==basisBytes(*originalTaskReagentBasis_);
    if (!registered)
    {
        error=receipt.transaction==TransactionState::CommitUnconfirmed && !receipt.committed ?
            QStringLiteral("事务提交未确认，且没有本页面登记的完整前版；不能猜测是否提交或孔范围，显示未知由Root保持。") :
            QStringLiteral("任务凭据没有本页面登记的完整冻结前版，提交事实保留，运行显示待核。");
        auto result=unadopted(receipt,error);
        if (cancelledOriginalTaskReagentBasis_ && basisBytes(receipt.basis)==basisBytes(*cancelledOriginalTaskReagentBasis_))
            result=rejectOriginalTaskReagentCommit(receipt,error);
        bool changed=false;
        for (const auto &hole : result.holes) if (hole.changed)
        {
            changed=true;
            if (!hole.available)
            {
                originalLevelViews_.remove(hole.hole);
            }
        }
        if (changed) { renderOriginalLevelViews(); emit originalLevelRuntimeChanged(); }
        emit originalTaskReagentAdopted(result);
        if (message) *message=error;
        return false;
    }
    if (!originalTaskReagentContextMatches(&error) || !verifyDisplayCommit(receipt,&error) ||
        busy_ || dirty_ || pendingOriginalReload_ || hasUnconfirmedOriginalLevelRuntime())
    {
        if (error.isEmpty()) error=QStringLiteral("页面还有草稿、重读或Ur/手工未知来源，本次任务显示待核，未清除原等待。");
        finishOriginalTaskReagentDisplay(receipt,rejectOriginalTaskReagentCommit(receipt,error));
        if (message) *message=error;
        return false;
    }
    originalTaskReagentCommit_=std::make_shared<const Commit>(receipt);
    originalTaskReagentOwnsBusy_=true; setBusy(true);
    // 只等待本次只读结果；超时不重发任务、不补偿，也不接普通loaded旁路。
    originalTaskReagentDeadline_=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    originalTaskReagentTimer_->start(10000);
    if (!QMetaObject::invokeMethod(store_,"loadOriginalTaskReagentCommit",Qt::QueuedConnection,Q_ARG(OriginalTaskReagents::Commit,receipt)))
    {
        error=QStringLiteral("任务已提交，但专用资料重读未能排入存储线程，显示待核。");
        finishOriginalTaskReagentDisplay(receipt,rejectOriginalTaskReagentCommit(receipt,error));
        if (message) *message=error;
        return false;
    }
    if (message) *message=QStringLiteral("任务数据库事实已收到，等待专用重读；尚未确认运行量采用。");
    return true;
}

OriginalTaskReagents::RuntimeAdoption ReagentsPage::rejectOriginalTaskReagentCommit(const Commit &receipt,const QString &reason)
{
    const bool unconfirmed=receipt.transaction==TransactionState::CommitUnconfirmed && !receipt.committed;
    const QString shown=unconfirmed ? QStringLiteral("任务提交结果尚未确认，旧试剂剩余量和可测次数待核。") : reason;
    if (unconfirmed) qInfo().noquote()<<"TASK_REAGENT_UNCONFIRMED_DISPLAY"<<receipt.receiptId<<int(receipt.transaction)<<receipt.committed<<reason;
    // 角色换代可以拒绝采用，但仍需撤同一Provider中尚属原basis的旧used。
    // 用生命周期token，避免Provider重建后地址或数值revision巧合复用。
    const bool active=originalTaskReagentBasis_ && basisBytes(receipt.basis)==basisBytes(*originalTaskReagentBasis_) &&
        originalTaskReagentBasisProviderToken_.lock()==originalTaskReagentProviderToken_;
    const bool cancelled=cancelledOriginalTaskReagentBasis_ && basisBytes(receipt.basis)==basisBytes(*cancelledOriginalTaskReagentBasis_) &&
        cancelledOriginalTaskReagentProviderToken_.lock()==originalTaskReagentProviderToken_;
    if (originalLevelRuntime_ && originalTaskReagentProviderToken_ && (active || cancelled) &&
        receipt.root==snapshot_.originalRoot && receipt.root==receipt.basis.root)
    {
        if (receipt.transaction==TransactionState::CommitUnconfirmed)
            return originalLevelRuntime_->markTaskUnconfirmed(receipt,shown);
        return originalLevelRuntime_->rejectTaskCommit(receipt,reason);
    }
    if (receipt.transaction==TransactionState::CommitUnconfirmed && !receipt.committed)
        return unadopted(receipt,shown+QStringLiteral(" 尚未核实对应孔位，继续保留待核显示。"));
    return unadopted(receipt,reason);
}

void ReagentsPage::finishOriginalTaskReagentDisplay(const Commit &receipt,RuntimeAdoption result)
{
    bool changed=false;
    for (const auto &hole : result.holes)
    {
        changed=changed || hole.changed;
        if (hole.changed && !hole.available)
        {
            originalLevelViews_.remove(hole.hole);
        }
    }
    originalTaskReagentReceiptSignature_=receiptSignature(receipt);
    originalTaskReagentLastSession_=originalTaskReagentSession_;
    originalTaskReagentLastIdentity_=originalTaskReagentIdentity_;
    originalTaskReagentLastProviderToken_=originalTaskReagentProviderToken_;
    originalTaskReagentLastAdoption_=std::make_shared<const RuntimeAdoption>(result);
    if (originalLevelRuntime_) originalLevelRuntime_->cancelTaskBasis(receipt.basis.basisToken);
    originalTaskReagentTimer_->stop(); originalTaskReagentCommit_.reset(); originalTaskReagentBasis_.reset();
    originalTaskReagentBasisProviderToken_.reset();
    originalTaskReagentViews_.clear(); originalTaskReagentSnapshot_=ReagentSnapshot(); originalTaskReagentProjects_.clear();
    originalTaskReagentCancelled_.clear();
    if (originalTaskReagentOwnsBusy_)
    { originalTaskReagentOwnsBusy_=false; setBusy(pendingDeviceRequest_!=0); }
    renderOriginalLevelViews();
    if (changed) emit originalLevelRuntimeChanged();
    setProperty("originalTaskReagentMessage",result.message);
    emit operationMessage(QStringLiteral("任务试剂剩余量"),result.message);
    emit originalTaskReagentAdopted(result);
}

void ReagentsPage::cancelOriginalTaskReagentBasis(const QString &token,const QString &reason)
{
    if (QThread::currentThread()!=thread() || !originalTaskReagentBasis_ || token!=originalTaskReagentBasis_->basisToken) return;
    const QString cancelledToken=token;
    originalTaskReagentCancelled_=reason.isEmpty() ? QStringLiteral("任务页面冻结依据已由原动作撤销。"):reason;
    if (originalTaskReagentCommit_)
    {
        const auto receipt=*originalTaskReagentCommit_;
        // 已知COMMIT先撤仍属旧basis的来源并回执；晚到只读结果不再应用到新动作。
        finishOriginalTaskReagentDisplay(receipt,rejectOriginalTaskReagentCommit(receipt,originalTaskReagentCancelled_));
        return;
    }
    if (originalLevelRuntime_) originalLevelRuntime_->cancelTaskBasis(cancelledToken);
    cancelledOriginalTaskReagentBasis_=originalTaskReagentBasis_;
    cancelledOriginalTaskReagentProviderToken_=originalTaskReagentBasisProviderToken_;
    originalTaskReagentBasis_.reset(); originalTaskReagentViews_.clear();
    originalTaskReagentBasisProviderToken_.reset();
    originalTaskReagentSnapshot_=ReagentSnapshot(); originalTaskReagentProjects_.clear();
}
