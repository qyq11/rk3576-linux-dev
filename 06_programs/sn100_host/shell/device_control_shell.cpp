#include "frmmain.h"
#include "ui_frmmain.h"
#include "instrument/device_control.h"
#include "instrument/instrument_page.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include "reagents/original_reagent_repository.h"
#include "samples/sample_page.h"
#include "history/history_page.h"
#include "calibration/calibration_page.h"
#include "ui/frmaccountsetting.h"
#include "ui/cusmessagebox.h"
#include <QFontMetrics>
#include <QTimer>
#include <QDebug>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QVariantMap>

namespace
{
QString projectUpdateKey(const OriginalProjectSaveNotice &notice)
{
    return notice.root+QChar(0)+notice.originalFileName;
}
// 已由后台校验并成功提交的Ur，仍须包含本次等待的孔位，才结束单孔等待。
bool reagentReplyContains(const QByteArray &payload, const QString &hole)
{
    if (!payload.startsWith("Ur[")) return false;
    const QList<QByteArray> fields=payload.mid(3,payload.size()-4).split(']');
    // 用原括号分隔读取；不可拿子串R_1误匹配R_10。
    QList<QByteArray> values;
    for (const auto &field : fields) values.append(field.startsWith('[') ? field.mid(1) : field);
    bool ok=false; const int count=values.value(0).toInt(&ok);
    if (!ok || count<0 || count>128) return false;
    for (int i=0;i<count;++i)
        if (QString::fromLatin1(values.value(1+i*15)).compare(hole,Qt::CaseInsensitive)==0) return true;
    return false;
}
}

bool frmMain::enableDeviceControl(bool connectAtStartup)
{
    if (deviceControl_) return false;
    deviceControl_ = new DeviceControl(this);
    updateDeviceActionReadiness();
    // 脱机占位曾禁用整棵灯控件；联机后恢复绘制，避免QLabel把彩图按禁用状态转灰。
    ui->wgtDivStateLamp->setEnabled(true);
    const QString configuredModel = property("configuredDeviceModel").toString();
    if (!configuredModel.isEmpty()) ui->lblDivType->setText(configuredModel);
    connect(deviceControl_, &DeviceControl::wireActivity, this, [](const QString &direction, const QByteArray &bytes)
    { qInfo().noquote() << "DEVICE_WIRE" << direction << bytes.toHex(' '); });
    connectDeviceAtStartup_ = connectAtStartup;
    if (instrument_) instrument_->attachDeviceControl(deviceControl_, connectAtStartup);
    connect(deviceControl_, &DeviceControl::changed, this, [this](const DeviceControlState &state)
    {
        if (!state.connected) reagentLevelRequestedUrConnection_=0;
        updateDeviceActionReadiness();
        updateDeviceStatusDisplay();
        setProperty("instrumentConnected", state.connected);
        setProperty("instrumentStatusFresh", state.fresh);
        setProperty("instrumentMainStatus", state.status.mainStatus);
        if (state.connected && state.fresh && !state.status.model.isEmpty())
        {
            ui->lblDivType->setText(state.status.model);
            ui->lblDivType->setToolTip(tr("主控实际返回机型：%1；配置机型：%2")
                .arg(state.status.model, property("configuredDeviceModel").toString()));
        }
        updateOriginalDeviceSync();
        updateDeviceControlActions();
    });
    updateDeviceStatusDisplay();
    connect(this, &frmMain::sessionChanged, this, [this](const AccountSession &)
    {
        invalidateDeviceTaskContext();
        // 页面的session槽也向后台排队；等同一轮GUI槽处理完再开始同步。
        QTimer::singleShot(0,this,[this](){ updateOriginalDeviceSync(); updateDeviceControlActions(); });
    });
    connect(ui->btnStart, &QPushButton::clicked, this, [this]()
    {
        const QString reason = deviceControl_->startBlocker().isEmpty()
                                   ? detectionStartBlocker() : deviceControl_->startBlocker();
        if (!reason.isEmpty())
        {
            CusMessageBox message(this);
            message.showWarningBox(reason);
            return;
        }
        CusMessageBox question(this);
        question.showQuestionBox(tr("是否开始测试？"));
        if (question.getClickButton() != QMessageBox::Yes || deviceTaskPreparing_) return;
        // 确认框内仍可能收到断线或页面变化，正式准备前再核对。
        if (!deviceControl_->startBlocker().isEmpty() || !detectionStartBlocker().isEmpty()) return;
        deviceTaskPreparing_=true;
        const quint64 request=++deviceTaskRequest_;
        deviceTaskConnection_=deviceControl_->connectionGeneration();
        const bool linuxNew=deviceControl_->state().status.generalFields.value(19)==QStringLiteral("LinuxNew");
        updateDeviceControlActions();
        QMetaObject::invokeMethod(deviceTaskStore_,"prepareDeviceTasks",Qt::QueuedConnection,
            Q_ARG(quint64,request),Q_ARG(bool,linuxNew));
        QTimer::singleShot(10000,this,[this,request]()
        {
            if (!deviceTaskPreparing_ || request!=deviceTaskRequest_) return;
            deviceTaskPreparing_=false;
            if (deviceTaskStore_) QMetaObject::invokeMethod(deviceTaskStore_,"cancelDeviceTaskPreparation",Qt::QueuedConnection);
            reportDeviceTaskError(tr("样本任务准备10秒未返回，未发送开始；请核对数据和连接后重试。"));
            updateDeviceControlActions();
        });
    });
    connect(ui->btnPause, &QPushButton::clicked, this, [this]()
    {
        CusMessageBox question(this);
        question.showQuestionBox(tr("是否暂停测试？"));
        if (question.getClickButton() == QMessageBox::Yes) deviceControl_->pause();
    });
    connect(ui->btnCloseBeep, &CusBadgeButton::clicked, this, [this]()
    {
        CusMessageBox question(this);
        question.showQuestionBox(tr("是否关闭报警蜂鸣器？"));
        if (question.getClickButton() == QMessageBox::Yes) deviceControl_->silenceAlarm();
    });
    connect(ui->btnAbnormal, &CusBadgeButton::clicked, this, [this]()
    {
        if (deviceControl_->state().connected) deviceControl_->query(Sn100Protocol::ReadOnlyQuery::Alarm);
    });
    // main在创建设备控制之后才挂业务页；事件循环开始时再做一次幂等绑定。
    QTimer::singleShot(0,this,[this](){ attachDeviceTaskStore(); updateOriginalDeviceSync(); });
    updateDeviceControlActions();
    return true;
}

void frmMain::updateDeviceActionReadiness()
{
    if (!deviceControl_) return;
    const auto &state = deviceControl_->state();
    // 原slotTimeTickerOut：仅Idle灯(1)配育温(1)/就绪(2)允许动作。
    // 断线或状态失效时不沿用旧Qj；暂停(6)和其他灯态仍为false。
    const bool actionReady = state.connected && state.fresh && state.status.ledStatus == 1 &&
        (state.status.mainStatus == 1 || state.status.mainStatus == 2);
    if (deviceActionReadyObserved_ && deviceActionReady_ == actionReady) return;
    deviceActionReadyObserved_ = true;
    deviceActionReady_ = actionReady;
    if (accounts_ && !QMetaObject::invokeMethod(accounts_, "setDeviceActionReady", Qt::QueuedConnection,
                                               Q_ARG(bool, actionReady)))
        reportDeviceTaskError(tr("实际设备动作状态未能排入账户工作线程，请核对程序接线。"));
    // 界面先使用最新状态；后台守卫在各自所属线程按排队顺序更新。
    applyDeviceActionReadiness(actionReady);
}

void frmMain::applyDeviceActionReadiness(bool actionReady)
{
    // SQL线程的旧状态回执可能晚于新的Qj，不使页面重新开放旧状态。
    if (deviceActionReadyObserved_ && actionReady != deviceActionReady_) return;
    if (accountPage()) accountPage()->setDeviceActionReady(actionReady);
    if (session_.actionReady == actionReady) return;
    session_.actionReady = actionReady;
    if (projects_) projects_->setSession(session_);
    if (reagents_) reagents_->setSession(session_);
    if (samples_) samples_->setSession(session_);
    if (calibration_) calibration_->setSession(session_);
    emit deviceActionStateChanged(session_);
    updateAccountStatus();
    updateDeviceControlActions();
}

void frmMain::updateDeviceControlActions()
{
    if (!deviceControl_) return;
    const auto &state = deviceControl_->state();
    const bool available = !deviceTaskPreparing_ && session_.authenticated && state.connected && state.fresh &&
                           state.pending.isEmpty() && !state.outcomeUnknown;
    ui->btnStart->setBtnDisabled(!available || !detectionStartBlocker().isEmpty() || (state.status.mainStatus != 2 && state.status.mainStatus != 6));
    ui->btnPause->setBtnDisabled(!available || state.status.mainStatus < 3 || state.status.mainStatus > 6);
    ui->btnCloseBeep->setEnabled(session_.authenticated && state.connected);
    ui->btnStart->setToolTip(deviceControl_->startBlocker().isEmpty()
                                ? detectionStartBlocker() : deviceControl_->startBlocker());
    ui->btnPause->setToolTip(deviceControl_->pauseBlocker());
    const QString startReason = ui->btnStart->toolTip();
    const QString pauseReason = ui->btnPause->toolTip();
    const QString actionState = QStringLiteral("start=%1 pause=%2 start_reason=%3 pause_reason=%4")
        .arg(ui->btnStart->isEnabled()).arg(ui->btnPause->isEnabled()).arg(startReason, pauseReason);
    if (property("deviceActionState").toString() != actionState)
    {
        setProperty("deviceActionState", actionState);
        qInfo().noquote() << "DEVICE_ACTION_STATE" << actionState;
    }
    ui->btnCloseBeep->setToolTip(tr("按原Windows命令关闭主控报警蜂鸣器。"));
    if (reagents_) reagents_->setInstrumentBusy(reagentLevelBusy() || deviceTaskPreparing_ ||
        (!deviceReagentUpdateHole_.isEmpty() && !reagentPriorityRequest_) ||
        deviceReagentUpdateUnknown_ || deviceReagentRecoveryRequired_ || deviceRecoveryPending_ || deviceRecoveryUnfinished_ ||
        (deviceConfigurationStarted_ && !deviceConfigurationBaseReady_) ||
        (!projectUpdateNotices_.isEmpty() && !reagentPriorityRequest_) ||
        (reagentPriorityUnknownConnection_ && reagentPriorityUnknownConnection_==deviceControl_->connectionGeneration()) ||
        (state.connected && (!state.fresh || !state.pending.isEmpty() || state.outcomeUnknown || state.status.mainStatus>2)));
    updateReagentPriorityAccess();
}

QString frmMain::reagentPositionBlocker() const
{
    const int configured=property("configuredReagentPositions").toInt();
    if (configured>0)
    {
        if (!deviceControl_ || deviceControl_->state().status.generalFields.size()<=20)
            return tr("当前配置为%1个试剂位，设备尚未上报试剂位数量。").arg(configured);
        bool ok=false;
        const QString reported=deviceControl_->state().status.generalFields.value(20);
        const int positions=reported.toInt(&ok);
        // 保留主题与主控原值；孔位表含义未确认时不能把同步通过当成可安全检测。
        if (!ok || positions!=configured)
            return tr("当前配置为%1个试剂位，设备返回%2，孔位对应尚未确认。").arg(configured).arg(reported);
    }
    return QString();
}

QString frmMain::detectionStartBlocker() const
{
    if (reagentLevelBusy()) return tr("N正在保存或等待页面读回，未并发开始新的任务。");
    const QString taskTransaction=taskReagentTransactionBlocker();
    if (!taskTransaction.isEmpty()) return taskTransaction;
    if (!session_.authenticated) return tr("请先登录后开始测试。");
    const QString projectUpdate=originalProjectUpdateBlocker();
    if (!projectUpdate.isEmpty()) return projectUpdate;
    const QString recovery=deviceRecoveryBlocker();
    if (!recovery.isEmpty()) return recovery;
    const QString positions=reagentPositionBlocker();
    if (!positions.isEmpty()) return positions+tr("未开始检测。");
    if (reagentPriorityUnknownConnection_ && deviceControl_ &&
        reagentPriorityUnknownConnection_==deviceControl_->connectionGeneration())
        return tr("优先使用的设备结果仍待核对，请重新连接并核对原配置后再开始。");
    if (!deviceTaskStore_) return tr("样本任务后台尚未接通。");
    if (!deviceReagentUpdateHole_.isEmpty() || deviceReagentUpdateUnknown_)
        return tr("试剂单孔更新尚未收到对应设备回读，请核对后重新连接。");
    if (!samples_ || !projects_ || !reagents_ || !samples_->isReady() ||
        !projects_->isReady() || !reagents_->isReady()) return tr("样本、项目或试剂资料尚未读取完成。");
    if (samples_->isBusy() || projects_->isBusy() || reagents_->isBusy()) return tr("样本、项目或试剂正在处理，请等待结果。");
    if (samples_->hasUnsavedChanges() || projects_->hasUnsavedChanges() || reagents_->hasUnsavedChanges())
        return tr("样本、项目或试剂设置尚未保存，请先保存后再开始测试。");
    int tasks = 0;
    for (const auto &rack : samples_->snapshot().racks)
        for (const auto &hole : rack.holes)
            for (const auto &task : hole.tasks) tasks += task.repeats;
    if (!tasks) return tr("未设置新任务，无法开始测试。");
    if (!samples_->originalProfile() || !reagents_->snapshot().originalProfile)
        return tr("样本和试剂尚未使用当前原数据库，请核对启动的数据根。");
    if (!deviceConfigurationReady_ || !deviceControl_ || deviceConfigurationConnection_!=deviceControl_->connectionGeneration())
        return tr("当前连接的原项目与试剂资料尚未完成核对。");
    return QString();
}

void frmMain::reportDeviceTaskError(const QString &message)
{
    setProperty("deviceTaskError",message);
    ui->lblDivState->setText(tr("任务处理失败"));
    ui->lblDivState->setToolTip(message);
    qWarning().noquote()<<"DEVICE_TASK_ERROR"<<message;
}

void frmMain::rememberReagentRecoveryContext()
{
    if (property("deviceReagentRecoveryContext").isValid()) return;
    // 所有未知结束路径在清请求/孔位/阶段前调用；后备路径不能先放一份残缺记录。
    QVariantMap context;
    context.insert(QStringLiteral("root"),deviceConfigurationRoot_);
    context.insert(QStringLiteral("connection"),QVariant::fromValue(deviceConfigurationConnection_));
    context.insert(QStringLiteral("hole"),deviceReagentUpdateHole_);
    context.insert(QStringLiteral("updateRequest"),QVariant::fromValue(deviceReagentUpdateRequest_));
    context.insert(QStringLiteral("updateUnknown"),deviceReagentUpdateUnknown_);
    context.insert(QStringLiteral("priorityRequest"),QVariant::fromValue(reagentPriorityRequest_));
    context.insert(QStringLiteral("priorityConnection"),QVariant::fromValue(reagentPriorityConnection_));
    context.insert(QStringLiteral("priorityStage"),int(reagentPriorityStage_));
    context.insert(QStringLiteral("priorityMayHaveQueued"),reagentPriorityMayHaveQueued_);
    context.insert(QStringLiteral("priorityUnknownConnection"),QVariant::fromValue(reagentPriorityUnknownConnection_));
    context.insert(QStringLiteral("sentSha256"),QCryptographicHash::hash(reagentPrioritySent_,QCryptographicHash::Sha256).toHex());
    context.insert(QStringLiteral("replySha256"),QCryptographicHash::hash(reagentPriorityReply_,QCryptographicHash::Sha256).toHex());
    setProperty("deviceReagentRecoveryContext",context);
}

void frmMain::invalidateDeviceTaskContext()
{
    cancelTaskReagentContexts(tr("连接、身份或原资料上下文已真实撤销，旧任务仍待真实后台收尾。"));
    invalidateReagentLevels();
    reagentLevelRequestedUrConnection_=0;
    // 清理的是本次等待，不是设备执行事实。普通Us尚无独立入队回执，等待时保守保留。
    const bool priorityNotSent=reagentPriorityRequest_ &&
        reagentPriorityStage_==ReagentPriorityStage::Preparing && !reagentPriorityMayHaveQueued_;
    if (deviceReagentUpdateUnknown_ || reagentPriorityUnknownConnection_ || reagentPriorityMayHaveQueued_ ||
        (!deviceReagentUpdateHole_.isEmpty() && !priorityNotSent))
    {
        deviceReagentRecoveryRequired_=true;
        rememberReagentRecoveryContext();
    }
    if (reagentPriorityRequest_)
        finishReagentPriority(false,tr("连接、身份或原资料上下文已改变，原优先使用请求已停止等待。"),reagentPriorityMayHaveQueued_);
    deviceTaskPreparing_=false; ++deviceTaskRequest_;
    deviceConfigurationStarted_=false; deviceConfigurationReady_=false;
    deviceConfigurationBaseReady_=false;
    projectUpdatePumpQueued_=false; projectUpdatePumpConnection_=0;
    deviceConfigurationConnection_=0; deviceConfigurationRoot_.clear();
    ++deviceRecoveryRequest_; deviceRecoveryConnection_=0; deviceRecoveryRoot_.clear();
    deviceRecoveryPending_=deviceRecoveryChecked_=deviceRecoveryUnfinished_=false;
    deviceRecoveryMessage_.clear();
    deviceReagentUpdateHole_.clear(); deviceReagentUpdateUnknown_=false; ++deviceReagentUpdateRequest_;
    if (deviceTaskStore_)
    {
        QMetaObject::invokeMethod(deviceTaskStore_,"invalidateOriginalDeviceSync",Qt::QueuedConnection);
        QMetaObject::invokeMethod(deviceTaskStore_,"cancelDeviceTaskPreparation",Qt::QueuedConnection);
    }
    setProperty("originalConfigurationReady",false);
}

QString frmMain::deviceRecoveryBlocker() const
{
    if (deviceReagentRecoveryRequired_)
        return tr("旧试剂更新的执行情况仍待核对；网络可重连，未自动重发或恢复业务同步。");
    if (deviceRecoveryPending_ || !deviceRecoveryChecked_ || !deviceControl_ ||
        deviceRecoveryConnection_!=deviceControl_->connectionGeneration() || !samples_ ||
        deviceRecoveryRoot_!=samples_->property("originalRoot").toString())
        return tr("正在核对原任务在途状态，尚未恢复业务同步。");
    return deviceRecoveryUnfinished_ ? deviceRecoveryMessage_ : QString();
}

void frmMain::updateOriginalDeviceSync()
{
    attachDeviceTaskStore();
    if (reagentLevelBusy()) return;
    advanceOriginalProjectUpdates();
    if (!deviceControl_ || !deviceTaskStore_) return;
    const auto &state=deviceControl_->state();
    const quint64 connection=deviceControl_->connectionGeneration();
    const QString root=samples_ ? samples_->property("originalRoot").toString() : QString();
    if ((!state.connected || (deviceConfigurationStarted_ &&
        (connection!=deviceConfigurationConnection_ || root!=deviceConfigurationRoot_)) ||
        (deviceRecoveryConnection_ && (connection!=deviceRecoveryConnection_ || root!=deviceRecoveryRoot_))) &&
        (deviceConfigurationStarted_ || deviceRecoveryConnection_))
        invalidateDeviceTaskContext();
    if (deviceConfigurationStarted_ || !session_.authenticated || !session_.actionReady || !state.connected || !state.fresh ||
        !state.pending.isEmpty() || state.outcomeUnknown || state.status.mainStatus>2 || root.isEmpty() ||
        !samples_ || !projects_ || !reagents_ || !samples_->isReady() || !projects_->isReady() || !reagents_->isReady() ||
        samples_->isBusy() || projects_->isBusy() || reagents_->isBusy() || samples_->hasUnsavedChanges() ||
        projects_->hasUnsavedChanges() || reagents_->hasUnsavedChanges() || !samples_->originalProfile() ||
        !reagents_->snapshot().originalProfile || !projects_->property("originalProfile").toBool() ||
        root!=reagents_->property("originalRoot").toString() || root!=projects_->property("originalRoot").toString()) return;
    if (!deviceRecoveryChecked_ && !deviceRecoveryPending_)
    {
        // 同一worker中排在旧连接失效/取消准备之后读取，不从页面快照猜在途任务。
        deviceRecoveryConnection_=connection; deviceRecoveryRoot_=root; deviceRecoveryPending_=true;
        const quint64 request=++deviceRecoveryRequest_;
        if (!QMetaObject::invokeMethod(deviceTaskStore_,"queryDeviceRecovery",Qt::QueuedConnection,
            Q_ARG(quint64,request),Q_ARG(quint64,connection),Q_ARG(QString,root)))
        {
            deviceRecoveryPending_=false; deviceRecoveryChecked_=deviceRecoveryUnfinished_=true;
            deviceRecoveryMessage_=tr("原任务状态查询未能排入后台，未自动恢复业务同步。");
            reportDeviceTaskError(deviceRecoveryMessage_);
        }
        QTimer::singleShot(10000,this,[this,request,connection,root]()
        {
            if (!deviceRecoveryPending_ || request!=deviceRecoveryRequest_ || connection!=deviceRecoveryConnection_ ||
                root!=deviceRecoveryRoot_) return;
            deviceRecoveryPending_=false; deviceRecoveryChecked_=deviceRecoveryUnfinished_=true;
            deviceRecoveryMessage_=tr("原任务状态10秒未确认，未自动恢复业务同步。");
            reportDeviceTaskError(deviceRecoveryMessage_); updateDeviceControlActions();
        });
        updateDeviceControlActions();
        return;
    }
    if (!deviceRecoveryBlocker().isEmpty()) return;
    // 每连接只启动一次；超时/拒绝不会因下次Qj而自动重发参数。
    deviceConfigurationStarted_=true; deviceConfigurationReady_=false;
    deviceConfigurationBaseReady_=false;
    deviceConfigurationConnection_=connection; deviceConfigurationRoot_=root;
    // 先登记真实同步等待，再撤销N许可，覆盖begin尚在SampleStore队列中的空隙。
    updateReagentLevelContext(false);
    const bool linuxNew=state.status.generalFields.value(19)==QStringLiteral("LinuxNew");
    QMetaObject::invokeMethod(deviceTaskStore_,"beginOriginalDeviceSync",Qt::QueuedConnection,
        Q_ARG(quint64,connection),Q_ARG(bool,linuxNew));
    updateDeviceControlActions();
}

void frmMain::attachDeviceTaskStore()
{
    if (!deviceControl_ || !samples_ || !reagents_ || !projects_) return;
    attachReagentPriority();
    attachReagentLevels();
    if (deviceTaskStore_) return;
    auto *store=samples_->taskStore();
    if (!store) return;
    deviceTaskStore_=store;
    attachTaskReagents();
    const QPointer<SampleStore> admissionConsumer=store;
    auto *projectStore=projects_->taskStore();
    auto *reagentStore=reagents_->taskStore();
    if (projectStore)
    {
        connect(projectStore,&ProjectsStore::originalProjectSaveAdmissionRequested,this,
            [admissionConsumer](quint64 ticket,OriginalProjectSaveNotice proposal,qint64 sourceDeadline)
        {
            if (!admissionConsumer || proposal.source!=QStringLiteral("projectSave")) return;
            QMetaObject::invokeMethod(admissionConsumer.data(),"reserveOriginalProjectSave",Qt::QueuedConnection,
                Q_ARG(quint64,ticket),Q_ARG(OriginalProjectSaveNotice,proposal),Q_ARG(qint64,sourceDeadline));
        });
        connect(projectStore,&ProjectsStore::originalProjectSaveAdmissionCancelled,this,
            [admissionConsumer](quint64 ticket,const QString &source)
        {
            if (!admissionConsumer || source!=QStringLiteral("projectSave")) return;
            QMetaObject::invokeMethod(admissionConsumer.data(),"releaseOriginalProjectSave",Qt::QueuedConnection,
                Q_ARG(quint64,ticket),Q_ARG(QString,source));
        });
        connect(store,&SampleStore::originalProjectSaveAdmissionReady,
            projectStore,&ProjectsStore::finishOriginalProjectSaveAdmission,Qt::QueuedConnection);
        connect(projectStore,&ProjectsStore::originalProjectSaveCommitted,this,
            [this](const OriginalProjectSaveNotice &notice)
        { if (notice.source==QStringLiteral("projectSave")) acceptOriginalProjectSave(notice,0,true); });
        QMetaObject::invokeMethod(projectStore,"setOriginalProjectSaveAdmissionEnabled",Qt::QueuedConnection,Q_ARG(bool,true));
        connect(store,&QObject::destroyed,projectStore,[projectStore]()
        { projectStore->setOriginalProjectSaveAdmissionEnabled(false); },Qt::QueuedConnection);
    }
    if (reagentStore)
    {
        connect(reagentStore,&ReagentStore::originalProjectSaveAdmissionRequested,this,
            [admissionConsumer](quint64 ticket,OriginalProjectSaveNotice proposal,qint64 sourceDeadline)
        {
            if (!admissionConsumer || proposal.source!=QStringLiteral("reagentGroupEnabled")) return;
            QMetaObject::invokeMethod(admissionConsumer.data(),"reserveOriginalProjectSave",Qt::QueuedConnection,
                Q_ARG(quint64,ticket),Q_ARG(OriginalProjectSaveNotice,proposal),Q_ARG(qint64,sourceDeadline));
        });
        connect(reagentStore,&ReagentStore::originalProjectSaveAdmissionCancelled,this,
            [admissionConsumer](quint64 ticket,const QString &source)
        {
            if (!admissionConsumer || source!=QStringLiteral("reagentGroupEnabled")) return;
            QMetaObject::invokeMethod(admissionConsumer.data(),"releaseOriginalProjectSave",Qt::QueuedConnection,
                Q_ARG(quint64,ticket),Q_ARG(QString,source));
        });
        connect(store,&SampleStore::originalProjectSaveAdmissionReady,
            reagentStore,&ReagentStore::finishOriginalProjectSaveAdmission,Qt::QueuedConnection);
        connect(reagentStore,&ReagentStore::originalProjectSaveCommitted,this,
            [this](const OriginalProjectSaveNotice &notice)
        { if (notice.source==QStringLiteral("reagentGroupEnabled")) acceptOriginalProjectSave(notice,0,true); });
        QMetaObject::invokeMethod(reagentStore,"setOriginalProjectSaveAdmissionEnabled",Qt::QueuedConnection,Q_ARG(bool,true));
        connect(store,&QObject::destroyed,reagentStore,[reagentStore]()
        { reagentStore->setOriginalProjectSaveAdmissionEnabled(false); },Qt::QueuedConnection);
    }
    connect(store,&SampleStore::originalProjectSaveCommitted,this,
        [this](const OriginalProjectSaveNotice &notice,quint64 intent) { acceptOriginalProjectSave(notice,intent,intent==0); });
    connect(store,&SampleStore::originalProjectUpdateState,this,&frmMain::applyOriginalProjectUpdateState);
    connect(store,&SampleStore::originalProjectUpdatePumpFinished,this,[this](quint64 connection)
    {
        if (connection!=projectUpdatePumpConnection_) return;
        projectUpdatePumpQueued_=false;
    });
    connect(store,&SampleStore::originalProjectUpdateBatch,this,
        [this,store](const QList<QByteArray> &packets,quint64 connection,quint64 intent,qint64 expiresAt)
    {
        const auto identity=session_;
        const auto allowed=[this,connection,expiresAt,&identity]()
        {
            QElapsedTimer now;
            now.start();
            if (expiresAt<=0 || now.msecsSinceReference()>=expiresAt) return false;
            if (!deviceControl_ || connection!=deviceControl_->connectionGeneration() ||
                !deviceConfigurationBaseReady_ || connection!=deviceConfigurationConnection_ ||
                !session_.authenticated || !session_.actionReady || session_.name!=identity.name || session_.role!=identity.role ||
                session_.source!=identity.source || session_.originalPolicySource!=identity.originalPolicySource ||
                session_.grantedPowerIds!=identity.grantedPowerIds || !deviceRecoveryBlocker().isEmpty() ||
                deviceTaskPreparing_ || reagentPriorityRequest_ || !deviceReagentUpdateHole_.isEmpty() ||
                deviceReagentUpdateUnknown_ || deviceReagentRecoveryRequired_) return false;
            const auto &state=deviceControl_->state();
            return state.connected && state.fresh && state.pending.isEmpty() && !state.outcomeUnknown && state.status.mainStatus<=2 &&
                state.status.generalFields.value(19)==QStringLiteral("LinuxNew");
        };
        const auto ownsIntent=[this,connection,intent]()
        {
            for (const auto &state : projectUpdateNotices_)
                if (state.intent==intent && state.connection==connection && state.stage==QStringLiteral("prepared")) return true;
            return false;
        };
        bool attempted=false,queued=false;
        QString message=tr("本地提交保留，当前守卫拒绝发送；没有自动重试。");
        if (packets.size()==2 && packets[0].startsWith("Up[") && packets[1].startsWith("UP[") && allowed() && ownsIntent())
        {
            attempted=true; // 入队日志允许同步断线；调用前先记可能发送。
            const bool updateQueued=deviceControl_->sendOriginalConfiguration(packets[0]);
            queued=updateQueued && allowed() && ownsIntent() && deviceControl_->sendOriginalConfiguration(packets[1]) && allowed();
            message=queued ? tr("Up与对应UP查询已进入当前连接队列，等待完整匹配回读。")
                : tr("Up可能已经入队，后续发送或连接未确认，结果待核；没有自动重发。");
        }
        QMetaObject::invokeMethod(store,"originalProjectUpdateSent",Qt::QueuedConnection,
            Q_ARG(quint64,intent),Q_ARG(quint64,connection),Q_ARG(bool,attempted),Q_ARG(bool,queued),Q_ARG(QString,message));
    });
    connect(store,&SampleStore::deviceRecoveryState,this,
        [this](quint64 request,quint64 connection,const QString &root,bool known,bool unfinished,const QString &message)
    {
        if (!deviceRecoveryPending_ || request!=deviceRecoveryRequest_ || connection!=deviceRecoveryConnection_ ||
            !deviceControl_ || connection!=deviceControl_->connectionGeneration() || root!=deviceRecoveryRoot_ ||
            !samples_ || root!=samples_->property("originalRoot").toString()) return;
        deviceRecoveryPending_=false; deviceRecoveryChecked_=true; deviceRecoveryUnfinished_=!known || unfinished;
        deviceRecoveryMessage_=message;
        setProperty("deviceRecoveryMessage",deviceRecoveryBlocker());
        if (deviceRecoveryUnfinished_) reportDeviceTaskError(message);
        updateOriginalDeviceSync(); updateDeviceControlActions();
    });
    connect(samples_,&SamplePage::snapshotChanged,this,[this](const SampleSnapshot &)
    { updateOriginalDeviceSync(); updateDeviceControlActions(); });
    connect(reagents_,&ReagentsPage::readyChanged,this,[this](bool)
    { updateOriginalDeviceSync(); updateDeviceControlActions(); });
    connect(projects_,&ProjectPage::snapshotChanged,this,[this](const ProjectRecords &,const ProjectUnits &)
    { updateOriginalDeviceSync(); updateDeviceControlActions(); });
    connect(samples_,&SamplePage::busyChanged,this,[this](bool){ updateDeviceControlActions(); });
    connect(projects_,&ProjectPage::busyChanged,this,[this](bool){ updateDeviceControlActions(); });
    connect(store,&SampleStore::deviceTasksPrepared,this,[this,store](quint64 request,bool success,const QString &detail)
    {
        if (!deviceTaskPreparing_ || request!=deviceTaskRequest_) return;
        deviceTaskPreparing_=false;
        const QString blocker=deviceTaskConnection_!=deviceControl_->connectionGeneration()
            ? tr("准备任务期间连接已改变，未向新连接发送开始。") : detectionStartBlocker();
        if (!success || !blocker.isEmpty() || !deviceControl_->start())
        {
            QMetaObject::invokeMethod(store,"cancelDeviceTaskPreparation",Qt::QueuedConnection);
            reportDeviceTaskError(!success ? detail : !blocker.isEmpty() ? blocker : deviceControl_->state().message);
        }
        updateDeviceControlActions();
    });
    connect(deviceControl_,&DeviceControl::startAccepted,store,[store](bool accepted)
    { if (!accepted) store->cancelDeviceTaskPreparation(); },Qt::QueuedConnection);
    connect(store,&SampleStore::originalConfigurationState,this,[this](quint64 connection,bool ready,const QString &message)
    {
        if (connection!=deviceConfigurationConnection_ || connection!=deviceControl_->connectionGeneration()) return;
        deviceConfigurationBaseReady_=ready;
        if (ready && projectUpdateNotices_.isEmpty()) projectUpdateReady_=true;
        updateOriginalProjectReadiness();
        // 同步完成只撤/核旧许可；新的N许可仍等下一条实际Qj签发。
        updateReagentLevelContext(false);
        setProperty("originalConfigurationMessage",message);
        ui->lblDivState->setToolTip(message);
        updateDeviceControlActions();
        advanceOriginalProjectUpdates();
    });
    connect(store,&SampleStore::originalConfigurationPacket,this,[this,store](const QByteArray &payload,quint64 connection)
    {
        if (connection!=deviceControl_->connectionGeneration()) return;
        if (!deviceRecoveryBlocker().isEmpty()) return;
        if (payload.startsWith("Us[") && (reagentPriorityRequest_ || deviceReagentUpdateUnknown_ ||
            (connection && reagentPriorityUnknownConnection_==connection)))
        {
            // 旧孔位准备结果可能晚于新请求到达；不能只在保存通知处检查一次。
            const bool unsafe=reagentPriorityMayHaveQueued_ || deviceReagentUpdateUnknown_ ||
                (connection && reagentPriorityUnknownConnection_==connection);
            if (reagentPriorityRequest_)
                finishReagentPriority(false,tr("收到另一孔位的延迟Us准备结果，优先使用已停止等待。"),unsafe);
            deviceReagentUpdateUnknown_=true;
            reportDeviceTaskError(tr("原孔位已在本地保存，但设备同步未确认；为避免两条Us并发，未发送延迟命令，请核对后重新连接。"));
            updateDeviceControlActions();
            return;
        }
        // 原初始同步确实发送纯Ur；先登记实际发送等待，不能被迟到ready布尔值放行N。
        const bool reagentQuery=payload==QByteArrayLiteral("Ur");
        if (reagentQuery)
        {
            reagentLevelRequestedUrConnection_=connection;
            updateReagentLevelContext(false);
        }
        if (!deviceControl_->sendOriginalConfiguration(payload))
        {
            if (reagentQuery && reagentLevelRequestedUrConnection_==connection)
                reagentLevelRequestedUrConnection_=0;
            deviceConfigurationReady_=false;
            QMetaObject::invokeMethod(store,"invalidateOriginalDeviceSync",Qt::QueuedConnection);
            reportDeviceTaskError(tr("原配置未能进入当前连接发送队列；请核对收发记录后重新连接。"));
            updateDeviceControlActions();
        }
    });
    connect(reagents_->taskStore(),&ReagentStore::originalHoleSaved,this,[this,store](const QString &hole)
    {
        const quint64 connection=deviceControl_->connectionGeneration();
        const bool unsafe=reagentPriorityMayHaveQueued_ || deviceReagentUpdateUnknown_ || deviceReagentRecoveryRequired_ ||
            (connection && reagentPriorityUnknownConnection_==connection) ||
            (reagentPriorityRequest_ && reagentPriorityStage_!=ReagentPriorityStage::Preparing);
        if (reagentPriorityRequest_)
            finishReagentPriority(false,tr("收到另一孔位已保存通知，优先使用已停止等待。"),unsafe);
        if (unsafe)
        {
            deviceReagentUpdateUnknown_=true;
            reportDeviceTaskError(tr("孔位资料已在本地保存，但上一条试剂更新结果尚未确认；未并发发送，请核对后重新连接。"));
            updateDeviceControlActions();
            return;
        }
        const auto &state=deviceControl_->state();
        if (!state.connected || state.status.generalFields.value(19)!=QStringLiteral("LinuxNew")) return;
        if (!state.fresh || state.outcomeUnknown || !state.pending.isEmpty() || state.status.mainStatus>2 ||
            !deviceConfigurationReady_ || deviceConfigurationConnection_!=deviceControl_->connectionGeneration())
        {
            deviceReagentUpdateUnknown_=true;
            reportDeviceTaskError(tr("孔位已保存到原库，但当前设备状态不允许同步，请核对后重新连接。"));
            updateDeviceControlActions(); return;
        }
        beginOriginalReagentWait(hole,tr("原库已保存，等待该孔位设备回读。"));
        QMetaObject::invokeMethod(store,"prepareOriginalReagentUpdate",Qt::QueuedConnection,
            Q_ARG(QString,hole),Q_ARG(bool,true),Q_ARG(quint64,deviceConfigurationConnection_));
        updateDeviceControlActions();
    });
    connect(deviceControl_,&DeviceControl::payloadReceivedForConnection,this,[this,store](const QByteArray &payload,quint64 connection)
    {
        if (connection!=deviceControl_->connectionGeneration()) return;
        // 优先使用的Ur由同一入口唯一分流；不得先让旧SampleStore路径提交再核归属。
        if (payload.startsWith("Ur") && routeReagentPriorityReply(payload,connection)) return;
        const QByteArray command=payload.left(2);
        if (command=="Ur" && reagentLevelBusy())
        {
            reportReagentLevel(tr("N正在保存或等待页面读回，未并发保存Ur；请核对原收发和资料后恢复。"));
            return;
        }
        if (command!="Tt" && command!="TD" && command!="Tr" && command!="TR" && command!="TB" &&
            payload!="TF" && payload!="TK" && command!="Ur" && command!="UP" && payload!="Tu") return;
        if (devicePackets_.size()+taskReagentRetainedPackets()>=32 ||
            devicePacketBytes_+taskReagentRetainedBytes()+payload.size()>1048576)
        {
            reportDeviceTaskError(tr("设备业务队列超过32包或1MiB，本包未保存，连接已关闭，请核对后恢复。"));
            deviceControl_->close(); return;
        }
        const quint64 packet=++devicePacket_;
        if (command=="Ur") reagentLevelUrContexts_.insert(packet,reagentLevelUrContext(connection));
        devicePackets_.insert(packet,payload); devicePacketBytes_+=payload.size();
        if (routeTaskReagentPacket(packet,connection,payload)) return;
        QMetaObject::invokeMethod(store,"receiveDevicePacket",Qt::QueuedConnection,Q_ARG(QByteArray,payload),
            Q_ARG(quint64,connection),Q_ARG(quint64,packet));
    });
    connect(store,&SampleStore::devicePacketProcessed,this,
        [this](quint64 packet,quint64 connection,const QByteArray &command,bool success,const QString &message)
    {
        const QByteArray payload=devicePackets_.take(packet); devicePacketBytes_-=payload.size();
        finishTaskReagentPacket(packet,connection,command,success,message);
        if (command=="Ur" && !payload.isEmpty() && reagentLevelRequestedUrConnection_==connection)
        {
            // 成败均结束这次已处理回包的等待；超时没有processed时保留到明确重连。
            reagentLevelRequestedUrConnection_=0;
            updateReagentLevelContext(false);
        }
        if (connection!=deviceControl_->connectionGeneration()) return;
        if (!success) { reportDeviceTaskError(message); return; }
        if (command=="Ur" && !deviceReagentUpdateHole_.isEmpty() && reagentReplyContains(payload,deviceReagentUpdateHole_))
        {
            deviceReagentUpdateHole_.clear(); deviceReagentUpdateUnknown_=false; ++deviceReagentUpdateRequest_;
            setProperty("originalReagentUpdateMessage",tr("已收到该孔位回读并保存，页面以设备实际返回为准。"));
            updateDeviceControlActions();
        }
    });
    connect(store,&SampleStore::deviceRackTasksPrepared,this,[this](const QByteArray &payload,quint64 connection)
    {
        if (connection!=deviceControl_->connectionGeneration() || !deviceRecoveryBlocker().isEmpty() ||
            !deviceControl_->sendTaskReply(payload))
            reportDeviceTaskError(payload=="TF" || payload=="TF[Emergency]"
                ? tr("预设架回应未能发送，未下发新任务；请核对连接和收发记录。")
                : tr("任务编号已保存，但未能下发；原记录保留，请核对连接和收发记录，勿重复开始。"));
    });
    connect(store,&SampleStore::deviceTaskError,this,&frmMain::reportDeviceTaskError);
    connect(store,&SampleStore::originalReagentsChanged,reagents_,&ReagentsPage::reloadOriginal);
    connect(store,&SampleStore::originalProjectsChanged,this,[this]()
    { if (projects_ && !projects_->isBusy() && !projects_->hasUnsavedChanges()) projects_->reload(); });
    connect(store,&SampleStore::originalResultsChanged,this,[this]()
    { if (history_ && !history_->isBusy() && !history_->hasUnsavedChanges()) history_->refresh(); });
}

void frmMain::beginOriginalReagentWait(const QString &hole,const QString &message)
{
    deviceReagentUpdateHole_=hole;
    const quint64 request=++deviceReagentUpdateRequest_;
    setProperty("originalReagentUpdateMessage",message);
    QTimer::singleShot(10000,this,[this,request]()
    {
        if (request!=deviceReagentUpdateRequest_ || deviceReagentUpdateHole_.isEmpty()) return;
        if (reagentPriorityRequest_)
        {
            finishReagentPriority(false,tr("优先使用10秒内未完成对应Ur保存和页面读回，未重发；请核对后重新连接。"),
                                  reagentPriorityMayHaveQueued_);
            return;
        }
        deviceReagentUpdateUnknown_=true;
        reportDeviceTaskError(tr("单孔更新10秒未收到对应孔位回读；原库已保存，设备执行情况待核对，未自动重发。"));
        updateDeviceControlActions();
    });
}

QString frmMain::reagentPriorityBlocker(bool ownPageBusy) const
{
    if (reagentLevelBusy()) return tr("N正在保存或等待页面读回，未并发发送优先使用。");
    if (!deviceControl_ || !reagents_ || !reagents_->taskStore() || !deviceTaskStore_)
        return tr("试剂页或原资料后台尚未接通，未发送优先使用。");
    const auto &state=deviceControl_->state();
    const quint64 connection=deviceControl_->connectionGeneration();
    if (!session_.authenticated || session_.role<0 || session_.role>5)
        return tr("请先使用原账户登录，未发送优先使用。");
    if (!reagentPriorityRequest_ && !projectUpdateNotices_.isEmpty())
        return tr("本地项目参数仍待设备确认，尚未开始另一条试剂更新。");
    const QString recovery=deviceRecoveryBlocker();
    if (!recovery.isEmpty()) return recovery;
    if (!state.connected || !state.fresh || !deviceActionReadyObserved_ || !deviceActionReady_ || !session_.actionReady ||
        state.status.ledStatus!=1 || (state.status.mainStatus!=1 && state.status.mainStatus!=2))
        return tr("设备未连接、状态已失效或尚未处于原空闲灯配育温/就绪状态，未发送优先使用。");
    if (!state.pending.isEmpty() || state.outcomeUnknown || deviceTaskPreparing_)
        return tr("设备还有控制动作或结果待核对，暂不能更新优先级。");
    if (state.status.generalFields.value(19)!=QStringLiteral("LinuxNew"))
        return tr("本片只接原LinuxNew的单孔Us/Ur，当前协议未开放此入口。");
    const int positions=property("configuredReagentPositions").toInt();
    if (positions!=20 && positions!=42) return tr("尚未读取已确认的20/42位试剂配置，未开放优先使用。");
    const QString positionsReason=reagentPositionBlocker();
    if (!positionsReason.isEmpty()) return positionsReason+tr("未开放优先使用。");
    const QString configuredModel=property("configuredDeviceModel").toString();
    if (configuredModel.isEmpty() || configuredModel!=state.status.model)
        return tr("原配置机型与当前主控机型未核对一致，未开放优先使用。");
    if ((reagentPriorityUnknownConnection_ && reagentPriorityUnknownConnection_==connection) || deviceReagentUpdateUnknown_)
        return tr("上一条试剂更新结果不明；请先核对原INI、设备和资料，再重新连接，未自动重试。");
    if (reagentPriorityReplyDigests_.size()>=4096)
        return tr("本连接的优先级回读归属记录已满，已保留旧记录；请核对后重新连接，未遗忘迟到包继续发送。");
    if ((!deviceReagentUpdateHole_.isEmpty() && !reagentPriorityRequest_) ||
        !deviceConfigurationBaseReady_ || deviceConfigurationConnection_!=connection)
        return tr("原项目/试剂同步或另一孔位更新尚未确认，请等待回读。");
    const QString root=reagents_->property("originalRoot").toString();
    if (!reagents_->isReady() || !reagents_->snapshot().originalProfile || root.isEmpty() || root!=deviceConfigurationRoot_)
        return tr("试剂页没有使用当前连接已核对的原资料，未更新优先级。");
    if (reagentPriorityRequest_)
    {
        if (!reagentPriorityClock_.isValid() || reagentPriorityClock_.elapsed()>=10000)
            return tr("优先使用等待已经超过10秒，迟到回复不再提交或报告成功。");
        const auto &identity=reagentPriorityIdentity_;
        if (reagentPriorityConnection_!=connection || identity.name!=session_.name || identity.role!=session_.role ||
            identity.source!=session_.source || identity.originalPolicySource!=session_.originalPolicySource ||
            identity.grantedPowerIds!=session_.grantedPowerIds)
            return tr("优先使用等待期间身份或连接已变化，原请求不再接收回读。");
    }
    if (reagents_->hasUnsavedChanges() || (!ownPageBusy && reagents_->isBusy()))
        return tr("试剂资料正在处理或仍有未保存输入，请完成后再优先使用。");
    if (!reagentPriorityRequest_ && (!devicePackets_.isEmpty() || (samples_ && samples_->isBusy()) ||
        (projects_ && (projects_->isBusy() || projects_->hasUnsavedChanges()))))
        return tr("业务资料正在处理或仍有未保存输入，请完成后再优先使用。");
    return QString();
}

void frmMain::updateReagentPriorityAccess()
{
    if (!reagentPriorityAttached_ || !deviceControl_ || !reagents_) return;
    const quint64 connection=deviceControl_->connectionGeneration();
    if (reagentPriorityHistoryConnection_!=connection)
    {
        reagentPriorityReplyDigests_.clear(); reagentPriorityHistoryConnection_=connection;
    }
    if (reagentPriorityUnknownConnection_ && reagentPriorityUnknownConnection_!=connection)
    {
        deviceReagentRecoveryRequired_=true;
        rememberReagentRecoveryContext();
        reagentPriorityUnknownConnection_=0;
    }
    const QString reason=reagentPriorityBlocker(reagentPriorityRequest_!=0);
    reagents_->setProperty("originalPriorityBlocker",reason);
    if (auto *button=reagents_->findChild<QPushButton*>(QStringLiteral("btnUpdatePriority"))) button->setToolTip(reason);
    if (reagentPriorityRequest_ && !reason.isEmpty())
    {
        finishReagentPriority(false,reason,reagentPriorityMayHaveQueued_);
        return;
    }
    const auto &state=deviceControl_->state();
    reagents_->setDeviceOperations(state.connected ? connection : 0,
        reason.isEmpty() ? QStringList{QStringLiteral("prioritizeHole")} : QStringList());
}

void frmMain::beginReagentPriority(quint64 request,quint64 connection,const QString &operation,const QString &hole)
{
    if (!reagents_ || !deviceControl_) return;
    QString reason=operation!=QStringLiteral("prioritizeHole") ? tr("本片仅接优先使用，没有发送其他动作。") :
        reagentPriorityRequest_ ? tr("已有优先使用请求正在等待，未重复发送。") :
        connection!=deviceControl_->connectionGeneration() ? tr("确认期间连接已改变，未向新连接发送旧请求。") :
        reagentPriorityBlocker(true);
    const int positions=property("configuredReagentPositions").toInt();
    const QRegularExpression names(positions==42 ? QStringLiteral("^([A-F]_[1-7]|SD_[1-4])$") :
        QStringLiteral("^(R_([1-9]|1[0-9]|20)|SD_[1-4])$"));
    if (!names.match(hole).hasMatch()) reason=tr("选中孔位不属于当前原面板，未猜测目标。");
    if (!request || !reason.isEmpty())
    {
        reagents_->finishDeviceOperation(request,connection,false,reason);
        return;
    }
    reagentPriorityRequest_=request; reagentPriorityConnection_=connection; reagentPriorityIdentity_=session_;
    reagentPriorityClock_.start();
    reagentPriorityStage_=ReagentPriorityStage::Preparing; reagentPriorityMayHaveQueued_=false;
    reagentPrioritySent_.clear(); reagentPriorityReply_.clear(); reagentPrioritySavedFingerprint_.clear();
    beginOriginalReagentWait(hole,tr("正在按原资料准备优先使用；尚未发送，原库优先级未先改写。"));
    qInfo().noquote()<<"REAGENT_PRIORITY_REQUEST"<<request<<connection<<hole;
    // 本页busy属于这个请求，不能当作外部占用反过来撤销它的一次性许可。
    updateDeviceControlActions();
    if (!QMetaObject::invokeMethod(reagents_->taskStore(),"prepareOriginalDeviceOperation",Qt::QueuedConnection,
        Q_ARG(quint64,request),Q_ARG(quint64,connection),Q_ARG(QString,operation),Q_ARG(QString,hole),Q_ARG(bool,true)))
        finishReagentPriority(false,tr("优先使用准备未能排入数据库线程，未发送。"),false);
}

void frmMain::finishReagentPriority(bool success,const QString &message,bool resultUnknown)
{
    if (!reagentPriorityRequest_) return;
    const quint64 request=reagentPriorityRequest_, connection=reagentPriorityConnection_;
    // 直接撤销线程安全许可，不能等GUI后续重算或后台队列才停止在途保存。
    if (reagents_ && reagents_->taskStore())
        reagents_->taskStore()->setOriginalDeviceOperationContext(0,QStringList());
    if (resultUnknown)
    {
        reagentPriorityUnknownConnection_=connection;
        rememberReagentRecoveryContext();
    }
    if (success && !reagentPriorityReply_.isEmpty())
        reagentPriorityReplyDigests_.insert(QCryptographicHash::hash(reagentPriorityReply_,QCryptographicHash::Sha256));
    reagentPriorityRequest_=0; reagentPriorityConnection_=0; reagentPriorityStage_=ReagentPriorityStage::None;
    reagentPriorityMayHaveQueued_=false; reagentPrioritySent_.clear(); reagentPriorityReply_.clear();
    reagentLevelPriorityUrContext_.reset();
    reagentPrioritySavedFingerprint_.clear(); ++deviceReagentUpdateRequest_;
    deviceReagentUpdateHole_.clear(); deviceReagentUpdateUnknown_=resultUnknown;
    setProperty("originalReagentUpdateMessage",message);
    qInfo().noquote()<<"REAGENT_PRIORITY_FINISHED"<<request<<connection<<success<<"unknown="<<resultUnknown<<message;
    if (reagents_) reagents_->finishDeviceOperation(request,connection,success,message);
    updateDeviceControlActions();
}

bool frmMain::routeReagentPriorityReply(const QByteArray &payload,quint64 connection)
{
    if (!deviceControl_ || !payload.startsWith("Ur")) return false;
    if (connection!=deviceControl_->connectionGeneration()) return true;
    const auto digest=QCryptographicHash::hash(payload,QCryptographicHash::Sha256);
    if (reagentPriorityHistoryConnection_==connection && reagentPriorityReplyDigests_.contains(digest))
    {
        qInfo().noquote()<<"REAGENT_PRIORITY_DUPLICATE_NOT_SAVED"<<connection;
        return true; // 同字节新旧回读无法区分，不当作新确认；也不再落入通用保存链。
    }
    if (!reagentPriorityRequest_)
    {
        if (reagentPriorityUnknownConnection_==connection && connection)
        {
            qWarning().noquote()<<"REAGENT_PRIORITY_LATE_REPLY_NOT_SAVED"<<connection;
            return true;
        }
        return false;
    }
    if (!reagentPriorityClock_.isValid() || reagentPriorityClock_.elapsed()>=10000)
    {
        finishReagentPriority(false,tr("对应Ur到达时已超过10秒，未提交迟到回读或自动重发。"),reagentPriorityMayHaveQueued_);
        return true;
    }
    if (connection!=reagentPriorityConnection_ || reagentPriorityStage_!=ReagentPriorityStage::WaitingReply)
    {
        qWarning().noquote()<<"REAGENT_PRIORITY_REPLY_OUT_OF_STAGE"<<reagentPriorityRequest_<<connection;
        return true;
    }
    OriginalReagentRepository verifier;
    QString reason;
    if (!verifier.priorityReplyMatches(payload,reagentPrioritySent_,&reason))
    {
        setProperty("originalReagentUpdateReplyIssue",reason);
        qWarning().noquote()<<"REAGENT_PRIORITY_REPLY_NOT_SAVED"<<reagentPriorityRequest_<<reason;
        return true; // 无关、满盘或坏包不续时、不改库，也不伪报成功。
    }
    reagentPriorityReply_=payload; reagentPriorityStage_=ReagentPriorityStage::SavingReply;
    reagentLevelPriorityUrContext_=reagentLevelUrContext(connection);
    if (!QMetaObject::invokeMethod(reagents_->taskStore(),"saveOriginalPriorityReply",Qt::QueuedConnection,
        Q_ARG(quint64,reagentPriorityRequest_),Q_ARG(quint64,connection),Q_ARG(QByteArray,payload)))
        finishReagentPriority(false,tr("对应Ur未能排入数据库线程，设备优先级和本地资料仍待核对。"),true);
    return true;
}

void frmMain::finishReagentPriorityIfDisplayed()
{
    if (!reagentPriorityRequest_ || reagentPriorityStage_!=ReagentPriorityStage::WaitingPage || !reagents_) return;
    if (!reagentPriorityClock_.isValid() || reagentPriorityClock_.elapsed()>=10000)
    {
        finishReagentPriority(false,tr("Ur已保存，但10秒内未完成页面同版本核对；结果待核对，未自动重发。"),true);
        return;
    }
    const auto snapshot=reagents_->snapshot();
    if (!snapshot.originalProfile || snapshot.originalRoot!=deviceConfigurationRoot_) return;
    for (const auto &hole : snapshot.holes)
        if (hole.name==deviceReagentUpdateHole_ && hole.priority==1 &&
            !reagentPrioritySavedFingerprint_.isEmpty() && hole.originalFingerprint==reagentPrioritySavedFingerprint_)
        {
            finishReagentPriority(true,tr("对应单孔Ur已核对并保存，试剂页已读回同一版本，当前瓶优先级为1。"),false);
            return;
        }
}

void frmMain::attachReagentPriority()
{
    if (reagentPriorityAttached_ || !reagents_ || !reagents_->taskStore() || !deviceControl_) return;
    reagentPriorityAttached_=true;
    connect(reagents_,&ReagentsPage::deviceOperationRequested,this,&frmMain::beginReagentPriority);
    connect(reagents_,&ReagentsPage::snapshotChanged,this,[this](const ReagentSnapshot &)
    { finishReagentPriorityIfDisplayed(); });
    connect(reagents_,&ReagentsPage::busyChanged,this,[this](bool)
    {
        // 页面先busy、后发请求；延到本轮GUI末尾再重算，避免尚未登记owner时清空许可。
        QTimer::singleShot(0,this,[this](){ updateDeviceControlActions(); });
    });
    connect(reagents_,&ReagentsPage::operationFinished,this,[this](const QString &operation,bool success)
    {
        if (!success && operation==QStringLiteral("prioritizeHole") && reagentPriorityRequest_)
            finishReagentPriority(false,tr("试剂页已停止等待优先使用，未自动重发。"),reagentPriorityMayHaveQueued_);
    });
    auto *store=reagents_->taskStore();
    connect(store,&ReagentStore::originalDevicePreparationFailed,this,
        [this](quint64 request,quint64 connection,const QString &operation,const QString &message)
    {
        if (request==reagentPriorityRequest_ && connection==reagentPriorityConnection_ && operation==QStringLiteral("prioritizeHole"))
            finishReagentPriority(false,message,reagentPriorityMayHaveQueued_);
    });
    connect(store,&ReagentStore::originalDeviceOperationPrepared,this,
        [this](quint64 request,quint64 connection,const QString &operation,const QList<QByteArray> &packets,bool localSaved)
    {
        if (request!=reagentPriorityRequest_ || connection!=reagentPriorityConnection_ || operation!=QStringLiteral("prioritizeHole") ||
            reagentPriorityStage_!=ReagentPriorityStage::Preparing) return;
        const QString blocker=reagentPriorityBlocker(true);
        if (!blocker.isEmpty() || localSaved || packets.size()!=1 ||
            !packets.value(0).startsWith("Us["+deviceReagentUpdateHole_.toLatin1()+']'))
        {
            finishReagentPriority(false,!blocker.isEmpty() ? blocker :
                localSaved ? tr("后台意外改变了本地资料，未发送；请核对原库。") : tr("后台未返回本孔唯一Us命令，未发送。"),false);
            return;
        }
        reagentPrioritySent_=packets.first(); reagentPriorityStage_=ReagentPriorityStage::Queuing;
        reagentPriorityMayHaveQueued_=true;
        const bool queued=deviceControl_->sendOriginalConfiguration(reagentPrioritySent_);
        if (request!=reagentPriorityRequest_) return; // 入队期间同步报错/断线已撤销owner。
        if (!queued)
        {
            finishReagentPriority(false,tr("Us未能确认进入发送队列，执行结果待核对，未重发。"),true);
            return;
        }
        reagentPriorityStage_=ReagentPriorityStage::WaitingReply;
        reagents_->deviceOperationProgress(request,connection,tr("Us已进入当前连接发送队列，等待对应单孔Ur；尚未报设备执行成功。"));
        qInfo().noquote()<<"REAGENT_PRIORITY_QUEUED"<<request<<connection<<reagentPrioritySent_.toHex(' ');
    });
    connect(store,&ReagentStore::originalPriorityReplyProcessed,this,
        [this](quint64 request,quint64 connection,bool committed,bool loaded,const QString &fingerprint,const QString &message)
    {
        if (request!=reagentPriorityRequest_ || connection!=reagentPriorityConnection_)
        {
            if (committed) qWarning().noquote()<<"REAGENT_PRIORITY_LATE_COMMIT"<<request<<connection<<message;
            return;
        }
        if (!committed || !loaded || fingerprint.isEmpty())
        {
            finishReagentPriority(false,message,true);
            return;
        }
        confirmReagentLevelUr(reagentPriorityReply_,connection,reagentLevelPriorityUrContext_);
        reagentLevelPriorityUrContext_.reset();
        reagentPrioritySavedFingerprint_=fingerprint; reagentPriorityStage_=ReagentPriorityStage::WaitingPage;
        reagents_->deviceOperationProgress(request,connection,tr("对应Ur已保存，正在核对试剂页是否读到同一资料版本。"));
        finishReagentPriorityIfDisplayed();
    });
    updateReagentPriorityAccess();
}

void frmMain::acceptOriginalProjectSave(const OriginalProjectSaveNotice &notice,quint64 intent,bool enqueue)
{
    if (!notice.commitSeen) return;
    const auto latest=projectUpdateNotices_.constFind(projectUpdateKey(notice));
    if (intent && latest!=projectUpdateNotices_.cend() &&
        (latest->notice.source!=notice.source || latest->notice.writeRequest!=notice.writeRequest ||
         latest->notice.beforeFingerprint!=notice.beforeFingerprint || latest->notice.committedFingerprint!=notice.committedFingerprint))
        return; // 后台给旧保存编号的回执，不能覆盖GUI已接到的新保存。
    OriginalProjectUpdateState state;
    state.notice=notice; state.intent=intent;
    state.connection=deviceControl_ ? deviceControl_->connectionGeneration() : 0;
    state.stage=notice.verifiedSnapshot ? QStringLiteral("queued") : QStringLiteral("unverified");
    state.message=notice.verifiedSnapshot ? tr("本地项目已保存，设备参数尚未确认。")
        : tr("本地项目已提交，但提交后版本尚未核实，未发送参数。");
    projectUpdateNotices_.insert(projectUpdateKey(notice),state);
    projectUpdateReady_=false;
    updateOriginalProjectReadiness();
    qInfo().noquote()<<"PROJECT_SAVE_COMMITTED"<<notice.source<<notice.writeRequest<<notice.root
        <<notice.originalFileName<<notice.projectId<<notice.beforeFingerprint<<notice.committedFingerprint<<notice.verifiedSnapshot;
    if (enqueue && deviceTaskStore_)
        QMetaObject::invokeMethod(deviceTaskStore_,"enqueueOriginalProjectUpdate",Qt::QueuedConnection,
            Q_ARG(OriginalProjectSaveNotice,notice));
    updateDeviceControlActions();
    advanceOriginalProjectUpdates();
}

QString frmMain::originalProjectUpdateBlocker() const
{
    const QVariant references=property("originalProjectReferencesNeedReview");
    if (references.isValid()) return references.value<OriginalProjectUpdateState>().message;
    const QVariant unknown=property("originalProjectUpdateUnknown");
    if (unknown.isValid()) return unknown.value<OriginalProjectUpdateState>().message;
    if (projectUpdateNotices_.isEmpty()) return QString();
    const auto state=projectUpdateNotices_.cbegin().value();
    return tr("项目%1本地已提交，设备参数尚未确认：%2").arg(state.notice.projectName,state.message);
}

void frmMain::updateOriginalProjectReadiness()
{
    deviceConfigurationReady_=deviceConfigurationBaseReady_ && projectUpdateReady_ && projectUpdateNotices_.isEmpty() &&
        !property("originalProjectUpdateUnknown").isValid();
    setProperty("originalConfigurationReady",deviceConfigurationReady_);
    setProperty("originalProjectUpdateMessage",originalProjectUpdateBlocker());
}

void frmMain::applyOriginalProjectUpdateState(const OriginalProjectUpdateState &state)
{
    qInfo().noquote()<<"PROJECT_UPDATE_STATE"<<state.intent<<state.connection<<state.notice.root
        <<state.notice.originalFileName<<state.stage<<state.message;
    const bool terminal=state.stage==QStringLiteral("confirmed") || state.stage==QStringLiteral("confirmed-initial") ||
        state.stage==QStringLiteral("reconciled-initial") || state.stage==QStringLiteral("legacy-local") ||
        state.stage==QStringLiteral("initial-file-confirmed") || state.stage==QStringLiteral("identity-changed");
    if (terminal && (!deviceControl_ || state.connection!=deviceControl_->connectionGeneration())) return;
    if ((state.stage==QStringLiteral("identity-changed") || state.stage==QStringLiteral("identity-review")) &&
        !property("originalProjectReferencesNeedReview").isValid())
        setProperty("originalProjectReferencesNeedReview",QVariant::fromValue(state));
    const QVariant unknown=property("originalProjectUpdateUnknown");
    if (state.stage==QStringLiteral("not-attempted-proof"))
    {
        if (unknown.isValid())
        {
            const auto old=unknown.value<OriginalProjectUpdateState>();
            if (old.intent==state.intent && old.connection==state.connection &&
                projectUpdateKey(old.notice)==projectUpdateKey(state.notice) &&
                old.notice.source==state.notice.source && old.notice.writeRequest==state.notice.writeRequest &&
                old.notice.beforeFingerprint==state.notice.beforeFingerprint &&
                old.notice.committedFingerprint==state.notice.committedFingerprint)
            {
                setProperty("originalProjectUpdateUnknown",QVariant());
                updateOriginalProjectReadiness(); updateDeviceControlActions(); advanceOriginalProjectUpdates();
            }
        }
        return; // 只是结清同批未知来由，不确认参数，也不替换新保存的等待状态。
    }
    if (state.stage==QStringLiteral("unknown") && !unknown.isValid())
        setProperty("originalProjectUpdateUnknown",QVariant::fromValue(state));
    if (terminal && unknown.isValid())
    {
        const auto old=unknown.value<OriginalProjectUpdateState>();
        if (old.intent==state.intent && projectUpdateKey(old.notice)==projectUpdateKey(state.notice))
        {
            setProperty("originalProjectUpdateUnknown",QVariant());
            projectUpdateReady_=state.ready;
            updateOriginalProjectReadiness(); updateDeviceControlActions();
        }
    }
    const QString key=projectUpdateKey(state.notice);
    auto latest=projectUpdateNotices_.find(key);
    if (latest==projectUpdateNotices_.end()) return;
    const bool initialProof=state.stage==QStringLiteral("initial-file-confirmed") &&
        (latest->notice.committedFingerprint==state.notice.committedFingerprint ||
         (!latest->notice.verifiedSnapshot && latest->connection!=state.connection));
    const bool sameSave=latest->notice.source==state.notice.source && latest->notice.writeRequest==state.notice.writeRequest &&
        latest->notice.beforeFingerprint==state.notice.beforeFingerprint &&
        latest->notice.committedFingerprint==state.notice.committedFingerprint && (!latest->intent || latest->intent==state.intent);
    if (!initialProof && !sameSave) return;
    if (terminal)
    {
        projectUpdateNotices_.erase(latest);
        projectUpdateReady_=state.ready;
    }
    else if (state.stage!=QStringLiteral("superseded")) *latest=state;
    updateOriginalProjectReadiness(); updateDeviceControlActions();
    advanceOriginalProjectUpdates();
}

void frmMain::advanceOriginalProjectUpdates()
{
    if (!deviceControl_ || !deviceTaskStore_ || projectUpdateNotices_.isEmpty() || projectUpdatePumpQueued_ ||
        !deviceConfigurationBaseReady_ || !session_.authenticated || !session_.actionReady ||
        deviceTaskPreparing_ || reagentPriorityRequest_ || !deviceReagentUpdateHole_.isEmpty() ||
        deviceReagentUpdateUnknown_ || deviceReagentRecoveryRequired_ || !deviceRecoveryBlocker().isEmpty() ||
        property("originalProjectUpdateUnknown").isValid()) return;
    const auto &state=deviceControl_->state();
    const quint64 connection=deviceControl_->connectionGeneration();
    if (!state.connected || !state.fresh || !state.pending.isEmpty() || state.outcomeUnknown || state.status.mainStatus>2 ||
        state.status.generalFields.value(19)!=QStringLiteral("LinuxNew") ||
        connection!=deviceConfigurationConnection_ || !samples_ || deviceConfigurationRoot_!=samples_->property("originalRoot").toString()) return;
    bool queued=false;
    for (const auto &update : projectUpdateNotices_)
    {
        if (update.stage==QStringLiteral("prepared") || update.stage==QStringLiteral("waiting") || update.stage==QStringLiteral("not-sent")) return;
        if (update.stage==QStringLiteral("queued") && update.notice.root==deviceConfigurationRoot_) queued=true;
    }
    if (!queued) return;
    projectUpdatePumpQueued_=true; projectUpdatePumpConnection_=connection;
    if (!QMetaObject::invokeMethod(deviceTaskStore_,"advanceOriginalProjectUpdates",Qt::QueuedConnection,Q_ARG(quint64,connection)))
    {
        projectUpdatePumpQueued_=false;
        reportDeviceTaskError(tr("已提交项目的同步准备未能排入后台，设备参数仍未确认。"));
    }
}
