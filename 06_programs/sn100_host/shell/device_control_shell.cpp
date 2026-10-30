#include "frmmain.h"
#include "ui_frmmain.h"
#include "instrument/device_control.h"
#include "instrument/instrument_page.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include "samples/sample_page.h"
#include "history/history_page.h"
#include "ui/cusmessagebox.h"
#include <QFontMetrics>
#include <QTimer>
#include <QDebug>

namespace
{
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
    connectDeviceAtStartup_ = connectAtStartup;
    if (instrument_) instrument_->attachDeviceControl(deviceControl_, connectAtStartup);
    connect(deviceControl_, &DeviceControl::changed, this, [this](const DeviceControlState &state)
    {
        QString text = !state.connected ? tr("等待连接") : !state.fresh ? tr("等待状态") :
            state.status.mainStatus == 5 ? tr("正在测试") : state.status.mainStatus == 6 ? tr("已暂停") :
            state.status.mainStatus == 2 ? tr("准备就绪") : tr("准备测试");
        if (!state.pending.isEmpty()) text = state.pending == QStringLiteral("pause") ? tr("暂停中") : tr("等待开始返回");
        if (state.outcomeUnknown) text = tr("命令结果待核对");
        ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(text, Qt::ElideRight, ui->lblDivState->width()));
        ui->lblDivState->setToolTip(state.message);
        ui->statusArea->setToolTip(tr("主控 %1:%2；%3").arg(state.address).arg(state.port).arg(state.message));
        setProperty("instrumentConnected", state.connected);
        setProperty("instrumentStatusFresh", state.fresh);
        setProperty("instrumentMainStatus", state.status.mainStatus);
        updateOriginalDeviceSync();
        updateDeviceControlActions();
    });
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
    ui->btnCloseBeep->setToolTip(tr("按原Windows命令关闭主控报警蜂鸣器。"));
    if (reagents_) reagents_->setInstrumentBusy(deviceTaskPreparing_ || !deviceReagentUpdateHole_.isEmpty() ||
        deviceReagentUpdateUnknown_ || (deviceConfigurationStarted_ && !deviceConfigurationReady_) ||
        (state.connected && (!state.fresh || !state.pending.isEmpty() || state.outcomeUnknown || state.status.mainStatus>2)));
}

QString frmMain::detectionStartBlocker() const
{
    if (!session_.authenticated) return tr("请先登录后开始测试。");
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

void frmMain::invalidateDeviceTaskContext()
{
    deviceTaskPreparing_=false; ++deviceTaskRequest_;
    deviceConfigurationStarted_=false; deviceConfigurationReady_=false;
    deviceConfigurationConnection_=0; deviceConfigurationRoot_.clear();
    deviceReagentUpdateHole_.clear(); deviceReagentUpdateUnknown_=false; ++deviceReagentUpdateRequest_;
    if (deviceTaskStore_)
    {
        QMetaObject::invokeMethod(deviceTaskStore_,"invalidateOriginalDeviceSync",Qt::QueuedConnection);
        QMetaObject::invokeMethod(deviceTaskStore_,"cancelDeviceTaskPreparation",Qt::QueuedConnection);
    }
    setProperty("originalConfigurationReady",false);
}

void frmMain::updateOriginalDeviceSync()
{
    attachDeviceTaskStore();
    if (!deviceControl_ || !deviceTaskStore_) return;
    const auto &state=deviceControl_->state();
    const quint64 connection=deviceControl_->connectionGeneration();
    const QString root=samples_ ? samples_->property("originalRoot").toString() : QString();
    if ((!state.connected || connection!=deviceConfigurationConnection_ || root!=deviceConfigurationRoot_) && deviceConfigurationStarted_)
        invalidateDeviceTaskContext();
    if (deviceConfigurationStarted_ || !session_.authenticated || !session_.actionReady || !state.connected || !state.fresh ||
        !state.pending.isEmpty() || state.outcomeUnknown || state.status.mainStatus>2 || root.isEmpty() ||
        !samples_ || !projects_ || !reagents_ || !samples_->isReady() || !projects_->isReady() || !reagents_->isReady() ||
        samples_->isBusy() || projects_->isBusy() || reagents_->isBusy() || samples_->hasUnsavedChanges() ||
        projects_->hasUnsavedChanges() || reagents_->hasUnsavedChanges() || !samples_->originalProfile() ||
        !reagents_->snapshot().originalProfile || !projects_->property("originalProfile").toBool() ||
        root!=reagents_->property("originalRoot").toString() || root!=projects_->property("originalRoot").toString()) return;
    // 每连接只启动一次；超时/拒绝不会因下次Qj而自动重发参数。
    deviceConfigurationStarted_=true; deviceConfigurationReady_=false;
    deviceConfigurationConnection_=connection; deviceConfigurationRoot_=root;
    const bool linuxNew=state.status.generalFields.value(19)==QStringLiteral("LinuxNew");
    QMetaObject::invokeMethod(deviceTaskStore_,"beginOriginalDeviceSync",Qt::QueuedConnection,
        Q_ARG(quint64,connection),Q_ARG(bool,linuxNew));
    updateDeviceControlActions();
}

void frmMain::attachDeviceTaskStore()
{
    if (deviceTaskStore_ || !deviceControl_ || !samples_ || !reagents_ || !projects_) return;
    auto *store=samples_->taskStore();
    if (!store) return;
    deviceTaskStore_=store;
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
        deviceConfigurationReady_=ready;
        setProperty("originalConfigurationReady",ready);
        setProperty("originalConfigurationMessage",message);
        ui->lblDivState->setToolTip(message);
        updateDeviceControlActions();
    });
    connect(store,&SampleStore::originalConfigurationPacket,this,[this,store](const QByteArray &payload,quint64 connection)
    {
        if (connection!=deviceControl_->connectionGeneration()) return;
        if (!deviceControl_->sendOriginalConfiguration(payload))
        {
            deviceConfigurationReady_=false;
            QMetaObject::invokeMethod(store,"invalidateOriginalDeviceSync",Qt::QueuedConnection);
            reportDeviceTaskError(tr("原配置未能进入当前连接发送队列；请核对收发记录后重新连接。"));
            updateDeviceControlActions();
        }
    });
    connect(reagents_->taskStore(),&ReagentStore::originalHoleSaved,this,[this,store](const QString &hole)
    {
        const auto &state=deviceControl_->state();
        if (!state.connected || state.status.generalFields.value(19)!=QStringLiteral("LinuxNew")) return;
        if (!state.fresh || state.outcomeUnknown || !state.pending.isEmpty() || state.status.mainStatus>2 ||
            !deviceConfigurationReady_ || deviceConfigurationConnection_!=deviceControl_->connectionGeneration())
        {
            deviceReagentUpdateUnknown_=true;
            reportDeviceTaskError(tr("孔位已保存到原库，但当前设备状态不允许同步，请核对后重新连接。"));
            updateDeviceControlActions(); return;
        }
        deviceReagentUpdateHole_=hole;
        const quint64 request=++deviceReagentUpdateRequest_;
        setProperty("originalReagentUpdateMessage",tr("原库已保存，等待该孔位设备回读。"));
        QMetaObject::invokeMethod(store,"prepareOriginalReagentUpdate",Qt::QueuedConnection,
            Q_ARG(QString,hole),Q_ARG(bool,true),Q_ARG(quint64,deviceConfigurationConnection_));
        QTimer::singleShot(10000,this,[this,request]()
        {
            if (request!=deviceReagentUpdateRequest_ || deviceReagentUpdateHole_.isEmpty()) return;
            deviceReagentUpdateUnknown_=true;
            reportDeviceTaskError(tr("单孔更新10秒未收到对应孔位回读；原库已保存，设备执行情况待核对，未自动重发。"));
            updateDeviceControlActions();
        });
        updateDeviceControlActions();
    });
    connect(deviceControl_,&DeviceControl::payloadReceived,this,[this,store](const QByteArray &payload)
    {
        const QByteArray command=payload.left(2);
        if (command!="Tt" && command!="TD" && command!="Tr" && command!="Ur" && command!="UP" && payload!="Tu") return;
        if (devicePackets_.size()>=32 || devicePacketBytes_+payload.size()>1048576)
        {
            reportDeviceTaskError(tr("设备业务队列超过32包或1MiB，本包未保存，连接已关闭，请核对后恢复。"));
            deviceControl_->close(); return;
        }
        const quint64 packet=++devicePacket_;
        devicePackets_.insert(packet,payload); devicePacketBytes_+=payload.size();
        QMetaObject::invokeMethod(store,"receiveDevicePacket",Qt::QueuedConnection,Q_ARG(QByteArray,payload),
            Q_ARG(quint64,deviceControl_->connectionGeneration()),Q_ARG(quint64,packet));
    });
    connect(store,&SampleStore::devicePacketProcessed,this,
        [this](quint64 packet,quint64 connection,const QByteArray &command,bool success,const QString &message)
    {
        const QByteArray payload=devicePackets_.take(packet); devicePacketBytes_-=payload.size();
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
        if (connection!=deviceControl_->connectionGeneration() || !deviceControl_->sendTaskReply(payload))
            reportDeviceTaskError(tr("任务编号已保存，但未能下发；原记录保留，请核对连接和收发记录，勿重复开始。"));
    });
    connect(store,&SampleStore::deviceTaskError,this,&frmMain::reportDeviceTaskError);
    connect(store,&SampleStore::originalReagentsChanged,reagents_,&ReagentsPage::reloadOriginal);
    connect(store,&SampleStore::originalProjectsChanged,this,[this]()
    { if (projects_ && !projects_->isBusy() && !projects_->hasUnsavedChanges()) projects_->reload(); });
    connect(store,&SampleStore::originalResultsChanged,this,[this]()
    { if (history_ && !history_->isBusy() && !history_->hasUnsavedChanges()) history_->refresh(); });
}
