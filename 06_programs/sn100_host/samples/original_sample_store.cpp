#include "sample_store.h"
#include "original_task_reagent_receipt_writer.h"
#include "original_sample_repository.h"
#include "original_detection_session.h"
#include "original_device_sync.h"
#include "original_files/original_ini.h"
#include <QThread>
#include <QCryptographicHash>
#include <QTimer>
#include <QFileInfo>
#include <QDir>

void SampleStore::initializeOriginal(QString root)
{
    if (QThread::currentThread() != thread())
    { emit error(0,QStringLiteral("initializeOriginal"),QStringLiteral("原样本初始化必须在数据库工作线程执行。")); return; }
    if (!pendingQuality_.change.changeToken.isEmpty())
    { emit error(0,QStringLiteral("initializeOriginal"),QStringLiteral("原有质控引用操作正在完成，未切换数据源。")); return; }
    release();
    QString message;
    if (!OriginalBusiness::workingRoot(root,&message))
    { emit error(0,QStringLiteral("initializeOriginal"),message); return; }
    const auto configurationSource=OriginalIni::InitializationSource::capture(root,&message);
    if (!configurationSource)
    { emit error(0,QStringLiteral("initializeOriginal"),message); return; }
    auto repository = std::unique_ptr<OriginalSampleRepository>(new OriginalSampleRepository);
    if (!repository->initialize(root, &message, originalDefaultContainerIsTube_, configurationSource))
    { emit error(0,QStringLiteral("initializeOriginal"),message); return; }
    if (layoutConfigured_ && (ordinaryHoles_!=repository->ordinaryHoles() || emergencyHoles_!=repository->emergencyHoles()))
    { emit error(0,QStringLiteral("initializeOriginal"),QStringLiteral("预先配置的普通/急诊孔数与本次原配置不一致，请重新加载资料。")); return; }
    auto detection = std::unique_ptr<OriginalDetectionSession>(new OriginalDetectionSession);
    if (!detection->initialize(repository.get(), &message, originalPageMetadata_, configurationSource))
    { emit error(0,QStringLiteral("initializeOriginal"),message); return; }
    std::unique_ptr<OriginalDeviceSync> synchronization(new OriginalDeviceSync);
    if (!synchronization->initialize(root,&message))
    { emit error(0,QStringLiteral("initializeOriginal"),message); return; }
    synchronization->setKnownProjectFiles(repository->projects());
    // 首次安装和所有就绪信号之前核对；失败只放弃本次局部对象，不假称项目原恢复动作已回滚。
    if (!configurationSource->current(root,&message))
    { emit error(0,QStringLiteral("initializeOriginal"),message); return; }
    originalSamples_ = std::move(repository);
    originalDetection_ = std::move(detection);
    originalDeviceSync_=std::move(synchronization);
    if (!originalProjectUpdateTimeout_)
    {
        originalProjectUpdateTimeout_=new QTimer(this);
        originalProjectUpdateTimeout_->setSingleShot(true);
        originalProjectUpdateTimeout_->setTimerType(Qt::PreciseTimer);
        connect(originalProjectUpdateTimeout_,&QTimer::timeout,this,[this]()
        {
            if (!originalDeviceSync_) return;
            originalDeviceSync_->expireProjectUpdate(originalDeviceSync_->activeProjectUpdateIntent(),originalDeviceSync_->connection());
            publishOriginalProjectUpdateStates();
        });
    }
    if (!originalConfigurationTimeout_)
    {
        originalConfigurationTimeout_=new QTimer(this);
        originalConfigurationTimeout_->setSingleShot(true);
        connect(originalConfigurationTimeout_,&QTimer::timeout,this,[this]()
        {
            if (!originalDeviceSync_ || !originalDeviceSync_->waiting()) return;
            const quint64 connection=originalDeviceSync_->connection();
            originalDeviceSync_->invalidate();
            publishOriginalProjectUpdateStates();
            emit originalConfigurationState(connection,false,QStringLiteral("设备配置10秒未确认，未重发或发送开始；请核对连接和回包后重新同步。"));
        });
    }
    ready_ = true;
    qRegisterMetaType<SampleQcPreset::Snapshot>();
    qRegisterMetaType<ProjectUnits>();
    emit originalProfileReady(root, originalSamples_->ordinaryHoles(), originalSamples_->emergencyHoles());
    emit originalUnitsReady(originalSamples_->units());
    emit originalProjectsReady(originalSamples_->projects());
    emit ready(root);
}

void SampleStore::loadOriginalQualityChoices(qint64 request)
{
    if (QThread::currentThread() != thread() || !ready_ || !originalSamples_)
    { emit originalQualityChoicesError(request, QStringLiteral("原样本资料尚未就绪，未读取其他质控数据。")); return; }
    SampleSnapshot samples;
    SampleQcPreset::Snapshot choices;
    QString message;
    if (!originalSamples_->load(&samples, &message) || !originalSamples_->qualityChoices(&choices, &message))
    { emit originalQualityChoicesError(request, message); return; }
    emit originalUnitsReady(originalSamples_->units());
    emit originalQualityChoicesReady(request, choices);
}

void SampleStore::saveOriginalCalibrationParameters(qint64 request, qint64 projectId, QString expectedVersion, QString lastValues)
{
    QString message;
    if (QThread::currentThread() != thread() || !ready_ || !originalSamples_ || !canEdit(session_) ||
        session_.originalPolicySource != originalSamples_->root())
    { emit originalCalibrationParametersSaved(request, false, QStringLiteral("原样本资料或当前账户身份无效，未保存校准浓度参数。")); return; }
    QString fileName;
    for (const auto &project : originalSamples_->projects())
        if (project.id==projectId && project.originalFingerprint==expectedVersion) fileName=project.originalFileName;
    if (!originalDeviceSync_ || fileName.isEmpty() || !originalDeviceSync_->canRecordProjectSave(fileName,&message))
    {
        emit originalCalibrationParametersSaved(request,false,message.isEmpty()
            ? QStringLiteral("请重读项目后再保存；原校准草稿保留，尚未提交。") : message);
        return;
    }
    OriginalProjectSaveNotice notice;
    const auto postCommitted=[this,request,&message](const OriginalProjectSaveNotice &committed)
    {
        auto value=committed;
        value.source=QStringLiteral("calibrationParameters"); value.writeRequest=request;
        value.message=value.verifiedSnapshot ? message : QStringLiteral("校准参数已经提交，但提交后快照未核实；设备参数仍未确认：")+message;
        // 统一由Root按锁内通知顺序登记；不能让CAL直达而其他保存绕经GUI，导致版本倒序。
        emit originalProjectSaveCommitted(value,0);
    };
    const bool ok = originalSamples_->saveCalibrationParameters(projectId, expectedVersion, lastValues, &message, &notice, postCommitted,fileName);
    if (notice.commitSeen && !ok) message=QStringLiteral("校准参数已经提交，但提交后快照未核实；设备参数仍未确认：")+message;
    if (ok)
    {
        emit originalUnitsReady(originalSamples_->units());
        emit originalProjectsReady(originalSamples_->projects());
        emit originalProjectsChanged();
    }
    emit originalCalibrationParametersSaved(request, ok, message);
}

void SampleStore::cleanOriginalProjectAdmissions(bool revokeUnclaimed)
{
    for (auto it=originalProjectAdmissions_.begin();it!=originalProjectAdmissions_.end();)
    {
        const auto &permit=it.value();
        if (revokeUnclaimed || permit->expired()) permit->revoke();
        const int state=permit->state();
        if (state==OriginalProjectSavePermit::Released || state==OriginalProjectSavePermit::Revoked ||
            state==OriginalProjectSavePermit::Accepted)
        {
            if (originalDeviceSync_) originalDeviceSync_->releaseProjectSave(it.key());
            it=originalProjectAdmissions_.erase(it);
        }
        else ++it; // 已认领不能伪取消；已提交保留完整通知，等统一消费者登记。
    }
    bool awaitingClaim=false;
    for (const auto &permit : originalProjectAdmissions_)
        awaitingClaim|=permit->state()==OriginalProjectSavePermit::Granted;
    if (originalProjectAdmissionTimer_ && !awaitingClaim) originalProjectAdmissionTimer_->stop();
}

void SampleStore::reserveOriginalProjectSave(quint64 ticket, OriginalProjectSaveNotice proposal, qint64 sourceDeadline)
{
    if (QThread::currentThread()!=thread()) return;
    cleanOriginalProjectAdmissions();
    OriginalProjectSaveAdmission reply;
    reply.ticket=ticket; reply.proposal=proposal; reply.sourceDeadline=sourceDeadline;
    const bool source=proposal.source==QStringLiteral("projectSave") || proposal.source==QStringLiteral("reagentGroupEnabled");
    QString message;
    ProjectRecords observed; ProjectUnits units; QStringList reagents,cleaners,diluents;
    OriginalProjectRepository probe;
    bool valid=sourceDeadline>originalProjectSaveMonotonicNow() && ticket &&
        !originalProjectAdmissions_.contains(ticket) && source && proposal.writeRequest>0 &&
        ready_ && originalSamples_ && originalDeviceSync_ && proposal.root==originalSamples_->root() &&
        !proposal.originalFileName.isEmpty() && QFileInfo(proposal.originalFileName).fileName()==proposal.originalFileName &&
        originalDeviceSync_->canRecordProjectSave(proposal.originalFileName,&message);
    // 同一文件可多次保存，但跨线程尚未登记的值对象也必须有数量上限。
    if (valid && originalProjectAdmissions_.size()>=originalSamples_->projects().size()+2)
    { valid=false; message=QStringLiteral("已有项目保存正在登记，本次尚未写库，请稍后重试，草稿保留。"); }
    if (valid) valid=probe.initialize(proposal.root,QString(),&message) &&
        probe.load(&observed,&units,&reagents,&cleaners,&diluents,&message);
    if (valid)
    {
        int matches=0;
        for (const auto &project : observed)
            if (project.originalFileName==proposal.originalFileName && project.id==proposal.projectId &&
                project.originalFingerprint==proposal.beforeFingerprint) ++matches;
        valid=matches==1;
        if (!valid) message=QStringLiteral("实际项目文件或版本与保存草稿不一致，本次尚未提交，请重新读取。");
    }
    // 真实项目重读也可能耗时；预留前再次核源请求截止点，不在此重新起10秒。
    if (sourceDeadline<=originalProjectSaveMonotonicNow())
    { valid=false; message=QStringLiteral("源保存请求10秒内尚未认领，尚未写库，草稿保留。"); }
    if (valid) valid=originalDeviceSync_->reserveProjectSave(ticket,proposal.originalFileName,&message);
    if (valid)
    {
        reply.permit=std::make_shared<OriginalProjectSavePermit>(ticket,proposal,sourceDeadline);
        if (reply.permit->expired())
        {
            originalDeviceSync_->releaseProjectSave(ticket);
            reply.permit.reset(); valid=false;
            message=QStringLiteral("源保存请求在预留时已到期，尚未写库，草稿保留。");
        }
        else
        {
            reply.allowed=true;
            originalProjectAdmissions_.insert(ticket,reply.permit);
            if (!originalProjectAdmissionTimer_)
            {
                originalProjectAdmissionTimer_=new QTimer(this);
                originalProjectAdmissionTimer_->setInterval(250);
                connect(originalProjectAdmissionTimer_,&QTimer::timeout,this,[this]() { cleanOriginalProjectAdmissions(); });
            }
            originalProjectAdmissionTimer_->start();
        }
    }
    reply.message=valid ? QStringLiteral("已预留本次项目保存登记；尚未写库或发送设备参数。") :
        (message.isEmpty() ? QStringLiteral("原项目保存登记后台尚未就绪或上下文无效，尚未提交，草稿保留。") : message);
    emit originalProjectSaveAdmissionReady(reply);
}

void SampleStore::releaseOriginalProjectSave(quint64 ticket,QString source)
{
    if (QThread::currentThread()!=thread()) return;
    const auto permit=originalProjectAdmissions_.value(ticket);
    if (permit && permit->proposal.source==source) permit->revoke();
    cleanOriginalProjectAdmissions();
}

void SampleStore::acceptOriginalProjectAdmission(const OriginalProjectSaveNotice &notice)
{
    for (auto it=originalProjectAdmissions_.cbegin();it!=originalProjectAdmissions_.cend();++it)
        if (it.value()->accept(notice)) break;
    cleanOriginalProjectAdmissions();
}

void SampleStore::enqueueOriginalProjectUpdate(OriginalProjectSaveNotice notice)
{
    if (QThread::currentThread()!=thread()) return;
    acceptOriginalProjectAdmission(notice);
    if (QThread::currentThread()!=thread() || !notice.commitSeen || !ready_ || !originalDeviceSync_ || !originalSamples_)
    {
        if (notice.commitSeen)
        {
            OriginalProjectUpdateState state;
            state.notice=notice; state.stage=QStringLiteral("unsupported");
            state.message=QStringLiteral("已提交事实保留，但原同步后台尚未就绪，未发送项目参数。");
            emit originalProjectUpdateState(state);
        }
        return;
    }
    const auto state=originalDeviceSync_->noteProjectSave(notice);
    emit originalProjectSaveCommitted(notice,state.intent);
    publishOriginalProjectUpdateStates();
}

void SampleStore::publishOriginalProjectUpdateStates()
{
    if (!originalDeviceSync_) return;
    if (originalProjectUpdateTimeout_ && !originalDeviceSync_->projectUpdateWaiting()) originalProjectUpdateTimeout_->stop();
    for (auto state : originalDeviceSync_->takeProjectUpdateStates())
    {
        if (state.stage==QStringLiteral("confirmed") || state.stage==QStringLiteral("confirmed-initial") ||
            state.stage==QStringLiteral("reconciled-initial") || state.stage==QStringLiteral("legacy-local") ||
            state.stage==QStringLiteral("initial-file-confirmed") || state.stage==QStringLiteral("identity-changed"))
        {
            QString reason;
            state.ready=originalDeviceSync_->parametersReady(originalDeviceSync_->usesSimplifiedProtocol(),&reason);
        }
        emit originalProjectUpdateState(state);
    }
}

void SampleStore::advanceOriginalProjectUpdates(quint64 connection)
{
    if (QThread::currentThread()!=thread()) return;
    QList<QByteArray> packets;
    QString message;
    const bool prepared=ready_ && originalDeviceSync_ && originalDetection_ &&
        !originalDetection_->hasUnfinishedIssuedTasks() && originalDeviceSync_->prepareProjectUpdate(connection,&packets,&message);
    publishOriginalProjectUpdateStates();
    if (prepared)
    {
        if (originalProjectUpdateTimeout_) originalProjectUpdateTimeout_->start(10000);
        emit originalProjectUpdateBatch(packets,connection,originalDeviceSync_->activeProjectUpdateIntent(),
                                        originalDeviceSync_->activeProjectUpdateExpiresAt());
    }
    emit originalProjectUpdatePumpFinished(connection);
}

void SampleStore::originalProjectUpdateSent(quint64 intent, quint64 connection, bool attempted, bool queued, QString message)
{
    if (QThread::currentThread()!=thread() || !originalDeviceSync_) return;
    originalDeviceSync_->projectUpdateSendResult(intent,connection,attempted,queued,message);
    publishOriginalProjectUpdateStates();
}

void SampleStore::registerEmptyOrdinaryRack(qint64 request, int ordinaryHoles, bool containerIsTube)
{
    const QString operation = QStringLiteral("registerEmptyOrdinaryRack");
    if (!check(request, operation, true)) return;
    if (!originalSamples_)
    {
        emit error(request, operation, QStringLiteral("原样本资料未就绪，未新增普通架。"));
        return;
    }
    qint64 rackId = 0;
    SampleSnapshot snapshot;
    QString message;
    if (!originalSamples_->registerEmptyOrdinaryRack(ordinaryHoles, containerIsTube, &rackId, &snapshot, &message))
    {
        emit error(request, operation, message);
        return;
    }
    // 回传同一工作线程已经登记的内存快照；不再接公共load及项目复制恢复。
    emit emptyOrdinaryRackRegistered(request, rackId, snapshot);
}

void SampleStore::loadOriginalRuntimeSnapshot(qint64 request)
{
    const QString operation = QStringLiteral("loadOriginalRuntimeSnapshot");
    if (!check(request, operation)) return;
    if (!originalSamples_)
    {
        emit error(request, operation, QStringLiteral("原样本资料未就绪，未读取运行态架列表。"));
        return;
    }
    emit snapshotReady(request, originalSamples_->snapshot());
}

bool SampleStore::originalRackChange(qint64 request, const QString &operation, const SampleRack &rack, qint64 id)
{
    if (!originalSamples_) return false;
    if (!check(request, operation, true)) return true;
    QString message;
    qint64 saved = id;
    bool success = false;
    if (operation == QStringLiteral("saveRack")) success = originalSamples_->saveRack(rack, &saved, &message);
    else if (operation == QStringLiteral("removeRack")) success = originalSamples_->removeRack(id, &message);
    else if (operation == QStringLiteral("clearOrdinaryRacks")) success = originalSamples_->clearOrdinary(&message);
    else message = QStringLiteral("原样本操作类型未识别。 ");
    if (success)
    {
        emit originalProjectsReady(originalSamples_->projects());
        emit succeeded(request, operation, saved);
    }
    else emit error(request, operation, message);
    return true;
}

void SampleStore::prepareDeviceTasks(quint64 request, bool linuxNew)
{
    if (QThread::currentThread() != thread() || !ready_ || !originalDetection_ || !canEdit(session_))
    { emit deviceTasksPrepared(request, false, QStringLiteral("当前账户、原样本库或工作线程未就绪，未准备设备任务。")); return; }
    QString message;
    if (!originalDeviceSync_ || !originalDeviceSync_->canStart(linuxNew,&message))
    { emit deviceTasksPrepared(request,false,message.isEmpty() ? QStringLiteral("原配置同步未就绪。") : message); return; }
    const bool success = originalDetection_->prepare(linuxNew, &message);
    emit deviceTasksPrepared(request, success, success ? QStringLiteral("已按当前原数据库核对样本任务，等待原开始命令。") : message);
}

void SampleStore::prepareUserDeviceTasks(OriginalTaskReagents::RuntimeBasis basis,bool linuxNew)
{
    if (QThread::currentThread()!=thread())
    {
        // 拒绝线程错误的调用，既不读仓库/账户，也不伪称已排worker完成。
        emit deviceTaskError(QStringLiteral("用户任务准备调用线程错误，未处理；这不是原后台完成回执。"));
        return;
    }
    OriginalTaskReagents::Commit receipt;
    const QString root=originalSamples_ ? originalSamples_->root() : QString();
    OriginalTaskReagents::ReceiptWriter writer(OriginalTaskReagents::Action::None,basis.root,QByteArray(),basis,&receipt,{});
    const auto &user=basis.userOperation;
    const bool account=user.authenticated==session_.authenticated && user.accountName==session_.name &&
        user.role==session_.role && user.sessionSource==int(session_.source) && user.policyRoot==session_.originalPolicySource &&
        user.grantedPowerIds==session_.grantedPowerIds;
    QString message;
    bool success=false;
    if (!ready_ || !originalDetection_ || !originalDeviceSync_ ||
        !canEdit(session_) || !account || !OriginalTaskReagents::taskOriginAllows(basis,OriginalTaskReagents::Action::None) ||
        basis.source!=OriginalTaskReagents::OperationSource::UserOperation || basis.root!=root ||
        originalDeviceSync_->connection()!=basis.connection)
        message=QStringLiteral("用户任务准备的账户、连接、资料或所属线程已经失效，未继续。 ");
    else if (!originalDeviceSync_->canStart(linuxNew,&message)) {}
    else success=originalDetection_->prepareUserOperation(basis,linuxNew,&message);
    if (!success && originalDetection_ && QThread::currentThread()==thread()) originalDetection_->cancelUnsent();
    // 原Session已经返回；本入口无reserve/SQL写，finished不早于工作真正结束。
    writer.finish();
    emit userTaskOperationFinished(receipt,success,message);
}

void SampleStore::cancelDeviceTaskPreparation()
{
    if (QThread::currentThread() == thread() && originalDetection_) originalDetection_->cancelUnsent();
}

void SampleStore::queryDeviceRecovery(quint64 request, quint64 connection, QString root)
{
    const bool known=QThread::currentThread()==thread() && ready_ && originalSamples_ && originalDetection_ &&
        !root.isEmpty() && root==originalSamples_->root();
    const bool unfinished=known && originalDetection_->hasUnfinishedIssuedTasks();
    emit deviceRecoveryState(request,connection,root,known,unfinished,!known
        ? QStringLiteral("尚不能确认原任务的在途状态，未自动恢复业务同步。")
        : unfinished ? QStringLiteral("旧连接仍有已发任务未收到完整Tr或TR终态；保留任务和试剂占用，未自动恢复业务同步。")
        : QString());
}

void SampleStore::answerDeviceRackRequest(QByteArray payload, quint64 connection)
{
    if (QThread::currentThread() != thread() || !originalDetection_ || !originalDeviceSync_ ||
        !connection || originalDeviceSync_->connection()!=connection)
    { emit deviceTaskError(QStringLiteral("原样本任务处理未就绪，不能回应设备。")); return; }
    QByteArray response;
    QString message;
    const bool success=originalDetection_->answerRackRequest(payload,&response,&message);
    emit originalReagentPageMetadataChanged();
    if (!success) { emit deviceTaskError(message); return; }
    emit deviceRackTasksPrepared(response, connection);
    emit originalCalibrationRecordsChanged();
}

void SampleStore::receiveDevicePacket(QByteArray payload, quint64 connection, quint64 packet)
{
    receiveDevicePacketImpl(payload,connection,packet,nullptr);
}

void SampleStore::receiveDevicePacketWithTaskBasis(QByteArray payload, quint64 connection, quint64 packet,
    OriginalTaskReagents::RuntimeBasis basis)
{
    // 仅原Tt和大写TR走任务凭据；Ur、TD、Tr和项目同步沿原入口规则。
    const bool taskAction=payload.startsWith("Tt") || payload.startsWith("TR");
    receiveDevicePacketImpl(payload,connection,packet,taskAction ? &basis : nullptr);
}

void SampleStore::receiveDevicePacketImpl(QByteArray payload, quint64 connection, quint64 packet,
    const OriginalTaskReagents::RuntimeBasis *taskBasis)
{
    OriginalTaskReagents::Commit taskReceipt;
    bool receiptObserved=false;
    OriginalTaskReagents::CommitHandler committed;
    if (taskBasis)
    {
        committed=[this,&receiptObserved](const OriginalTaskReagents::Commit &receipt)
        {
            if (!receipt.committed || receiptObserved) return;
            receiptObserved=true;
            // Session写连接已关闭，但资料锁仍在；这里只排纯值，不读库、不等待GUI。
            emit taskReagentCommitObserved(receipt);
        };
    }
    QString message;
    bool success = false;
    if (QThread::currentThread() != thread() || !originalDetection_)
        message = QStringLiteral("原样本/试剂处理未就绪，未保存或回应设备数据。");
    else if (!originalDeviceSync_ || !connection || originalDeviceSync_->connection()!=connection)
        message = QStringLiteral("该数据不属于当前已核对的设备连接，未写入或下发任务。");
    else if (taskBasis && (payload.startsWith("Tt") || payload.startsWith("TR")) &&
        (taskBasis->source!=OriginalTaskReagents::OperationSource::DevicePacket ||
         !OriginalTaskReagents::validTaskOrigin(*taskBasis) || taskBasis->arrivalPacketId!=packet ||
         taskBasis->connection!=connection || taskBasis->payloadSha256!=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex())))
        message=QStringLiteral("设备原包与任务来源不对应，未借用户意图或另一包执行事务。 ");
    else if (payload.startsWith("Tt"))
    {
        QByteArray response;
        success = taskBasis
            ? originalDetection_->answerRackRequest(payload,&response,&message,*taskBasis,&taskReceipt,committed)
            : originalDetection_->answerRackRequest(payload,&response,&message);
        emit originalReagentPageMetadataChanged();
        if (success) { emit deviceRackTasksPrepared(response,connection); emit originalCalibrationRecordsChanged(); }
        else if (taskBasis && taskReceipt.committed)
        {
            // 已占号/预留但外层失败：锁已释放，只让原读者核实际写入，不发送任务。
            emit deviceTaskStateChanged();
            emit originalResultsChanged();
            emit originalCalibrationRecordsChanged();
        }
    }
    else if (payload=="TF")
    {
        QByteArray response;
        success=originalDetection_->rackConfiguration(&response,&message);
        if (success) emit deviceRackTasksPrepared(response,connection);
    }
    else if (payload=="TK")
        message=QStringLiteral("主控请求授权解锁码；本轮未接授权码读取和保密发送，不解除任务占用，请交整合核对实际INI。");
    else if (payload=="Tu")
    {
        QByteArray response;
        success=originalDetection_->reagentConfiguration(&response,&message);
        if (success) emit originalConfigurationPacket(response,connection);
    }
    else if (payload.startsWith("UP"))
    {
        bool handled=false,saved=false;
        success=originalDeviceSync_->receiveProjectUpdate(payload,connection,&handled,&saved,&message);
        if (handled)
        {
            publishOriginalProjectUpdateStates();
            if (saved)
            {
                SampleSnapshot snapshot;
                if (originalSamples_->load(&snapshot,&message))
                { emit originalProjectsReady(originalSamples_->projects()); emit originalProjectsChanged(); emit deviceTaskStateChanged(); }
                else emit deviceTaskError(QStringLiteral("项目UP已提交，但样本项目快照重读失败：")+message);
            }
            emit devicePacketProcessed(packet,connection,payload.left(2),success,message);
            return;
        }
        OriginalConfigurationStep step;
        success=originalDeviceSync_ && originalDeviceSync_->projectReply(payload,connection,&step,&message);
        if (step.commitSeen || (success && !step.ignored))
        {
            SampleSnapshot snapshot;
            QString refreshMessage;
            const bool loaded=originalSamples_->load(&snapshot,&refreshMessage);
            if (loaded)
            {
                emit originalProjectsReady(originalSamples_->projects());
                emit deviceTaskStateChanged();
            }
            // 实际写入即让其他读者尝试重读；本线程重读失败也不能藏掉已经提交的事实。
            if (step.commitSeen || loaded) emit originalProjectsChanged();
            if (!loaded)
            {
                success=false;
                message+=(message.isEmpty() ? QString() : QStringLiteral(" "))+
                    QStringLiteral("样本项目快照重读失败：")+refreshMessage;
            }
            if (success && !step.ignored) sendOriginalConfigurationStep(step);
        }
        if (!success && originalDeviceSync_)
        {
            if (step.commitSeen) message=QStringLiteral("项目UP已经提交，但完整同步确认尚未完成；实际写入保留：")+message;
            originalDeviceSync_->invalidate();
            publishOriginalProjectUpdateStates();
            if (originalConfigurationTimeout_) originalConfigurationTimeout_->stop();
            emit originalConfigurationState(connection,false,message);
        }
    }
    else
    {
        const bool taskReturn=taskBasis && payload.startsWith("TR");
        success = taskReturn
            ? originalDetection_->receiveDevicePayload(payload,&message,*taskBasis,&taskReceipt,committed)
            : originalDetection_->receiveDevicePayload(payload,&message);
        const bool committedTask=taskReturn && taskReceipt.committed;
        if ((success || committedTask) && (payload.startsWith("TD") || payload.startsWith("Tr") || payload.startsWith("TR") || payload.startsWith("TB"))) emit deviceTaskStateChanged();
        if ((success || committedTask) && (payload.startsWith("Tr") || payload.startsWith("TR"))) emit originalResultsChanged();
        if ((success || committedTask) && (payload.startsWith("TD") || payload.startsWith("Tr") || payload.startsWith("TR"))) emit originalCalibrationRecordsChanged();
        // 新链实提交由Root等Processed后专用回读，避免普通reload先使冻结前版失效。
        if (success && !committedTask && (payload.startsWith("Ur") || payload.startsWith("TR"))) emit originalReagentsChanged();
        if (payload.startsWith("Ur") && originalDeviceSync_ && originalDeviceSync_->waitingForReagents())
        {
            OriginalConfigurationStep step;
            if (success) success=originalDeviceSync_->reagentReplySaved(connection,&step,&message);
            if (success) sendOriginalConfigurationStep(step);
            else
            {
                originalDeviceSync_->invalidate();
                publishOriginalProjectUpdateStates();
                if (originalConfigurationTimeout_) originalConfigurationTimeout_->stop();
                emit originalConfigurationState(connection,false,message);
            }
        }
    }
    if (taskBasis && !receiptObserved &&
        taskReceipt.transaction==OriginalTaskReagents::TransactionState::CommitUnconfirmed)
    {
        receiptObserved=true;
        emit taskReagentCommitObserved(taskReceipt); // 保留committed=false，不猜回滚或重放。
    }
    // Root以此确认Session已返回、两锁已释放，再排Page专用回读；不能收到锁内通知就重读。
    emit devicePacketProcessed(packet,connection,payload.left(2),success,message);
}

void SampleStore::beginOriginalDeviceSync(quint64 connection, bool linuxNew)
{
    if (QThread::currentThread()!=thread() || !ready_ || !originalDeviceSync_ || !canEdit(session_))
    {
        emit originalConfigurationState(connection,false,QStringLiteral("当前会话或原配置未就绪，未发送设备参数。"));
        return;
    }
    if (!originalDetection_ || originalDetection_->hasUnfinishedIssuedTasks())
    {
        emit originalConfigurationState(connection,false,QStringLiteral("旧连接任务尚未核清，未建立新同步或重发任务。"));
        return;
    }
    OriginalConfigurationStep step; QString message;
    if (!originalDeviceSync_->begin(connection,linuxNew,&step,&message))
    { emit originalConfigurationState(connection,false,message); return; }
    sendOriginalConfigurationStep(step);
}

void SampleStore::invalidateOriginalDeviceSync()
{
    if (QThread::currentThread()!=thread()) return;
    if (originalConfigurationTimeout_) originalConfigurationTimeout_->stop();
    if (originalDetection_) originalDetection_->markConnectionUncertain();
    if (!originalDeviceSync_) return;
    const quint64 connection=originalDeviceSync_->connection();
    originalDeviceSync_->invalidate();
    if (originalProjectUpdateTimeout_) originalProjectUpdateTimeout_->stop();
    publishOriginalProjectUpdateStates();
    emit originalConfigurationState(connection,false,QStringLiteral("设备连接或项目配置已变化，需重新核对同步状态。"));
}

void SampleStore::sendOriginalConfigurationStep(const OriginalConfigurationStep &step)
{
    const quint64 connection=originalDeviceSync_->connection();
    if (originalConfigurationTimeout_)
    {
        originalConfigurationTimeout_->stop();
        if (originalDeviceSync_->waiting()) originalConfigurationTimeout_->start(10000);
    }
    emit originalConfigurationState(connection,step.complete,step.message);
    for (const auto &packet : step.packets) emit originalConfigurationPacket(packet,connection);
    publishOriginalProjectUpdateStates();
}

void SampleStore::prepareOriginalReagentUpdate(QString holeName, bool updateAmount, quint64 connection)
{
    if (QThread::currentThread()!=thread() || !ready_ || !originalSamples_ || !originalDeviceSync_ ||
        !connection || originalDeviceSync_->connection()!=connection || !canEdit(session_))
    { emit deviceTaskError(QStringLiteral("原试剂更新的会话或设备连接已变化，未下发。")); return; }
    // 原单孔设置只在仪器声明LinuxNew时发送Us；完整协议由Tu按需读取本地保存。
    if (!originalDeviceSync_->usesSimplifiedProtocol()) return;
    OriginalReagentRepository repository;
    OriginalReagentState state;
    QByteArray payload; QString message;
    if (!repository.initialize(originalSamples_->root(),&message) || !repository.load(&state,&message) ||
        !repository.singleReagentSettingPayload(state,holeName,updateAmount,&payload,&message))
    { emit deviceTaskError(message); return; }
    emit originalConfigurationPacket(payload,connection);
}
