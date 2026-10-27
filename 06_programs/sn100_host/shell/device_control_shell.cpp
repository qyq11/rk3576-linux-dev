#include "frmmain.h"
#include "ui_frmmain.h"
#include "instrument/device_control.h"
#include "instrument/instrument_page.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include "samples/sample_page.h"
#include "ui/cusmessagebox.h"
#include <QFontMetrics>

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
        if (question.getClickButton() == QMessageBox::Yes)
            deviceControl_->start(detectionStartBlocker());
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
    updateDeviceControlActions();
    return true;
}

void frmMain::updateDeviceControlActions()
{
    if (!deviceControl_) return;
    const auto &state = deviceControl_->state();
    const bool available = session_.authenticated && state.connected && state.fresh &&
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
    return tr("样本任务尚未生成带稳定任务编号的设备下发记录，请先完成任务准备。");
}
