#ifndef SN100_ORIGINAL_REAGENT_EDITOR_H
#define SN100_ORIGINAL_REAGENT_EDITOR_H
#include "original_reagent_repository.h"
#include "projects/original_project_repository.h"
#include "samples/original_task_reagent_receipt.h"
#include <functional>

// 只在ReagentStore数据库线程使用；界面仍用原表单，原名称关联不转成catalog外键。
class OriginalReagentEditor
{
public:
    bool initialize(const QString &root, QString *message);
    bool load(ReagentSnapshot *snapshot, ReagentProjects *projects, QString *message);
    // 调用者持项目→试剂两锁；仅用于临时副本，成功后再整体采用，绝不调用普通load/recover。
    bool loadTaskReagentCommit(const OriginalTaskReagents::Commit &receipt, ReagentSnapshot *snapshot,
                               ReagentProjects *projects, QString *message);
    bool saveReagent(const ReagentRecord &record, QString *message);
    bool saveBottle(const ReagentBottle &record, QString *message);
    bool saveBarcode(const ReagentBarcode &record, QString *message);
    bool saveBatch(const ReagentBatch &record, QString *message);
    bool saveBatchGroupEnabled(const ReagentProject &project, bool enabled, QString *message,
        OriginalProjectSaveNotice *result = nullptr, const std::function<bool()> &claim = {},
        const std::function<void(const OriginalProjectSaveNotice &)> &committed = {});
    bool saveHole(const ReagentHole &record, QString *message);
    bool clearHole(const QString &name, QString *message);
    // 调用者先核真实连接、权限、舱盖和回读处理；本方法仅准备报文及原本地资料变化。
    bool prepareDeviceOperation(const QString &operation, const QString &holeName, bool linuxNew,
                                QList<QByteArray> *packets, bool *localSaved, QString *message,
                                const std::function<bool()> &stillCurrent);
    bool remove(const QString &kind, qint64 id, QString *message);
    QString root() const { return root_; }
    QMap<QString,QString> deviceVersions() const { return state_.versions; }
    bool applyPriorityReply(const QByteArray &payload, const QByteArray &sentPayload,
                            const QMap<QString,QString> &expectedVersions, const std::function<bool()> &stillCurrent,
                            bool *committed, QString *savedFingerprint, QString *message)
    {
        return repository_.applyPriorityReply(payload,sentPayload,expectedVersions,stillCurrent,
                                              committed,savedFingerprint,message);
    }
private:
    QString root_;
    OriginalReagentRepository repository_;
    OriginalReagentState state_;
    OriginalBusiness::Rows isiRows_;
    ProjectRecords projects_;
    QMap<QString,QString> versions_;
    void projectSnapshot(const OriginalReagentState &observed, const OriginalBusiness::Rows &isiRows,
                         const ProjectRecords &records, const QMap<QString,QString> &versions,
                         ReagentSnapshot *snapshot, ReagentProjects *choices) const;
    bool mutate(const QString &file, const QString &expected, const std::function<bool(OriginalBusiness::Database &, QString *)> &change,
                QString *message, const ProjectRecord *projectSave = nullptr, const QString &groupEnabled = QString(),
                OriginalProjectSaveNotice *result = nullptr, const std::function<bool()> &claim = {},
                const std::function<void(const OriginalProjectSaveNotice &)> &committed = {});
    bool saveRow(OriginalBusiness::Database &database, const QString &table, qint64 id, QVariantMap values, QString *message);
    bool projectUses(const QString &name, bool bottle, QString *message) const;
};
#endif
