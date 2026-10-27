#ifndef SN100_SAMPLE_QC_PROTOCOL_PROPOSAL_H
#define SN100_SAMPLE_QC_PROTOCOL_PROPOSAL_H
#include "sample_qc_preset.h"
#include <QMetaType>

// 单孔质控预设共同合同的值对象。QC侧必须提供真实持久保护，不能以模拟成功替代。
struct SampleQcIdentity
{
    SampleQcPreset::Selection selected;
    QString slotToken;   // 每次提交的孔位引用UUID，永不使用名称/表格行号作键。
    QString changeToken; // 所属跨库变更UUID；仅用于保护/恢复，不是仪器任务ID。
};
struct SampleQcClaim
{
    QString slotToken;
    SampleQcPreset::Selection selected;
};
struct SampleQcChange
{
    QString changeToken;
    QString ownerDatasetId;
    QString qualityDatasetId;
    QString catalogCanonicalPath;
    qint64 expectedRevision = 0;
    QList<SampleQcClaim> claims;
};
struct SampleQcReservationReply
{
    QString changeToken;
    QString ownerDatasetId;
    QString qualityDatasetId;
    bool reserved = false;
    QString message;
    QList<SampleQcClaim> claims;
};
struct SampleQcFinishReply
{
    QString changeToken;
    QString ownerDatasetId;
    QString qualityDatasetId;
    bool reconciled = false;
    QString message;
};
inline bool sameSampleQcSelection(const SampleQcPreset::Selection &a,
                                  const SampleQcPreset::Selection &b)
{
    if (a.datasetId != b.datasetId || a.controlId != b.controlId || a.batchId != b.batchId ||
        a.controlName != b.controlName || a.batchName != b.batchName || a.sampleId != b.sampleId ||
        a.projects.size() != b.projects.size()) return false;
    for (int i = 0; i < a.projects.size(); ++i)
        if (a.projects[i].associationId != b.projects[i].associationId ||
            a.projects[i].projectId != b.projects[i].projectId) return false;
    return true;
}
Q_DECLARE_METATYPE(SampleQcPreset::Snapshot)
Q_DECLARE_METATYPE(SampleQcChange)
Q_DECLARE_METATYPE(SampleQcReservationReply)
Q_DECLARE_METATYPE(SampleQcFinishReply)
Q_DECLARE_METATYPE(SampleQcIdentity)
#endif
