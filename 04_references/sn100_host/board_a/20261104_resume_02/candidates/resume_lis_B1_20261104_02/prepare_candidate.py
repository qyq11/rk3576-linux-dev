"""在三份候选副本补只读映射值和孔位自动编号标志，保留CRLF。"""
import hashlib
import json
from pathlib import Path

work = Path(__file__).resolve().parent
source = work / "candidate/06_programs/sn100_host/samples"
changes = []

def replace(name, old, new):
    path = source / name
    data = path.read_bytes()
    before = old.replace("\n", "\r\n").encode("utf-8")
    after = new.replace("\n", "\r\n").encode("utf-8")
    assert data.count(before) == 1, (name, old[:100], data.count(before))
    data = data.replace(before, after)
    path.write_bytes(data)
    changes.append({"file": name, "old": old, "new": new})

replace("sample_types.h", "    QString sampleId;\n    int container = 1;", """    QString sampleId;
    // 原S_SampleHoleInfo的孔位标志，独立于patient.isAutoId；LIS清患者不清此值。
    bool isAutoId = true;
    int container = 1;""")

replace("original_sample_repository.h", "class OriginalSampleRepository;\nclass OriginalLisPreparedSampleUpdate", """class OriginalSampleRepository;
// 按原项目/表行顺序读取的完整映射值；不暴露可写成员，也不代表应用许可或设备来源。
class OriginalLisMappingSnapshot
{
public:
    bool valid() const { return valid_; }
    QString projectSourceDigest() const { return projectSourceDigest_; }
    QString digest() const { return digest_; }
    const Sn100Lis::OriginalLisMappingProjects &projects() const { return projects_; }
private:
    friend class OriginalSampleRepository;
    bool valid_ = false;
    QString projectSourceDigest_, digest_;
    Sn100Lis::OriginalLisMappingProjects projects_;
};

class OriginalLisPreparedSampleUpdate""")

replace("original_sample_repository.h", "    // 以下仅在现仓库线程操作同一snapshot；不发送QRY/ACK、不写报告或预留任务。", """    // 只读原表及同次INI，不用编辑缓存、不执行未完成复制的恢复；失败输出为无效空快照。
    bool captureOriginalLisMappings(OriginalLisMappingSnapshot *snapshot, QString *message) const;
    // 以下仅在现仓库线程操作同一snapshot；不发送QRY/ACK、不写报告或预留任务。""")

replace("original_sample_repository.h", "    bool originalLisMappings(Sn100Lis::OriginalLisMappingProjects *mappings, QString *digest, QString *message) const;\n", "")

replace("original_sample_repository.cpp", """    out << QStringLiteral("SN100-LIS-hole-input-v1") << hole.number << hole.sampleId << hole.sampleBarcode
        << hole.container << hole.needleType << hole.revise << hole.sampleType;""", """    out << QStringLiteral("SN100-LIS-hole-input-v2") << hole.number << hole.sampleId << hole.sampleBarcode
        << hole.isAutoId << hole.container << hole.needleType << hole.revise << hole.sampleType;""")

replace("original_sample_repository.cpp", """bool OriginalSampleRepository::originalLisMappings(Sn100Lis::OriginalLisMappingProjects *mappings,
                                                   QString *mappingDigest, QString *message) const
{
    if (!mappings || !mappingDigest) return fail(message, QStringLiteral("没有原LIS映射输出位置。"));
    QString beforeSource; QStringList files;""", """bool OriginalSampleRepository::captureOriginalLisMappings(OriginalLisMappingSnapshot *snapshot, QString *message) const
{
    if (!snapshot) return fail(message, QStringLiteral("没有原LIS映射输出位置。"));
    *snapshot = OriginalLisMappingSnapshot();
    QString beforeSource; QStringList files;""")

replace("original_sample_repository.cpp", """    *mappings = result; *mappingDigest = digest(bytes);
    return true;
}

bool OriginalSampleRepository::captureOriginalLisHole""", """    OriginalLisMappingSnapshot complete;
    complete.projectSourceDigest_ = beforeSource; complete.digest_ = digest(bytes);
    complete.projects_ = std::move(result); complete.valid_ = true;
    *snapshot = std::move(complete);
    return true;
}

bool OriginalSampleRepository::captureOriginalLisHole""")

replace("original_sample_repository.cpp", """    Sn100Lis::OriginalLisMappingProjects mappings; QString mappingDigest;
    if (!originalLisMappings(&mappings, &mappingDigest, message)) return false;
    const auto plan = Sn100Lis::buildOriginalLisApplicationPlan(decoded, originalLisValue(root_, lisRepositoryInstance_, rack, before), mappings);""", """    OriginalLisMappingSnapshot mappings;
    if (!captureOriginalLisMappings(&mappings, message)) return false;
    const auto plan = Sn100Lis::buildOriginalLisApplicationPlan(decoded, originalLisValue(root_, lisRepositoryInstance_, rack, before), mappings.projects());""")

replace("original_sample_repository.cpp", """    complete.before_ = target; complete.after_ = std::move(after); complete.mappingDigest_ = mappingDigest; complete.prepared_ = true;""", """    complete.before_ = target; complete.after_ = std::move(after); complete.mappingDigest_ = mappings.digest(); complete.prepared_ = true;""")

(work / "edit_operations.json").write_text(json.dumps(changes, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
result = []
for name in ("sample_types.h", "original_sample_repository.h", "original_sample_repository.cpp"):
    before = (work / "base/06_programs/sn100_host/samples" / name).read_bytes()
    after = (source / name).read_bytes()
    assert after.count(b"\n") == after.count(b"\r\n")
    result.append({"path": "samples/" + name, "before": hashlib.sha256(before).hexdigest(), "after": hashlib.sha256(after).hexdigest()})
(work / "target_changes.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"actualExit": 0, "changedSources": len(result), "edits": len(changes), "lineEnding": "CRLF"}))
