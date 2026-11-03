#ifndef SN100_ORIGINAL_REAGENT_REPOSITORY_H
#define SN100_ORIGINAL_REAGENT_REPOSITORY_H
#include "original_files/original_business_database.h"
#include "samples/original_task_codec.h"
#include "reagents/reagent_types.h"
#include <functional>

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
    // 优先使用只改发送快照为priority=1，按原Us末项0编码；不先修改本地试剂资料。
    bool singleReagentPriorityPayload(const OriginalReagentState &state, const QString &holeName,
                                     QByteArray *payload, QString *message) const;
    // 只核合法Ur是否对应本次优先使用；连接归属、超时和回读提交由调用者另行核对。
    bool priorityReplyMatches(const QByteArray &payload, const QByteArray &sentPayload, QString *message) const;
    // 仅保存明确单孔的本次优先级回读；版本仅含原试剂/瓶型/孔位三库，许可回调在工作线程调用。
    // 提交后的取消/指纹失败返回false，但committed仍为true；调用者不能据false假定没有写入。
    bool applyPriorityReply(const QByteArray &payload, const QByteArray &sentPayload,
                            const QMap<QString, QString> &expectedVersions, const std::function<bool()> &stillCurrent,
                            bool *committed, QString *savedFingerprint, QString *message);
    // 仅准备原Mg报文，不检查设备就绪/舱盖，也不发送或把报文准备视为探测完成。
    bool detectionPayload(const OriginalReagentState &state, const QStringList &holeNames,
                          const QMap<QString, int> &needleSelection, QByteArray *payload, QString *message) const;
    bool saveRemaining(const OriginalReagentState &observed, const QMap<qint64, double> &remaining,
                       QString *message);
    bool applyDeviceReply(const QByteArray &payload, QString *message);
private:
    QString root_, table_;
};
#endif
