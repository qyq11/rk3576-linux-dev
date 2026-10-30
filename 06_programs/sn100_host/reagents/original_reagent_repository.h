#ifndef SN100_ORIGINAL_REAGENT_REPOSITORY_H
#define SN100_ORIGINAL_REAGENT_REPOSITORY_H
#include "original_files/original_business_database.h"
#include "samples/original_task_codec.h"
#include "reagents/reagent_types.h"

struct OriginalReagentState
{
    OriginalBusiness::Rows reagents, bottles, batchGroups, barcodes, holes;
    QString root;
    QString settingTable;
    QMap<QString, QString> versions;
};
// 原表原字段直接读取；任务计算用原始数值，不先压成早期显示模型的整数/外键。
class OriginalReagentRepository
{
public:
    bool initialize(const QString &root, QString *message);
    bool load(OriginalReagentState *state, QString *message);
    bool cleanerReady(const OriginalReagentState &state, QString *message) const;
    bool auxiliaryVolumesReady(const OriginalReagentState &state, const QList<OriginalTasks::Task> &tasks,
                               QString *message) const;
    bool assignTask(const OriginalReagentState &state, OriginalTasks::Task *task,
                    QMap<qint64, double> *remaining, QString *message) const;
    bool reagentUpdatePayload(const OriginalReagentState &state, QByteArray *payload, QString *message) const;
    bool singleReagentUpdatePayload(const OriginalReagentState &state, const QString &holeName, bool updateAmount,
                                   QByteArray *payload, QString *message) const;
    // 原单孔设置弹窗发送前统一把优先级设99；通用Us编码仍按调用者给定的孔位输出。
    bool singleReagentSettingPayload(const OriginalReagentState &state, const QString &holeName, bool updateAmount,
                                    QByteArray *payload, QString *message) const;
    bool saveRemaining(const OriginalReagentState &observed, const QMap<qint64, double> &remaining,
                       QString *message);
    bool applyDeviceReply(const QByteArray &payload, QString *message);
private:
    QString root_, table_;
};
#endif
