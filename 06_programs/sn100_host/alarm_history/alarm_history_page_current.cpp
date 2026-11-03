#include "alarm_history_page.h"
#include "ui_maskdlgabnormal.h"
#include "ui/cusdialogmask.h"
#include "ui/cusmessagebox.h"
#include "qtmaterialscrollbar.h"
#include <QLabel>
#include <QPushButton>
#include <QScroller>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>

namespace
{
// 本地显示容量候选，与历史页同量级；不是原设备规格或已验收资源预算。
constexpr int maximumCurrentRows = 10000;
constexpr qint64 maximumCurrentBytes = 16 * 1024 * 1024;
constexpr int currentRowsPerTurn = 32;
qint64 rowBytes(const AlarmHistory::CurrentAlarmRow &row)
{
    return qint64(row.errorCode.toUtf8().size()) + row.dateTime.toUtf8().size()
        + row.description.toUtf8().size() + row.measure.toUtf8().size();
}
}

void AlarmHistoryPage::enableCurrentAlarms(QString historyPath, QDate today)
{
    if (currentEnabled_ || closingDialog_) return;
    currentEnabled_ = true;
    if (!historySourceRequested_)
    {
        sourcePath_ = historyPath;
        today_ = today;
        ui->ldtStartDate->setText(today.toString(QStringLiteral("yyyy/MM/dd")));
        ui->ldtEndDate->setText(today.toString(QStringLiteral("yyyy/MM/dd")));
    }
    auto *table = ui->tblAbnormal;
    table->setEnabled(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::scroller(table)->grabGesture(table, QScroller::LeftMouseButtonGesture);
    auto *scrollbar = new QtMaterialScrollBar(table);
    scrollbar->setSliderColor(QColor(34,34,34,50));
    scrollbar->setBackgroundColor(Qt::transparent);
    scrollbar->setCanvasColor(Qt::transparent);
    scrollbar->setHideOnMouseOut(true);
    table->setVerticalScrollBar(scrollbar);
    // 原版没有选行后填措施的调用，保留空白区域与原三列，不另造等级或操作。
    ui->txtTestInfo->setEnabled(true);
    ui->txtTestInfo->setReadOnly(true);
    ui->txtTestInfo->clear();
    ui->txtTestInfo->setToolTip(QString());
    ui->btnReset->setEnabled(false);
    ui->btnReset->setToolTip(tr("复位涉及任务和设备动作，本轮只读查询未接入。"));
    setCurrentStatus(tr("尚未收到本连接的完整当前告警消息。"), false);
    updateControls();
}

void AlarmHistoryPage::renderStatus()
{
    const bool current = currentEnabled_ && ui->stackedWidget->currentIndex() == 0;
    QString text = current ? currentStatus_ : status_;
    if (current && currentPendingIndex_ < currentPending_.size())
        text += tr(" 正在分批显示；表内保留本次打开期间按说明去重的记录。");
    state_->setText(state_->fontMetrics().elidedText(text, Qt::ElideRight, state_->width()));
    state_->setToolTip(text);
    state_->setAccessibleName(text);
    // 沿用既有顶部状态区，不改变原颜色和布局；错误与来源说明保留全文提示。
    state_->setProperty("statusError", current ? currentError_ : historyError_);
    setWindowTitle(text);
}

void AlarmHistoryPage::setCurrentStatus(QString message, bool error)
{
    currentStatus_ = message;
    currentError_ = error;
    ui->tblAbnormal->setToolTip(message);
    renderStatus();
}

void AlarmHistoryPage::clearCurrentDisplay()
{
    ++currentDisplayGeneration_;
    currentAppendQueued_ = false;
    currentPending_.clear();
    currentPendingIndex_ = 0;
    currentDescriptions_.clear();
    currentRetainedBytes_ = 0;
    QSignalBlocker blocker(ui->tblAbnormal);
    ui->tblAbnormal->setRowCount(0);
}

void AlarmHistoryPage::showCurrentRows(QVector<AlarmHistory::CurrentAlarmRow> rows,
                                      QString message, bool activeBeforeDelivery,bool error)
{
    if (!currentEnabled_ || closingDialog_ || forceClose_) return;
    QVector<AlarmHistory::CurrentAlarmRow> additions;
    QSet<QString> descriptions = activeBeforeDelivery ? currentDescriptions_ : QSet<QString>();
    qint64 bytes = activeBeforeDelivery ? currentRetainedBytes_ : 0;
    for (const auto &row : rows)
    {
        // 原refreshAbnormalTable按说明文字去重；相同说明的不同编号也只显示首条。
        if (descriptions.contains(row.description)) continue;
        descriptions.insert(row.description);
        bytes += rowBytes(row);
        additions.append(row);
    }
    if (descriptions.size() > maximumCurrentRows || bytes > maximumCurrentBytes)
    {
        const QString reason = tr("当前列表超过本地10000行或16MiB显示容量，本批未采用；旧记录仅供核对，当前状态待核。");
        setCurrentStatus(reason, true);
        emit currentDisplayRejected(reason);
        return;
    }
    // 必须使用接收前捕获的活动状态；show/raise会改变它，不能到这里再读。
    if (!activeBeforeDelivery)
    {
        clearCurrentDisplay();
        ui->stackedWidget->setCurrentIndex(0);
    }
    currentDescriptions_ = descriptions;
    currentRetainedBytes_ = bytes;
    currentPending_ += additions;
    setCurrentStatus(message + tr(" 表内按原规则保留本次打开期间的记录，不代表所有行此刻仍存在。"), error);
    appendCurrentLater();
    if (!activeBeforeDelivery && !warning_ && !calendar_)
    {
        show();
        ui->mask->showAnimation();
    }
}

void AlarmHistoryPage::appendCurrentLater()
{
    if (closingDialog_ || currentAppendQueued_ || currentPendingIndex_ >= currentPending_.size()) return;
    currentAppendQueued_ = true;
    const quint64 generation = currentDisplayGeneration_;
    QTimer::singleShot(0, this, [this, generation]()
    {
        if (generation != currentDisplayGeneration_) return;
        currentAppendQueued_ = false;
        appendCurrentTurn();
    });
}

void AlarmHistoryPage::appendCurrentTurn()
{
    if (closingDialog_ || forceClose_) return;
    auto *table = ui->tblAbnormal;
    const QSignalBlocker blocker(table);
    int added = 0;
    while (currentPendingIndex_ < currentPending_.size() && added++ < currentRowsPerTurn)
    {
        const auto &row = currentPending_.at(currentPendingIndex_++);
        const int index = table->rowCount();
        table->insertRow(index);
        const QStringList texts = {row.errorCode, row.dateTime, row.description};
        for (int column = 0; column < texts.size(); ++column)
        {
            auto *item = new QTableWidgetItem(texts.at(column));
            QFont font=table->font();
#ifdef Q_OS_LINUX
            font.setFamily(QStringLiteral("Noto Sans CJK SC"));
#else
            font.setFamily(QStringLiteral("苹方 粗体"));
#endif
            font.setPointSize(13);
            item->setFont(font);
            item->setTextAlignment((column == 2 ? Qt::AlignLeft : Qt::AlignHCenter) | Qt::AlignVCenter);
            if (column == 0) item->setData(Qt::UserRole, row.measure);
            table->setItem(index, column, item);
        }
        table->setRowHeight(index, 41);
    }
    if (currentPendingIndex_ == currentPending_.size())
    {
        currentPending_.clear();
        currentPendingIndex_ = 0;
    }
    renderStatus();
    appendCurrentLater();
}

void AlarmHistoryPage::showHistoricalRecords()
{
    if (!currentEnabled_ || isBusy() || closingDialog_) return;
    currentHistoryRefreshPending_=false;
    ui->stackedWidget->setCurrentIndex(1);
    const QDate today = QDate::currentDate();
    if (!sourceReady_)
    {
        openExistingHistory(sourcePath_, today);
        return;
    }
    // 原“日志记录”重置为今天最近20条；手动日期查询仍保留原完整区间入口。
    today_ = today;
    ui->ldtStartDate->setText(today.toString(QStringLiteral("yyyy/MM/dd")));
    ui->ldtEndDate->setText(today.toString(QStringLiteral("yyyy/MM/dd")));
    beginQuery(AlarmHistory::Scope::LatestToday20);
}

void AlarmHistoryPage::queueCurrentHistoryRefresh()
{
    if(!currentEnabled_||closingDialog_)return;
    currentHistoryRefreshPending_=true;
    refreshCurrentHistoryWhenIdle();
}

void AlarmHistoryPage::refreshCurrentHistoryWhenIdle()
{
    if(!currentHistoryRefreshPending_||currentHistoryRefreshQueued_||closingDialog_)return;
    currentHistoryRefreshQueued_=true;
    QTimer::singleShot(0,this,[this]()
    {
        currentHistoryRefreshQueued_=false;
        if(!currentHistoryRefreshPending_||closingDialog_||isBusy())return;
        currentHistoryRefreshPending_=false;
        const QDate today=QDate::currentDate();
        if(!sourceReady_)
        {
            // 原当前槽也会读今天最近20条；异步开库不切走用户正在看的当前页。
            openExistingHistory(sourcePath_,today,false);
            return;
        }
        today_=today;
        ui->ldtStartDate->setText(today.toString(QStringLiteral("yyyy/MM/dd")));
        ui->ldtEndDate->setText(today.toString(QStringLiteral("yyyy/MM/dd")));
        beginQuery(AlarmHistory::Scope::LatestToday20);
    });
}
