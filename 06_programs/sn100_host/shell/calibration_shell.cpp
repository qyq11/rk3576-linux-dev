#include "frmmain.h"
#include "ui_frmmain.h"
#include "calibration/calibration_page.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>

void frmMain::attachCalibrationPage(CalibrationPage *page)
{
    if (!page || calibration_ || !projects_ || !reagents_) return;
    calibration_ = page;
    const int previous = currentMainPage();
    calibrationViewport_ = new QScrollArea(ui->stkCenterAdptive);
    calibrationViewport_->setObjectName(QStringLiteral("calibrationPageViewport"));
    calibrationViewport_->setFrameShape(QFrame::NoFrame);
    calibrationViewport_->setWidgetResizable(false);
    page->setFixedSize(1751, 968);
    calibrationViewport_->setWidget(page);
    QWidget *placeholder = ui->stkCenterAdptive->widget(3);
    ui->stkCenterAdptive->removeWidget(placeholder);
    delete placeholder;
    ui->stkCenterAdptive->insertWidget(3, calibrationViewport_);
    ui->stkCenterAdptive->setCurrentIndex(previous);
    page->setSession(session_);
    connect(this, &frmMain::sessionChanged, page, &CalibrationPage::setSession);
    const bool originalProfile = !property("originalDataRoot").toString().isEmpty();
    connect(projects_, &ProjectPage::snapshotChanged, page,
            [page, originalProfile](ProjectRecords records, ProjectUnits units)
    {
        ProjectRecords local;
        for (const auto &record : records)
            if ((!record.originalFileName.isEmpty()) == originalProfile) local.append(record);
        // 原模式沿用原项目编号；独立模式仍只传自身catalog编号。
        page->setProjects(local, units);
    });
    connect(reagents_, &ReagentsPage::snapshotChanged, page, &CalibrationPage::setReagents);
    connect(page, &CalibrationPage::statusChanged, this, [this](const QString &message)
    {
        calibrationStatus_ = message;
        updateCalibrationStatus();
        updateAccountStatus();
    });
    connect(page, &CalibrationPage::operationFinished, this, [this](const QString &, bool success)
    {
        calibrationStatusError_ = !success;
        updateCalibrationStatus();
        updateAccountStatus();
    });
    connect(page, &CalibrationPage::busyChanged, this, [this](bool)
    {
        updateCalibrationStatus();
        updateAccountStatus();
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now)
    {
        if (now && calibration_->isAncestorOf(now)) lastCalibrationFocus_ = now;
    });
    updateAccountStatus();
}

bool frmMain::calibrationPageIdle() const
{
    return !calibration_ || !calibration_->isBusy();
}

bool frmMain::requestCalibrationLeave(const QString &action, bool currentOnly)
{
    if (!calibration_ || (currentOnly && currentMainPage() != 3)) return true;
    if (calibration_->prepareToLeave()) return true;
    emit navigationRejected(tr("校准：请先完成操作或处理草稿，再%1。").arg(action));
    return false;
}

void frmMain::updateCalibrationStatus()
{
    if (!calibration_ || currentMainPage() != 3) return;
    if (deviceControl_) { updateDeviceControlActions(); return; }
    const QString state = calibrationStatusError_ ? tr("脱机／失败")
        : calibration_->isBusy() ? tr("脱机／处理中")
        : calibration_->hasUnsavedChanges() ? tr("脱机／未保存")
        : calibration_->isReady() ? tr("脱机／就绪") : tr("脱机／未知");
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(
        state, Qt::ElideRight, ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(calibrationStatusError_
        ? QStringLiteral("QLabel{color:#E83323;}") : QStringLiteral("QLabel{color:rgb(170,170,170);}"));
    ui->lblDivState->setToolTip(calibrationStatus_);
    ui->statusArea->setToolTip(tr("本地校准原始点；未拟合、未审核、未应用。%1").arg(calibrationStatus_));
}

void frmMain::restoreCalibrationFocus()
{
    if (!calibrationViewport_ || !calibration_) return;
    const QPointer<QWidget> target = lastCalibrationFocus_;
    QTimer::singleShot(0, this, [this, target]()
    {
        QWidget *focus = target && target->isVisible() && target->isEnabled()
            ? target.data() : static_cast<QWidget *>(calibration_);
        calibrationViewport_->ensureWidgetVisible(focus, 10, 10);
        focus->setFocus(Qt::OtherFocusReason);
    });
}
