#ifndef SN100_ORIGINAL_CONTROL_TASKS_H
#define SN100_ORIGINAL_CONTROL_TASKS_H
#include "original_task_codec.h"
#include "reagents/original_reagent_repository.h"

namespace OriginalControlTasks
{
bool assignCalibration(const OriginalReagentRepository &repository, const OriginalReagentState &state,
                       bool linuxNew, OriginalTasks::Task *task, QMap<qint64, double> *remaining, QString *message);
OriginalReagentAllocation::AllocationResult assignCalibration(const OriginalReagentRepository &repository,
                       const OriginalReagentState &state, bool linuxNew, OriginalTasks::Task *task,
                       OriginalReagentAllocation::SelectionContext *selection, OriginalReagentAllocation::AmountWork *work);
bool attachCalibration(OriginalBusiness::Database &database, const QString &root, QString *message);
// 由检测会话已建立的同一个事务调用，不能自行提交或另开编号库。
bool reserveCalibration(OriginalBusiness::Database &database, QList<OriginalTasks::Task> *tasks, QString *message);
bool calibrationState(OriginalBusiness::Database &database, const OriginalTasks::Task &task,
                      const QString &state, QString *message);
}
#endif
