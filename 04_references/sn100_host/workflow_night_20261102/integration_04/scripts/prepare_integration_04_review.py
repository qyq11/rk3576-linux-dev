"""在独立临时目录拼接已复核候选，不修改正在编译的正式源码。"""
from pathlib import Path
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[3]
task = Path(__file__).resolve().parent
snapshot = json.loads((task / "integration_03/source_snapshot.json").read_text(encoding="utf-8"))
stage = task / "integration_04_review"
stage.mkdir(exist_ok=False)
for relative, expected in snapshot.items():
    source = root / "06_programs/sn100_host" / relative
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != expected:
        raise RuntimeError("正式源码已偏离第三轮固定输入，未拼接混合副本：" + relative)
    target = stage / "06_programs/sn100_host" / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
patches = (
    ("project_default_calibration/project_default_calibration.patch", "a4da9f32bc1d66a625f763efc7d58ef334022e72c572636044c0ff95304211dd"),
    ("project_edit_permissions/project_edit_permissions.patch", "205d136e7ddf47b1c8c5ca11c17cdc1afa81ba466b6a32f9f99131949d0b8258"),
    ("priority_connection_20261102/priority_reagents_delta.patch", "d587e1df1b86779ebb0b008bec691afad855346f7496c6531c0a638f1527163c"),
    ("priority_connection_20261102/priority_transport_delta.patch", "18660dde2c586f169d869e72554dda5c8d6417be97a7482cd0c80fd88028f255"),
    ("priority_connection_20261102/priority_shell_delta.patch", "4fdd31794ae74befea16e94f1a315d618162bc8db2ebd9a44f2b26eeba8bcb4f"),
)
prefix = stage.relative_to(root).as_posix()
applied = []
for relative, expected in patches:
    patch = task / relative
    if hashlib.sha256(patch.read_bytes()).hexdigest() != expected:
        raise RuntimeError("候选补丁不是已复核版本，未应用：" + relative)
    command = ["git", "apply", "--ignore-space-change", "--directory=" + prefix]
    subprocess.run(command + ["--check", str(patch)], cwd=root, check=True)
    subprocess.run(command + [str(patch)], cwd=root, check=True)
    applied.append({"patch": relative, "sha256": expected})
result = {"source_base": "integration_03/source_snapshot.json", "base_files": len(snapshot),
          "applied_patches": applied, "formal_source_modified": False,
          "compiled": False, "program_executed": False}
(stage / "merge_review.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("ISOLATED_REVIEW_MERGE_PASSED=" + str(len(applied)))
print("FORMAL_SOURCE_MODIFIED=0")
