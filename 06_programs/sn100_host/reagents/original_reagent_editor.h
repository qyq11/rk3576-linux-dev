#ifndef SN100_ORIGINAL_REAGENT_EDITOR_H
#define SN100_ORIGINAL_REAGENT_EDITOR_H
#include "original_reagent_repository.h"
#include "projects/original_project_repository.h"
#include <functional>

// 只在ReagentStore数据库线程使用；界面仍用原表单，原名称关联不转成catalog外键。
class OriginalReagentEditor
{
public:
    bool initialize(const QString &root, QString *message);
    bool load(ReagentSnapshot *snapshot, ReagentProjects *projects, QString *message);
    bool saveReagent(const ReagentRecord &record, QString *message);
    bool saveBottle(const ReagentBottle &record, QString *message);
    bool saveBarcode(const ReagentBarcode &record, QString *message);
    bool saveBatch(const ReagentBatch &record, QString *message);
    bool saveHole(const ReagentHole &record, QString *message);
    bool clearHole(const QString &name, QString *message);
    bool remove(const QString &kind, qint64 id, QString *message);
    QString root() const { return root_; }
private:
    QString root_;
    OriginalReagentRepository repository_;
    OriginalReagentState state_;
    OriginalBusiness::Rows isiRows_;
    ProjectRecords projects_;
    QMap<QString,QString> versions_;
    bool mutate(const QString &file, const QString &expected, const std::function<bool(OriginalBusiness::Database &, QString *)> &change,
                QString *message);
    bool saveRow(OriginalBusiness::Database &database, const QString &table, qint64 id, QVariantMap values, QString *message);
    bool projectUses(const QString &name, bool bottle, QString *message) const;
};
#endif
