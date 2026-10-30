#include "frmmain.h"
#include "ui_frmmain.h"
#include "instrument/device_control.h"
#include "instrument/instrument_page.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include "samples/sample_page.h"
#include "ui/cusmessagebox.h"
#include <QFontMetrics>
#include <QTimer>
#include <QDebug>

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
        updateDeviceControlActions();
    });
    connect(this, &frmMain::sessionChanged, this, [this](const AccountSession &) { updateDeviceControlActions(); });
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
        deviceTaskPreparing_ = true;
        const quint64 request = ++deviceTaskRequest_;
        deviceTaskConnection_ = deviceControl_->connectionGeneration();
        const bool linuxNew = deviceControl_->state().status.generalFields.value(19) == QStringLiteral("LinuxNew");
        updateDeviceControlActions();
        QMetaObject::invokeMethod(samples_->taskStore(), "prepareDeviceTasks", Qt::QueuedConnection,
            Q_ARG(quint64, request), Q_ARG(bool, linuxNew));
        QTimer::singleShot(10000, this, [this, request]()
        {
            if (!deviceTaskPreparing_ || request != deviceTaskRequest_) return;
            deviceTaskPreparing_ = false;
            QMetaObject::invokeMethod(samples_->taskStore(), "cancelDeviceTaskPreparation", Qt::QueuedConnection);
            updateDeviceControlActions();
            CusMessageBox message(this);
            message.showWarningBox(tr("原样本任务准备超过10秒，未发送开始命令，请核对数据读取结果。"));
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
    attachDeviceTaskStore();
    updateDeviceControlActions();
    return true;
}

void frmMain::updateDeviceControlActions()
{
    if (!deviceControl_) return;
    const auto &state = deviceControl_->state();
    const bool available = !deviceTaskPreparing_ && session_.authenticated && state.connected && state.fresh &&
                           state.pending.isEmpty() && !state.outcomeUnknown;
    ui->btnStart->setBtnDisabled(!available || (state.status.mainStatus != 2 && state.status.mainStatus != 6));
    ui->btnPause->setBtnDisabled(!available || state.status.mainStatus < 3 || state.status.mainStatus > 6);
    ui->btnCloseBeep->setEnabled(session_.authenticated && state.connected);
    ui->btnStart->setToolTip(deviceControl_->startBlocker().isEmpty()
                                ? detectionStartBlocker() : deviceControl_->startBlocker());
    ui->btnPause->setToolTip(deviceControl_->pauseBlocker());
    ui->btnCloseBeep->setToolTip(tr("按原Windows命令关闭主控报警蜂鸣器。"));
}

QString frmMain::detectionStartBlocker() const
{
    if (!session_.authenticated) return tr("请先登录后开始测试。");
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
    if (!samples_->originalProfile()) return tr("样本页尚未使用当前原数据库，请核对启动的数据根。 ");
    return QString();
}

void frmMain::attachDeviceTaskStore()
{
    if (deviceTaskStoreAttached_ || !deviceControl_ || !samples_) return;
    auto *store = samples_->taskStore();
    if (!store) return;
    deviceTaskStoreAttached_ = true;
    connect(store, &SampleStore::deviceTasksPrepared, this, [this, store](quint64 request, bool success, const QString &detail)
    {
        if (!deviceTaskPreparing_ || request != deviceTaskRequest_) return;
        deviceTaskPreparing_ = false;
        const QString blocker = deviceTaskConnection_ != deviceControl_->connectionGeneration()
            ? tr("准备样本任务期间连接已改变，未向新连接发送开始命令。") : detectionStartBlocker();
        if (success && blocker.isEmpty())
        {
            if (!deviceControl_->start()) QMetaObject::invokeMethod(store, "cancelDeviceTaskPreparation", Qt::QueuedConnection);
        }
        else
        {
            QMetaObject::invokeMethod(store, "cancelDeviceTaskPreparation", Qt::QueuedConnection);
            CusMessageBox message(this);
            message.showWarningBox(success ? blocker : detail);
        }
        updateDeviceControlActions();
    });
    connect(deviceControl_, &DeviceControl::startAccepted, store, [store](bool accepted)
    { if (!accepted) store->cancelDeviceTaskPreparation(); }, Qt::QueuedConnection);
    connect(deviceControl_, &DeviceControl::payloadReceived, this, [this, store](const QByteArray &payload)
    {
        const QByteArray command = payload.left(2);
        if (command != "Tt" && command != "TD" && command != "Tr" && command != "Ur") return;
        // 本地最多32包/1MiB在后台排队；满时明确断开，不让设备数据无限占用GUI内存。
        if (devicePackets_.size() >= 32 || devicePacketBytes_ + payload.size() > 1048576)
        {
            const QString detail = tr("设备业务数据处理队列已满，本包尚未保存，连接已关闭。已排队数据继续处理，请核对仪器与原始记录后恢复连接。");
            deviceControl_->close();
            setProperty("deviceTaskError", detail);
            ui->lblDivState->setText(tr("设备数据积压"));
            ui->lblDivState->setToolTip(detail);
            qWarning().noquote() << "DEVICE_TASK_BACKPRESSURE" << detail;
            return;
        }
        const quint64 packet = ++devicePacket_;
        devicePackets_.insert(packet,payload.size());
        devicePacketBytes_ += payload.size();
        QMetaObject::invokeMethod(store, "receiveDevicePacket", Qt::QueuedConnection,
            Q_ARG(QByteArray, payload), Q_ARG(quint64, deviceControl_->connectionGeneration()), Q_ARG(quint64, packet));
    });
    connect(store, &SampleStore::devicePacketProcessed, this,
        [this](quint64 packet, quint64 connection, const QByteArray &, bool success, const QString &detail)
    {
        if (devicePackets_.contains(packet)) devicePacketBytes_ -= devicePackets_.take(packet);
        if (success || connection != deviceControl_->connectionGeneration()) return;
        setProperty("deviceTaskError", detail);
        ui->lblDivState->setText(tr("任务处理失败"));
        ui->lblDivState->setToolTip(detail);
        qWarning().noquote() << "DEVICE_PACKET_ERROR" << detail;
    });
    connect(store, &SampleStore::deviceRackTasksPrepared, this, [this](const QByteArray &payload, quint64 connection)
    {
        if (connection != deviceControl_->connectionGeneration() || !deviceControl_->sendTaskReply(payload))
        {
            const QString detail = tr("任务编号已保存，但未能进入设备发送队列。原任务保留，请核对连接和收发记录，不要重复开始。");
            setProperty("deviceTaskError", detail);
            ui->lblDivState->setText(tr("任务下发失败"));
            ui->lblDivState->setToolTip(detail);
            qWarning().noquote() << "DEVICE_TASK_QUEUE_ERROR" << detail;
        }
    });
    connect(store, &SampleStore::deviceTaskError, this, [this](const QString &detail)
    {
        setProperty("deviceTaskError", detail);
        ui->lblDivState->setText(tr("任务处理失败"));
        ui->lblDivState->setToolTip(detail);
        qWarning().noquote() << "DEVICE_TASK_ERROR" << detail;
    });
}
