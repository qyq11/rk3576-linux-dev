#include "original_current_alarm_state.h"
#include "protocol/read_only_messages.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QPointer>
#include <QThread>
#include <limits>
#include <utility>

namespace AlarmHistory
{
namespace
{
// 与当前页面共用16MiB本地文本候选上限；不是原协议或已获实测认可的资源门槛。
constexpr qint64 maximumObservationBytes = 16 * 1024 * 1024;

QString substituteDescription(QString description, const QStringList &parameters)
{
    // 原MaskDlgAbnormal::convertAlarm只展开1至5个非空参数，其他数量保留配置原文。
    switch (parameters.size())
    {
    case 1: return description.arg(parameters[0]);
    case 2: return description.arg(parameters[0], parameters[1]);
    case 3: return description.arg(parameters[0], parameters[1], parameters[2]);
    case 4: return description.arg(parameters[0], parameters[1], parameters[2], parameters[3]);
    case 5: return description.arg(parameters[0], parameters[1], parameters[2], parameters[3], parameters[4]);
    default: return description;
    }
}

bool displayRows(const Sn100Protocol::ReadOnlyMessage &message,
                 const QVector<OriginalRuntime::AlarmRule> &rules,
                 const QDateTime &receivedAt, CurrentAlarmRows *result, QString *error)
{
    const bool fromBoard = message.command == "QK";
    const QJsonArray input = message.data.value(QStringLiteral("alarms")).toArray();
    CurrentAlarmRows rows;
    rows.reserve(input.size());
    qint64 textBytes = 0;
    for (const QJsonValue &value : input)
    {
        const QJsonObject alarm = value.toObject();
        const QString code = alarm.value(QStringLiteral("error_code")).toString();
        CurrentAlarmRow row;
        row.errorCode = code + (fromBoard ? QStringLiteral("*") : QString());
        row.dateTime = receivedAt.toString(QStringLiteral("yyyy/MM/dd HH:mm:ss"));
        if (fromBoard)
        {
            // 原convertAbnormal直接用主控三字段，不用本地规则替换其描述和措施。
            row.description = alarm.value(QStringLiteral("description")).toString();
            row.measure = alarm.value(QStringLiteral("measure")).toString();
        }
        else
        {
            const QString rawParameters = alarm.value(QStringLiteral("raw_parameters")).toString();
            QStringList parameters = rawParameters.split(QLatin1Char('|'));
            parameters.removeAll(QString());
            for (const auto &rule : rules)
            {
                if (rule.code != code) continue;
                row.description = substituteDescription(rule.description, parameters);
                row.measure = rule.measure;
                break;
            }
            if (row.description.isEmpty())
            {
                // 原未匹配或空描述时拼接输入二字段，不猜等级、解释或控制动作。
                row.description = code + rawParameters;
                row.measure.clear();
            }
        }
        const qint64 rowBytes = qint64(row.errorCode.toUtf8().size()) +
            row.description.toUtf8().size() + row.measure.toUtf8().size() + row.dateTime.toUtf8().size();
        if (rowBytes > maximumObservationBytes - textBytes)
        {
            *error = QObject::tr("本批告警描述展开后超过16MiB本地显示候选上限，未发布部分结果。");
            return false;
        }
        textBytes += rowBytes;
        rows.append(row);
    }
    *result = rows;
    return true;
}
}

OriginalCurrentAlarmState::OriginalCurrentAlarmState(QVector<OriginalRuntime::AlarmRule> rules,
                                                   QObject *parent)
    : QObject(parent), rules_(std::move(rules))
{
    qRegisterMetaType<CurrentAlarmRow>();
    qRegisterMetaType<CurrentAlarmRows>();
    qRegisterMetaType<CurrentAlarmObservation>();
    qRegisterMetaType<CurrentAlarmQueryState>();
    state_.message = tr("当前告警尚未查询，状态未知。");
}

bool OriginalCurrentAlarmState::inGuiThread() const
{
    if (QThread::currentThread() == thread() && QCoreApplication::instance() &&
        thread() == QCoreApplication::instance()->thread()) return true;
    qWarning() << "ORIGINAL_CURRENT_ALARM_WRONG_THREAD";
    return false;
}

bool OriginalCurrentAlarmState::publishState()
{
    // 先提交完整状态，再对外通知；直接槽可能断线、换连接、收新包或销毁本对象。
    const quint64 revision = revision_;
    const CurrentAlarmQueryState snapshot = state_;
    QPointer<OriginalCurrentAlarmState> self(this);
    emit stateChanged(snapshot);
    if (!self) return false;
    return revision_ == revision;
}

CurrentAlarmObservation OriginalCurrentAlarmState::lastObservation(const QByteArray &command) const
{
    if (command == "Qk") return lastQk_;
    if (command == "QK") return lastQK_;
    return {};
}

void OriginalCurrentAlarmState::markUnknown(const QString &message)
{
    state_.valid = false;
    state_.message = message;
    ++revision_;
    publishState();
}

void OriginalCurrentAlarmState::setConnection(quint64 generation, bool connected)
{
    if (!inGuiThread()) return;
    connected = connected && generation != 0;
    if (state_.connection == generation && state_.connected == connected) return;
    state_.connection = generation;
    state_.connected = connected;
    state_.refreshPending = false;
    state_.refreshQueued = false;
    state_.refreshId = 0;
    // 最后一批原行仍可供界面保留为旧显示；不能赋新连接号后当成新结果。
    markUnknown(connected
        ? tr("主控连接已改变，旧显示仅供核对；当前告警待重新查询。")
        : tr("主控未连接，旧显示已失效；当前告警状态未知。"));
}

quint64 OriginalCurrentAlarmState::beginRefresh(quint64 generation)
{
    if (!inGuiThread() || generation != state_.connection ||
        !state_.connected || state_.refreshPending) return 0;
    if (nextRefresh_ == std::numeric_limits<quint64>::max())
    {
        markUnknown(tr("当前告警的本地刷新编号已耗尽，未复用旧编号；请重新核对程序状态。"));
        return 0;
    }
    const quint64 refreshId = ++nextRefresh_;
    state_.valid = false;
    state_.refreshPending = true;
    state_.refreshQueued = false;
    state_.refreshId = refreshId;
    state_.message = tr("正在提交当前告警查询；旧显示保留待核对。");
    ++revision_;
    QPointer<OriginalCurrentAlarmState> self(this);
    publishState();
    if (!self) return 0;
    // 另一类QK观察可以到达，仍保留本地Qk等待；换连接、Qk已到或新刷新则不用旧入口发送。
    return state_.connection == generation && state_.connected && state_.refreshPending &&
        state_.refreshId == refreshId ? refreshId : 0;
}

void OriginalCurrentAlarmState::finishRefreshQueue(quint64 generation, quint64 refreshId, bool queued)
{
    if (!inGuiThread() || generation != state_.connection || !state_.connected ||
        !refreshId || refreshId != state_.refreshId || !state_.refreshPending || state_.refreshQueued) return;
    state_.refreshQueued = queued;
    if (!queued)
    {
        state_.refreshPending = false;
        markUnknown(tr("当前告警查询未能进入发送队列；旧显示保留，当前状态未知。"));
        return;
    }
    state_.message = tr("当前告警查询已进入发送队列，等待完整Qk消息；入队不代表主控已回应。");
    ++revision_;
    publishState();
}

void OriginalCurrentAlarmState::receivePayload(QByteArray payload, quint64 generation,
                                              QDateTime receivedAt)
{
    if (!inGuiThread() || generation != state_.connection || !state_.connected) return;
    // 新事件Ql/QL继续归原接收器和历史保存链，当前观察不接管也不重复保存。
    const QByteArray command = payload.left(2);
    if (command != "Qk" && command != "QK") return;
    Sn100Protocol::ReadOnlyMessage parsed;
    QString problem;
    if (!Sn100Protocol::parseReadOnlyMessage(payload, &parsed, &problem))
    {
        markUnknown(tr("当前告警消息不完整或无效，本次未更新；旧显示保留待核对：") + problem);
        return;
    }
    if (!receivedAt.isValid() || receivedAt.date().year() < 1 || receivedAt.date().year() > 9999)
    {
        markUnknown(tr("当前告警消息缺少有效接收时间，本次未更新；旧显示保留待核对。"));
        return;
    }
    CurrentAlarmObservation observation;
    observation.connection = generation;
    observation.command = parsed.command;
    observation.receivedAt = receivedAt;
    if (!displayRows(parsed, rules_, receivedAt, &observation.rows, &problem))
    {
        markUnknown(problem + tr("旧显示保留待核对；当前状态未知。"));
        return;
    }
    if (command == "Qk") lastQk_ = observation;
    else lastQK_ = observation;
    state_.valid = true;
    if (command == "Qk")
    {
        state_.refreshPending = false;
        state_.refreshQueued = false;
        state_.message = tr("本连接收到完整Qk当前告警消息，本批%1条；这不是全部正常的结论。")
            .arg(observation.rows.size());
    }
    else
    {
        state_.message = state_.refreshPending
            ? tr("本连接收到完整QK当前异常消息，本批%1条；Qk仍在等待，未用QK确认本次刷新。")
                .arg(observation.rows.size())
            : tr("本连接收到完整QK当前异常消息，本批%1条；另一类当前告警状态未由本消息确认。")
                .arg(observation.rows.size());
    }
    ++revision_;
    if (!publishState()) return;
    // 状态通知重入后不能继续把旧观察送入新连接或覆盖更新的显示。
    emit currentObserved(observation);
}

void OriginalCurrentAlarmState::refreshTimedOut(quint64 generation, quint64 refreshId)
{
    if (!inGuiThread() || generation != state_.connection || !refreshId ||
        refreshId != state_.refreshId || !state_.refreshPending) return;
    state_.refreshPending = false;
    state_.refreshQueued = false;
    markUnknown(tr("当前告警查询等待结束，尚未收到可采用的Qk消息；旧显示保留，当前状态未知。"));
}

void OriginalCurrentAlarmState::invalidateCurrent(quint64 generation, QString reason)
{
    if (!inGuiThread() || generation != state_.connection) return;
    if (reason.isEmpty()) reason = tr("当前告警显示未能采用，旧显示保留待核对；当前状态未知。");
    markUnknown(reason);
}
}
