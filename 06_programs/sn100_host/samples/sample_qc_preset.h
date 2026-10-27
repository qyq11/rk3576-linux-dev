#ifndef SN100_SAMPLE_QC_PRESET_H
#define SN100_SAMPLE_QC_PRESET_H

#include <QList>
#include <QString>

// 只校验已提交值快照中的选择，不读库、不保存，也不表示任务可执行。
// QcStore专用读取接口提供质控身份，SamplePage补入已提交项目启用状态。
namespace SampleQcPreset
{
struct Batch
{
    qint64 id = 0;
    qint64 controlId = 0;
    QString name;
};
struct Association
{
    qint64 id = 0;
    qint64 controlId = 0;
    qint64 projectId = 0;
    QString projectName;
};
struct Control
{
    qint64 id = 0;
    QString name;
    QList<Batch> batches;
    QList<Association> projects;
};
struct Project
{
    qint64 id = 0;
    QString name;
    bool enabled = false;
};
struct Snapshot
{
    bool ready = false;
    QString datasetId; // 未来只能取质控库已有dataset_uuid，不能从名称或行号生成。
    QList<Control> controls;
    QList<Project> projects; // 已提交项目列表的顺序；不是临床测试顺序。
};
struct SelectedProject
{
    qint64 associationId = 0;
    qint64 projectId = 0;
};
struct Selection
{
    QString datasetId;
    qint64 controlId = 0;
    qint64 batchId = 0;
    QString controlName;
    QString batchName;
    QString sampleId;
    QList<SelectedProject> projects;
};
struct Reply
{
    QString code;
    QString message;
    Selection selection; // 失败时为空；调用者保留原草稿，不能用空输出覆盖它。
    bool ok() const { return code == QStringLiteral("OK"); }
};

// 原入口按质控关联自动选择已启用项目；不增加单位、失控或有效期判断。
Reply prepare(const Snapshot &snapshot, qint64 controlId, qint64 batchId);
// 提交前重新核对身份；只检查传入快照，不提供跨库锁或删除保护保证。
Reply recheck(const Snapshot &snapshot, const Selection &draft);
}
#endif
