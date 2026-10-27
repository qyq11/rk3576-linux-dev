#include "frmmain.h"
#include "ui_frmmain.h"
#include "alarm_history/alarm_history_host.h"
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QTimer>

bool frmMain::enableAlarmHistory(AlarmHistory::Store *store, const QString &absolutePath)
{
    if (!store || alarmHistory_ || !QDir::isAbsolutePath(absolutePath)
        || absolutePath.contains(QChar::Null)) return false;
    alarmHistoryPath_ = absolutePath;
    alarmHistory_ = new AlarmHistoryHost(store, this, this);
    alarmHistory_->setObjectName(QStringLiteral("unifiedAlarmHistoryHost"));
    connect(alarmHistory_, &AlarmHistoryHost::statusChanged, this,
            [this](const QString &message, bool error)
    {
        ui->btnAbnormal->setToolTip(message + tr("；实时告警、严重度及设备处置未知。"));
        qInfo().noquote() << (error ? "ALARM_HISTORY_ERROR" : "ALARM_HISTORY_STATUS") << message;
        updateAccountStatus();
    });
    const auto retryPendingShutdown = [this]()
    {
        if (!alarmHistoryShutdownPending_ || alarmHistoryShutdownRetryQueued_) return;
        alarmHistoryShutdownRetryQueued_ = true;
        QTimer::singleShot(0, this, [this]()
        {
            alarmHistoryShutdownRetryQueued_ = false;
            if (!alarmHistoryShutdownPending_) return;
            // Clear this history-specific continuation before the other guards
            // may reject a draft discard; do not keep retrying their decision.
            if (!alarmHistory_ || !alarmHistory_->isOpen()) alarmHistoryShutdownPending_ = false;
            window()->close();
        });
    };
    connect(alarmHistory_, &AlarmHistoryHost::busyChanged, this,
            [this, retryPendingShutdown](bool busy)
    {
        updateAccountStatus();
        // A real warning ACK can become idle without starting Page::requestClose.
        // Retry the same pending exit; never auto-ACK or dismiss that warning.
        if (!busy) retryPendingShutdown();
    });
    connect(alarmHistory_, &AlarmHistoryHost::viewClosed, this, [this, retryPendingShutdown]()
    {
        qInfo() << "ALARM_HISTORY_VIEW_CLOSED";
        updateAccountStatus();
        retryPendingShutdown();
    });
    // storageClosed is deliberately not wired to thread.quit(): error recovery
    // closes a source while the same view/worker remains available for reopen.
    connect(ui->btnAbnormal, &CusBadgeButton::clicked, this, [this]()
    {
        if (!alarmHistoryAvailable_ || rejectLoginNavigation() || logoutPending_) return;
        alarmHistory_->showExistingHistory(alarmHistoryPath_, QDate::currentDate());
    });
    setAlarmHistoryAvailable(false);
    return true;
}

void frmMain::setAlarmHistoryAvailable(bool available)
{
    alarmHistoryAvailable_ = alarmHistory_ && available;
    ui->btnAbnormal->setEnabled(alarmHistoryAvailable_);
    // CusBadgeButton's existing "0" sentinel hides its badge; never show a
    // historical row count or claim an active alarm count/severity here.
    ui->btnAbnormal->setText(QStringLiteral("0"));
    ui->btnAbnormal->setToolTip(alarmHistoryAvailable_
        ? tr("只读打开显式本地告警历史；文件不存在/不兼容会明确失败，不创建空历史。实时告警及设备处置未知。")
        : tr("本地历史入口尚未启用或worker未就绪；实时告警及设备处置未知。"));
}

bool frmMain::alarmHistoryIdle() const
{
    return !alarmHistory_ || !alarmHistory_->isBusy();
}

bool frmMain::requestAlarmHistoryLeave(const QString &action)
{
    if (!alarmHistory_ || alarmHistory_->requestLeave(action)) return true;
    emit navigationRejected(tr("本地历史仍在关闭/等待提示确认，请在只读来源和窗口释放后再%1。").arg(action));
    return false;
}
