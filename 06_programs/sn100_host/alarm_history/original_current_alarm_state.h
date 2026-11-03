#ifndef SN100_ORIGINAL_CURRENT_ALARM_STATE_H
#define SN100_ORIGINAL_CURRENT_ALARM_STATE_H

#include "original_files/original_system_profile.h"
#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

namespace AlarmHistory
{
// 当前显示行与历史记录分开，不能把本批查询结果交给历史追加接口。
struct CurrentAlarmRow
{
    QString errorCode;
    QString description;
    QString measure; // 保留原措施文字；本对象不据此显示新详情或执行设备动作。
    QString dateTime;
};
using CurrentAlarmRows = QVector<CurrentAlarmRow>;

// 一条完整Qk或QK所得的本批观察，不代表另一种命令或仪器全部正常。
struct CurrentAlarmObservation
{
    quint64 connection = 0;
    QByteArray command;
    QDateTime receivedAt;
    CurrentAlarmRows rows;
};

struct CurrentAlarmQueryState
{
    quint64 connection = 0;
    bool connected = false;
    // valid只说明最近一批观察完整且属于本连接；不代表刷新已确认或全部正常。
    bool valid = false;
    bool refreshPending = false;
    bool refreshQueued = false;
    quint64 refreshId = 0; // 仅标记本地等待，线上报文没有此编号。
    QString message;
};

// 只在GUI所属线程调用和读取；值类型信号可以排队传递，对象本身不能跨线程使用。
// 本对象不发命令、不读写SQL、不创建线程，也不处理Ql/QL新事件。
class OriginalCurrentAlarmState : public QObject
{
    Q_OBJECT
public:
    explicit OriginalCurrentAlarmState(QVector<OriginalRuntime::AlarmRule> rules,
                                       QObject *parent = nullptr);
    CurrentAlarmQueryState queryState() const { return state_; }
    // 两类完整观察各自保留原连接号；不存在的命令返回空值，不推断另一类已回应。
    CurrentAlarmObservation lastObservation(const QByteArray &command) const;

public slots:
    void setConnection(quint64 generation, bool connected);
    // 调用方先登记，再实际发送Qk；0表示未登记，非0只用于匹配本地排队和等待通知。
    quint64 beginRefresh(quint64 generation);
    // 只登记实际排队结果；true不是主控已收到，更不是查询或设备动作完成。
    void finishRefreshQueue(quint64 generation, quint64 refreshId, bool queued);
    void receivePayload(QByteArray payload, quint64 generation, QDateTime receivedAt);
    // 本切片没有原超时门槛依据；仅由总控采用已有明确门槛后调用，不内设定时器。
    void refreshTimedOut(quint64 generation, quint64 refreshId);
    // 显示容量等本地失败只使状态失效，不删除原观察、不假称等待中的查询已结束。
    void invalidateCurrent(quint64 generation, QString reason);

signals:
    void currentObserved(AlarmHistory::CurrentAlarmObservation observation);
    void stateChanged(AlarmHistory::CurrentAlarmQueryState state);

private:
    QVector<OriginalRuntime::AlarmRule> rules_;
    CurrentAlarmQueryState state_;
    CurrentAlarmObservation lastQk_, lastQK_;
    quint64 nextRefresh_ = 0;
    quint64 revision_ = 0;

    bool inGuiThread() const;
    bool publishState();
    void markUnknown(const QString &message);
};
}

Q_DECLARE_METATYPE(AlarmHistory::CurrentAlarmRow)
Q_DECLARE_METATYPE(AlarmHistory::CurrentAlarmRows)
Q_DECLARE_METATYPE(AlarmHistory::CurrentAlarmObservation)
Q_DECLARE_METATYPE(AlarmHistory::CurrentAlarmQueryState)

#endif
