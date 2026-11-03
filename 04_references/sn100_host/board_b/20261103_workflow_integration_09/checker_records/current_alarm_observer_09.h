#ifndef SN100_CURRENT_ALARM_OBSERVER_09_H
#define SN100_CURRENT_ALARM_OBSERVER_09_H

// 仅供09同版检查器使用。基础24项结束且连接尚未关闭时调用一次run(shell)。
// 只点击原顶部入口、读取真实信号和当前表；不注入报文，不调用私有查询或保存接口。
#include "shell/frmmain.h"
#include "shell/widgets/cusbadgebutton.h"
#include "alarm_history/alarm_history_host.h"
#include "alarm_history/original_current_alarm_state.h"
#include "instrument/device_control.h"
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <functional>

namespace CurrentAlarmObserver09
{
inline bool waitUntil(const std::function<bool()> &ready, int milliseconds)
{
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll, deadline;
    poll.setInterval(10);
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (ready()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);
    poll.start(); deadline.start(milliseconds);
    loop.exec();
    return ready();
}

inline QJsonObject stateJson(const AlarmHistory::CurrentAlarmQueryState &state, qint64 milliseconds)
{
    return {{"elapsed_ms", static_cast<double>(milliseconds)},
        {"connection", QString::number(state.connection)}, {"connected", state.connected},
        {"valid_observation", state.valid}, {"refresh_pending", state.refreshPending},
        {"refresh_queued", state.refreshQueued}, {"local_refresh", QString::number(state.refreshId)},
        {"message", state.message}};
}

// 只解析实际TX块。Qj/Qk是单片短查询；遇到其他形状保留原hex并拒绝自动判通过。
inline bool actualSingleFramePayload(const QByteArray &bytes, QByteArray *payload)
{
    const auto byte = [&](int index) { return static_cast<unsigned char>(bytes.at(index)); };
    if (bytes.size() < 12 || byte(0) != 2 || byte(1) != 3) return false;
    const int length = (byte(2) << 8) | byte(3);
    if (length <= 4 || bytes.size() != length + 7 || byte(length + 5) != 4 || byte(length + 6) != 5
        || byte(4) != 0 || byte(5) != 0 || byte(6) != 0 || byte(7) != 1) return false;
    int checksum = 0;
    for (int index = 4; index < length + 4; ++index) checksum ^= byte(index);
    if (checksum != byte(length + 4)) return false;
    *payload = bytes.mid(8, length - 4);
    return true;
}

inline bool takeText(qint64 bytes, qint64 *remaining, bool *complete)
{
    if (bytes < 0 || bytes > *remaining) { *complete = false; return false; }
    *remaining -= bytes;
    return true;
}

inline QJsonObject rowJson(const AlarmHistory::CurrentAlarmRow &row)
{
    return {{"error_code", row.errorCode}, {"date_time", row.dateTime},
        {"description", row.description}, {"measure", row.measure}};
}

inline QJsonArray observationRows(const AlarmHistory::CurrentAlarmRows &rows, qint64 *remaining, bool *complete)
{
    QJsonArray result;
    for (const auto &row : rows)
    {
        const qint64 bytes = row.errorCode.toUtf8().size() + row.dateTime.toUtf8().size()
            + row.description.toUtf8().size() + row.measure.toUtf8().size();
        if (!takeText(bytes, remaining, complete)) break;
        result.append(rowJson(row));
    }
    return result;
}

inline QJsonObject pageJson(AlarmHistoryPage *page, qint64 *remaining, bool *complete)
{
    QJsonObject result{{"exists", page != nullptr}, {"capture_complete", true}};
    if (!page) return result;
    auto *table = page->findChild<QTableWidget *>(QStringLiteral("tblAbnormal"));
    auto *stack = page->findChild<QStackedWidget *>(QStringLiteral("stackedWidget"));
    result["table_exists"] = table != nullptr;
    result["closing"] = page->isClosing(); result["busy"] = page->isBusy();
    result["current_page_visible"] = table && table->isVisibleTo(page);
    result["stacked_index"] = stack ? stack->currentIndex() : -1;
    QJsonArray rows;
    if (table)
    {
        result["row_count"] = table->rowCount();
        for (int index = 0; index < table->rowCount(); ++index)
        {
            const auto text = [&](int column)
            { auto *item = table->item(index, column); return item ? item->text() : QString(); };
            auto *first = table->item(index, 0);
            const AlarmHistory::CurrentAlarmRow row{text(0), text(2),
                first ? first->data(Qt::UserRole).toString() : QString(), text(1)};
            const qint64 bytes = row.errorCode.toUtf8().size() + row.dateTime.toUtf8().size()
                + row.description.toUtf8().size() + row.measure.toUtf8().size();
            if (!takeText(bytes, remaining, complete)) { result["capture_complete"] = false; break; }
            rows.append(rowJson(row));
        }
    }
    result["rows"] = rows;
    return result;
}

inline bool pageMatchesObservations(const QJsonObject &page, const QJsonArray &observations)
{
    if (!page.value("table_exists").toBool() || !page.value("capture_complete").toBool()
        || observations.isEmpty()) return false;
    const auto actual = page.value("rows").toArray();
    QSet<QString> descriptions;
    for (const auto &value : actual)
    {
        const auto row = value.toObject();
        const QString description = row.value("description").toString();
        if (descriptions.contains(description)) return false;
        descriptions.insert(description);
        bool found = false;
        for (const auto &event : observations)
            for (const auto &known : event.toObject().value("rows").toArray())
                if (known.toObject() == row) found = true;
        if (!found) return false;
    }
    // 原活动窗口按说明去重保留旧行，不能错误要求当前表恰好等于最后一批。
    for (const auto &value : observations.last().toObject().value("rows").toArray())
        if (!descriptions.contains(value.toObject().value("description").toString())) return false;
    return true;
}

inline QJsonObject run(frmMain *shell)
{
    QJsonObject result{{"status", "pending"}, {"evaluated", false}, {"passed", false},
        {"triggered", false}, {"query_queued", false}, {"page_opened", false}, {"page_closed", false},
        {"qk_observed", false}, {"qk_count", 0}, {"qK_count", 0}, {"generation", QString()},
        {"reason", QString()}, {"local_timeout_state_event_observed", false},
        {"exact_request_reply_correlation_proven", false}, {"all_instrument_healthy", false},
        {"full_run_tx_independently_checked_here", false}, {"physical_screen_verified", false},
        {"history_database_unchanged_claimed", false}};
    if (!shell || QThread::currentThread() != shell->thread())
    { result["reason"] = QStringLiteral("没有GUI线程中的真实主窗；未触发查询。"); return result; }
    if (shell->property("review09CurrentAlarmPhaseStarted").toBool())
    { result["reason"] = QStringLiteral("本主窗已进入过本检查；拒绝再次触发Qk。"); return result; }
    shell->setProperty("review09CurrentAlarmPhaseStarted", true);
    const auto observers = shell->findChildren<AlarmHistory::OriginalCurrentAlarmState *>(QString(), Qt::FindDirectChildrenOnly);
    const auto hosts = shell->findChildren<AlarmHistoryHost *>(QStringLiteral("unifiedAlarmHistoryHost"), Qt::FindDirectChildrenOnly);
    auto *button = shell->findChild<CusBadgeButton *>(QStringLiteral("btnAbnormal"));
    QPointer<frmMain> owner(shell);
    QPointer<DeviceControl> control(shell->deviceControl());
    QPointer<AlarmHistory::OriginalCurrentAlarmState> observer(observers.size() == 1 ? observers.first() : nullptr);
    QPointer<AlarmHistoryHost> host(hosts.size() == 1 ? hosts.first() : nullptr);
    if (!control || !observer || !host || !button)
    { result["status"] = "fail"; result["evaluated"] = true; result["reason"] = QStringLiteral("正式告警观察对象、宿主或原顶部按钮缺失/不唯一。"); return result; }
    const quint64 generation = control->connectionGeneration();
    result["generation"] = QString::number(generation);
    if (host->isOpen())
    {
        // 不在已有告警窗口上补发查询；仍尝试原关闭按钮，避免把禁用主窗交给finishReview死等。
        result["page_preexisting"] = true;
        result["page_opened"] = true;
        QObject cleanupContext; int closedSignals = 0;
        QObject::connect(host.data(), &AlarmHistoryHost::viewClosed, &cleanupContext, [&]() { ++closedSignals; });
        auto *page = host->page();
        auto *close = page ? page->findChild<QPushButton *>(QStringLiteral("btnClose")) : nullptr;
        if (close && close->isEnabled() && !page->isClosing()) close->click();
        waitUntil([&]() { return host && !host->isOpen() && closedSignals > 0; }, 15000);
        const bool closed = host && !host->isOpen() && closedSignals > 0;
        if (closed) waitUntil([&]() { return !owner || owner->isEnabled(); }, 2000);
        result["page_closed"] = closed;
        result["view_closed_signal_count"] = closedSignals;
        result["shell_enabled_after_close"] = owner && owner->isEnabled();
        result["reason"] = QStringLiteral("进入检查前已有告警窗口，本次未发Qk；仅按原关闭流程收尾，查询仍待验。");
        return result;
    }
    const auto initial = control->state();
    if (!generation || !initial.connected || !initial.fresh || initial.status.ledStatus == 0
        || !shell->isEnabled() || !button->isEnabled() || !button->isVisibleTo(shell)
        || observer->queryState().refreshPending)
    { result["reason"] = QStringLiteral("实际连接、原入口或空闲告警窗口条件未满足；未触发Qk。"); return result; }

    QObject context;
    QElapsedTimer clock; clock.start();
    QJsonArray states, payloads, observations, txBlocks;
    qint64 remaining = 2 * 1024 * 1024;
    bool captureComplete = true, timeoutObserved = false, qKWaitEvaluated = false, qKWaitPassed = true;
    bool observing = true;
    bool displayRejected = false;
    int qkCount = 0, qKCount = 0, rawQkCount = 0, rawQKCount = 0, qkTx = 0, qjTx = 0;
    int beforeQueryQk = 0, beforeQueryQK = 0;
    int otherTx = 0, invalidTx = 0, foreignCurrentPayloads = 0, newEventPayloads = 0, viewClosed = 0, buttonClicks = 0;
    qint64 qkQueuedAt = -1;
    quint64 refresh = 0;
    const quint64 previousRefresh = observer->queryState().refreshId;
    result["query_state_before"] = stateJson(observer->queryState(), 0);
    QObject::connect(button, &CusBadgeButton::clicked, &context, [&]() { ++buttonClicks; });
    QObject::connect(host.data(), &AlarmHistoryHost::viewClosed, &context, [&]() { ++viewClosed; });
    QObject::connect(host.data(), &AlarmHistoryHost::currentDisplayRejected, &context,
        [&](const QString &) { displayRejected = true; });
    QObject::connect(observer.data(), &AlarmHistory::OriginalCurrentAlarmState::stateChanged, &context,
        [&](AlarmHistory::CurrentAlarmQueryState state)
    {
        if (!observing) return;
        if (state.connection == generation && state.refreshId != previousRefresh && state.refreshPending)
            refresh = state.refreshId;
        if (state.connection == generation && refresh && state.refreshId == refresh && !state.refreshPending
            && state.message.contains(QStringLiteral("当前告警查询等待结束"))) timeoutObserved = true;
        if (states.size() < 128 && takeText(state.message.toUtf8().size(), &remaining, &captureComplete))
            states.append(stateJson(state, clock.elapsed()));
        else captureComplete = false;
    });
    QObject::connect(observer.data(), &AlarmHistory::OriginalCurrentAlarmState::currentObserved, &context,
        [&](AlarmHistory::CurrentAlarmObservation observation)
    {
        if (!observing || !observer || observation.connection != generation) return;
        const auto current = observer->queryState();
        const bool afterQueryTx = qkTx > 0;
        if (observation.command == "Qk") { if (afterQueryTx) ++qkCount; else ++beforeQueryQk; }
        else if (observation.command == "QK")
        {
            if (afterQueryTx) ++qKCount; else ++beforeQueryQK;
            if (afterQueryTx && refresh && qkCount == 0 && !timeoutObserved
                && qkQueuedAt >= 0 && clock.elapsed() - qkQueuedAt < 10000)
            {
                qKWaitEvaluated = true;
                qKWaitPassed = qKWaitPassed && current.refreshId == refresh && current.refreshPending;
            }
        }
        if (observations.size() >= 32) { captureComplete = false; return; }
        const auto rows = observationRows(observation.rows, &remaining, &captureComplete);
        observations.append(QJsonObject{{"command", QString::fromLatin1(observation.command)},
            {"after_explicit_qk_tx", afterQueryTx},
            {"connection", QString::number(observation.connection)}, {"elapsed_ms", static_cast<double>(clock.elapsed())},
            {"received_at", observation.receivedAt.toString(Qt::ISODateWithMs)},
            {"row_count", observation.rows.size()}, {"rows", rows},
            {"query_state_after_observation", stateJson(current, clock.elapsed())}});
    });
    QObject::connect(control.data(), &DeviceControl::payloadReceivedForConnection, &context,
        [&](QByteArray payload, quint64 connection)
    {
        if (!observing) return;
        const auto command = payload.left(2);
        if (command == "Ql" || command == "QL") { ++newEventPayloads; return; }
        if (command != "Qk" && command != "QK") return;
        if (connection != generation) { ++foreignCurrentPayloads; return; }
        if (command == "Qk") ++rawQkCount; else ++rawQKCount;
        if (payloads.size() >= 32 || !takeText(3 * qint64(payload.size()), &remaining, &captureComplete))
        { captureComplete = false; return; }
        payloads.append(QJsonObject{{"command", QString::fromLatin1(command)},
            {"after_explicit_qk_tx", qkTx > 0},
            {"connection", QString::number(connection)}, {"elapsed_ms", static_cast<double>(clock.elapsed())},
            {"payload_hex", QString::fromLatin1(payload.toHex(' '))}});
    });
    QObject::connect(control.data(), &DeviceControl::wireActivity, &context,
        [&](QString direction, QByteArray bytes)
    {
        if (!direction.startsWith(QStringLiteral("TX"))) return;
        QByteArray payload;
        if (!actualSingleFramePayload(bytes, &payload)) ++invalidTx;
        else if (payload == "Qk") { ++qkTx; if (qkQueuedAt < 0) qkQueuedAt = clock.elapsed(); }
        else if (payload == "Qj") ++qjTx;
        else ++otherTx;
        if (txBlocks.size() >= 64 || !takeText(3 * qint64(bytes.size()), &remaining, &captureComplete))
        { captureComplete = false; return; }
        txBlocks.append(QJsonObject{{"direction", direction}, {"generation", QString::number(control->connectionGeneration())},
            {"elapsed_ms", static_cast<double>(clock.elapsed())}, {"hex", QString::fromLatin1(bytes.toHex(' '))}});
    });

    // 这对QA边界只记真实操作的连接号，供事后把原24项Qj区间与本区间分开。
    qInfo().noquote() << "REVIEW_CURRENT_ALARM_QUERY_BEGIN" << "generation=" << QString::number(generation);
    result["triggered"] = true;
    const QPointF point(button->rect().center());
    QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(button, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(button, &release);
    result["page_opened"] = host && host->isOpen();
    if (observer) result["query_state_after_click"] = stateJson(observer->queryState(), clock.elapsed());
    const bool queued = qkTx == 1 && refresh != 0;
    result["query_queued"] = queued;
    if (queued)
        waitUntil([&]()
        {
            return !owner || !control || !observer || !control->state().connected
                || control->connectionGeneration() != generation || qkCount > 0 || timeoutObserved;
        }, 11000);
    const bool sameConnectionBeforeDisplay = control && control->state().connected && control->connectionGeneration() == generation;
    const qint64 observedMilliseconds = clock.elapsed();
    QJsonObject page;
    bool pageMatch = false;
    if (qkCount > 0 && sameConnectionBeforeDisplay && captureComplete && !displayRejected && newEventPayloads == 0)
        pageMatch = waitUntil([&]()
        {
            // 轮询副本各有独立预算，不因等待绘制而把同一表反复记入总预算。
            qint64 tableBudget = remaining; bool tableComplete = true;
            page = pageJson(host ? host->page() : nullptr, &tableBudget, &tableComplete);
            return tableComplete && pageMatchesObservations(page, observations);
        }, 3000);
    if (page.isEmpty()) page = pageJson(host ? host->page() : nullptr, &remaining, &captureComplete);
    if (observer) result["query_state_end_of_observation"] = stateJson(observer->queryState(), clock.elapsed());
    const bool sameConnection = control && control->state().connected && control->connectionGeneration() == generation;
    observing = false;
    result["payload_and_page_window_end_ms"] = static_cast<double>(clock.elapsed());

    // btnReturn仅切页签。btnClose才走原requestClose/SQL释放/动画/销毁链。
    bool closeClicked = false;
    const int beforeClose = viewClosed;
    if (host && host->page())
    {
        auto *close = host->page()->findChild<QPushButton *>(QStringLiteral("btnClose"));
        if (close && close->isEnabled() && !host->page()->isClosing())
        { closeClicked = true; close->click(); }
        waitUntil([&]() { return host && !host->isOpen() && viewClosed > beforeClose; }, 15000);
    }
    const bool closed = host && !host->isOpen() && viewClosed > 0;
    if (closed) waitUntil([&]() { return !owner || owner->isEnabled(); }, 2000);
    pageMatch = pageMatch && sameConnection && captureComplete && !displayRejected && newEventPayloads == 0;
    result["page_closed"] = closed;
    result["top_button_clicked_signal_count"] = buttonClicks;
    result["close_button_clicked"] = closeClicked;
    result["view_closed_signal_count"] = viewClosed;
    result["shell_enabled_after_close"] = owner && owner->isEnabled();
    result["page_snapshot_before_close"] = page;
    result["page_correspondence_evaluated"] = qkCount > 0 && sameConnection && newEventPayloads == 0 && captureComplete;
    result["page_correspondence_passed"] = pageMatch;
    result["page_correspondence_scope"] = QStringLiteral("表内每行来自本区间真实观察，最后一批各说明均已显示；按原活动窗口去重保留规则，不要求每包替换。新事件混入时不自动判显示通过。");
    result["qk_observed"] = qkCount > 0;
    result["qk_count"] = qkCount; result["qK_count"] = qKCount;
    result["observation_count_scope"] = QStringLiteral("qk_count/qK_count仅计本连接明确Qk入队之后的完整观察；此前真实观察另记，不当作查询回包。");
    result["pre_query_qk_observation_count"] = beforeQueryQk;
    result["pre_query_qK_observation_count"] = beforeQueryQK;
    result["actual_payload_qk_count"] = rawQkCount; result["actual_payload_qK_count"] = rawQKCount;
    result["foreign_generation_current_payload_count"] = foreignCurrentPayloads;
    result["new_event_payload_count"] = newEventPayloads;
    result["tx_qk_count"] = qkTx; result["tx_qj_count"] = qjTx;
    result["explicit_qk_tx_elapsed_ms"] = static_cast<double>(qkQueuedAt);
    result["tx_other_count"] = otherTx; result["tx_unparsed_block_count"] = invalidTx;
    result["local_timeout_state_event_observed"] = timeoutObserved;
    result["observation_wait_ms"] = static_cast<double>(observedMilliseconds);
    result["qK_non_confirmation_evaluated"] = qKWaitEvaluated;
    result["qK_non_confirmation_passed"] = qKWaitEvaluated && qKWaitPassed;
    result["same_connection_at_observation_end"] = sameConnection;
    result["capture_complete"] = captureComplete;
    result["actual_payloads"] = payloads; result["current_observations"] = observations;
    result["query_states"] = states; result["actual_tx_blocks"] = txBlocks;
    const bool displayMismatch = qkCount > 0 && sameConnection && captureComplete
        && newEventPayloads == 0 && !displayRejected && !pageMatch;
    result["display_mismatch_after_wait"] = displayMismatch;
    const bool contradiction = qkTx > 1 || buttonClicks > 1 || otherTx > 0 || invalidTx > 0
        || !qKWaitPassed || displayRejected || displayMismatch;
    const bool passed = queued && sameConnection && qkCount > 0 && pageMatch && captureComplete
        && newEventPayloads == 0 && !contradiction && closed && owner && owner->isEnabled();
    result["evaluated"] = passed || contradiction;
    result["passed"] = passed;
    result["status"] = passed ? "pass" : contradiction ? "fail" : "pending";
    result["reason"] = passed ? QStringLiteral("已通过原入口排队一次Qk，采用同连接真实完整观察并核当前表，原关闭链已结束；未证明请求精确关联或整机健康。")
        : displayMismatch ? QStringLiteral("同连接已收到完整Qk、证据完整且无新事件混入，等待绘制后当前表仍不对应；按实际显示矛盾记录失败。")
        : contradiction ? QStringLiteral("实际发送、QK等待或显示拒绝出现矛盾；保留原信号与表格，不补造成功。")
        : !queued ? QStringLiteral("原入口没有观察到唯一Qk排队，查询待验；未直接调用私有查询。")
        : !closed ? QStringLiteral("原告警窗口尚未完成正常关闭；未隐藏、确认警告或绕过SQL收尾。")
        : !sameConnection ? QStringLiteral("观察期间连接改变或断开；未使用新代或旧代数据补充通过。")
        : qkCount == 0 ? QStringLiteral("窗口内未采用真实完整Qk；本地等待提示单列，未重发，设备回包待验。")
        : QStringLiteral("真实消息已记录，但当前表对应或有界证据不足，显示与关闭结果按字段保留待验。");
    qInfo().noquote() << "REVIEW_CURRENT_ALARM_QUERY_END" << "generation=" << QString::number(generation);
    return result;
}
}
#endif
