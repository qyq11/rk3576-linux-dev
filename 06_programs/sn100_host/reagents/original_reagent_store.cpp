#include "reagent_store.h"
#include "original_reagent_editor.h"
#include "projects/original_project_copy.h"
#include <QDir>
#include <QLockFile>
#include <QThread>
#include <QTimer>

void ReagentStore::setOriginalDeviceOperationContext(quint64 connection,QStringList operations)
{
    const auto before=std::atomic_load(&deviceOperationPermit_);
    if (before && connection && before->connection==connection && before->operations==operations) return;
    std::shared_ptr<const OriginalDeviceOperationPermit> permit;
    if (connection && !operations.isEmpty())
    {
        auto next=std::make_shared<OriginalDeviceOperationPermit>();
        next->connection=connection; next->operations=operations;
        permit=next;
    }
    std::atomic_store(&deviceOperationPermit_,permit);
}

bool ReagentStore::authorizeOriginalDeviceOperation(quint64 request,quint64 connection,const QString &operation)
{
    auto before=std::atomic_load(&deviceOperationPermit_);
    if (!request || !before || before->connection!=connection || !before->operations.contains(operation)) return false;
    auto next=std::make_shared<OriginalDeviceOperationPermit>(*before);
    next->request=request; next->operation=operation;
    next->preparationClaimed=false;
    // 原单孔等待为10秒；界面事件延迟不能把已超时的优先级回读重新放行。
    next->deadline=operation==QStringLiteral("prioritizeHole") ?
        std::chrono::steady_clock::now()+std::chrono::seconds(10) : std::chrono::steady_clock::time_point::max();
    std::shared_ptr<const OriginalDeviceOperationPermit> permit=next;
    return std::atomic_compare_exchange_strong(&deviceOperationPermit_,&before,permit);
}

void ReagentStore::initializeOriginal(QString root)
{
    qRegisterMetaType<ReagentProject>();
    qRegisterMetaType<OriginalTaskReagents::Commit>("OriginalTaskReagents::Commit");
    if (!onWorker(QStringLiteral("初始化原试剂"))) return;
    close();
    std::unique_ptr<OriginalReagentEditor> editor(new OriginalReagentEditor);
    QString message;
    if (!editor->initialize(root,&message))
    { emit error(QStringLiteral("初始化原试剂"),message); return; }
    originalEditor_=std::move(editor);
    ready_=true;
    loadOriginal();
    emit ready();
}

void ReagentStore::setInstrumentBusy(bool busy)
{
    if (onWorker(QStringLiteral("更新仪器占用状态"))) instrumentBusy_=busy;
}

bool ReagentStore::originalMayEdit(const QString &operation)
{
    if (!onWorker(operation)) return false;
    if (!ready_ || !originalEditor_)
    { emit error(operation,QStringLiteral("原试剂资料未就绪。")); return false; }
    if (!session_.authenticated || !session_.actionReady || session_.role<0 || session_.role>5)
    { emit error(operation,QStringLiteral("当前账户没有试剂编辑权限。")); return false; }
    if (instrumentBusy_)
    { emit error(operation,QStringLiteral("仪器正在检测或有待核对命令，不能修改检测所用试剂。")); return false; }
    return true;
}

void ReagentStore::loadOriginalLevelObservation(QString root)
{
    const QString operation=QStringLiteral("读取原试剂");
    if (!onWorker(operation)) return;
    if (!ready_ || !originalEditor_ || root!=originalEditor_->root())
    { emit error(operation,QStringLiteral("N专用重读的原资料根未就绪或已变化。")); return; }
    QLockFile project(QDir(root).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagent(QDir(root).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    project.setStaleLockTime(0); reagent.setStaleLockTime(0);
    QString message;
    if (!project.tryLock(0) || !reagent.tryLock(0))
    { emit error(operation,QStringLiteral("N页面重读所需项目或试剂锁被占用，未复用旧详情。")); return; }
    if (!OriginalProjectCopy::noPending(root,&message))
    { emit error(operation,QStringLiteral("N页面重读遇到待核复制，未由N触发恢复：")+message); return; }
    loadOriginal(); // 两锁覆盖共享编辑器读取及快照发出，N不调用复制恢复入口。
}

bool ReagentStore::loadOriginal()
{
    if (!onWorker(QStringLiteral("读取原试剂")) || !originalEditor_) return false;
    QString message; ReagentSnapshot snapshot; ReagentProjects projects;
    if (!originalEditor_->load(&snapshot,&projects,&message))
    { emit error(QStringLiteral("读取原试剂"),message); return false; }
    emit originalProfileReady(snapshot.originalRoot,projects);
    emit loaded(snapshot);
    return true;
}

void ReagentStore::prepareOriginalDeviceOperation(quint64 request, quint64 connection,
    QString operation, QString holeName, bool linuxNew)
{
    QString message;
    auto permit=std::atomic_load(&deviceOperationPermit_);
    if (permit && !permit->preparationClaimed && permit->request==request && permit->connection==connection &&
        permit->operation==operation && permit->operations.contains(operation))
    {
        auto claimed=std::make_shared<OriginalDeviceOperationPermit>(*permit);
        claimed->preparationClaimed=true;
        std::shared_ptr<const OriginalDeviceOperationPermit> active=claimed;
        if (std::atomic_compare_exchange_strong(&deviceOperationPermit_,&permit,active)) permit=active;
        else permit.reset();
    }
    else permit.reset();
    const auto stillCurrent=[this,permit,request,connection,operation]()
    {
        return permit && std::chrono::steady_clock::now()<permit->deadline &&
            permit->preparationClaimed && permit->request==request && permit->connection==connection &&
            permit->operation==operation && permit->operations.contains(operation) &&
            std::atomic_load(&deviceOperationPermit_)==permit;
    };
    if (QThread::currentThread()!=thread() || !request || !connection || !ready_ || !originalEditor_)
        message=QStringLiteral("原试剂操作请求、连接或数据库工作线程未就绪，未修改资料或准备命令。");
    else if (!session_.authenticated || !session_.actionReady || session_.role<0 || session_.role>5 || instrumentBusy_)
        message=QStringLiteral("当前账户或仪器占用状态不允许试剂操作。");
    else if ((operation==QStringLiteral("fillHole") && session_.role!=5) ||
             (operation==QStringLiteral("clearPanel") && session_.role<4))
        message=QStringLiteral("当前账户没有更换试剂或清盘权限。");
    else if (!stillCurrent())
        message=QStringLiteral("原试剂操作许可已经失效或不属于当前请求，未改写资料或准备命令。");
    if (!message.isEmpty())
    {
        emit originalDevicePreparationFailed(request,connection,operation,message);
        return;
    }
    QList<QByteArray> packets;
    bool localSaved=false;
    if (!originalEditor_->prepareDeviceOperation(operation,holeName,linuxNew,&packets,&localSaved,&message,stillCurrent))
    {
        if (localSaved) message=QStringLiteral("本地资料已保存，但设备操作准备未完成：")+message;
        emit originalDevicePreparationFailed(request,connection,operation,message);
        return;
    }
    if (localSaved && !loadOriginal())
    {
        emit originalDevicePreparationFailed(request,connection,operation,
            QStringLiteral("本地资料已保存，但重新读取失败；未发送设备命令，请核对原库后再继续。"));
        return;
    }
    if (!stillCurrent())
    {
        emit originalDevicePreparationFailed(request,connection,operation,localSaved ?
            QStringLiteral("本地资料已保存，但提交期间连接或会话改变；未发送设备命令，请核对当前资料和设备状态。") :
            QStringLiteral("命令准备期间连接或会话改变，未发送。"));
        return;
    }
    priorityReply_=OriginalPriorityReplyContext();
    if (operation==QStringLiteral("prioritizeHole") && linuxNew)
    {
        if (packets.size()!=1 || !packets.first().startsWith(QByteArrayLiteral("Us[")))
        {
            emit originalDevicePreparationFailed(request,connection,operation,
                QStringLiteral("新协议优先使用没有得到唯一的原Us命令，未缓存回读或发送。"));
            return;
        }
        const auto versions=originalEditor_->deviceVersions();
        const QStringList files{QStringLiteral("db/userSet/reagent_Information.db"),
            QStringLiteral("db/systemInfo/reagent_Bottle_Type.db"),QStringLiteral("db/userSet/reagent_Setting.db")};
        bool versionsReady=versions.size()==files.size();
        for (const auto &file : files) versionsReady=versionsReady && !versions.value(file).isEmpty();
        if (!versionsReady || !stillCurrent())
        {
            emit originalDevicePreparationFailed(request,connection,operation,
                QStringLiteral("本次优先使用缺少有效三库版本或请求许可已变化，未缓存回读或发送。"));
            return;
        }
        priorityReply_.request=request; priorityReply_.connection=connection;
        priorityReply_.sentPayload=packets.first(); priorityReply_.versions=versions; priorityReply_.permit=permit;
    }
    emit originalDeviceOperationPrepared(request,connection,operation,packets,localSaved);
}

void ReagentStore::saveOriginalPriorityReply(quint64 request, quint64 connection, QByteArray payload)
{
    if (QThread::currentThread()!=thread())
    {
        emit originalPriorityReplyProcessed(request,connection,false,false,QString(),
            QStringLiteral("优先级回读必须在原试剂数据库工作线程保存，未读取缓存或改写资料。"));
        return;
    }
    if (!request || !connection || priorityReply_.request!=request || priorityReply_.connection!=connection ||
        !priorityReply_.permit)
    {
        emit originalPriorityReplyProcessed(request,connection,false,false,QString(),
            QStringLiteral("这条优先级回读没有本次唯一接收资格，未重复提交或使用其他请求的资料。"));
        return;
    }
    const OriginalPriorityReplyContext receipt=priorityReply_;
    // 一旦开始处理这次回读就消费资格；无论成功、冲突还是格式错误，都不能再次提交。
    priorityReply_=OriginalPriorityReplyContext();
    const auto stillCurrent=[this,receipt,request,connection]()
    {
        const auto &permit=receipt.permit;
        return ready_ && originalEditor_ && permit && std::chrono::steady_clock::now()<permit->deadline &&
            session_.authenticated && session_.actionReady &&
            session_.role>=0 && session_.role<=5 && !instrumentBusy_ && permit && permit->preparationClaimed &&
            permit->request==request && permit->connection==connection &&
            permit->operation==QStringLiteral("prioritizeHole") &&
            permit->operations.contains(QStringLiteral("prioritizeHole")) &&
            std::atomic_load(&deviceOperationPermit_)==permit;
    };
    if (!stillCurrent())
    {
        emit originalPriorityReplyProcessed(request,connection,false,false,QString(),
            QStringLiteral("优先使用的请求、连接、会话或动作许可已失效，回读未提交。"));
        return;
    }

    bool committed=false, loaded=false;
    QString fingerprint, message;
    const bool accepted=originalEditor_->applyPriorityReply(payload,receipt.sentPayload,receipt.versions,
        stillCurrent,&committed,&fingerprint,&message);
    if (committed)
    {
        // 提交后的失效不能抹掉落库事实；仍重读当前资料，禁止用false冒充已经回滚。
        const bool reloaded=loadOriginal();
        if (!reloaded)
            message=QStringLiteral("Ur已保存，但页面资料重读失败；未报告优先使用完成，请核对原库和设备回读。")+
                (message.isEmpty() ? QString() : QStringLiteral(" ")+message);
        else if (fingerprint.isEmpty() || originalEditor_->deviceVersions().value(
                     QStringLiteral("db/userSet/reagent_Setting.db"))!=fingerprint)
            message=QStringLiteral("Ur已保存，但页面重读时孔位资料版本已改变或保存指纹不可用；未报告操作完成。")+
                (message.isEmpty() ? QString() : QStringLiteral(" ")+message);
        else if (!accepted || !stillCurrent())
            message=QStringLiteral("Ur已保存且已重新读取，但请求许可已变化或提交后核对未通过，结果仍待核对。")+
                (message.isEmpty() ? QString() : QStringLiteral(" ")+message);
        else
        {
            loaded=true;
            message=QStringLiteral("本次优先级Ur已提交，原资料重读版本与保存指纹一致；仍须主窗口核对页面快照后结束等待。");
        }
    }
    emit originalPriorityReplyProcessed(request,connection,committed,loaded,fingerprint,message);
}

void ReagentStore::setOriginalProjectSaveAdmissionEnabled(bool enabled)
{
    if (QThread::currentThread()!=thread()) return;
    projectSaveAdmissionEnabled_=enabled;
    if (!enabled) cancelPendingOriginalGroupSave(QStringLiteral("组开关保存登记消费者已关闭，尚未写库，草稿保留。"));
}

void ReagentStore::cancelPendingOriginalGroupSave(QString message)
{
    if (!pendingOriginalGroupSave_.ticket) return;
    const auto pending=pendingOriginalGroupSave_; pendingOriginalGroupSave_=PendingOriginalGroupSave();
    if (projectSaveAdmissionTimer_) projectSaveAdmissionTimer_->stop();
    emit originalProjectSaveAdmissionCancelled(pending.ticket,QStringLiteral("reagentGroupEnabled"));
    emit error(QStringLiteral("保存试剂组开关"),message);
}

void ReagentStore::saveOriginalBatchGroupEnabled(ReagentProject project, bool enabled)
{
    // 与普通项目一致：仅从本存储槽开始处理计时，不冒称覆盖GUI排队时间。
    const qint64 sourceDeadline=originalProjectSaveMonotonicNow()+10000;
    const QString operation=QStringLiteral("保存试剂组开关");
    if (!originalMayEdit(operation)) return;
    if (pendingOriginalGroupSave_.ticket)
    { emit error(operation,QStringLiteral("另一项组开关保存仍在预检，尚未提交，草稿保留。")); return; }
    if (!projectSaveAdmissionEnabled_) { commitOriginalGroupSave(project,enabled); return; }
    pendingOriginalGroupSave_.ticket=nextOriginalProjectSaveTicket();
    pendingOriginalGroupSave_.project=project; pendingOriginalGroupSave_.enabled=enabled;
    pendingOriginalGroupSave_.actor=session_; pendingOriginalGroupSave_.deadline=sourceDeadline;
    if (!projectSaveAdmissionTimer_)
    {
        projectSaveAdmissionTimer_=new QTimer(this); projectSaveAdmissionTimer_->setSingleShot(true);
        connect(projectSaveAdmissionTimer_,&QTimer::timeout,this,[this]()
        { cancelPendingOriginalGroupSave(QStringLiteral("组开关保存预检10秒未完成，尚未写库，草稿保留。")); });
    }
    projectSaveAdmissionTimer_->start(static_cast<int>(qMax<qint64>(1,sourceDeadline-originalProjectSaveMonotonicNow())));
    OriginalProjectSaveNotice proposal;
    proposal.source=QStringLiteral("reagentGroupEnabled"); proposal.writeRequest=static_cast<qint64>(pendingOriginalGroupSave_.ticket);
    proposal.root=project.originalRoot; proposal.originalFileName=project.originalFileName;
    proposal.projectId=project.id; proposal.projectName=project.name; proposal.beforeFingerprint=project.originalFingerprint;
    emit originalProjectSaveAdmissionRequested(pendingOriginalGroupSave_.ticket,proposal,sourceDeadline);
}

void ReagentStore::finishOriginalProjectSaveAdmission(OriginalProjectSaveAdmission admission)
{
    if (QThread::currentThread()!=thread() || admission.proposal.source!=QStringLiteral("reagentGroupEnabled")) return;
    if (!pendingOriginalGroupSave_.ticket || admission.ticket!=pendingOriginalGroupSave_.ticket)
    {
        if (admission.permit) admission.permit->revoke();
        emit originalProjectSaveAdmissionCancelled(admission.ticket,QStringLiteral("reagentGroupEnabled"));
        return;
    }
    const auto pending=pendingOriginalGroupSave_; pendingOriginalGroupSave_=PendingOriginalGroupSave();
    if (projectSaveAdmissionTimer_) projectSaveAdmissionTimer_->stop();
    QString reason;
    if (!admission.allowed || !admission.permit) reason=admission.message;
    else if (!projectSaveAdmissionEnabled_ || !sameOriginalProjectSaveActor(pending.actor,session_) ||
        pending.deadline<=originalProjectSaveMonotonicNow() || admission.sourceDeadline!=pending.deadline ||
        admission.permit->sourceDeadline!=pending.deadline || admission.permit->expired() || !originalEditor_ || pending.project.originalRoot!=originalEditor_->root() ||
        admission.permit->ticket!=admission.ticket || !sameOriginalProjectSave(admission.proposal,admission.permit->proposal) ||
        admission.proposal.writeRequest!=static_cast<qint64>(pending.ticket) || admission.proposal.root!=pending.project.originalRoot ||
        admission.proposal.originalFileName!=pending.project.originalFileName || admission.proposal.projectId!=pending.project.id ||
        admission.proposal.beforeFingerprint!=pending.project.originalFingerprint)
        reason=QStringLiteral("组开关预检已过期或账户、文件、版本已变化，尚未写库，草稿保留。");
    if (!reason.isEmpty() || !admission.allowed || !admission.permit)
    {
        if (admission.permit) admission.permit->revoke();
        emit originalProjectSaveAdmissionCancelled(admission.ticket,QStringLiteral("reagentGroupEnabled"));
        emit error(QStringLiteral("保存试剂组开关"),reason.isEmpty() ? QStringLiteral("保存未获预留，尚未提交。") : reason);
        return;
    }
    if (!originalMayEdit(QStringLiteral("保存试剂组开关")))
    {
        admission.permit->revoke();
        emit originalProjectSaveAdmissionCancelled(admission.ticket,QStringLiteral("reagentGroupEnabled"));
        return;
    }
    commitOriginalGroupSave(pending.project,pending.enabled,admission.permit);
}

void ReagentStore::commitOriginalGroupSave(ReagentProject project,bool enabled,
    const std::shared_ptr<OriginalProjectSavePermit> &permit)
{
    const QString operation=QStringLiteral("保存试剂组开关");
    QString message; OriginalProjectSaveNotice result;
    const auto claim=[permit]() { return permit->claim(permit->proposal); };
    const auto post=[this,permit](OriginalProjectSaveNotice notice)
    {
        notice.source=QStringLiteral("reagentGroupEnabled"); notice.writeRequest=permit->proposal.writeRequest;
        permit->committed(notice);
        emit originalProjectSaveCommitted(notice);
    };
    const bool success=originalEditor_->saveBatchGroupEnabled(project,enabled,&message,permit ? &result : nullptr,
        permit ? std::function<bool()>(claim) : std::function<bool()>(),
        permit ? std::function<void(const OriginalProjectSaveNotice &)>(post) : std::function<void(const OriginalProjectSaveNotice &)>());
    if (permit && !result.commitSeen)
    {
        permit->releaseUncommitted();
        emit originalProjectSaveAdmissionCancelled(permit->ticket,QStringLiteral("reagentGroupEnabled"));
    }
    if (success || result.commitSeen)
    {
        project.originalFingerprint.clear();
        project.originalGroupEnabled=enabled ? QStringLiteral("1") : QStringLiteral("0");
        emit originalProjectGroupEnableCommitted(project);
    }
    if (result.commitSeen && !success) message=QStringLiteral("组开关已经提交，但提交后快照未核实；设备参数尚未确认：")+message;
    finishOriginal(operation,success,message);
}

void ReagentStore::finishOriginal(const QString &operation, bool success, const QString &message)
{
    if (!success) { emit error(operation,message); return; }
    emit succeeded(operation);
    loadOriginal();
}
