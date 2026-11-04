#ifndef SN100_ORIGINAL_REAGENT_REPOSITORY_H
#define SN100_ORIGINAL_REAGENT_REPOSITORY_H
#include "original_files/original_business_database.h"
#include "samples/original_task_codec.h"
#include "reagents/reagent_types.h"
#include <QSet>
#include "original_reagent_page_metadata.h"
#include <memory>
#include <functional>
#include "original_files/original_ini.h"

struct OriginalReagentState
{
    OriginalBusiness::Rows reagents, bottles, batchGroups, barcodes, holes;
    QString root;
    QString settingTable;
    QMap<QString, QString> versions;
};

namespace OriginalReagentAllocation
{
enum class Mode { Normal, Calibration };
enum class Status { Selected, OriginalSkipped, Fatal };
enum class SkipScope { None, Test, Project };

// 本次请求的逻辑余量；只有真正选中步骤才进入touched，最终SQL直接使用最后6位有效数字(g6)文本。
struct AmountWork
{
    bool initialized = false;
    QString root;
    QMap<QString,QString> versions;
    QMap<qint64,QVariant> initialUsed;
    QMap<qint64,double> remaining;
    QMap<qint64,QString> finalUsedText;
    QSet<qint64> touched;
    OriginalReagentPageMetadata::View pageMetadata; // 与真实盘面同寿命的批号；仅本次计算持副本。
};

// 普通/质控(Normal/QC)每样本孔一份全项目资料；校准(CAL)每项目一份，次数不随扣量重算。
struct SelectionContext
{
    bool initialized = false, linuxNew = false;
    Mode mode = Mode::Normal;
    QString root, scopeId;
    QMap<QString,QString> versions;
    QStringList multiBottleExceptions;
    QMap<QString,QMap<QString,QList<qint64>>> candidates;
    QMap<QString,QMap<qint64,int>> availableTests;
    QSet<QString> incompleteProjects;
    QMap<QString,QString> invalidProjects;
    QMap<QString,QList<ProjectActionFields>> projectSteps;
    QMap<QString,QString> projectRelated;
};

struct StepAllocation
{
    qint64 rowId = 0;
    int stepIndex = 0;
    QString reagentName, configuredText, usedText;
    double configuredVolume = 0;
};

struct AllocationResult
{
    Status status = Status::Fatal;
    SkipScope skipScope = SkipScope::None;
    QString scopeId, reason;
    QList<StepAllocation> steps;
    QMap<qint64,double> configuredVolumes; // 原配置量按孔汇总，保留选中的0 key，不含舍入损益。
    QStringList limitations; // 已保留的保护差异或尚未复原的原控件副作用，不据此伪称完全等价。
};
}
// 原表原字段直接读取；任务计算用原始数值，不先压成早期显示模型的整数/外键。
class OriginalReagentRepository
{
public:
    bool initialize(const QString &root, QString *message,
                    OriginalIni::InitializationSourcePtr configurationSource = {});
    OriginalIni::InitializationSourcePtr configurationSource() const { return configurationSource_; }
    void setPageMetadata(std::shared_ptr<OriginalReagentPageMetadata> metadata) { pageMetadata_ = std::move(metadata); }
    bool load(OriginalReagentState *state, QString *message);
    bool cleanerReady(const OriginalReagentState &state, QString *message) const;
    bool auxiliaryVolumesReady(const OriginalReagentState &state, const QList<OriginalTasks::Task> &tasks,
                               QString *message) const;
    // 旧调用兼容只持当前项目，不能证明全项目首次同名用量；正式会话使用下方缓存重载。
    bool assignTask(const OriginalReagentState &state, OriginalTasks::Task *task,
                    QMap<qint64, double> *remaining, QString *message) const;
    bool initializeAmountWork(const OriginalReagentState &state, OriginalReagentAllocation::AmountWork *work,
                              QString *message) const;
    // scopeProjects为空表示完整orderedProjects；CAL传当前项目，分母仍查完整原项目顺序。
    bool beginSelectionScope(const OriginalReagentState &state, const ProjectRecords &orderedProjects,
                             const QStringList &scopeProjects, OriginalReagentAllocation::Mode mode,
                             bool linuxNew, const QString &scopeId, const QStringList &multiBottleExceptions,
                             const OriginalReagentAllocation::AmountWork &work,
                             OriginalReagentAllocation::SelectionContext *selection, QString *message) const;
    OriginalReagentAllocation::AllocationResult assignTask(const OriginalReagentState &state, OriginalTasks::Task *task,
                             OriginalReagentAllocation::SelectionContext *selection,
                             OriginalReagentAllocation::AmountWork *work) const;
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
    OriginalIni::InitializationSourcePtr configurationSource_;
    QString root_, table_;
    std::shared_ptr<OriginalReagentPageMetadata> pageMetadata_;
    bool publishMetadataOverwrite(const QStringList &names, bool rebuild,
        const OriginalReagentPageMetadata::SourceTicket &ticket, QString *message);
};
#endif
