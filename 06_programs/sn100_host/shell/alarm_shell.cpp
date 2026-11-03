#include "frmmain.h"
#include "ui_frmmain.h"
#include "alarm_history/alarm_history_host.h"
#include "alarm_history/original_alarm_receiver.h"
#include "instrument/device_control.h"
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

bool frmMain::enableOriginalAlarmReceiver(AlarmHistory::OriginalAlarmReceiver *receiver)
{
    if (!receiver || originalAlarmReceiver_ || !deviceControl_) return false;
    originalAlarmReceiver_ = receiver;
    connect(deviceControl_, &DeviceControl::payloadReceived, this, [this](const QByteArray &payload)
    {
        if (!originalAlarmReceiver_) return;
        // 原userRoleName存的是登录账户名；接收时捕获，SQL排队期间注销不改归属。
        const QString user = session_.name.isEmpty() ? QStringLiteral("Guest") : session_.name;
        originalAlarmReceiver_->receivePayload(payload, user, QDateTime::currentDateTime());
    });
    connect(receiver, &AlarmHistory::OriginalAlarmReceiver::newEventsReceived, this,
            [](quint64 requestId, const AlarmHistory::Rows &rows)
    {
        qInfo() << "ORIGINAL_ALARM_NEW_EVENTS" << requestId << "rows=" << rows.size()
                << "history_saved=false";
    });
    connect(receiver, &AlarmHistory::OriginalAlarmReceiver::historyCommitted, this,
            [this](quint64 requestId, const AlarmHistory::Rows &rows)
    {
        qInfo() << "ORIGINAL_ALARM_HISTORY_COMMITTED" << requestId << "rows=" << rows.size();
        if (alarmHistory_) alarmHistory_->originalEventsCommitted(rows);
    });
    connect(receiver, &AlarmHistory::OriginalAlarmReceiver::historyFailed, this,
            [](quint64 requestId, const AlarmHistory::Rows &rows, bool submitted, const QString &message)
    {
        qWarning().noquote() << "ORIGINAL_ALARM_HISTORY_FAILED" << requestId
            << "rows=" << rows.size() << "submitted_to_store=" << submitted << message;
    });
    connect(receiver, &AlarmHistory::OriginalAlarmReceiver::recoveryFinished, this,
            [](quint64 requestId, const QString &savedFile, const QString &error)
    {
        if (!error.isEmpty())
            qCritical().noquote() << "ORIGINAL_ALARM_RECOVERY_SAVE_FAILED" << requestId << error;
        else
            qWarning().noquote() << "ORIGINAL_ALARM_RECOVERY_SAVED" << requestId << savedFile
                                << "automatic_replay=false";
    });
    connect(receiver, &AlarmHistory::OriginalAlarmReceiver::storageError, this,
            [this](const QString &message)
    {
        ui->btnAbnormal->setToolTip(message);
        emit navigationRejected(message);
    });
    connect(receiver, &AlarmHistory::OriginalAlarmReceiver::stopped, this, [this]()
    {
        if (!originalAlarmShutdownPending_) return;
        originalAlarmShutdownPending_ = false;
        QTimer::singleShot(0, this, [this]() { window()->close(); });
    });
    return true;
}

bool frmMain::requestOriginalAlarmShutdown()
{
    if (!originalAlarmReceiver_ || originalAlarmReceiver_->isStopped()) return true;
    if (!originalAlarmShutdownPending_)
    {
        originalAlarmShutdownPending_ = true;
        // 之前所有草稿守卫已通过；停止新设备输入后，等已有批次确认才退出事件循环。
        setEnabled(false);
        if (deviceControl_) deviceControl_->close();
        originalAlarmReceiver_->requestStop();
    }
    return false;
}

bool frmMain::originalAlarmShutdownComplete() const
{
    return !originalAlarmReceiver_ || originalAlarmReceiver_->isStopped();
}
