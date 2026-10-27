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
    const QString state = lisStatusError_ ? tr("脱机／失败")
        : lisPage()->isBusy() ? tr("脱机／处理中")
        : lisPage()->isDirty() ? tr("脱机／未保存") : tr("脱机／就绪");
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(
        state, Qt::ElideRight, ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(lisStatusError_
        ? QStringLiteral("QLabel { color: #E83323; }")
        : QStringLiteral("QLabel { color: rgb(170,170,170); }"));
    ui->lblDivState->setToolTip(lisStatus_);
    ui->statusArea->setToolTip(tr("LIS本地设置；未建立通信或打开串口。%1").arg(lisStatus_));
}
