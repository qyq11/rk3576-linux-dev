#include "sample_qc_preset.h"
#include <QHash>
#include <QSet>
#include <QUuid>

namespace SampleQcPreset
{
namespace
{
Reply failure(const char *code, const QString &message)
{
    Reply reply;
    reply.code = QString::fromLatin1(code);
    reply.message = message;
    return reply;
}
}

Reply prepare(const Snapshot &snapshot, qint64 controlId, qint64 batchId)
{
    if (!snapshot.ready)
    {
        return failure("SNAPSHOT_NOT_READY", QStringLiteral("质控选择资料尚未就绪，保留原草稿。"));
    }
    if (QUuid(snapshot.datasetId).isNull() || controlId <= 0 || batchId <= 0)
    {
        return failure("INVALID_IDENTITY", QStringLiteral("质控数据集或选择ID无效。"));
    }

    const Control *control = nullptr;
    for (const Control &known : snapshot.controls)
    {
        if (known.id != controlId) continue;
        if (control) return failure("AMBIGUOUS_CONTROL", QStringLiteral("质控品ID重复，须重新读取。"));
        control = &known;
    }
    if (!control) return failure("CONTROL_MISSING", QStringLiteral("质控品已不存在。"));
    if (control->name.isEmpty()) return failure("EMPTY_NAME", QStringLiteral("质控品和批号不允许为空。"));

    const Batch *batch = nullptr;
    for (const Batch &known : control->batches)
    {
        if (known.id != batchId) continue;
        if (batch) return failure("AMBIGUOUS_BATCH", QStringLiteral("批次ID重复，须重新读取。"));
        batch = &known;
    }
    if (!batch) return failure("BATCH_NOT_IN_CONTROL", QStringLiteral("批次不存在或不属于所选质控品。"));
    if (batch->controlId != controlId)
    {
        return failure("BATCH_BINDING_MISMATCH", QStringLiteral("批次归属不一致，保留原草稿。"));
    }
    if (batch->name.isEmpty()) return failure("EMPTY_NAME", QStringLiteral("质控品和批号不允许为空。"));

    QHash<qint64, const Project *> catalog;
    for (const Project &project : snapshot.projects)
    {
        if (project.id <= 0 || catalog.contains(project.id))
        {
            return failure("INVALID_PROJECT_CATALOG", QStringLiteral("已提交项目ID无效或重复。"));
        }
        catalog.insert(project.id, &project);
    }
    QHash<qint64, qint64> associations;
    QSet<qint64> associationIds;
    for (const Association &association : control->projects)
    {
        const Project *project = catalog.value(association.projectId, nullptr);
        if (association.id <= 0 || association.controlId != controlId ||
            associationIds.contains(association.id) || associations.contains(association.projectId) ||
            !project || project->name != association.projectName)
        {
            return failure("INVALID_ASSOCIATION", QStringLiteral("质控项目关联已失效或身份不一致，请重载核对。"));
        }
        associationIds.insert(association.id);
        associations.insert(association.projectId, association.id);
    }

    Reply reply;
    reply.code = QStringLiteral("OK");
    reply.selection.datasetId = snapshot.datasetId;
    reply.selection.controlId = controlId;
    reply.selection.batchId = batchId;
    reply.selection.controlName = control->name;
    reply.selection.batchName = batch->name;
    // 原保存代码使用连字符，旧结构注释中的“空格”不是实际保存合同。
    reply.selection.sampleId = control->name + QStringLiteral("-") + batch->name;
    for (const Project &project : snapshot.projects)
    {
        if (project.enabled && associations.contains(project.id))
        {
            reply.selection.projects.append({associations.value(project.id), project.id});
        }
    }
    // 原保存未禁止空项目；空预设不等于有可执行任务，不能伪造默认项目。
    return reply;
}

Reply recheck(const Snapshot &snapshot, const Selection &draft)
{
    if (draft.datasetId != snapshot.datasetId)
    {
        return failure("DATASET_MISMATCH", QStringLiteral("质控数据集已变化，不能按同名资料重绑定。"));
    }
    Reply current = prepare(snapshot, draft.controlId, draft.batchId);
    if (!current.ok()) return current;
    const Selection &selected = current.selection;
    bool same = draft.controlName == selected.controlName && draft.batchName == selected.batchName &&
                draft.sampleId == selected.sampleId && draft.projects.size() == selected.projects.size();
    for (int index = 0; same && index < draft.projects.size(); ++index)
    {
        same = draft.projects[index].associationId == selected.projects[index].associationId &&
               draft.projects[index].projectId == selected.projects[index].projectId;
    }
    if (!same)
    {
        // 资料变化后要求重选，不让旧草稿悄悄跟随新名称或新关联。
        return failure("SELECTION_CHANGED", QStringLiteral("质控名称或关联已变化，请重新确认选择；原草稿保留。"));
    }
    return current;
}
}
