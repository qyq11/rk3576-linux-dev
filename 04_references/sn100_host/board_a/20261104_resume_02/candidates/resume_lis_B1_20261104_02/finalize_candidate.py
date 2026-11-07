"""冻结本片清单并复核恢复依据/正式前像，不写正式工程。"""
import hashlib
import json
from pathlib import Path

work = Path(__file__).resolve().parent
repo = work.parents[2]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
restored = json.loads((work / "restore_result.json").read_text(encoding="utf-8"))
for row in restored["restored"]:
    path = work / "inputs" / row["path"]
    assert sha(path) == row["sha256"] and path.stat().st_size == row["bytes"]
before = json.loads((work / "before_source_manifest.json").read_text(encoding="utf-8-sig"))
for name, expected in before.items():
    assert sha(repo / "06_programs/sn100_host" / name) == expected, name
after = json.loads((work / "after_target_manifest.json").read_text(encoding="utf-8"))
for name, expected in after.items():
    assert sha(work / "candidate/06_programs/sn100_host" / name) == expected
    assert sha(work / "packaging_01/replay/06_programs/sn100_host" / name) == expected
result = {
    "actualExit": 0, "restoredInputsChecked": len(restored["restored"]),
    "formalPhysicalRead": len(before), "formalSourceMismatch": 0,
    "candidateAndReplayChecked": len(after), "replayMismatch": 0,
    "patchSha256": sha(work / "packaging_01/lis_B1_snapshot_auto_id.patch"),
    "handoffSha256": sha(work / "handoff.md"), "afterTargetManifestSha256": sha(work / "after_target_manifest.json"),
    "compiled": False, "sqlExecuted": False, "productOrTestsRun": False,
    "networkOrBoardUsed": False, "windowsRuntimeCheck": False,
}
(work / "final_verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
files = []
for path in sorted(work.rglob("*")):
    if not path.is_file() or ".git" in path.relative_to(work).parts or path.name == "output_manifest.json":
        continue
    files.append({"path": str(path.relative_to(work)), "bytes": path.stat().st_size, "sha256": sha(path)})
(work / "output_manifest.json").write_text(json.dumps({"scope": "resume_lis_B1_20261104_02_candidate_only", "files": files}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({**result, "outputManifestSha256": sha(work / "output_manifest.json"), "manifestFiles": len(files)}))
