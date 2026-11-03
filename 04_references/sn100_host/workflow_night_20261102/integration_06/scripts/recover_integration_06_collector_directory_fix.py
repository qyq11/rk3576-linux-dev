"""核对首次归档器留下的七文件，改名保留；不执行归档器，不删文件。"""
from pathlib import Path
import hashlib
import json
import os
import stat

TASK = Path(__file__).resolve().parent
ROOT = TASK.parents[2]
WORK = TASK / "integration_06"
PARENT = ROOT / "04_references/sn100_host/workflow_night_20261102"
PARTIAL = PARENT / "integration_06"
PRESERVED = PARENT / "integration_06_collector_incomplete_01"
RECORDS = WORK / "collector_directory_fix"
OLD_TOOL_SHA = "a7bf5381fcaf107592795ea81d965d2a65cccf9d8d2e496f159cfdd62e851f94"
FIXED_TOOL_SHA = "bd5d199ae648a9ee8c7db802baa332e89a22280736cbc7ec90861f1c4c1c193a"
FAILURE_SHA = "c7c892a190de7680d26faf9dbe148ec72f016b539bf7fb96346ba6f3e39bdb12"
FILES = {
    "build.log": (WORK / "logs/build.log", "440e1756494dfdab53de938ed038e60c789a9b1def9277fd8941833a4bfd1c8b"),
    "build_exit.txt": (WORK / "build_exit.txt", "9a271f2a916b0b6ee6cecb2426f0b3206ef074578be55d9bc94f6f3fe3ab86aa"),
    "build_inputs.tsv": (WORK / "build_inputs.tsv", "315bf3b463a230e51c772e21d7e709fce2a43bac7820b7ba0ebe5385a59ba0fb"),
    "merge_inputs.json": (WORK / "merge_inputs.json", "079ad9dabd9acd28b5db8d3b1b7a19cf9b9eb45aa2290d5ea8ed92bfeab298d2"),
    "input_verification.json": (WORK / "input_verification.json", "b4e06c647cfb98310032d489443f82cc01d87a228bc77586624c559c2318286e"),
    "build_complete.json": (WORK / "build_complete.json", "e90806afa8b096adf086aa2498fdb559de1bbde1685761f8f27bcc197c21a02f"),
    "promotion_progress.json": (TASK / "integration_06_review/promotion_progress.json", "079ad9dabd9acd28b5db8d3b1b7a19cf9b9eb45aa2290d5ea8ed92bfeab298d2"),
}


def ordinary_path(path):
    path.resolve().relative_to(ROOT.resolve())
    for entry in (path, *path.parents):
        if entry.is_symlink() or (hasattr(entry, "is_junction") and entry.is_junction()):
            raise RuntimeError("路径经过链接，停止：" + str(entry))
        if entry == ROOT:
            break


def raw(path):
    ordinary_path(path)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise RuntimeError("不是单链接普通原件：" + str(path))
    return path.read_bytes()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def check_partial(directory):
    ordinary_path(directory)
    directory.resolve().relative_to(PARENT.resolve())
    if directory.parent != PARENT or not directory.is_dir():
        raise RuntimeError("不是本批04证据目录。")
    entries = list(directory.iterdir())
    if {entry.name for entry in entries} != set(FILES):
        raise RuntimeError("部分目录不再仅含七个已知文件；保留未知内容，不移动。")
    values = {}
    for name, (source, wanted) in FILES.items():
        current = raw(directory / name)
        if sha(current) != wanted or raw(source) != current:
            raise RuntimeError("部分归档与已核原输入不等：" + name)
        values[name] = wanted
    return values


def save_new(path, value):
    data = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    with path.open("xb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    if os.name != "nt":
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def main():
    if RECORDS.exists() or RECORDS.is_symlink() or PRESERVED.exists() or PRESERVED.is_symlink():
        raise RuntimeError("已有目录恢复或保留记录，拒绝自动重跑/覆盖。")
    old_tool = TASK / "verify_and_save_integration_06.py.before_network_copy_directory_fix"
    fixed_tool = TASK / "verify_and_save_integration_06.py"
    failure_path = TASK / "collector_attempt_01_tool_result.json"
    if sha(raw(old_tool)) != OLD_TOOL_SHA or sha(raw(fixed_tool)) != FIXED_TOOL_SHA or sha(raw(failure_path)) != FAILURE_SHA:
        raise RuntimeError("原失败工具、固定修补工具或真实工具返回转存件已变化。")
    failure = json.loads(raw(failure_path))
    if (failure.get("format") != "sn100-recorded-exec-command-result-v1" or failure.get("tool") != "exec_command"
            or failure.get("chunk_id") != "3ae95b" or failure.get("exit_code") != 1
            or not all(text in failure.get("output", "") for text in
                       ("FileNotFoundError", "[Errno 2]", "stage_network_recovery", "board_b_stage.log"))):
        raise RuntimeError("真实失败返回不是已核的missing-parent首次失败。")
    files = check_partial(PARTIAL)
    ordinary_path(PRESERVED)
    PRESERVED.resolve().relative_to(PARENT.resolve())
    if PRESERVED.parent != PARTIAL.parent or PRESERVED == PARTIAL:
        raise RuntimeError("保留目标越出本批04证据父目录。")
    missing = PARTIAL / "stage_network_recovery/inputs/integration_06/logs/board_b_stage.log"
    if missing.parent.exists():
        raise RuntimeError("首次缺失目标的父目录已出现，需重新人工核对。")
    record = {
        "format": "sn100-integration-06-collector-directory-recovery-v1", "complete": False,
        "original_collector_exit": 1, "original_failed_tool_sha256": OLD_TOOL_SHA,
        "fixed_collector_sha256": FIXED_TOOL_SHA, "failure_tool_result_sha256": FAILURE_SHA,
        "failure_tool_result_path": failure_path.relative_to(ROOT).as_posix(),
        "failure_evidence_origin": "根转存当次exec_command真实返回；不是collector自行写出的原日志，不是重跑。",
        "cause": "网络输入副本缺目标父目录；同源Path合并还覆盖了根级原255保存位置。修复为独立循环并先建父目录。",
        "failed_target_path_characters": len(str(missing)), "path_length_failure_observed": False,
        "original_directory": PARTIAL.relative_to(ROOT).as_posix(),
        "preserved_directory": PRESERVED.relative_to(ROOT).as_posix(), "partial_files": files,
        "verification_was_present": False, "rename_performed": False, "new_evidence_directory_created": False,
        "recovery_tool_sha256": sha(raw(Path(__file__).resolve())),
    }
    ordinary_path(RECORDS)
    RECORDS.resolve().relative_to(TASK.resolve())
    RECORDS.mkdir(exist_ok=False)
    save_new(RECORDS / "started.json", record)  # 先保存真实失败、七文件和移动意图。
    if check_partial(PARTIAL) != files or sha(raw(failure_path)) != FAILURE_SHA or sha(raw(fixed_tool)) != FIXED_TOOL_SHA:
        raise RuntimeError("移动前证据漂移；记录保留，不继续。")
    ordinary_path(PARTIAL)
    ordinary_path(PRESERVED)
    PARTIAL.resolve().relative_to(PARENT.resolve())
    PRESERVED.resolve().relative_to(PARENT.resolve())
    if PRESERVED.exists() or PRESERVED.is_symlink():
        raise RuntimeError("移动目标刚出现，未覆盖。")
    PARTIAL.rename(PRESERVED)
    if check_partial(PRESERVED) != files or PARTIAL.exists() or PARTIAL.is_symlink():
        raise RuntimeError("改名后状态需人工核对；不回滚或重跑。")
    record.update({"complete": True, "rename_performed": True})
    save_new(RECORDS / "complete.json", record)
    print("INCOMPLETE_COLLECTOR_01_PRESERVED=1\nORIGINAL_COLLECTOR_EXIT=1\nPRESERVED_MATCHING_FILES=7\nNEW_EVIDENCE_DIRECTORY_CREATED=0\nCOLLECTOR_EXECUTED=0")


if __name__ == "__main__":
    main()
