#include "frmmain.h"
#include "ui_frmmain.h"
#include "instrument/instrument_page.h"
#include "projects/project_page.h"

#include <QDebug>
#include <QDir>
#include <QLabel>
#include <QScrollArea>

void frmMain::attachInstrumentPage(InstrumentPage *page)
{
    if (!page || instrument_)
    {
        qWarning() << "Instrument page must be attached once before startup";
        return;
    }
    instrument_ = page;
    const int previous = currentMainPage();
    auto *viewport = new QScrollArea(ui->stkCenterAdptive);
    viewport->setObjectName(QStringLiteral("instrumentPageViewport"));
    viewport->setFrameShape(QFrame::NoFrame);
    viewport->setWidgetResizable(false);
    // 原1751x968画布及控件坐标保留，小屏沿用主壳外部滚动。
    page->setFixedSize(1751,968);
    viewport->setWidget(page);
    QWidget *placeholder = ui->stkCenterAdptive->widget(7);
    ui->stkCenterAdptive->removeWidget(placeholder);
    delete placeholder;
    ui->stkCenterAdptive->insertWidget(7,viewport);
    ui->stkCenterAdptive->setCurrentIndex(previous);
    page->setSession(session_);
    connect(this,&frmMain::sessionChanged,page,&InstrumentPage::setSession);
    connect(page,&InstrumentPage::statusChanged,this,[this](const QString &message)
    {
        instrumentStatus_ = message;
        updateInstrumentStatus();
    });
    connect(page,&InstrumentPage::busyChanged,this,[this](bool)
    {
        updateInstrumentStatus();
        updateAccountStatus();
    });
    connect(page,&InstrumentPage::operationFinished,this,[this](const QString &,bool success)
    {
        instrumentStatusError_ = !success;
        updateInstrumentStatus();
        updateAccountStatus();
    });
    // 原项目工作目录同时保留config.ini；只转交文件路径，读写仍在存储线程。
    const auto loadOriginalConnection = [this, page]()
    {
        if (!projects_ || !projects_->property("originalProfile").toBool() || !page->isReady() || page->isBusy()) return;
        const QString root = projects_->property("originalRoot").toString();
        if (root.isEmpty()) return;
        const QString file = QDir(root).filePath(QStringLiteral("config.ini"));
        if (page->property("connectionConfigurationFile").toString() != file)
            page->setConnectionConfigurationFile(file);
    };
    if (projects_)
        connect(projects_, &ProjectPage::operationFinished, page,
                [loadOriginalConnection](const QString &operation, bool success)
                { if (operation == QStringLiteral("load") && success) loadOriginalConnection(); });
    connect(page, &InstrumentPage::operationFinished, page,
            [loadOriginalConnection](const QString &, bool success)
            { if (success) loadOriginalConnection(); });
    loadOriginalConnection();
    if (deviceControl_) page->attachDeviceControl(deviceControl_, connectDeviceAtStartup_);
}

bool frmMain::requestInstrumentLeave(const QString &action)
{
    if (instrument_ && !instrument_->prepareToLeave())
    {
        emit navigationRejected(tr("仪器操作：请等待维护记录处理完成，再%1。").arg(action));
        return false;
    }
    return true;
}

void frmMain::updateInstrumentStatus()
{
    if (!instrument_ || currentMainPage() != 7) return;
    // 实际连接状态由DeviceControl的更新信号呈现，不能被本地维护的“脱机”字样覆盖。
    if (deviceControl_) return;
    if (readOnlyStatusEnabled_)
    {
        updateReadOnlyStatusDisplay();
        if (!instrumentStatus_.isEmpty())
        {
            const QString detail = ui->lblDivState->toolTip()
                + tr("\n本地维护：%1").arg(instrumentStatus_);
            ui->lblDivState->setToolTip(detail);
            ui->statusArea->setToolTip(detail);
        }
        return;
    }
    const QString state = instrumentStatusError_ ? tr("脱机／失败")
        : instrument_->isBusy() ? tr("脱机／处理中") : tr("脱机／未知");
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(
        state,Qt::ElideRight,ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(instrumentStatusError_
        ? QStringLiteral("QLabel { color: #E83323; }")
        : QStringLiteral("QLabel { color: rgb(170,170,170); }"));
    ui->lblDivState->setToolTip(instrumentStatus_);
    ui->statusArea->setToolTip(tr("脱机；真实仪器状态未知。%1").arg(instrumentStatus_));
}
