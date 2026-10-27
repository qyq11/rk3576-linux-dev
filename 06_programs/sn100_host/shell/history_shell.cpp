#include "frmmain.h"
#include "ui_frmmain.h"
#include "history/history_page.h"
#include "history/report_controller.h"
#include "ui/frmhospinfosetting.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QLabel>
#include <QPrintPreviewDialog>
#include <QScrollArea>
#include <QTableWidget>
#include <QTimer>

// 这里只接入历史页面。SQL连接与线程仍由main创建、初始化和关闭，
// 账户身份仍只由AccountStore驱动，不在报告页复制一套角色权限。
void frmMain::attachHistoryPage(HistoryPage *page)
{
    if (!page || history_)
    {
        qWarning() << "History page must be attached once before startup";
        return;
    }
    const int previousPage = currentMainPage();
    QWidget *placeholder = ui->stkCenterAdptive->widget(2);
    history_ = page;
    historyViewport_ = new QScrollArea(ui->stkCenterAdptive);
    historyViewport_->setObjectName(QStringLiteral("historyPageViewport"));
    historyViewport_->setFrameShape(QFrame::NoFrame);
    historyViewport_->setWidgetResizable(false);
    // 原报告画布1751×968比原主栈略高。只增加内容滚动，保留每个业务控件
    // 原坐标/尺寸和1751×968画布，不拉伸、不重排右侧操作或底部分页。
    historyViewport_->setWidget(page);
    ui->stkCenterAdptive->removeWidget(placeholder);
    delete placeholder;
    ui->stkCenterAdptive->insertWidget(2, historyViewport_);
    ui->stkCenterAdptive->setCurrentIndex(previousPage);
    page->setSession(session_);
    connect(this, &frmMain::sessionChanged, page, &HistoryPage::setSession);

    connect(page, &HistoryPage::statusChanged, this, [this](const QString &message)
    {
        historyStatus_ = message;
        if (history_->isBusy()) historyStatusError_ = false;
        updateHistoryStatus();
        updateAccountStatus();
    });
    connect(page, &HistoryPage::operationFinished, this, [this](const QString &, bool success)
    {
        historyStatusError_ = !success;
        updateHistoryStatus();
        updateAccountStatus();
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now)
    {
        if (now && (now == history_ || history_->isAncestorOf(now))) lastHistoryFocus_ = now;
    });
    updateAccountStatus();
}

HistoryPage *frmMain::historyPage() const
{
    return history_;
}

bool frmMain::enableHistoryReports(const QString &directory)
{
    if (!history_ || reports_ || !QDir::isAbsolutePath(directory)) return false;
    QString error;
    if (!HistoryReportDocument::anonymousOptions(&reportOptions_, &error))
    {
        reportStatus(error, true);
        return false;
    }
    // Project columns remain the explicit anonymous O04 contract until the
    // project module is migrated. The hospital comes from the shared store.
    reportOptions_.hospitalName = committedHospitalName_;
    reports_ = new HistoryReportController(this);
    reports_->setObjectName(QStringLiteral("unifiedHistoryReports"));
    reports_->setDataDirectory(directory);
    reports_->setPrintOptions(reportOptions_);
    history_->setReportPreviewEnabled(true);
    connect(history_, &HistoryPage::reportPreviewRequested, this, [this](const HistoryReport &report)
    {
        if (hospitalPage()->isBusy())
        {
            reportHasOutcome_ = true;
            reportStatus(tr("医院配置正在读取或保存，请等待结果后再预览报告。"), true);
            return;
        }
        reports_->preview(report);
    });
    connect(reports_, &HistoryReportController::busyChanged, this, [this](bool busy)
    {
        history_->setReportOperationBusy(busy);
        // Report busy changes do not emit a database operationFinished signal.
        // Refresh shell status/logout immediately as well as the page buttons.
        if (busy)
        {
            reportHasOutcome_ = false;
            reportStatus(tr("报告预览或HTML/PDF导出处理中。"));
        }
        else if (!reportHasOutcome_) reportStatus(tr("报告操作已结束。"));
        else
        {
            // Closing a modal preview must not erase an export failure or the
            // successfully published path before the user can see it.
            updateHistoryStatus();
            updateAccountStatus();
        }
    });
    connect(reports_, &HistoryReportController::failed, this,
            [this](const QString &operation, const QString &message)
    {
        reportHasOutcome_ = true;
        reportStatus(operation + QStringLiteral(": ") + message, true);
        qWarning().noquote() << "REPORT_ERROR" << operation << message;
    });
    connect(reports_, &HistoryReportController::nativePrintFinished, this,
            [this](const QString &, const QString &outcome, const QString &printerName, const QString &)
    {
        // Failed/Unknown already use the existing failed signal and error status.
        // Software submission is deliberately not presented as paper completion.
        if (outcome != QStringLiteral("Submitted") && outcome != QStringLiteral("Cancelled")) return;
        reportHasOutcome_ = true;
        const QString message = outcome == QStringLiteral("Submitted")
            ? tr("打印任务已提交给本机打印系统：%1；实际出纸尚未确认。").arg(printerName)
            : tr("已取消打印，未提交打印任务。");
        reportStatus(message);
        qInfo().noquote() << "REPORT_NATIVE_PRINT" << outcome << printerName;
    });
    connect(reports_, &HistoryReportController::pdfExported, this,
            [this](const QString &path, int rows)
    {
        reportHasOutcome_ = true;
        reportStatus(tr("PDF已导出：%1行，%2").arg(rows).arg(path));
        qInfo().noquote() << "REPORT_PDF" << rows << path;
    });
    connect(reports_, &HistoryReportController::htmlExported, this,
            [this](const QString &path, int rows)
    {
        reportHasOutcome_ = true;
        reportStatus(tr("HTML已导出：%1行，%2").arg(rows).arg(path));
        qInfo().noquote() << "REPORT_HTML" << rows << path;
    });
    connect(reports_, &HistoryReportController::previewReady, this, []()
    {
        qInfo() << "REPORT_PREVIEW_READY";
    });
    return true;
}

void frmMain::updateReportHospitalName(const QString &name)
{
    committedHospitalName_ = name;
    if (!reports_) return;
    reportOptions_.hospitalName = name;
    reports_->setPrintOptions(reportOptions_);
}

void frmMain::reportStatus(const QString &message, bool error)
{
    if (!history_) return;
    history_->setProperty("lastStatus", message);
    emit history_->statusChanged(message);
    // The ordinary status receiver clears errors during in-flight work.
    // Apply this report outcome last, including failures while preview is open.
    historyStatus_ = message;
    historyStatusError_ = error;
    updateHistoryStatus();
    updateAccountStatus();
}

bool frmMain::requestHistoryLeave(const QString &action)
{
    if (!history_) return true;
    if (history_->requestLeave(action)) return true;
    emit navigationRejected(history_->isBusy()
        ? tr("历史数据正在处理，请稍后再%1。").arg(action)
        : tr("已继续编辑历史数据，未%1。").arg(action));
    return false;
}

void frmMain::restoreHistoryFocus()
{
    QTimer::singleShot(0, this, [this]()
    {
        if (!history_ || currentMainPage() != 2) return;
        QWidget *focus = lastHistoryFocus_.data();
        if (!focus || !focus->isVisible() || !focus->isEnabled())
            focus = history_->findChild<QTableWidget *>(QStringLiteral("tableResultReportOne"));
        if (focus && focus->isEnabled())
        {
            historyViewport_->ensureWidgetVisible(focus, 10, 10);
            focus->setFocus(Qt::OtherFocusReason);
        }
    });
}

void frmMain::updateHistoryStatus()
{
    if (!history_ || currentMainPage() != 2) return;
    const QString state = historyStatusError_ ? tr("脱机／失败")
        : (history_->isBusy() ? tr("脱机／处理中")
           : (history_->hasUnsavedChanges() ? tr("脱机／未保存")
              : (history_->isReady() ? tr("脱机／就绪") : tr("脱机／未知"))));
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(
        state, Qt::ElideRight, ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(historyStatusError_
        ? QStringLiteral("QLabel { color: #E83323; }")
        : QStringLiteral("QLabel { color: rgb(170,170,170); }"));
    ui->lblDivState->setToolTip(historyStatus_);
    ui->statusArea->setToolTip(tr("脱机；仪器状态未知。历史查询：%1").arg(historyStatus_));
}
