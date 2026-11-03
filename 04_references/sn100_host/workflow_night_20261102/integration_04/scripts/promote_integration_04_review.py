"""按已审顺序应用窄补丁，并核正式代码与隔离副本一致；不构建或运行。"""
from pathlib import Path
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[3]
task = Path(__file__).resolve().parent
stage = task / "integration_04_review"
result = json.loads((stage / "merge_review.json").read_text(encoding="utf-8"))
base = json.loads((task / "integration_03/source_snapshot.json").read_text(encoding="utf-8"))
progress_file = stage / "promotion_progress.json"
if progress_file.exists():
    raise RuntimeError("已存在应用记录，请先核对，不重复应用。")
for name, digest in base.items():
    if hashlib.sha256((root / "06_programs/sn100_host" / name).read_bytes()).hexdigest() != digest:
        raise RuntimeError("正式输入已被另行修改，请先合并：" + name)
applied = []
for item in result["applied_patches"]:
    patch = task / item["patch"]
    if hashlib.sha256(patch.read_bytes()).hexdigest() != item["sha256"]:
        raise RuntimeError("候选已漂移：" + item["patch"])
    command = ["git", "apply", "--ignore-space-change"]
    subprocess.run(command + ["--check", str(patch)], cwd=root, check=True)
    subprocess.run(command + [str(patch)], cwd=root, check=True)
    applied.append(item)
    progress_file.write_text(json.dumps({"applied": applied, "complete": False}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
changed = []
newline_only = []
text_types = {".cpp", ".h", ".py", ".sh", ".pri", ".pro", ".ui", ".qrc"}
for name, digest in base.items():
    formal_path = root / "06_programs/sn100_host" / name
    formal = formal_path.read_bytes()
    isolated = (stage / "06_programs/sn100_host" / name).read_bytes()
    if formal != isolated:
        if formal_path.suffix not in text_types or formal.replace(b"\r\n", b"\n") != isolated.replace(b"\r\n", b"\n"):
            raise RuntimeError("正式拼接与隔离审阅副本不符：" + name)
        newline_only.append(name)
    if hashlib.sha256(formal).hexdigest() != digest:
        changed.append(name)
final = {"complete": True, "applied": applied, "verified_source_files": len(base),
         "changed_files": changed, "newline_only_differences_from_review": newline_only,
         "compiled": False, "program_executed": False}
progress_file.write_text(json.dumps(final, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("FORMAL_PATCHES_APPLIED=" + str(len(applied)))
print("FORMAL_FILES_MATCH_REVIEW=" + str(len(base)))
print("CHANGED_FILES=" + str(len(changed)) + "\nPRODUCT_EXECUTED=0")
