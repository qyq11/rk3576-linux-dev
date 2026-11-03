"""向独立审阅副本追加一份已固定补丁，并核对实际变更与正式输入。"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
task = Path(__file__).resolve().parent
stage = task / "integration_04_review"
patch = (task / sys.argv[1]).resolve()
patch.relative_to(task)
expected = sys.argv[2]
if hashlib.sha256(patch.read_bytes()).hexdigest() != expected:
    raise RuntimeError("补丁指纹不是已复核版本。")
result_path = stage / "merge_review.json"
result = json.loads(result_path.read_text(encoding="utf-8"))
relative = patch.relative_to(task).as_posix()
if any(item["patch"] == relative for item in result["applied_patches"]):
    raise RuntimeError("这份补丁已拼接，不重复应用。")
targets = []
for line in patch.read_text(encoding="utf-8").splitlines():
    if line.startswith("+++ b/"):
        target = line[6:].split("\t", 1)[0]
        path = stage / target
        path.resolve().relative_to(stage.resolve())
        targets.append((target, hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None))
if not targets:
    raise RuntimeError("补丁没有可核对的变更目标。")
command = ["git", "apply", "--ignore-space-change", "--directory=" + stage.relative_to(root).as_posix()]
subprocess.run(command + ["--check", str(patch)], cwd=root, check=True)
subprocess.run(command + [str(patch)], cwd=root, check=True)
for target, before in targets:
    if hashlib.sha256((stage / target).read_bytes()).hexdigest() == before:
        raise RuntimeError("补丁目标没有实际变化：" + target)
snapshot = json.loads((task / "integration_03/source_snapshot.json").read_text(encoding="utf-8"))
for name, digest in snapshot.items():
    if hashlib.sha256((root / "06_programs/sn100_host" / name).read_bytes()).hexdigest() != digest:
        raise RuntimeError("正式源码意外变化：" + name)
result["applied_patches"].append({"patch": relative, "sha256": expected, "changed_files": len(targets)})
result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("ISOLATED_REVIEW_PATCH_APPLIED=" + relative)
print("CHANGED_FILES=" + str(len(targets)) + "\nFORMAL_SOURCE_MODIFIED=0")
