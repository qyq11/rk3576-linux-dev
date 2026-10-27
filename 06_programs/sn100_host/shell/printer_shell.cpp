#include "frmmain.h"
#include "frmuserset.h"
#include "ui_frmmain.h"
#include "ui/frmprintersetting.h"

#include <QDebug>
#include <QLabel>

frmPrinterSetting *frmMain::printerPage() const
{
    return userSet_->printerPage();
}

void frmMain::attachPrinterPage(frmPrinterSetting *page)
{
    if (!page || printerPage())
    {
        qWarning() << "Printer settings page must be attached once before startup";
        return;
    }
    userSet_->attachPrinterPage(page);
    connect(page, &frmPrinterSetting::statusChanged, this, [this](const QString &message, bool error)
    {
        printerStatus_ = message;
        printerStatusError_ = error;
        updatePrinterStatus();
        updateAccountStatus();
        if (error) qWarning().noquote() << "PRINTER_SETTINGS_ERROR" << message;
    });
    connect(page, &frmPrinterSetting::busyChanged, this, [this](bool)
    {
        updatePrinterStatus();
        updateAccountStatus();
    });
}

void frmMain::updatePrinterStatus()
{
    if (!printerPage() || currentMainPage() != 6 || currentUserPage() != 3) return;
    const QString state = printerStatusError_ ? tr("脱机／失败")
        : printerPage()->isBusy() ? tr("脱机／处理中")
        : printerPage()->isDirty() ? tr("脱机／未保存") : tr("脱机／就绪");
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(
        state, Qt::ElideRight, ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(printerStatusError_
        ? QStringLiteral("QLabel { color: #E83323; }")
        : QStringLiteral("QLabel { color: rgb(170,170,170); }"));
    ui->lblDivState->setToolTip(printerStatus_);
    ui->statusArea->setToolTip(tr("本地打印设置；未打印、未更改系统默认设备。%1").arg(printerStatus_));
}
