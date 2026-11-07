"""只恢复本片所需的只读依据，不执行旧候选里的命令。"""
import hashlib
import json
from pathlib import Path
import tarfile

work = Path(__file__).resolve().parent
repo = work.parents[2]
archive = repo / "07_backups/sn100_host/source_candidates/20261104_before_temp_cleanup"
manifest = json.loads((archive / "development_files.json").read_text(encoding="utf-8-sig"))
names = {
    "temp/sn100_host/lis_repository_B1_on15_20261104_01/handoff.md",
    "temp/sn100_host/lis_repository_B1_on15_20261104_01/dependency_and_scope.md",
    "temp/sn100_host/lis_repository_B1_on15_20261104_01/resume_only_prompt.md",
    "temp/sn100_host/lis_repository_B1_on15_20261104_01/review_target_manifest_01.json",
    "temp/sn100_host/lis_repository_B1_on15_20261104_01/output_manifest.json",
    "temp/sn100_host/lis_repository_B1_on15_20261104_01/reviews/on15_empty_registration_review_01.md",
    "temp/sn100_host/lis_repository_B1_on15_20261104_01/reviews/on15_identity_review_01.md",
    "temp/sn100_host/lis_repository_B1_candidate_20261104_01/revision_03/handoff.md",
    "temp/sn100_host/lis_repository_B1_candidate_20261104_01/reviews/windows_B1_behavior_matrix_01.md",
    "temp/sn100_host/lis_repository_B1_candidate_20261104_01/inputs/read_basis.json",
    "temp/sn100_host/lis_repository_B1_candidate_20261104_01/inputs/handoff_B.md",
    "temp/sn100_host/lis_B1_field_crosscheck_20261104_01/field_crosscheck.md",
}
for name in ("sample_types.h", "original_sample_repository.h", "original_sample_repository.cpp"):
    names.add("temp/sn100_host/lis_repository_B1_on15_20261104_01/candidate/06_programs/sn100_host/samples/" + name)
expected = {row["path"]: row for row in manifest["files"] if row["path"] in names}
assert expected.keys() == names, sorted(names - expected.keys())
result = []
with tarfile.open(archive / "development_files.tar.gz", "r|gz") as tar:
    for member in tar:
        if member.name not in names:
            continue
        assert member.isfile() and not member.issym() and not member.islnk()
        data = tar.extractfile(member).read()
        sha = hashlib.sha256(data).hexdigest()
        row = expected[member.name]
        assert len(data) == row["bytes"] and sha == row["sha256"], member.name
        target = work / "inputs" / member.name
        assert not target.exists(), str(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        result.append({"path": member.name, "bytes": len(data), "sha256": sha})
assert len(result) == len(names)
formal_map = json.loads((archive / "formal_source_after.json").read_text(encoding="utf-8-sig"))
current = {name: hashlib.sha256((repo / "06_programs/sn100_host" / name).read_bytes()).hexdigest() for name in formal_map}
assert current == formal_map
for name in ("sample_types.h", "original_sample_repository.h", "original_sample_repository.cpp"):
    old = work / "inputs/temp/sn100_host/lis_repository_B1_on15_20261104_01/candidate/06_programs/sn100_host/samples" / name
    formal = repo / "06_programs/sn100_host/samples" / name
    assert old.read_bytes() == formal.read_bytes(), name
    for view in ("base", "candidate"):
        target = work / view / "06_programs/sn100_host/samples" / name
        assert not target.exists()
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(formal.read_bytes())
(work / "before_source_manifest.json").write_bytes((archive / "formal_source_after.json").read_bytes())
(work / "restore_result.json").write_text(json.dumps({
    "actualExit": 0, "restored": result, "formalPhysicalRead": len(current), "formalMismatch": 0,
    "on15TargetsEqualFormal": 3, "archiveWasReadOnly": True,
}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"actualExit": 0, "restored": len(result), "physicalFormalSources": len(current), "mismatch": 0}))
