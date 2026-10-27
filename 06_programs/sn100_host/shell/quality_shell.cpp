#include "frmmain.h"
#include "ui_frmmain.h"
#include "quality_control/quality_control_page.h"
#include "projects/project_page.h"
#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>

namespace
{
// 这里只传已提交项目及其单位快照，不把项目编辑框中的草稿用于质控。
QcProjectRefs qualityProjects(const ProjectRecords &records, const ProjectUnits &units)
{
    QcProjectRefs choices;
    for (const ProjectRecord &record : records)
    {
        QcProjectRef choice;
        choice.id = record.id;
        choice.name = record.name;
        for (const ProjectResult &result : record.results)
        {
            const QStringList types = {QStringLiteral("Original"), QStringLiteral("Calibration"),
                                       QStringLiteral("Rate"), QStringLiteral("INR")};
            const int type = types.indexOf(result.type) + 1;
            if (!type) continue;
            bool foundUnit = false;
            QcResultRef value;
            value.type = type;
            value.name = result.name;
            value.unit = result.unit;
            value.decimals = result.decimals;
            for (const ProjectUnit &unit : units)
            {
                if (unit.name == result.unit)
                {
                    value.ratio = unit.ratio;
                    foundUnit = true;
                    break;
                }
            }
            // 缺单位时不能猜倍率；保留项目读取，关联保存由存储明确拒绝。
            if (foundUnit) choice.results.append(value);
        }
        if (!choice.results.isEmpty()) choices.append(choice);
    }
    return choices;
}
}

void frmMain::attachQualityControlPage(QualityControlPage *page)
{
    if (!page || qualityControl_ || !projects_) return;
    qualityControl_ = page;
    const int previous = currentMainPage();
    qualityViewport_ = new QScrollArea(ui->stkCenterAdptive);
    qualityViewport_->setObjectName(QStringLiteral("qualityControlViewport"));
    qualityViewport_->setFrameShape(QFrame::NoFrame);
    qualityViewport_->setWidgetResizable(false);
    page->setFixedSize(1751, 968);
    qualityViewport_->setWidget(page);
    QWidget *placeholder = ui->stkCenterAdptive->widget(4);
    ui->stkCenterAdptive->removeWidget(placeholder);
    delete placeholder;
    ui->stkCenterAdptive->insertWidget(4, qualityViewport_);
    ui->stkCenterAdptive->setCurrentIndex(previous);
    connect(projects_, &ProjectPage::snapshotChanged, page,
            [page](ProjectRecords records, ProjectUnits units)
    {
        page->setProjects(qualityProjects(records, units));
    });
    connect(page, &QualityControlPage::statusChanged, this,
            [this](const QString &message, bool error)
    {
        qualityStatus_ = message;
        qualityStatusError_ = error;
        updateQualityStatus();
        updateAccountStatus();
    });
    connect(page, &QualityControlPage::busyChanged, this, [this](bool)
    {
        updateQualityStatus();
        updateAccountStatus();
    });
    connect(page, &QualityControlPage::operationFinished, this, [this](QString, bool success)
    {
        qualityStatusError_ = !success;
        updateQualityStatus();
        updateAccountStatus();
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now)
    {
        if (now && qualityControl_->isAncestorOf(now)) lastQualityFocus_ = now;
    });
    updateAccountStatus();
}

bool frmMain::qualityPagesIdle() const
{
    return !qualityControl_ || !qualityControl_->isBusy();
}

bool frmMain::requestQualityLeave(const QString &action, bool currentOnly)
{
    if (!qualityControl_ || (currentOnly && currentMainPage() != 4)) return true;
    if (qualityControl_->requestLeave(action)) return true;
    emit navigationRejected(tr("质控：请先完成操作或处理草稿，再%1。").arg(action));
    return false;
}

void frmMain::updateQualityStatus()
{
    if (!qualityControl_ || currentMainPage() != 4) return;
    const QString state = qualityStatusError_ ? tr("脱机／失败")
        : qualityControl_->isBusy() ? tr("脱机／处理中")
        : qualityControl_->isDirty() ? tr("脱机／未保存")
        : qualityControl_->isReady() ? tr("脱机／就绪") : tr("脱机／未知");
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(
        state, Qt::ElideRight, ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(qualityStatusError_
        ? QStringLiteral("QLabel{color:#E83323;}") : QStringLiteral("QLabel{color:rgb(170,170,170);}"));
    ui->lblDivState->setToolTip(qualityStatus_);
    ui->statusArea->setToolTip(tr("质控品/项目/批次本地设置；真实曲线和失控判定待验。%1").arg(qualityStatus_));
}

void frmMain::restoreQualityFocus()
{
    if (!qualityViewport_ || !qualityControl_) return;
    const QPointer<QWidget> target = lastQualityFocus_;
    QTimer::singleShot(0, this, [this, target]()
    {
        QWidget *focus = target && target->isVisible() && target->isEnabled()
            ? target.data() : static_cast<QWidget *>(qualityControl_);
        qualityViewport_->ensureWidgetVisible(focus, 10, 10);
        focus->setFocus(Qt::OtherFocusReason);
    });
}
