#include "frmmain.h"
#include "frmuserset.h"
#include "ui_frmmain.h"
#include "ui/frmlissetting.h"

#include <QDebug>
#include <QLabel>

frmLisSetting *frmMain::lisPage() const
{
    return userSet_->lisPage();
}

void frmMain::attachLisPage(frmLisSetting *page)
{
    if (!page || lisPage())
    {
        qWarning() << "Lis settings page must be attached once before startup";
        return;
    }
    userSet_->attachLisPage(page);
    connect(page, &frmLisSetting::statusChanged, this, [this](const QString &message, bool error)
    {
        lisStatus_ = message;
        lisStatusError_ = error;
        updateLisStatus();
        updateAccountStatus();
        if (error) qWarning().noquote() << "LIS_SETTINGS_ERROR" << message;
    });
    connect(page, &frmLisSetting::busyChanged, this, [this](bool)
    {
        updateLisStatus();
        updateAccountStatus();
    });
}

void frmMain::updateLisStatus()
{
    if (!lisPage() || currentMainPage() != 6 || currentUserPage() != 4) return;
    if (deviceControl_) { updateDeviceStatusDisplay(); return; }
    const QString state = lisStatusError_ ? tr("脱机／失败")
        : lisPage()->isBusy() ? tr("脱机／处理中")
        : lisPage()->isDirty() ? tr("脱机／未保存") : tr("脱机／就绪");
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(
        state, Qt::ElideRight, ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(lisStatusError_
        ? QStringLiteral("QLabel { color: #E83323; }")
        : QStringLiteral("QLabel { color: rgb(170,170,170); }"));
    ui->lblDivState->setToolTip(lisStatus_);
    ui->statusArea->setToolTip(tr("LIS设置：%1；通信状态请查看顶部LIS图标。").arg(lisStatus_));
}

void frmMain::setLisConnectionState(Sn100Lis::ConnectionState state)
{
    // LIS有自己的原图标，不占用仪器状态、灯或设置保存状态。
    const bool connected = !state.closing && state.connectedPeers > 0;
    const QString image = connected ? QStringLiteral("lison") : QStringLiteral("lisoff");
    ui->btnLis->setNormalBgImage(QStringLiteral(":/shell/main_state/%1_normal.png").arg(image));
    ui->btnLis->setHoverBgImage(QStringLiteral(":/shell/main_state/%1_hover.png").arg(image));
    ui->btnLis->setPressedBgImage(QStringLiteral(":/shell/main_state/%1_pressed.png").arg(image));
    ui->btnLis->setText(connected ? QStringLiteral("on") : QStringLiteral("off"));
    ui->btnLis->setEnabled(true); // 只恢复原图标效果；原点击没有连接/断开业务。
    const QMap<QString, QString> names{
        {QStringLiteral("Unconfigured"), tr("配置未就绪")},
        {QStringLiteral("Configured"), tr("配置已准备，尚未打开")},
        {QStringLiteral("LocalSettingsOnly"), tr("仅保存独立本地设置")},
        {QStringLiteral("SerialPending"), tr("当前为串口配置，通信待接入")},
        {QStringLiteral("SettingsUnconfirmed"), tr("配置待重新读取，连接已停止")},
        {QStringLiteral("Listening"), tr("正在监听，尚无对端连接")},
        {QStringLiteral("Connecting"), tr("正在连接")},
        {QStringLiteral("Connected"), tr("已有对端连接")},
        {QStringLiteral("ReconnectWaiting"), tr("等待重新连接")},
        {QStringLiteral("Disconnected"), tr("连接已断开")},
        {QStringLiteral("Failed"), tr("连接失败")},
        {QStringLiteral("Stopped"), tr("通信已停止")}
    };
    const QString text = names.value(state.state, state.state);
    ui->btnLis->setToolTip(tr("LIS：%1；配置版本%2，连接代次%3，已连接对端%4。%5")
        .arg(text).arg(state.configuration.revision).arg(state.generation).arg(state.connectedPeers).arg(state.detail));
}
