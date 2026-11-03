#include "reagents_page.h"
#include "original_reagent_level_runtime.h"
#include "original_reagent_level_worker.h"
#include "cusreagenthole.h"
#include "ui_frmreagentmaterial.h"
#include <QTimer>
#include <QMetaObject>
#include <QSet>
#include <cmath>
#include <limits>

namespace
{
QString settingVersion(const ReagentSnapshot &snapshot,const QString &hole)
{
    for (const auto &row : snapshot.holes) if (row.name==hole) return row.originalFingerprint;
    return QString();
}

QString runtimeSource(const OriginalLevels::CurrentHoleRuntime &runtime)
{
    using OriginalLevels::MaximumSource;
    QString source;
    switch (runtime.maximumSource)
    {
    case MaximumSource::InitialBottleByName: source=QStringLiteral("本地原资料初始化的同名瓶型"); break;
    case MaximumSource::ConfirmedUr: source=QStringLiteral("已保存并核对的Ur回读"); break;
    case MaximumSource::ManualHoleSave: source=QStringLiteral("已核对的本地手工保存"); break;
    case MaximumSource::FilledHole: source=QStringLiteral("已核对的本地充满资料"); break;
    case MaximumSource::ClearedHole: source=QStringLiteral("已核对的清孔运行值"); break;
    case MaximumSource::Unknown: source=QStringLiteral("尚未确认"); break;
    }
    // maximumSource只证明独立上限的来源，不能据此把所有当前量说成本次探测实测。
    return QStringLiteral("当前孔独立运行量；最大量来源：%1。amount与预留used分开保留，不表示本次设备液面实测或动作完成。")
        .arg(source);
}

bool displayInteger(double amount, QString *text)
{
    if (!std::isfinite(amount) || amount<std::numeric_limits<int>::min() || amount>std::numeric_limits<int>::max())
    { *text=QStringLiteral("待核"); return false; }
    *text=QString::number(static_cast<int>(amount));
    return true;
}

bool finiteInput(const QVariant &value, double *number)
{
    bool ok=false;
    *number=value.toString().toDouble(&ok);
    return ok && std::isfinite(*number);
}

bool existingCountInputMatches(const ReagentSnapshot &snapshot, const OriginalLevels::CurrentHoleRuntime &runtime)
{
    const ReagentHole *hole=nullptr;
    for (const auto &row : snapshot.holes) if (row.name==runtime.hole)
    {
        if (hole) return false;
        hole=&row;
    }
    if (!hole || hole->originalValues.value("bottle_type").toString()!=runtime.bottle) return false;
    double amount=0;
    if (!finiteInput(hole->originalValues.value("amount_uL"),&amount)) return false;
    // 只核已有displayHoleTests的实际液量输入，不重算次数，也不按区域换瓶型。
    for (const auto &bottle : snapshot.bottles)
        if (bottle.originalValues.value("bottle_type_name").toString()==runtime.bottle)
        {
            double maximum=0;
            return bottle.originalFingerprint==runtime.bottleFingerprint &&
                finiteInput(bottle.originalValues.value("max_amount_ul"),&maximum) &&
                runtime.amountMicrolitres==qMin(amount,maximum);
        }
    return false; // 无首个同名瓶型时不以构造缺省值证明次数来源。
}

bool projectVersions(const ReagentProjects &projects, const QString &root, QVariantMap *versions)
{
    versions->clear();
    for (const auto &project : projects)
    {
        if (project.originalRoot!=root || project.originalFileName.isEmpty() || project.originalFingerprint.isEmpty() ||
            versions->contains(project.originalFileName)) return false;
        versions->insert(project.originalFileName,project.originalFingerprint);
    }
    return true;
}
}

OriginalLevels::CurrentHoleRuntime ReagentsPage::originalLevelRuntime(const QString &hole) const
{
    return originalLevelRuntime_ ? originalLevelRuntime_->current(hole) : OriginalLevels::CurrentHoleRuntime();
}

QString ReagentsPage::originalLevelRuntimeReason(const QString &hole) const
{
    return originalLevelRuntime_ ? originalLevelRuntime_->diagnostic(hole) : QStringLiteral("原孔位运行快照尚未建立。");
}

void ReagentsPage::notifyOriginalLevel(const QString &message)
{
    setProperty("originalLevelMessage",message);
    emit operationMessage(QStringLiteral("液位回报"),message);
}

void ReagentsPage::originalLevelReloadFailed(const QString &problem)
{
    const auto pending=pendingOriginalLevelPresentation_;
    const bool hasUr=!pendingOriginalLevelUr_.isEmpty();
    const auto manual=pendingOriginalLevelManual_;
    if (!pending && !hasUr && manual.isEmpty()) return;
    originalLevelProjectCountsVerified_=false;
    originalLevelViews_.clear();
    if (pending && !failedOriginalLevelHoles_.contains(pending->after.hole)) failedOriginalLevelHoles_.append(pending->after.hole);
    pendingOriginalLevelPresentation_.reset();
    pendingOriginalLevelUr_.clear(); pendingOriginalLevelManual_.clear();
    if (originalLevelRuntime_)
    {
        if (hasUr) { originalLevelRuntime_->invalidate(); originalLevelViews_.clear(); }
        else
        {
            for (const auto &hole : manual)
            { originalLevelRuntime_->invalidate(hole); originalLevelViews_.remove(hole); }
            if (pending)
            { originalLevelRuntime_->invalidate(pending->after.hole); originalLevelViews_.remove(pending->after.hole); }
        }
    }
    const QString message=(pending ? QStringLiteral("N已经保存，但页面资料重读失败，未报告显示完成：") :
        QStringLiteral("Ur或本地保存后的来源重读失败，等待清单已撤销，相关运行值待重新确认："))+problem;
    notifyOriginalLevel(message);
    renderOriginalLevelViews();
    emit originalLevelRuntimeChanged();
    if (pending)
        emit originalLevelPresentationFinished(pending->context.packetId,pending->context.connection,
            pending->context.permissionRevision,false,pending->savedFingerprint,message);
}

void ReagentsPage::rejectOriginalLevelObservation(const QString &hole,bool committed,const QString &message)
{
    originalLevelProjectCountsVerified_=false;
    originalLevelViews_.clear();
    if (!hole.isEmpty() && !failedOriginalLevelHoles_.contains(hole)) failedOriginalLevelHoles_.append(hole);
    if (committed && originalLevelRuntime_ && !hole.isEmpty()) originalLevelRuntime_->invalidate(hole);
    notifyOriginalLevel(message);
    renderOriginalLevelViews();
    emit originalLevelRuntimeChanged();
    // 已提交后只标待核；普通读取可触发06复制恢复，不能由失败N自动安排。
}

void ReagentsPage::setOriginalLevelContext(quint64 connection,quint64 permissionRevision)
{
    const bool changed=connection!=originalLevelConnection_ || permissionRevision!=originalLevelPermissionRevision_;
    const bool hadSources=!pendingOriginalLevelUr_.isEmpty() || !pendingOriginalLevelManual_.isEmpty();
    bool runtimeChanged=false;
    if (pendingOriginalLevelPresentation_ && changed)
    {
        const auto pending=pendingOriginalLevelPresentation_;
        pendingOriginalLevelPresentation_.reset();
        const QString message=QStringLiteral("N已在后台保存，但连接或观测许可已变，未把排队旧展示应用到新上下文。");
        notifyOriginalLevel(message);
        emit originalLevelPresentationFinished(pending->context.packetId,pending->context.connection,
            pending->context.permissionRevision,false,pending->savedFingerprint,message);
    }
    if (changed)
    {
        pendingOriginalLevelUr_.clear(); pendingOriginalLevelManual_.clear();
        QMap<QString,QVariantMap> preserved;
        if (!hadSources && (!connection || connection==originalLevelConnection_))
            for (auto view=originalLevelViews_.cbegin();view!=originalLevelViews_.cend();++view)
            {
                const auto runtime=originalLevelRuntime(view.key());
                if (originalTaskViewMatches(view.value(),runtime)) preserved.insert(view.key(),view.value());
                else
                {
                    const auto source=originalTaskIdentitySourceView(view.value(),runtime);
                    if (!source.isEmpty()) preserved.insert(view.key(),source);
                }
            }
        originalLevelViews_=preserved;
        // 独立运行值不属于许可：Ready→Testing/许可续签保留已确认值；仅撤未确认来源。
        if (originalLevelRuntime_ && hadSources)
        { originalLevelRuntime_->invalidate(); runtimeChanged=true; }
        if (connection && connection!=originalLevelConnection_)
        { originalLevelRuntime_.reset(); originalTaskReagentProviderToken_.reset(); runtimeChanged=true; }
        if (hadSources) notifyOriginalLevel(QStringLiteral("连接或观测许可已变化，待重读的Ur/本地来源已撤销，未认证到新上下文。"));
    }
    // 0表示暂撤观测许可；记住最近真实连接代次，Qj恢复同代次时不重建运行值。
    if (connection) originalLevelConnection_=connection;
    originalLevelPermissionRevision_=permissionRevision;
    originalLevelFreshUntil_=permissionRevision ? std::chrono::steady_clock::now()+std::chrono::milliseconds(3000) :
        std::chrono::steady_clock::time_point();
    if (!originalLevelRuntime_ && snapshot_.originalProfile)
    {
        originalLevelRuntime_.reset(new OriginalLevels::RuntimeProvider);
        originalTaskReagentProviderToken_=std::make_shared<char>(0);
        originalLevelRuntime_->observeSnapshot(snapshot_);
        runtimeChanged=true;
    }
    renderOriginalLevelViews();
    if (runtimeChanged) emit originalLevelRuntimeChanged();
}

void ReagentsPage::confirmOriginalLevelUr(const QByteArray &payload,bool linuxNew,quint64 connection)
{
    if (!connection || connection!=originalLevelConnection_ || !originalLevelRuntime_ || !originalLevelPermissionRevision_ ||
        std::chrono::steady_clock::now()>=originalLevelFreshUntil_)
    { notifyOriginalLevel(QStringLiteral("Ur来源没有当前连接的有效观测许可，未排队认证运行值。")); return; }
    int bytes=payload.size();
    for (const auto &pending : pendingOriginalLevelUr_) bytes+=pending.first.size();
    if (pendingOriginalLevelUr_.size()>=32 || bytes>1048576)
    {
        originalLevelRuntime_->invalidate();
        originalLevelViews_.clear(); pendingOriginalLevelUr_.clear(); pendingOriginalLevelManual_.clear();
        notifyOriginalLevel(QStringLiteral("Ur运行来源待重读清单超过32包或1MiB，未静默合并；N保存保持关闭，需重新核对资料。"));
        renderOriginalLevelViews(); emit originalLevelRuntimeChanged();
        return;
    }
    pendingOriginalLevelUr_.append(qMakePair(payload,linuxNew));
    reloadOriginal();
}

void ReagentsPage::confirmOriginalLevelManual(const QString &hole,quint64 connection)
{
    if (!connection || connection!=originalLevelConnection_ || !originalLevelRuntime_ || !originalLevelPermissionRevision_ ||
        std::chrono::steady_clock::now()>=originalLevelFreshUntil_)
    { notifyOriginalLevel(QStringLiteral("本地孔位来源没有当前连接的有效观测许可，未排队认证运行值。")); return; }
    if (pendingOriginalLevelManual_.size()>=32)
    {
        originalLevelRuntime_->invalidate(hole);
        originalLevelViews_.remove(hole);
        notifyOriginalLevel(QStringLiteral("本地孔位来源待核清单已满，未补造当前最大量。"));
        renderOriginalLevelViews(); emit originalLevelRuntimeChanged();
        return;
    }
    pendingOriginalLevelManual_.append(hole);
    reloadOriginal();
}

bool ReagentsPage::enqueueOriginalLevelPresentation(const OriginalLevels::ObservationPresentation &presentation,QString *message)
{
    const auto &context=presentation.context;
    const auto reject=[&](const QString &reason)
    { if (message) *message=reason; notifyOriginalLevel(reason); return false; };
    if (!originalProfile_ || !ready_ || context.connection!=originalLevelConnection_ ||
        !originalLevelPermissionRevision_ || std::chrono::steady_clock::now()>=originalLevelFreshUntil_ || context.permissionRevision!=originalLevelPermissionRevision_ ||
        context.root!=snapshot_.originalRoot || presentation.savedFingerprint.isEmpty() ||
        presentation.after.settingFingerprint!=presentation.savedFingerprint ||
        settingVersion(presentation.snapshot,presentation.after.hole)!=presentation.savedFingerprint)
        return reject(QStringLiteral("N已保存，但展示来源、连接、工作根或许可不属于当前页面，未覆盖页面。"));
    if (pendingOriginalLevelPresentation_)
        return reject(QStringLiteral("上一份N仍等待页面重读，未把它静默合并或替换为新展示。"));
    if (busy_ || dirty_ || pendingOriginalReload_)
        return reject(QStringLiteral("N已保存，但页面正忙或有草稿，未排入旧普通重读或复用详情。"));
    pendingOriginalLevelPresentation_=std::make_shared<const OriginalLevels::ObservationPresentation>(presentation);
    notifyOriginalLevel(QStringLiteral("N已保存且后台已核对，等待试剂后台和页面同版本重读；未报告探测完成。"));
    // 更新同一编辑器缓存，但N走持锁且拒pending的专用读口；不发保存或设备完成信号。
    setBusy(true);
    if (!QMetaObject::invokeMethod(store_,"loadOriginalLevelObservation",Qt::QueuedConnection,Q_ARG(QString,context.root)))
    {
        setBusy(false);
        originalLevelReloadFailed(QStringLiteral("N专用页面重读未能排入存储线程。"));
        return false;
    }
    return true;
}

void ReagentsPage::consumeOriginalLevelSnapshot()
{
    if (!snapshot_.originalProfile)
    {
        originalLevelRuntime_.reset(); originalTaskReagentProviderToken_.reset(); originalLevelViews_.clear();
        pendingOriginalLevelUr_.clear(); pendingOriginalLevelManual_.clear();
        const auto pending=pendingOriginalLevelPresentation_; pendingOriginalLevelPresentation_.reset();
        renderOriginalLevelViews(); emit originalLevelRuntimeChanged();
        if (pending)
        {
            const QString reason=QStringLiteral("N已提交，但页面已离开原资料上下文，未应用旧展示。");
            notifyOriginalLevel(reason);
            emit originalLevelPresentationFinished(pending->context.packetId,pending->context.connection,
                pending->context.permissionRevision,false,pending->savedFingerprint,reason);
        }
        return;
    }
    if (!originalLevelRuntime_)
    {
        originalLevelRuntime_.reset(new OriginalLevels::RuntimeProvider);
        originalTaskReagentProviderToken_=std::make_shared<char>(0);
    }
    const auto pending=pendingOriginalLevelPresentation_;
    bool accepted=false;
    QString message;
    const bool sourceOverlap=!pendingOriginalLevelUr_.isEmpty() || !pendingOriginalLevelManual_.isEmpty();
    if (pending && sourceOverlap)
        message=QStringLiteral("N已提交，但同轮还有Ur或本地来源待确认，保守拒绝本次N展示，未报告页面应用成功。");
    if (pending && !sourceOverlap && pending->context.connection==originalLevelConnection_ &&
        std::chrono::steady_clock::now()<originalLevelFreshUntil_ &&
        pending->context.permissionRevision==originalLevelPermissionRevision_ &&
        pending->context.root==snapshot_.originalRoot &&
        settingVersion(snapshot_,pending->after.hole)==pending->savedFingerprint)
        accepted=originalLevelRuntime_->acceptObservation(pending->previousSettingFingerprint,pending->after,snapshot_,&message);
    const auto pendingUr=pendingOriginalLevelUr_;
    pendingOriginalLevelUr_.clear();
    if (!pendingUr.isEmpty()) originalLevelViews_.clear();
    const bool sourcesCurrent=originalLevelConnection_ && originalLevelPermissionRevision_ &&
        std::chrono::steady_clock::now()<originalLevelFreshUntil_;
    for (const auto &entry : pendingUr)
    {
        if (!sourcesCurrent)
        {
            originalLevelRuntime_->invalidate();
            notifyOriginalLevel(QStringLiteral("Ur等待重读期间观测许可已失效，未认证该排队来源。"));
            break;
        }
        QString problem;
        if (!originalLevelRuntime_->confirmUr(entry.first,entry.second,snapshot_,&problem)) notifyOriginalLevel(problem);
    }
    const auto pendingManual=pendingOriginalLevelManual_;
    pendingOriginalLevelManual_.clear();
    for (const QString &hole : pendingManual)
    {
        originalLevelViews_.remove(hole);
        if (!sourcesCurrent)
        {
            originalLevelRuntime_->invalidate(hole);
            notifyOriginalLevel(QStringLiteral("本地孔位等待重读期间观测许可已失效，未认证该排队来源。"));
            continue;
        }
        QString problem;
        if (!originalLevelRuntime_->confirmManualHole(hole,snapshot_,&problem)) notifyOriginalLevel(problem);
    }
    if (!accepted) originalLevelRuntime_->observeSnapshot(snapshot_);
    if (pending)
    {
        pendingOriginalLevelPresentation_.reset();
        if (!accepted) originalLevelViews_.remove(pending->after.hole);
        if (accepted)
        {
            failedOriginalLevelHoles_.removeAll(pending->after.hole);
            originalLevelProjectCountsVerified_=true; // 新N已独立核完项目来源，不能由普通重读或Qj解除。
            QVariantMap view;
            view.insert(QStringLiteral("amount"),pending->after.amountMicrolitres);
            view.insert(QStringLiteral("used"),pending->after.usedMicrolitres);
            view.insert(QStringLiteral("maximum"),pending->after.maximumMicrolitres);
            view.insert(QStringLiteral("state"),pending->after.state);
            view.insert(QStringLiteral("testsAvailable"),pending->selectedHoleTests.available);
            view.insert(QStringLiteral("tests"),pending->selectedHoleTests.tests);
            view.insert(QStringLiteral("testsReason"),pending->selectedHoleTests.reason);
            QVariantMap testVersions;
            view.insert(QStringLiteral("testProjectVersionsAvailable"),projectVersions(pending->projects,pending->after.root,&testVersions));
            view.insert(QStringLiteral("testProjectVersions"),testVersions);
            // 只比较已有原项目顺序记录，不用新顺序重新猜首个项目或重算旧N次数。
            view.insert(QStringLiteral("testProjectOrder"),pending->snapshot.originalProjectOrder);
            view.insert(QStringLiteral("connection"),QVariant::fromValue(pending->context.connection));
            view.insert(QStringLiteral("permission"),QVariant::fromValue(pending->context.permissionRevision));
            view.insert(QStringLiteral("root"),pending->after.root);
            view.insert(QStringLiteral("reagent"),pending->after.reagent);
            view.insert(QStringLiteral("bottle"),pending->after.bottle);
            view.insert(QStringLiteral("area"),pending->after.area);
            const auto source=pending->capacity.source;
            const QString sourceText=source==OriginalRuntime::ReagentCapacitySource::Geometry ? QStringLiteral("本孔瓶型几何后备") :
                source==OriginalRuntime::ReagentCapacitySource::Matrix ? QStringLiteral("原标定矩阵") : QStringLiteral("零高度分支的原矩阵结果");
            view.insert(QStringLiteral("source"),QStringLiteral("最近已保存N回报：高度%1mm，采用%2；显示量与数据库预留量分别保留，不表示整次探测完成。")
                .arg(QString::number(pending->capacity.rawHeightMm),sourceText));
            originalLevelViews_.insert(pending->after.hole,view);
            message=QStringLiteral("N指定字段已保存并重读，页面已应用对应运行量；设备动作完成仍须原独立回执。");
        }
        else if (message.isEmpty()) message=QStringLiteral("N已提交，但页面重读版本或上下文已改变，未报告显示完成。");
        notifyOriginalLevel(message);
        emit originalLevelPresentationFinished(pending->context.packetId,pending->context.connection,
            pending->context.permissionRevision,accepted,pending->savedFingerprint,message);
    }
    renderOriginalLevelViews();
    emit originalLevelRuntimeChanged();
}

void ReagentsPage::renderOriginalLevelViews()
{
    if (!originalLevelProjectCountsVerified_)
        for (int row=0;row<ui->projectUseageTable->rowCount();++row)
            if (auto *item=ui->projectUseageTable->item(row,1))
            {
                item->setText(QStringLiteral("待核"));
                item->setToolTip(QStringLiteral("项目来源未核实，旧项目次数不复用；等待新的有效N。"));
            }
    bool taskAllProjects=originalTaskReagentDisplayUnknown(QString());
    bool taskMaskActive=taskAllProjects;
    QSet<QString> taskReagents;
    for (const auto &name : originalTaskReagentUnknownHoles_)
    {
        if (!originalTaskReagentDisplayUnknown(name)) continue;
        taskMaskActive=true;
        bool found=false;
        for (const auto &hole : snapshot_.holes) if (hole.name==name)
        {
            found=true;
            const QString reagent=hole.originalValues.value(QStringLiteral("reagent_name")).toString();
            if (!reagent.isEmpty() && reagent!=QStringLiteral("none")) taskReagents.insert(reagent);
        }
        if (!found) taskAllProjects=true; // 不能确认孔与项目关系时，不猜仍有正常次数。
    }
    if (taskMaskActive)
        for (int row=0;row<ui->projectUseageTable->rowCount();++row)
        {
            const auto *name=ui->projectUseageTable->item(row,0);
            auto *count=ui->projectUseageTable->item(row,1);
            if (!name || !count) continue;
            bool affected=taskAllProjects, found=false;
            for (const auto &project : projects_) if (project.name==name->text())
            {
                found=true;
                for (const auto &reagent : project.reagentNames)
                    if (taskReagents.contains(reagent)) affected=true;
            }
            if (affected || !found)
            { count->setText(QStringLiteral("待核")); count->setToolTip(originalTaskReagentDisplayUnknownReason()); }
        }
    // selectHole之外的撤许可/失败路径也必须刷新详情，不能留下上一份N文字或tooltip。
    if (snapshot_.originalProfile)
    {
        ui->lblAmount->setText(QStringLiteral("待核"));
        ui->lblTests->setText(QStringLiteral("待核"));
        ui->lblAmount->setToolTip(originalLevelRuntimeReason(selectedHole_));
        ui->lblTests->setToolTip(QStringLiteral("当前孔运行来源尚未确认，不以预留used冒充原显示量或0次。"));
        if (originalTaskReagentDisplayUnknown(selectedHole_))
        {
            ui->lblAmount->setToolTip(originalTaskReagentDisplayUnknownReason());
            ui->lblTests->setToolTip(originalTaskReagentDisplayUnknownReason());
        }
    }
    for (auto *widget : ui->reagentPanel->findChildren<cusReagentHole*>())
    {
        widget->setProperty("originalNView",false);
        widget->setProperty("originalLevelUnavailable",snapshot_.originalProfile);
        widget->setProperty("originalNTestsAvailable",false); widget->setProperty("originalNTests",0);
        widget->setToolTip(QString());
        if (!snapshot_.originalProfile) { widget->update(); continue; }
        const QString hole=widget->objectName();
        if (originalTaskReagentDisplayUnknown(hole))
        {
            widget->setToolTip(originalTaskReagentDisplayUnknownReason()); widget->update();
            continue;
        }
        if (failedOriginalLevelHoles_.contains(hole))
        {
            const QString reason=QStringLiteral("本孔最近N未完成核对，旧详情未作为本次结果；等待来源重核后的新Qj和新N。");
            widget->setToolTip(reason); widget->update();
            if (hole==selectedHole_) { ui->lblAmount->setToolTip(reason); ui->lblTests->setToolTip(reason); }
            continue;
        }
        const auto view=originalLevelViews_.value(hole);
        const auto runtime=originalLevelRuntime(hole);
        if (!runtime.available || runtime.root!=snapshot_.originalRoot ||
            runtime.settingFingerprint!=settingVersion(snapshot_,hole) || !std::isfinite(runtime.amountMicrolitres) ||
            !std::isfinite(runtime.maximumMicrolitres) || runtime.maximumMicrolitres<0)
        { widget->setToolTip(originalLevelRuntimeReason(hole)); widget->update(); continue; }
        const bool tagged=view.contains(QStringLiteral("taskIdentityRevision"));
        const bool viewScope=tagged ? originalTaskViewMatches(view,runtime) :
            originalLevelPermissionRevision_ && view.value(QStringLiteral("connection")).toULongLong()==originalLevelConnection_ &&
            view.value(QStringLiteral("permission")).toULongLong()==originalLevelPermissionRevision_;
        // 独立身份只续原显示证明，原N permission原样保留，不由此开放N写入。
        const bool nView=!view.isEmpty() && viewScope &&
            view.value(QStringLiteral("root")).toString()==runtime.root &&
            view.value(QStringLiteral("reagent")).toString()==runtime.reagent &&
            view.value(QStringLiteral("bottle")).toString()==runtime.bottle && view.value(QStringLiteral("area")).toString()==runtime.area &&
            runtime.amountMicrolitres==view.value(QStringLiteral("amount")).toDouble() &&
            runtime.usedMicrolitres==view.value(QStringLiteral("used")).toDouble() &&
            runtime.maximumMicrolitres==view.value(QStringLiteral("maximum")).toDouble() &&
            runtime.state==view.value(QStringLiteral("state")).toString();
        const QString source=nView ? view.value(QStringLiteral("source")).toString() :
            runtimeSource(runtime)+QStringLiteral(" ")+originalLevelRuntimeReason(hole);
        // 量相同才能复用原库估算；N次数还必须绑定原项目文件版本。
        const auto existing=snapshot_.holeTestCounts.value(hole);
        QVariantMap currentTestVersions;
        const bool nProjectsMatch=nView && !busy_ && !pendingOriginalReload_ &&
            view.value(QStringLiteral("testProjectVersionsAvailable")).toBool() &&
            projectVersions(projects_,runtime.root,&currentTestVersions) &&
            currentTestVersions==view.value(QStringLiteral("testProjectVersions")).toMap() &&
            snapshot_.originalProjectOrder==view.value(QStringLiteral("testProjectOrder")).toStringList();
        const bool existingInputMatches=!nView && existingCountInputMatches(snapshot_,runtime);
        const bool testsAvailable=originalLevelProjectCountsVerified_ && (nView ?
            nProjectsMatch && view.value(QStringLiteral("testsAvailable")).toBool() : existing.available && existingInputMatches);
        const int tests=nView ? view.value(QStringLiteral("tests")).toInt() : existing.tests;
        const QString testsReason=!originalLevelProjectCountsVerified_ ?
            QStringLiteral("N项目来源尚未重新核对，旧次数不复用；需新Qj及新的有效N，不重放失败观测。") : nView ? (nProjectsMatch ? view.value(QStringLiteral("testsReason")).toString() :
            QStringLiteral("N次数所用项目文件、版本或已记录顺序不再对应当前项目，液量保留，次数待核；未重新猜首个项目。")) :
            (!existingInputMatches ? QStringLiteral("当前运行量与已有次数估算的原库液量输入不对应或无法核实，次数待核；未重复计算。") : existing.reason);
        widget->setProperty("originalLevelUnavailable",false);
        widget->setProperty("originalNView",true);
        widget->setProperty("originalNAmount",runtime.amountMicrolitres);
        widget->setProperty("originalNMaximum",runtime.maximumMicrolitres);
        widget->setProperty("originalNState",runtime.state);
        widget->setProperty("originalNTestsAvailable",testsAvailable);
        widget->setProperty("originalNTests",tests);
        widget->setToolTip(source);
        widget->update();
        if (hole==selectedHole_)
        {
            QString amountText;
            const bool amountAvailable=displayInteger(runtime.amountMicrolitres,&amountText);
            ui->lblAmount->setText(amountText);
            ui->lblAmount->setToolTip(amountAvailable ? source : QStringLiteral("运行液量超出原详情int显示范围，保留原double，未截断溢出。"));
            ui->lblTests->setText(!testsAvailable ? QStringLiteral("待核") : !tests ? QStringLiteral("-") :
                tests>999 ? QStringLiteral(">999") : QString::number(tests));
            ui->lblTests->setToolTip(testsAvailable ? source : testsReason);
        }
    }
}
