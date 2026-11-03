"""核源码恢复输入并保存编译/板B暂存证据，不执行产品。"""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import shutil
import tarfile

root = Path(__file__).resolve().parents[3]
task = Path(__file__).resolve().parent
work = task / "integration_05"
archive = Path((work / "archive_path.txt").read_text(encoding="utf-8").strip())
provenance = json.loads((archive / "provenance.json").read_text(encoding="utf-8"))
snapshot = json.loads((archive / "source_snapshot.json").read_text(encoding="utf-8"))
release_id = provenance["release_id"]
source = archive / ("sn100_sources_" + release_id + ".tar.gz")
seen = set()
with tarfile.open(source, "r:gz") as bundle:
    for member in bundle:
        prefix = "06_programs/sn100_host/"
        if not member.isfile() or not member.name.startswith(prefix):
            raise RuntimeError("源码包含非预期条目。")
        name = member.name[len(prefix):]
        if name in seen or name not in snapshot:
            raise RuntimeError("源码包条目重复或不在匹配清单。")
        data = bundle.extractfile(member).read()
        if hashlib.sha256(data).hexdigest() != snapshot[name]:
            raise RuntimeError("源码包解压后的文件指纹不符：" + name)
        seen.add(name)
if seen != set(snapshot):
    raise RuntimeError("源码包缺少编译输入。")
if (work / "board_b_stage.exit").read_text().strip() != "0":
    raise RuntimeError("板B独立版本暂存没有通过。")
board_log = (work / "logs/board_b_stage.log").read_text(encoding="utf-8")
for expected in ("SERIAL=8314b1e8ffd0321f", "STAGED_RELEASE=" + release_id,
                 "ACTIVATED=0", "PROGRAM_STARTED=0", provenance["application_sha256"]):
    if expected not in board_log:
        raise RuntimeError("板B结果缺少预期身份或范围：" + expected)
if "not found" in board_log:
    raise RuntimeError("板B存在缺少运行库，未记暂存通过。")
provenance["board_staging"] = "passed_manifest_sha256_and_runtime_dependencies"
provenance["source_archive_readback_files"] = len(seen)
(archive / "provenance.json").write_text(json.dumps(provenance, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
destination = root / "04_references/sn100_host/workflow_night_20261102/integration_05"
destination.mkdir(parents=True, exist_ok=False)
copies = {
    work / "logs/build.log": "build.log",
    work / "build_exit.txt": "build_exit.txt",
    work / "build_inputs.tsv": "build_inputs.tsv",
    work / "merge_inputs.json": "merge_inputs.json",
    task / "integration_05_review/promotion_progress.json": "promotion_progress.json",
    work / "logs/board_b_stage.log": "board_b_stage.log",
    work / "board_b_stage.exit": "board_b_stage.exit",
    task / "board_b_controller_exit_readonly.log": "board_b_controller_exit_readonly.log",
    task / "board_b_controller_exit_readonly.exit": "board_b_controller_exit_readonly.exit",
    task / "board_b_capacity_files_readonly.log": "board_b_capacity_files_readonly.log",
    task / "board_b_capacity_files_readonly.exit": "board_b_capacity_files_readonly.exit",
    task / "board_b_controller_exe_readonly.log": "board_b_controller_exe_readonly.log",
    task / "board_b_controller_exe_readonly.exit": "board_b_controller_exe_readonly.exit",
    archive / "source_snapshot.json": "source_snapshot.json",
    archive / "dependency_report.json": "dependency_report.json",
    archive / "provenance.json": "provenance.json",
}
for original, name in copies.items():
    shutil.copyfile(original, destination / name)
for relative in ('report_average_config/handoff.md', 'report_average_config/candidate_manifest.json', 'theme_consumers/handoff.md', 'theme_consumers/candidate_manifest.json', 'theme_consumers/field_comparison.md', 'device_reconnect/handoff.md', 'device_reconnect_independent_review.md', 'device_reconnect_v2/handoff.md', 'device_reconnect_v2/candidate_manifest.json', 'device_reconnect_v2/independent_fix_review.md', 'reagent_group_enable/handoff.md', 'reagent_group_enable_independent_review.md', 'reagent_group_enable_v2/handoff.md', 'reagent_group_enable_v2/candidate_manifest.json', 'reagent_group_enable_v2/independent_fix_review.md', 'integration_05_review/cross_patch_review.md'):
    target = destination / "source_review" / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(task / relative, target)
scripts = destination / "scripts"
scripts.mkdir()
for name in ("build_integration_05.sh", "stage_board_b_release.sh", "stage_board_b_release_05.py",
             "archive_integration_05.py", "verify_and_save_integration_05.py", "verify_integration_05_inputs.py",
             "board_b_controller_exit_readonly.sh", "run_controller_exit_readonly.py",
             "board_b_capacity_files_readonly.sh", "run_capacity_files_readonly.py",
             "board_b_controller_exe_readonly.sh", "run_controller_exe_readonly.py",
             "prepare_integration_05_review.py", "extend_integration_05_review.py", "promote_integration_05_review.py"):
    shutil.copyfile(task / name, scripts / name)
verification = {
    "captured_at": datetime.now().astimezone().isoformat(),
    "release_id": release_id, "arm64_build_exit": 0,
    "compiled_objects": len(list((work / "arm64_build").glob("obj_*.o"))),
    "compiled_objects_scope": "main_obj_files",
    "helper_objects": len(list((work / "arm64_build").glob("helper_*.o"))),
    "source_archive_readback_files": len(seen), "board": "board_b",
    "board_stage_exit": 0, "current_release_changed": False,
    "new_program_executed": False, "controller_restarted": False,
    "rk_function_verified": False, "instrument_verified": False,
    "runtime_data_or_credentials_archived": False,
}
(destination / "verification.json").write_text(json.dumps(verification, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

def checksum_files(directory):
    entries = []
    for path in sorted(directory.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            entries.append(digest + "  " + path.relative_to(directory).as_posix())
    (directory / "SHA256SUMS").write_text("\n".join(entries) + "\n", encoding="ascii")

checksum_files(archive)
checksum_files(destination)
print(json.dumps(verification, ensure_ascii=False))
