"""把06晋级结果、实际构建输入和打包结果逐份对上；不运行产品。"""
from pathlib import Path, PurePosixPath
import argparse
import ast
import hashlib
import json
import os
import re
import stat

TASK = Path(__file__).resolve().parent
ROOT = TASK.parents[2]
PROGRAM = ROOT / "06_programs/sn100_host"
WORK = TASK / "integration_06"
BASE_SHA256 = "e785702f907aecee9bc645614f122c05f1c6053d33ecb1b94f7e08ee8eaaf111"
NEW_FILES = {
    "instrument/original_channel_receiver.cpp", "instrument/original_channel_receiver.h",
    "instrument/original_channel_store.cpp", "instrument/original_channel_store.h",
    "projects/original_project_copy.cpp", "projects/original_project_copy.h",
    "projects/original_project_copy_dialog.cpp", "projects/original_project_copy_dialog.h",
    "projects/copyprojectdialog.ui", "projects/original_project_save_notice.h",
}


def read(path):
    if not stat.S_ISREG(path.lstat().st_mode):
        raise RuntimeError("输入不是普通文件：" + str(path))
    return path.read_bytes()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe_name(name):
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name
            or PurePosixPath(name).is_absolute() or any(x in ("", ".", "..") for x in name.split("/"))):
        raise RuntimeError("清单相对路径无效。")
    return name


def save_once(path, data):
    if path.exists() or path.is_symlink():
        if read(path) != data:
            raise RuntimeError("已有核查证据不同，保留并停止：" + str(path))
        return
    with path.open("xb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--after-package", action="store_true")
    args = parser.parse_args()
    promoter_raw = read(TASK / "promote_integration_06_review.py")
    constants = {}
    for node in ast.parse(promoter_raw).body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name in ("BASE_SHA256", "REVIEW_SHA256", "PATCHES"):
                constants[name] = ast.literal_eval(node.value)
    if constants.get("BASE_SHA256") != BASE_SHA256 or len(constants.get("PATCHES", ())) != 8:
        raise RuntimeError("不是固定05底稿和八片的06晋级脚本。")
    progress_raw = read(TASK / "integration_06_review/promotion_progress.json")
    progress = json.loads(progress_raw)
    patches = [{"patch": name, "sha256": value} for name, value in constants["PATCHES"]]
    review_raw = read(TASK / "integration_06_review/merge_review.json")
    if (progress.get("format") != "sn100-integration-06-promotion-v1"
            or progress.get("complete") is not True or progress.get("phase") != "complete"
            or progress.get("current_patch") is not None or progress.get("preflight_applied") != list(range(1, 9))
            or progress.get("patches") != patches or len(progress.get("applied", ())) != 8
            or progress.get("source_snapshot_sha256") != BASE_SHA256
            or progress.get("merge_review_sha256") != constants.get("REVIEW_SHA256")
            or sha(review_raw) != constants.get("REVIEW_SHA256")
            or progress.get("promotion_script_sha256") != sha(promoter_raw)):
        raise RuntimeError("06正式晋级未完整成功，未开始或继续构建。")
    for index, (name, value) in enumerate(constants["PATCHES"], 1):
        if sha(read(TASK / safe_name(name))) != value:
            raise RuntimeError("固定补丁已变化：" + name)
        if progress["applied"][index - 1] != {"index": index, "patch": name, "sha256": value}:
            raise RuntimeError("正式逐片记录顺序不完整。")
    snapshot_raw = read(WORK / "source_snapshot.json")
    snapshot = json.loads(snapshot_raw)
    if snapshot != progress.get("final_source_files") or not NEW_FILES.issubset(snapshot):
        raise RuntimeError("全源码快照不等于已审晋级结果，或漏了4+5+1新增文件。")
    expected_added = {"06_programs/sn100_host/" + x for x in NEW_FILES}
    if set(progress.get("added_files", ())) != expected_added or progress.get("deleted_files") != []:
        raise RuntimeError("新增/删除范围不是已审06范围。")
    for name, value in snapshot.items():
        path = PROGRAM / safe_name(name)
        path.resolve().relative_to(PROGRAM.resolve())
        if not re.fullmatch(r"[a-f0-9]{64}", value) or sha(read(path)) != value:
            raise RuntimeError("源码已漂移：" + name)
    checked, counts = set(), {}
    for line in read(WORK / "build_inputs.tsv").decode("utf-8").splitlines():
        kind, value = line.split("\t", 1)
        if kind == "include":
            continue
        if kind not in ("source", "form", "header", "resource", "helper_source"):
            raise RuntimeError("未知构建输入类型：" + kind)
        path = Path(value)
        relative = path.relative_to(PROGRAM).as_posix()
        if relative not in snapshot or sha(read(path)) != snapshot[relative]:
            raise RuntimeError("实际构建输入未被全源码快照覆盖：" + relative)
        checked.add(relative)
        counts[kind] = counts.get(kind, 0) + 1
    direct_new = NEW_FILES - {"projects/original_project_save_notice.h"}
    if counts.get("source", 0) < 100 or len(counts) != 5 or not direct_new.issubset(checked):
        raise RuntimeError("整程序构建清单不完整或未接入新增源码/表单。")
    evidence = {"format": "sn100-integration-06-inputs-v1", "source_count": len(snapshot),
                "unique_build_inputs": len(checked), "kinds": counts, "source_snapshot_sha256": sha(snapshot_raw),
                "promotion_progress_sha256": sha(progress_raw), "all_new_files_covered": sorted(NEW_FILES),
                "program_executed": False, "board_runtime_verified": False}
    save_once(WORK / "merge_inputs.json", progress_raw)
    save_once(WORK / "input_verification.json", json_bytes(evidence))
    print("SOURCE_FILES_COVERED=" + str(len(snapshot)))
    print("BUILD_INPUTS_COVERED_BY_SOURCE_SNAPSHOT=" + str(len(checked)))
    if not args.after_package:
        return
    binaries = {}
    for name in ("sn100_shell", "sn100_report_renderer"):
        raw = read(WORK / "build" / name)
        if (len(raw) < 64 or raw[:6] != b"\x7fELF\x02\x01" or int.from_bytes(raw[18:20], "little") != 183
                or int.from_bytes(raw[16:18], "little") not in (2, 3)):
            raise RuntimeError("不是ARM64 ELF主程序或报表程序：" + name)
        binaries[name] = sha(raw)
    dependency_raw = read(WORK / "dependencies/dependency_report.json")
    dependency = json.loads(dependency_raw)
    if (dependency.get("errors") != [] or dependency.get("offline_link_and_sql_closure_passed") is not True
            or dependency.get("board_runtime_verified") is not False):
        raise RuntimeError("静态依赖核查未通过。")
    package = Path(read(WORK / "packages/latest_package").decode("utf-8").strip())
    package.resolve().relative_to((WORK / "packages").resolve())
    if not re.fullmatch(r"sn100_shell_[a-f0-9]{64}\.tar\.gz", package.name):
        raise RuntimeError("打包路径或版本名无效。")
    evidence.update({"format": "sn100-integration-06-build-complete-v1", "arm64_build_and_package_passed": True,
                     "binaries": binaries, "package": package.name, "package_sha256": sha(read(package)),
                     "dependency_report_sha256": sha(dependency_raw)})
    save_once(WORK / "build_complete.json", json_bytes(evidence))
    print("ARM64_INTEGRATION_06_ARTIFACTS_VERIFIED=1")


if __name__ == "__main__":
    main()
