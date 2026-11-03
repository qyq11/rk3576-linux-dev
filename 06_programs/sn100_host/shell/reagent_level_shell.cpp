#include "frmmain.h"
#include "task_reagent_context.h"
#include "instrument/device_control.h"
#include "reagents/reagents_page.h"
#include "reagents/original_reagent_level_worker.h"
#include "samples/sample_page.h"
#include "samples/sample_store.h"
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <limits>

namespace
{
bool sameLevelContext(const OriginalLevels::Context &a,const OriginalLevels::Context &b)
{
    return a.connection==b.connection && a.root==b.root && a.actualModel==b.actualModel &&
        a.configuredModel==b.configuredModel && a.themeFingerprint==b.themeFingerprint &&
        a.capacityRelativeFile==b.capacityRelativeFile && a.capacityFileState==b.capacityFileState &&
        a.capacityFingerprint==b.capacityFingerprint && a.protocolToken==b.protocolToken && a.linuxNew==b.linuxNew &&
        a.configuredPositions==b.configuredPositions && a.reportedPositions==b.reportedPositions &&
        a.mainStatus==b.mainStatus && a.capacityCalibrationActive==b.capacityCalibrationActive;
}
}

void frmMain::reportReagentLevel(const QString &message)
{
    setProperty("originalLevelMessage",message);
    qInfo().noquote()<<"REAGENT_LEVEL"<<message;
    if (reagents_) reagents_->notifyOriginalLevel(message);
}

QString frmMain::reagentLevelSyncBlocker(quint64 connection) const
{
    if (connection && reagentLevelRequestedUrConnection_==connection)
        return tr("本连接已经请求Ur，尚未收到存储处理收尾；N未抢占这条回读。");
    if (deviceRecoveryPending_ && connection==deviceRecoveryConnection_)
        return tr("原同步正在后台核对旧任务，未并发接收N写入。");
    if (deviceConfigurationStarted_ && (connection!=deviceConfigurationConnection_ ||
        (reagents_ && deviceConfigurationRoot_!=reagents_->property("originalRoot").toString())))
        return tr("原同步等待属于另一连接或资料根，N保持关闭。");
    if (deviceConfigurationStarted_ && !deviceConfigurationBaseReady_)
        return tr("本连接已登记初始同步，仍在后台准备或等待Ur/UP；N未抢占已请求的试剂回读。失败待明确重连，不重发。");
    return QString();
}

bool frmMain::reagentLevelBusy() const
{
    return bool(reagentLevelActive_) || (reagentLevelWorker_ && reagentLevelWorker_->isBusy()) ||
        (reagents_ && reagents_->hasPendingOriginalLevelPresentation());
}

void frmMain::invalidateReagentLevels()
{
    if (reagentLevelWorker_) reagentLevelWorker_->revoke();
    reagentLevelContext_.reset();
    reagentLevelUrContexts_.clear(); reagentLevelPriorityUrContext_.reset();
    if (reagents_) reagents_->setOriginalLevelContext(0,0);
}

void frmMain::reviewReagentLevelSource(const QString &root,quint64 connection)
{
    if (!reagentLevelWorker_ || root.isEmpty() || reagentLevelClosing_) return;
    invalidateReagentLevels();
    reagentLevelSourceRoot_=root; reagentLevelSourceConnection_=connection;
    reagentLevelSource_.reset();
    if (reagentLevelSourceRequest_==std::numeric_limits<quint64>::max())
    { reportReagentLevel(tr("容量来源重核编号已满，未复用旧结果。")); return; }
    const quint64 request=++reagentLevelSourceRequest_;
    if (!QMetaObject::invokeMethod(reagentLevelWorker_,"prepareSource",Qt::QueuedConnection,
        Q_ARG(quint64,request),Q_ARG(QString,root)))
        reportReagentLevel(tr("容量来源重核未排入存储线程，N保持关闭。"));
}

std::shared_ptr<OriginalLevels::Context> frmMain::reagentLevelUrContext(quint64 connection) const
{
    if (!reagentLevelContext_ || !reagentLevelWorker_ || reagentLevelClosing_ ||
        connection!=reagentLevelContext_->connection || !reagentLevelWorker_->isContextCurrent(*reagentLevelContext_))
        return {};
    return std::make_shared<OriginalLevels::Context>(*reagentLevelContext_);
}

void frmMain::confirmReagentLevelUr(const QByteArray &payload,quint64 connection,
    const std::shared_ptr<OriginalLevels::Context> &arrivalContext)
{
    const auto current=reagentLevelUrContext(connection);
    if (!arrivalContext || !current || !sameLevelContext(*arrivalContext,*current) ||
        arrivalContext->permissionRevision!=current->permissionRevision)
    {
        reportReagentLevel(tr("Ur已在原接收链保存，但到达时的协议、资料或许可已不可确认；未给N补造运行来源。"));
        return;
    }
    beginTaskReagentUrSource(payload,connection);
    reagents_->confirmOriginalLevelUr(payload,arrivalContext->linuxNew,connection);
    finishTaskReagentSourceEnqueue();
}

bool frmMain::requestReagentLevelShutdown()
{
    reagentLevelClosing_=true;
    invalidateReagentLevels();
    if (reagentLevelActive_ || (reagentLevelWorker_ && reagentLevelWorker_->isBusy()))
    {
        reportReagentLevel(tr("正在撤销N观测并等待在途保存结束；不终止数据库线程或假定已回滚。"));
        return false;
    }
    qInfo() << "REAGENT_LEVEL_SHUTDOWN_READY active" << bool(reagentLevelActive_) << "worker_busy" << (reagentLevelWorker_ && reagentLevelWorker_->isBusy());
    return true;
}

void frmMain::updateReagentLevelContext(bool validQj)
{
    if (!reagentLevelWorker_ || !deviceControl_ || !reagents_) return;
    const auto &state=deviceControl_->state();
    const quint64 connection=deviceControl_->connectionGeneration();
    const QString root=reagents_->property("originalRoot").toString();
    if (!root.isEmpty() && !reagentLevelClosing_ && (root!=reagentLevelSourceRoot_ ||
        (state.connected && connection!=reagentLevelSourceConnection_)))
    {
        // 同根重连也重新核文件版本；读取失败不在每个Qj上无限重试。
        reviewReagentLevelSource(root,state.connected ? connection : 0);
    }
    QString reason;
    const QString synchronization=reagentLevelSyncBlocker(connection);
    if (!state.connected || !synchronization.isEmpty())
        reagentLevelWorker_->setReagentReadbackPending(state.connected ? connection : 0,true);
    else if (validQj)
        reagentLevelWorker_->setReagentReadbackPending(connection,false); // 回读收尾不伪造Qj，等实际新Qj再开N写门。
    OriginalLevels::Context context;
    if (reagentLevelClosing_) reason=tr("程序正在关闭，未接收新的N保存。");
    else if (!state.connected || !state.fresh) reason=tr("主控未连接或Qj已失效，N未写库。");
    else if (!reagents_->isReady() || !reagents_->snapshot().originalProfile || root.isEmpty())
        reason=tr("原试剂资料尚未就绪，N未写库。");
    else if (!reagentLevelSource_ || reagentLevelSource_->root!=root)
        reason=tr("实际主题和容量文件版本尚未核实，N未写库。");
    else
    {
        context=*reagentLevelSource_;
        context.connection=connection;
        context.actualModel=state.status.model;
        context.protocolToken=state.status.generalFields.value(19);
        context.linuxNew=context.protocolToken==QStringLiteral("LinuxNew");
        context.mainStatus=state.status.mainStatus;
        bool countOk=false;
        context.reportedPositions=state.status.generalFields.value(20).toInt(&countOk);
        if (!countOk || context.reportedPositions!=context.configuredPositions ||
            (context.configuredPositions!=20 && context.configuredPositions!=42))
            reason=tr("配置%1试剂位与主控%2未核对一致，N未写库，未放宽42/48对应。")
                .arg(context.configuredPositions).arg(state.status.generalFields.value(20));
        else if (context.actualModel.isEmpty() || context.actualModel!=context.configuredModel ||
                 context.configuredModel!=property("configuredDeviceModel").toString() ||
                 context.configuredPositions!=property("configuredReagentPositions").toInt())
            reason=tr("生效主题、页面和主控机型/孔数不一致，N未写库。");
        else if (context.mainStatus<0 || context.mainStatus>6)
            reason=tr("主控状态不是原0至6有效状态，N未写库。");
        else if (!deviceConfigurationRoot_.isEmpty() && deviceConfigurationRoot_!=root)
            reason=tr("当前连接正在使用另一个原资料根，N未写库。");
        // 当前容量标定入口尚未启用，没有原m_param批次；以后接入时必须补真实批次状态。
        context.capacityCalibrationActive=false;
    }
    setProperty("originalLevelBlocker",reason.isEmpty() ? synchronization : reason);
    if (!reason.isEmpty())
    {
        reagentLevelWorker_->revoke(); reagentLevelContext_.reset();
        reagents_->setOriginalLevelContext(state.connected ? connection : 0,0);
        taskReagentContextChanged();
        return;
    }
    if (!validQj)
    {
        // changed也会由错误或其他命令触发，不能用它们续Qj新鲜期限。
        if (reagentLevelContext_ && !sameLevelContext(context,*reagentLevelContext_)) invalidateReagentLevels();
        taskReagentContextChanged();
        return;
    }
    if (!reagentLevelContext_ || !sameLevelContext(context,*reagentLevelContext_))
    {
        if (reagentLevelPermissionRevision_==std::numeric_limits<quint64>::max())
        { invalidateReagentLevels(); taskReagentContextChanged(); reportReagentLevel(tr("N许可编号已满，未复用旧编号。")); return; }
        context.permissionRevision=++reagentLevelPermissionRevision_;
    }
    else context.permissionRevision=reagentLevelContext_->permissionRevision;
    QString message;
    // 任务门只限新N；真实Qj仍更新Ur和在途N所需事实，不保留旧mainStatus或伪续期限。
    const bool observationOnly=taskReagentObservationOnly(connection,true);
    const bool accepted=observationOnly ?
        reagentLevelWorker_->setTaskObservationFacts(context,3000,taskReagentFenceToken(),&message) :
        reagentLevelWorker_->setObservationContext(context,3000,&message);
    if (!accepted)
    {
        if (observationOnly)
        {
            qWarning().noquote()<<"TASK_REAGENT_OBSERVATION_FACTS_REJECTED"<<connection<<taskReagentFenceToken()<<message;
            // 任务门拒绝不等于真实来源撤销；原worker仍按原数据/期限守卫收尾。
            taskReagentContextChanged();
            reportReagentLevel(tr("当前试剂资料尚未完成安全核对，暂不保存新的液面结果。"));
            return;
        }
        invalidateReagentLevels(); taskReagentContextChanged(); reportReagentLevel(message); return;
    }
    reagentLevelContext_=std::make_shared<OriginalLevels::Context>(context);
    reagents_->setOriginalLevelContext(connection,context.permissionRevision);
    taskReagentContextChanged();
}

void frmMain::attachReagentLevels()
{
    if (reagentLevelWorker_ || !deviceControl_ || !reagents_ || !reagents_->taskStore() || !samples_ || !samples_->taskStore()) return;
    auto *receiver=new OriginalLevels::ObservationWorker;
    reagentLevelWorker_=receiver;
    receiver->moveToThread(reagents_->taskStore()->thread());
    connect(receiver->thread(),&QThread::finished,receiver,&QObject::deleteLater);
    connect(reagents_->taskStore(),&ReagentStore::closed,receiver,[receiver](){receiver->revoke();});
    connect(receiver,&OriginalLevels::ObservationWorker::sourceReady,this,
        [this](quint64 request,OriginalLevels::Context source,const QString &message)
    {
        if (request!=reagentLevelSourceRequest_ || reagentLevelClosing_ || source.root!=reagentLevelSourceRoot_ ||
            source.root!=reagents_->property("originalRoot").toString()) return;
        if (!message.isEmpty())
        {
            reagentLevelSource_.reset();
            reagents_->rejectOriginalLevelObservation(QString(),false,message);
            reportReagentLevel(message);
        }
        else reagentLevelSource_=std::make_shared<OriginalLevels::Context>(source);
        updateReagentLevelContext(false);
        taskReagentSourceReviewed(message.isEmpty());
    });
    connect(deviceControl_,&DeviceControl::changed,this,[this](const DeviceControlState &){updateReagentLevelContext(false);});
    connect(reagents_,&ReagentsPage::snapshotChanged,this,[this](const ReagentSnapshot &){updateReagentLevelContext(false);});
    connect(reagents_,&ReagentsPage::busyChanged,this,[this](bool busy)
    {
        if (busy || reagentLevelClosing_) return;
        // pending可先于专用读取结束被撤销；真正归还Page忙碌时再补一次继续，不依赖下条Qj。
        QTimer::singleShot(0,this,[this]()
        {
            if (!reagentLevelClosing_) updateOriginalDeviceSync();
            advanceTaskReagents();
        });
    });
    connect(reagents_,&ReagentsPage::originalLevelRuntimeChanged,this,[this]()
    {
        checkTaskReagentSource();
        QTimer::singleShot(0,this,[this](){advanceTaskReagents();});
        if (reagentLevelRuntimeHole_.isEmpty() || !reagentLevelWorker_ || !deviceControl_) return;
        const quint64 connection=deviceControl_->connectionGeneration();
        const auto runtime=reagents_->originalLevelRuntime(reagentLevelRuntimeHole_);
        QString problem;
        if (!runtime.available || !reagentLevelContext_ || !reagentLevelWorker_->publishRuntime(connection,runtime,&problem))
        {
            reagentLevelWorker_->invalidateRuntime(connection,reagentLevelRuntimeHole_);
            if (!problem.isEmpty()) reportReagentLevel(problem);
        }
    });
    connect(reagents_->taskStore(),&ReagentStore::error,reagents_,[this](const QString &operation,const QString &message)
    { if (operation==QStringLiteral("读取原试剂")) reagents_->originalLevelReloadFailed(message); });
    connect(reagents_->taskStore(),&ReagentStore::originalHoleSaved,this,[this](const QString &hole)
    {
        const quint64 connection=deviceControl_->connectionGeneration();
        beginTaskReagentManualSource(hole,connection);
        reagents_->confirmOriginalLevelManual(hole,connection);
        finishTaskReagentSourceEnqueue();
    });
    // 此绑定在旧泛接收绑定之前建立，先取唯一队列中的原Ur，旧槽随后才take。
    connect(samples_->taskStore(),&SampleStore::devicePacketProcessed,this,
        [this](quint64 packet,quint64 connection,const QByteArray &command,bool success,const QString &)
    {
        const auto arrivalContext=reagentLevelUrContexts_.take(packet);
        if (command!="Ur" || !success || connection!=deviceControl_->connectionGeneration()) return;
        const QByteArray payload=devicePackets_.value(packet);
        if (payload.isEmpty()) { reportReagentLevel(tr("Ur已保存但原包不在唯一队列，未补造运行最大量。")); return; }
        confirmReagentLevelUr(payload,connection,arrivalContext);
    });
    connect(deviceControl_,&DeviceControl::payloadReceivedForConnection,this,[this](const QByteArray &payload,quint64 arrival)
    {
        if (arrival!=deviceControl_->connectionGeneration()) return;
        if (payload.startsWith("Qj")) { updateReagentLevelContext(true); return; }
        if (!payload.startsWith('N')) return;
        const QString taskBlocker=taskReagentNBlocker();
        if (!taskBlocker.isEmpty()) { reportReagentLevel(taskBlocker); return; }
        QString message;
        if (!reagentLevelContext_ || reagentLevelClosing_)
        { reportReagentLevel(property("originalLevelBlocker").toString()); return; }
        const QString synchronization=reagentLevelSyncBlocker(arrival);
        if (!synchronization.isEmpty()) { reportReagentLevel(synchronization); return; }
        if (reagentLevelBusy() || reagents_->hasUnconfirmedOriginalLevelRuntime() || reagents_->isBusy() ||
            reagents_->hasUnsavedChanges() || reagentPriorityRequest_ || deviceTaskPreparing_ ||
            !devicePackets_.isEmpty() || !deviceReagentUpdateHole_.isEmpty())
        { reportReagentLevel(tr("N到达时资料、其他回读或展示仍在处理；未并发写库、换版本或合并旧N。")); return; }
        OriginalLevels::ReceivedPacket packet;
        if (!OriginalLevels::decodePacket(payload,reagentLevelContext_->linuxNew,&packet,&message))
        { reportReagentLevel(message); return; }
        if (packet.kind==OriginalLevels::PacketKind::SampleIgnored) return;
        const auto runtime=reagents_->originalLevelRuntime(packet.hole);
        if (!runtime.available)
        { reportReagentLevel(reagents_->originalLevelRuntimeReason(packet.hole)); return; }
        if (!reagentLevelWorker_->publishRuntime(arrival,runtime,&message))
        { reportReagentLevel(message); return; }
        if (reagentLevelPacket_==std::numeric_limits<quint64>::max())
        { reportReagentLevel(tr("N本地接收编号已满，未复用旧编号。")); return; }
        const quint64 packetId=++reagentLevelPacket_;
        if (!reagentLevelWorker_->submit(arrival,packetId,payload,&message))
        { reportReagentLevel(message); return; }
        reagentLevelActive_=std::make_shared<OriginalLevels::Context>(*reagentLevelContext_);
        reagentLevelActive_->packetId=packetId;
        reagentLevelRuntimeHole_=packet.hole;
        qInfo().noquote()<<"REAGENT_LEVEL_ACCEPTED"<<packetId<<arrival<<payload.toHex(' ');
        reportReagentLevel(tr("N已进入现有存储线程，尚未保存或报告页面完成。"));
        updateDeviceControlActions();
    });
    connect(receiver,&OriginalLevels::ObservationWorker::processed,this,
        [this](OriginalLevels::Context context,bool committed,bool loaded,const QString &fingerprint,const QString &message)
    {
        qInfo().noquote()<<"REAGENT_LEVEL_PROCESSED"<<context.packetId<<context.connection<<committed<<loaded<<fingerprint<<message;
        QTimer::singleShot(0,this,[this](){advanceTaskReagents();});
        if (!reagentLevelActive_ || context.packetId!=reagentLevelActive_->packetId ||
            context.permissionRevision!=reagentLevelActive_->permissionRevision || !sameLevelContext(context,*reagentLevelActive_))
        { reportReagentLevel(tr("收到不属于当前N等待的后台收尾，保留提交记录，未撤销另一条N许可。")); return; }
        const QString hole=reagentLevelRuntimeHole_;
        reagentLevelActive_.reset();
        reagentLevelRuntimeHole_.clear();
        if (!loaded) reagents_->rejectOriginalLevelObservation(hole,committed,message);
        reportReagentLevel(message);
        // 保存失败后仅重新核来源，不重放N或发送探测；下一次真实Qj才可重新签发许可。
        if (!loaded && !reagentLevelClosing_ && context.connection==deviceControl_->connectionGeneration())
            reviewReagentLevelSource(reagents_->property("originalRoot").toString(),context.connection);
        if (!reagentLevelClosing_) updateOriginalDeviceSync();
        updateDeviceControlActions();
        // 保存收尾后重试关闭外层窗口，使首次被拦的外层关闭流程继续执行。
        if (reagentLevelClosing_ && !reagentLevelWorker_->isBusy()) QTimer::singleShot(0,this,[this](){window()->close();});
    });
    connect(receiver,&OriginalLevels::ObservationWorker::presentationReady,this,[this](const OriginalLevels::ObservationPresentation &presentation)
    {
        if (!reagentLevelActive_ || presentation.context.packetId!=reagentLevelActive_->packetId ||
            presentation.context.connection!=reagentLevelActive_->connection ||
            !reagentLevelContext_ || reagentLevelClosing_ || !reagentLevelWorker_->isContextCurrent(presentation.context) || !sameLevelContext(presentation.context,*reagentLevelContext_) ||
            presentation.context.permissionRevision!=reagentLevelContext_->permissionRevision)
        { reportReagentLevel(tr("N已保存，但GUI收到展示时上下文已变，未把旧展示归给当前连接。")); return; }
        QString message;
        if (!reagents_->enqueueOriginalLevelPresentation(presentation,&message))
        {
            reagents_->rejectOriginalLevelObservation(presentation.after.hole,true,message);
            reportReagentLevel(message);
            if (reagentLevelContext_ && reagentLevelContext_->connection==presentation.context.connection)
                reviewReagentLevelSource(reagents_->property("originalRoot").toString(),presentation.context.connection);
        }
        else
        {
            if (!taskReagentBridge_) attachTaskReagents();
            if (!taskReagentBridge_) taskReagentBridge_=std::make_shared<TaskReagentRouting::Bridge>();
            const quint64 packet=presentation.context.packetId, connection=presentation.context.connection;
            taskReagentBridge_->nReaders.insert(packet,connection);
            const QPointer<frmMain> owner(this);
            const QPointer<ReagentStore> store(reagents_->taskStore());
            // Page已把专用读取排给这个Store；同队列的内存哨兵只能在实际槽返回后执行。
            const bool queued=store && QMetaObject::invokeMethod(store.data(),[owner,packet,connection]()
            {
                if (!owner) return;
                const bool returned=QMetaObject::invokeMethod(owner.data(),[owner,packet,connection]()
                {
                    if (!owner || !owner->taskReagentBridge_) return;
                    auto &readers=owner->taskReagentBridge_->nReaders;
                    const auto reader=readers.find(packet);
                    if (reader==readers.end() || reader.value()!=connection) return;
                    readers.erase(reader);
                    owner->advanceTaskReagents();
                },Qt::QueuedConnection);
                if (!returned)
                    qWarning().noquote()<<"REAGENT_LEVEL_N_READER_GUI_QUEUE_FAILED"<<packet<<connection
                        <<QStringLiteral("读取后哨兵未能投回主窗口，读取等待仍保留，未假报完成。");
            },Qt::QueuedConnection);
            if (!queued)
                reportReagentLevel(tr("N专用读取已排入，但读取完成哨兵未能排入同一存储队列；读取等待仍保留，未假报完成。"));
        }
        updateDeviceControlActions();
    });
    connect(reagents_,&ReagentsPage::originalLevelPresentationFinished,this,
        [this](quint64 packet,quint64 connection,quint64 permission,bool shown,const QString &fingerprint,const QString &message)
    {
        qInfo().noquote()<<"REAGENT_LEVEL_DISPLAY"<<packet<<connection<<permission<<shown<<fingerprint<<message;
        QTimer::singleShot(0,this,[this](){advanceTaskReagents();});
        reportReagentLevel(message); updateDeviceControlActions();
        if (!shown && reagentLevelContext_ && permission==reagentLevelContext_->permissionRevision &&
            !reagentLevelClosing_ && connection==deviceControl_->connectionGeneration())
            reviewReagentLevelSource(reagents_->property("originalRoot").toString(),connection);
        if (!reagentLevelClosing_) updateOriginalDeviceSync(); // 显示成功或失败都已归还N等待，继续原同步准备。
    });
    updateReagentLevelContext(false);
}
