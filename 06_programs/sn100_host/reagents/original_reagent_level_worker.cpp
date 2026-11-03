#include "original_reagent_level_worker.h"
#include "reagents/original_reagent_editor.h"
#include "projects/original_project_repository.h"
#include "projects/original_project_copy.h"
#include "original_files/theme_configuration.h"
#include <QDir>
#include <QMetaObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QThread>
#include <chrono>
#include <cmath>

namespace OriginalLevels
{
namespace
{
const QString informationFile=QStringLiteral("db/userSet/reagent_Information.db");
const QString bottleFile=QStringLiteral("db/systemInfo/reagent_Bottle_Type.db");
const QString settingFile=QStringLiteral("db/userSet/reagent_Setting.db");

bool fail(QString *message, const QString &reason)
{
    if (message) *message=reason;
    return false;
}

qint64 monotonicNanoseconds()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool sameContext(const Context &first, const Context &second)
{
    // packetId属于每次到达，不属于Qj许可的语义。
    return first.connection==second.connection && first.permissionRevision==second.permissionRevision &&
        first.root==second.root && first.actualModel==second.actualModel &&
        first.configuredModel==second.configuredModel && first.themeFingerprint==second.themeFingerprint &&
        first.capacityRelativeFile==second.capacityRelativeFile && first.capacityFileState==second.capacityFileState &&
        first.capacityFingerprint==second.capacityFingerprint &&
        first.protocolToken==second.protocolToken && first.linuxNew==second.linuxNew && first.configuredPositions==second.configuredPositions &&
        first.reportedPositions==second.reportedPositions && first.mainStatus==second.mainStatus &&
        first.capacityCalibrationActive==second.capacityCalibrationActive;
}

bool sameVersions(const CurrentHoleRuntime &first, const CurrentHoleRuntime &second)
{
    return first.root==second.root && first.settingFingerprint==second.settingFingerprint &&
        first.bottleFingerprint==second.bottleFingerprint && first.informationFingerprint==second.informationFingerprint;
}

bool sameRuntime(const CurrentHoleRuntime &first, const CurrentHoleRuntime &second)
{
    return sameVersions(first,second) && first.available==second.available && first.hole==second.hole &&
        first.reagent==second.reagent && first.bottle==second.bottle && first.area==second.area &&
        first.reagentType==second.reagentType && first.state==second.state && first.revision==second.revision &&
        first.maximumSource==second.maximumSource && first.maximumMicrolitres==second.maximumMicrolitres &&
        first.amountMicrolitres==second.amountMicrolitres && first.usedMicrolitres==second.usedMicrolitres;
}

bool knownMaximum(MaximumSource source)
{
    switch (source)
    {
    case MaximumSource::InitialBottleByName:
    case MaximumSource::ConfirmedUr:
    case MaximumSource::ManualHoleSave:
    case MaximumSource::FilledHole:
    case MaximumSource::ClearedHole: return true;
    case MaximumSource::Unknown: return false;
    }
    return false;
}

bool savedFieldsMatch(const ReagentSnapshot &snapshot, const PreparedObservation &prepared,
                      const QString &fingerprint, QString *message)
{
    if (!snapshot.originalProfile || snapshot.originalRoot!=prepared.context.root || fingerprint.isEmpty())
        return fail(message,QStringLiteral("N已提交，但重读的页面资料工作根或保存指纹不符。"));
    int matches=0;
    for (const auto &hole : snapshot.holes)
        if (hole.id==prepared.holeId && hole.name==prepared.received.hole)
        {
            ++matches;
            const auto &row=hole.originalValues;
            bool amountOk=false;
            const qlonglong amount=row.value(QStringLiteral("amount_uL")).toString().toLongLong(&amountOk);
            if (hole.originalFingerprint!=fingerprint || !row.contains(QStringLiteral("formula_level")) ||
                !row.contains(QStringLiteral("amount_uL")) || !row.contains(QStringLiteral("used_ul")) ||
                row.value(QStringLiteral("formula_level")).toString()!=prepared.formulaText ||
                !amountOk || amount!=prepared.amountInteger ||
                row.value(QStringLiteral("used_ul")).toString()!=prepared.usedText)
                return fail(message,QStringLiteral("N已提交，但目标孔重读的高度、整数液量或used_ul未对应本次保存。"));
        }
    return matches==1 || fail(message,QStringLiteral("N已提交，但重读未取得唯一目标孔，未报告展示就绪。"));
}
}

struct ObservationWorker::Permit
{
    explicit Permit(const Context &value) : context(value) {}
    const Context context;
    std::atomic<qint64> freshUntil{0};
};

struct ObservationWorker::Job
{
    Context context;
    ReceivedPacket packet;
    std::shared_ptr<Permit> permit;
    std::shared_ptr<const CurrentHoleRuntime> runtime;
};

ObservationWorker::ObservationWorker(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<Context>("OriginalLevels::Context");
    qRegisterMetaType<ObservationPresentation>("OriginalLevels::ObservationPresentation");
}

ObservationWorker::~ObservationWorker()
{
    revoke();
}

void ObservationWorker::prepareSource(quint64 request, QString root)
{
    Context source;
    source.root=root;
    if (QThread::currentThread()!=thread())
    {
        emit sourceReady(request,source,QStringLiteral("N来源预读必须排入所属存储线程，未在调用线程读取资料。"));
        return;
    }
    if (root.isEmpty() || !QDir::isAbsolutePath(root))
    {
        emit sourceReady(request,source,QStringLiteral("N来源工作根必须是已选择的绝对路径。"));
        return;
    }
    QString message;
    if (!OriginalProjectCopy::noPending(root,&message))
    {
        emit sourceReady(request,source,QStringLiteral("N项目复制仍待核，观测来源保持关闭：")+message);
        return;
    }
    const QString themePath=QDir(root).filePath(QStringLiteral("theme_config.ini"));
    const QString before=OriginalProjectRepository::databaseFingerprint(themePath,&message);
    OriginalRuntime::ThemeOptions theme;
    if (before.isEmpty() || !loadThemeConfiguration(themePath,&theme,&message))
    {
        emit sourceReady(request,source,QStringLiteral("N来源主题读取失败：")+
            (message.isEmpty() ? QStringLiteral("未取得有效主题及版本。") : message));
        return;
    }
    OriginalRuntime::ReagentCapacityResult capacity;
    if (!OriginalRuntime::readReagentCapacityVersion(root,theme.driverModel,&capacity,&message))
    {
        emit sourceReady(request,source,QStringLiteral("N来源容量版本读取失败：")+
            (message.isEmpty() ? QStringLiteral("未取得选定容量文件的存在状态与版本。") : message));
        return;
    }
    const QString after=OriginalProjectRepository::databaseFingerprint(themePath,&message);
    if (after.isEmpty() || after!=before)
    {
        emit sourceReady(request,source,QStringLiteral("N来源预读期间主题版本变化或无法复核：")+
            (message.isEmpty() ? QStringLiteral("请重新读取来源。") : message));
        return;
    }
    if (!OriginalProjectCopy::noPending(root,&message))
    {
        emit sourceReady(request,source,QStringLiteral("N来源预读期间出现待核项目复制，未发布来源：")+message);
        return;
    }
    source.configuredModel=theme.driverModel;
    source.configuredPositions=theme.reagentPositions;
    source.themeFingerprint=before;
    source.capacityRelativeFile=capacity.relativeFile;
    source.capacityFileState=capacity.fileState;
    source.capacityFingerprint=capacity.fingerprint;
    // 连接、actualModel和permissionRevision仍未知；这里只回交已读来源，不签发许可。
    emit sourceReady(request,source,QString());
}

bool ObservationWorker::setObservationContext(Context context, int freshMs, QString *message)
{
    return setObservationContextImpl(context,freshMs,0,false,message);
}

bool ObservationWorker::setTaskAdmissionFence(quint64 connection, quint64 token, bool blocked, QString *message)
{
    if (message) message->clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connection || !token)
        return fail(message,QStringLiteral("任务N准入门缺少原连接或非零任务令牌，未改变准入。"));
    if (blocked)
    {
        if (token<taskFenceToken_ || (token==taskFenceToken_ && connection!=taskFenceConnection_))
            return fail(message,QStringLiteral("任务N准入门令牌已过期或连接不符，未改变准入。"));
        // 相同任务的重复关闭不重放；即使已经开门，也不能被晚到的旧关闭再次挡住。
        if (token==taskFenceToken_) return true;
        taskFenceConnection_=connection;
        taskFenceToken_=token;
        taskFenceBlocked_=true;
        observationAdmission_=false;
        return true;
    }
    if (connection!=taskFenceConnection_ || token!=taskFenceToken_)
        return fail(message,QStringLiteral("任务N准入门只允许原连接和当前任务令牌开门，未改变准入。"));
    if (!taskFenceBlocked_) return true;
    taskFenceBlocked_=false;
    // 只解除任务占用；任务期间续过的旧许可不能直接受理新的N。
    observationAdmission_=false;
    return true;
}

bool ObservationWorker::setTaskObservationFacts(Context context, int freshMs, quint64 token, QString *message)
{
    return setObservationContextImpl(context,freshMs,token,true,message);
}

bool ObservationWorker::setObservationContextImpl(Context context, int freshMs, quint64 taskToken,
                                                   bool taskFacts, QString *message)
{
    if (message) message->clear();
    // 等待互斥锁不能延长这次实际Qj的新鲜期限；无效freshMs不会使用这个期限。
    const qint64 deadline=monotonicNanoseconds()+qint64(freshMs)*1000000;
    std::lock_guard<std::mutex> lock(mutex_);
    if (taskFacts)
    {
        if (!taskFenceBlocked_ || !taskToken || taskToken!=taskFenceToken_ ||
            !context.connection || context.connection!=taskFenceConnection_)
            return fail(message,QStringLiteral("任务Qj事实不属于当前关闭的原连接和任务令牌，未改变N来源。"));
    }
    else if (taskFenceBlocked_)
        return fail(message,QStringLiteral("任务期间只接收对应任务的实际Qj事实，未签发新N准入。"));
    if (freshMs<=0 || freshMs>3000 || !context.connection || !context.permissionRevision ||
        context.root.isEmpty() || !QDir::isAbsolutePath(context.root) || context.actualModel.isEmpty() ||
        context.actualModel!=context.configuredModel || context.themeFingerprint.isEmpty() ||
        context.linuxNew!=(context.protocolToken==QStringLiteral("LinuxNew")) ||
        context.capacityRelativeFile!=(context.actualModel==QStringLiteral("SN450") ? QStringLiteral("calibrationFile/ReagentAmount_450.json") : QStringLiteral("calibrationFile/ReagentAmount_600.json")) ||
        context.capacityFileState==OriginalRuntime::ReagentCapacityFileState::Unknown ||
        (context.capacityFileState==OriginalRuntime::ReagentCapacityFileState::Present && context.capacityFingerprint.isEmpty()) ||
        (context.capacityFileState==OriginalRuntime::ReagentCapacityFileState::Missing && !context.capacityFingerprint.isEmpty()) ||
        (context.configuredPositions!=20 && context.configuredPositions!=42) ||
        context.configuredPositions!=context.reportedPositions || context.mainStatus<0 || context.mainStatus>6 ||
        context.capacityCalibrationActive)
    {
        permit_.reset();
        runtimes_.clear();
        observationAdmission_=false;
        return fail(message,QStringLiteral("实际Qj上下文或1至3000ms新鲜期限无效，N保存许可已撤销。"));
    }
    context.packetId=0;
    if (!permit_ || !sameContext(permit_->context,context))
    {
        permit_.reset();
        runtimes_.clear();
        if (idConnection_!=context.connection)
        {
            idConnection_=context.connection;
            lastAcceptedPacket_=0;
        }
        permit_=std::make_shared<Permit>(context);
    }
    permit_->freshUntil.store(deadline,std::memory_order_release);
    observationAdmission_=!taskFacts;
    // 旧job仍占在途名额，直到它在所属线程结束；不能因换许可并排接收第二条。
    return true;
}

bool ObservationWorker::isContextCurrent(const Context &context) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return permit_ && sameContext(permit_->context,context) &&
        monotonicNanoseconds()<permit_->freshUntil.load(std::memory_order_acquire);
}

void ObservationWorker::revoke()
{
    std::lock_guard<std::mutex> lock(mutex_);
    permit_.reset();
    runtimes_.clear();
    observationAdmission_=false;
}

bool ObservationWorker::publishRuntime(quint64 arrivalConnection, const CurrentHoleRuntime &runtime, QString *message)
{
    if (message) message->clear();
    const QStringList states{QStringLiteral("r_none"),QStringLiteral("r_normal"),QStringLiteral("r_warning"),
                            QStringLiteral("r_useout"),QStringLiteral("r_unknown"),QStringLiteral("r_outdate")};
    if (!runtime.available || !runtime.revision || runtime.revision==std::numeric_limits<quint64>::max() ||
        !knownMaximum(runtime.maximumSource) || runtime.settingFingerprint.isEmpty() ||
        runtime.bottleFingerprint.isEmpty() || runtime.informationFingerprint.isEmpty() ||
        !std::isfinite(runtime.maximumMicrolitres) || runtime.maximumMicrolitres<0 ||
        runtime.maximumMicrolitres>std::numeric_limits<int>::max() || !std::isfinite(runtime.amountMicrolitres) ||
        !std::isfinite(runtime.usedMicrolitres) || !states.contains(runtime.state) || runtime.bottle.isEmpty() ||
        (runtime.area!=QStringLiteral("r_area") && runtime.area!=QStringLiteral("s_area")))
        return fail(message,QStringLiteral("本孔独立运行快照缺少有效来源、版本、revision或数值，未发布N保存依据。"));
    std::lock_guard<std::mutex> lock(mutex_);
    if (!permit_ || !arrivalConnection || arrivalConnection!=permit_->context.connection ||
        runtime.root!=permit_->context.root || monotonicNanoseconds()>=permit_->freshUntil.load(std::memory_order_acquire))
        return fail(message,QStringLiteral("运行快照不属于当前新鲜连接，未改贴连接代次。"));
    const QRegularExpression visible(permit_->context.configuredPositions==42 ?
        QStringLiteral("^([A-F]_[1-7]|SD_[1-4])$") : QStringLiteral("^(R_([1-9]|1[0-9]|20)|SD_[1-4])$"));
    if (!visible.match(runtime.hole).hasMatch())
        return fail(message,QStringLiteral("运行快照孔位不属于当前20或42位面板。"));
    const auto before=runtimes_.value(runtime.hole);
    if (before)
    {
        if (sameRuntime(*before,runtime)) return true;
        if (runtime.revision<=before->revision)
            return fail(message,QStringLiteral("运行快照revision没有前进，未用旧值替换当前N依据。"));
    }
    for (auto entry=runtimes_.begin();entry!=runtimes_.end();)
        if (!sameVersions(*entry.value(),runtime)) entry=runtimes_.erase(entry);
        else ++entry;
    runtimes_.insert(runtime.hole,std::make_shared<const CurrentHoleRuntime>(runtime));
    return true;
}

void ObservationWorker::invalidateRuntime(quint64 arrivalConnection, const QString &hole)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!permit_ || arrivalConnection!=permit_->context.connection) return;
    if (hole.isEmpty()) runtimes_.clear();
    else runtimes_.remove(hole);
}

bool ObservationWorker::submit(quint64 arrivalConnection, quint64 localPacketId, QByteArray payload, QString *message)
{
    if (message) message->clear();
    std::shared_ptr<Permit> permit;
    quint64 admissionTaskToken=0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (taskFenceBlocked_ || !observationAdmission_)
            return fail(message,QStringLiteral("任务暂停新N准入，或尚未收到开门后的实际Qj，未排队。"));
        if (readbackPending_ || !arrivalConnection || readbackConnection_!=arrivalConnection)
            return fail(message,QStringLiteral("原试剂回读/准备占用，或尚未由新Qj重核开放N写入门，未排队。"));
        permit=permit_;
        if (!permit || !arrivalConnection || arrivalConnection!=permit->context.connection ||
            monotonicNanoseconds()>=permit->freshUntil.load(std::memory_order_acquire))
            return fail(message,QStringLiteral("N没有到达原连接的新鲜保存许可，未排队。"));
        admissionTaskToken=taskFenceToken_;
    }
    ReceivedPacket packet;
    if (!decodePacket(payload,permit->context.linuxNew,&packet,message)) return false;
    if (packet.kind!=PacketKind::Reagent)
        return fail(message,QStringLiteral("本入口只被动保存试剂N，样本N已忽略。"));
    std::shared_ptr<const Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (taskFenceBlocked_ || !observationAdmission_ || taskFenceToken_!=admissionTaskToken)
            return fail(message,QStringLiteral("解码期间任务N准入变化，或尚未收到开门后的实际Qj，未排队。"));
        if (readbackPending_ || readbackConnection_!=arrivalConnection || permit_!=permit ||
            monotonicNanoseconds()>=permit->freshUntil.load(std::memory_order_acquire))
            return fail(message,QStringLiteral("解码期间N许可变化，未排队。"));
        if (pending_) return fail(message,QStringLiteral("已有一条N正在处理，本条未保存或合并，请保留接收记录。"));
        if (!localPacketId || idConnection_!=arrivalConnection || localPacketId<=lastAcceptedPacket_)
            return fail(message,QStringLiteral("N本地接收序号无效或已过期，未重复保存。"));
        const auto runtime=runtimes_.value(packet.hole);
        if (!runtime) return fail(message,QStringLiteral("本孔没有当前独立运行快照，N未排队保存。"));
        if (runtime->revision==std::numeric_limits<quint64>::max())
            return fail(message,QStringLiteral("本孔运行revision已用尽，N未排队或溢出后重用旧编号。"));
        auto next=std::make_shared<Job>();
        next->context=permit->context; next->context.packetId=localPacketId;
        next->packet=packet; next->permit=permit; next->runtime=runtime;
        job=next; pending_=job; lastAcceptedPacket_=localPacketId;
    }
    if (!QMetaObject::invokeMethod(this,[this,job]() { process(job); },Qt::QueuedConnection))
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_==job) pending_.reset();
        return fail(message,QStringLiteral("N未能排入所属存储线程；接收序号已消费，不自动重试。"));
    }
    return true;
}

bool ObservationWorker::isBusy() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return bool(pending_);
}

void ObservationWorker::setReagentReadbackPending(quint64 connection,bool pending)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 清掉在途依据，使短暂关门后也不能让旧N恢复；不改变真实Qj或Ur到达语义。
    if (pending || connection!=readbackConnection_) runtimes_.clear();
    readbackConnection_=connection;
    readbackPending_=pending || !connection;
}

bool ObservationWorker::currentLocked(const std::shared_ptr<const Job> &job) const
{
    return job && !readbackPending_ && readbackConnection_==job->context.connection &&
        pending_==job && permit_==job->permit && sameContext(permit_->context,job->context) &&
        job->context.connection==job->permit->context.connection &&
        monotonicNanoseconds()<job->permit->freshUntil.load(std::memory_order_acquire) &&
        runtimes_.value(job->packet.hole)==job->runtime && job->runtime->revision!=0;
}

bool ObservationWorker::current(const std::shared_ptr<const Job> &job) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return currentLocked(job);
}

void ObservationWorker::invalidateRelatedLocked(const std::shared_ptr<const Job> &job,
                                                const QString &savedFingerprint, bool committed)
{
    if (!committed)
    {
        // 无法确认依赖版本时不保留这一许可下的其他旧依据；新上下文的快照不受旧job影响。
        if (permit_==job->permit) runtimes_.clear();
        return;
    }
    // 已提交会改变全孔位库版本；只使同根、同资料版的旧/本次写入相关快照失效。
    for (auto entry=runtimes_.begin();entry!=runtimes_.end();)
    {
        const auto &runtime=*entry.value();
        if (runtime.root==job->runtime->root && runtime.informationFingerprint==job->runtime->informationFingerprint &&
            runtime.bottleFingerprint==job->runtime->bottleFingerprint &&
            (runtime.settingFingerprint==job->runtime->settingFingerprint ||
             (!savedFingerprint.isEmpty() && runtime.settingFingerprint==savedFingerprint)))
            entry=runtimes_.erase(entry);
        else ++entry;
    }
}

void ObservationWorker::process(const std::shared_ptr<const Job> &job)
{
    bool committed=false, loaded=false;
    QString fingerprint, message;
    PreparedObservation prepared;
    ObservationPresentation presentation;
    do
    {
        if (QThread::currentThread()!=thread())
        { message=QStringLiteral("N处理未在所属存储线程执行，未读取资料或写库。"); break; }
        if (!current(job))
        { message=QStringLiteral("N排队期间连接、许可、新鲜期限或运行快照变化，未写入。"); break; }
        OriginalReagentRepository repository;
        OriginalReagentState state;
        ProjectSnapshot projects;
        if (!repository.initialize(job->context.root,&message) || !repository.load(&state,&message) ||
            !readProjectSnapshot(job->context.root,&projects,&message)) break;
        if (!current(job))
        { message=QStringLiteral("N资料读取期间许可变化，未写入。"); break; }
        if (!prepareObservation(state,job->packet,job->context,*job->runtime,projects,&prepared,&message)) break;
        const auto stillCurrent=[this,job]() { return current(job); };
        const bool accepted=saveObservation(prepared,stillCurrent,&committed,&fingerprint,&message);
        if (!committed) break;

        // 提交后即使失效仍重读；失败也不能把已经写入的事实说成回滚。
        QLockFile projectLock(QDir(job->context.root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
        QLockFile reagentLock(QDir(job->context.root).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
        projectLock.setStaleLockTime(0); reagentLock.setStaleLockTime(0);
        if (!projectLock.tryLock(0) || !reagentLock.tryLock(0))
        { message=QStringLiteral("N已提交，但重读所需原项目或试剂锁正被占用，页面结果待核。"); break; }
        // 06编辑器的共享项目初始化可触发复制恢复；本链持锁并先拒pending，不由N做恢复。
        QString pendingError;
        if (!OriginalProjectCopy::noPending(job->context.root,&pendingError))
        { message=QStringLiteral("N已提交，但项目复制仍待核，未进入共享编辑器恢复/重读：")+pendingError; break; }
        OriginalReagentEditor editor;
        ReagentSnapshot snapshot;
        ReagentProjects choices;
        QString reloadError;
        if (!editor.initialize(job->context.root,&reloadError) || !editor.load(&snapshot,&choices,&reloadError))
        { message=QStringLiteral("N已提交，但原资料重读失败：")+reloadError; break; }
        const auto versions=editor.deviceVersions();
        if (fingerprint.isEmpty() || versions.value(settingFile)!=fingerprint ||
            versions.value(informationFile)!=prepared.observed.versions.value(informationFile) ||
            versions.value(bottleFile)!=prepared.observed.versions.value(bottleFile))
        { message=QStringLiteral("N已提交，但重读的孔位或关联资料版本已变化，未发布当前运行快照。"); break; }
        if (!savedFieldsMatch(snapshot,prepared,fingerprint,&message)) break;
        ProjectSnapshot displayedProjects;
        if (!readProjectSnapshot(job->context.root,&displayedProjects,&reloadError) ||
            displayedProjects.versions!=prepared.projects.versions ||
            OriginalProjectRepository::databaseFingerprint(QDir(job->context.root).filePath(QStringLiteral("theme_config.ini")),&reloadError)
                !=job->context.themeFingerprint)
        { message=QStringLiteral("N已提交，但项目顺序或主题在重读时变化，页面数据待核对。 ")+reloadError; break; }
        if (!accepted || !current(job))
        { message=QStringLiteral("N已提交且已重读，但保存后的许可核对未通过；当前运行值与页面仍待核对。 ")+message; break; }
        presentation.context=job->context;
        presentation.snapshot=snapshot; presentation.projects=choices;
        presentation.after=prepared.after;
        presentation.after.settingFingerprint=fingerprint;
        presentation.after.revision=job->runtime->revision+1;
        presentation.selectedHoleTests=prepared.selectedHoleTests;
        presentation.capacity=prepared.capacity;
        presentation.previousSettingFingerprint=prepared.before.settingFingerprint;
        presentation.savedFingerprint=fingerprint;
        loaded=true;
    } while (false);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loaded && !currentLocked(job))
        {
            loaded=false;
            message=QStringLiteral("N已提交，但发布前连接或运行快照变化，未把旧展示排入页面。");
        }
        if (loaded)
        {
            for (auto entry=runtimes_.begin();entry!=runtimes_.end();)
            {
                if (sameVersions(*entry.value(),*job->runtime))
                {
                    // 自有N只改目标孔；其他孔仅推进全库版本，保留其真实运行值和revision。
                    auto advanced=std::make_shared<CurrentHoleRuntime>(*entry.value());
                    advanced->settingFingerprint=fingerprint;
                    entry.value()=advanced;
                    ++entry;
                }
                else entry=runtimes_.erase(entry);
            }
            runtimes_.insert(job->packet.hole,std::make_shared<const CurrentHoleRuntime>(presentation.after));
            message=QStringLiteral("N指定字段已提交并重读核对，运行值已绑定保存版本；页面是否接受由专用展示入口另核。");
        }
        else invalidateRelatedLocked(job,fingerprint,committed);
        if (pending_==job) pending_.reset();
    }
    // 解锁后仅通知不可变结果；页面仍须按原连接、许可及前/后指纹核对，不能普通loaded覆盖。
    if (loaded) emit presentationReady(presentation);
    emit processed(job->context,committed,loaded,fingerprint,message);
}
}
