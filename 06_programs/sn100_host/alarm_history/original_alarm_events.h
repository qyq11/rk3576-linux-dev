#ifndef SN100_ORIGINAL_ALARM_EVENTS_H
#define SN100_ORIGINAL_ALARM_EVENTS_H
#include "alarm_history_store.h"
#include "original_files/original_system_profile.h"
#include "protocol/read_only_messages.h"
#include <QDateTime>

namespace AlarmHistory
{
// 输入须先经完整报文解析；Ql/QL是新事件，Qk/QK仅为当前告警查询，不产生历史行。
// 时间和操作员由接收时现场上下文明确提供，不从状态轮询或猜测值生成。
bool originalNewEvents(const Sn100Protocol::ReadOnlyMessage &message,
                       const QVector<OriginalRuntime::AlarmRule> &rules,
                       const QString &currentUser, const QDateTime &receivedAt,
                       Rows *result, QString *error);
}
#endif
