"""从第四轮固定输入建立独立审阅副本，先拼接已固定的均值和主题接线。"""
from pathlib import Path
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[3]
task = Path(__file__).resolve().parent
snapshot = json.loads((task / "integration_04/source_snapshot.json").read_text(encoding="utf-8"))
stage = task / "integration_05_review"
stage.mkdir(exist_ok=False)
for relative, expected in snapshot.items():
    data = (root / "06_programs/sn100_host" / relative).read_bytes()
    if hashlib.sha256(data).hexdigest() != expected:
        raise RuntimeError("正式输入已偏离第四轮，未建立混合副本：" + relative)
    target = stage / "06_programs/sn100_host" / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
patches = (
    ("report_average_config/report_average_config.patch", "b58f03caaea1da2333607a35ed8de2c9ebf8fb761f87d6ea5afcd2ef40f5642b"),
    ("theme_consumers/theme_consumers.patch", "be48e2e9531f1e9adbeec6fa6b5eec44c30eb9046e4fb294efce9c8109a69fcd"),
)
applied = []
for relative, expected in patches:
    patch = task / relative
    if hashlib.sha256(patch.read_bytes()).hexdigest() != expected:
        raise RuntimeError("补丁不是已复核版本：" + relative)
    command = ["git", "apply", "--ignore-space-change", "--directory=" + stage.relative_to(root).as_posix()]
    subprocess.run(command + ["--check", str(patch)], cwd=root, check=True)
    subprocess.run(command + [str(patch)], cwd=root, check=True)
    applied.append({"patch": relative, "sha256": expected})
result = {"source_base": "integration_04/source_snapshot.json", "base_files": len(snapshot),
          "applied_patches": applied, "formal_source_modified": False,
          "compiled": False, "program_executed": False}
(stage / "merge_review.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("ISOLATED_REVIEW_MERGE_PASSED=" + str(len(applied)) + "\nFORMAL_SOURCE_MODIFIED=0")
