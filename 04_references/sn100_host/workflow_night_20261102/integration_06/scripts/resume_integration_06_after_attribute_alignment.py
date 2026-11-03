"""凭根已审计的换行属性对齐证据，只恢复第06批第5—8片；不构建或联网。"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import runpy
import stat
import subprocess

TASK = Path(__file__).resolve().parent
ROOT = TASK.parents[2]
PREFIX = "06_programs/sn100_host/"
PROMOTER_SHA256 = "34dfbbd41c81ef7c1d7a51a700007c2788c8fa8e652b8fc6ba31a46ff70d01a6"
FAILED_PROGRESS_SHA256 = "377805ecb45d79d9c13ba95c5de9b5b47235a038fc04a59ef0431f12e1786449"
MERGE_SHA256 = "e48ec9c5d5c3ffa9ee63f248dad9482536aebc7a48b871c6e00b1941513a13fa"
ATTRIBUTES_SHA256 = "5de2ac8a8bf53f9c7775f129eac002896013a7d2415ae731a679616852a600a1"
AUDIT_SHA256 = "9b5aad0c47a81172e44e9cdf52acfa24e21688fa1b99c2cb4a4afcaa4de274b5"
RECORDS = TASK / "integration_06_recovery"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def regular(path):
    if not stat.S_ISREG(path.lstat().st_mode):
        raise RuntimeError("证据或目标不是普通文件：" + str(path))
    return path.read_bytes()


def relative_file(name):
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name or name.startswith("/")
            or any(x in ("", ".", "..") for x in name.split("/"))):
        raise RuntimeError("恢复证据路径无效。")
    path = ROOT / name
    path.resolve().relative_to(ROOT.resolve())
    for entry in (path, *path.parents):
        if entry.is_symlink() or (hasattr(entry, "is_junction") and entry.is_junction()):
            raise RuntimeError("恢复证据经过链接。")
        if entry == ROOT:
            break
    return path


def evidence(item):
    path = relative_file(item["path"])
    raw = regular(path)
    if not re.fullmatch(r"[a-f0-9]{64}", item["sha256"]) or sha(raw) != item["sha256"]:
        raise RuntimeError("恢复证据指纹不符：" + item["path"])
    return path, raw


def write_new(path, value):
    raw = value if isinstance(value, bytes) else (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    with path.open("xb") as output:
        output.write(raw)
        output.flush()
        os.fsync(output.fileno())
    if os.name != "nt":
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(raw)}


def check_attributes():
    if sha(regular(ROOT / ".gitattributes")) != ATTRIBUTES_SHA256:
        raise RuntimeError("正式路径属性已变化。")
    for name, wanted in (("core.autocrlf", b"true"), ("core.eol", b"")):
        result = subprocess.run(["git", "config", "--get", name], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode not in (0, 1) or result.stdout.strip() != wanted:
            raise RuntimeError("Git换行配置已变化：" + name)
    result = subprocess.run(["git", "check-attr", "-z", "text", "eol", "--", PREFIX + "projects/copyprojectdialog.ui"],
                            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    wanted = (PREFIX + "projects/copyprojectdialog.ui\0text\0unset\0" + PREFIX + "projects/copyprojectdialog.ui\0eol\0unspecified\0").encode()
    if result.stdout != wanted:
        raise RuntimeError("正式表单必须仍按原-text属性保存字节。")


def scan_blocked(path):
    path.resolve().relative_to(TASK.resolve())
    if path.parent != TASK or path.name != "integration_06" or path.is_symlink() or not path.is_dir():
        raise RuntimeError("被拦下的构建目录不是任务内固定integration_06。")
    files, directories = {}, []
    for entry in sorted(path.rglob("*")):
        entry.resolve().relative_to(path.resolve())
        if entry.is_symlink() or (hasattr(entry, "is_junction") and entry.is_junction()):
            raise RuntimeError("被拦构建目录中出现链接，未移动。")
        name = entry.relative_to(path).as_posix()
        if entry.is_dir():
            directories.append(name)
        elif entry.is_file() and entry.lstat().st_nlink == 1:
            raw = regular(entry)
            if raw.startswith(b"\x7fELF") or entry.suffix in (".o", ".a", ".so", ".exe"):
                raise RuntimeError("已有对象或程序产物，未当作预构建失败移动。")
            files[name] = sha(raw)
        else:
            raise RuntimeError("被拦构建目录有非普通条目，未移动。")
    if (set(files) != {"build_exit.txt", "build_inputs.tsv", "source_snapshot.json", "logs/build.log"}
            or set(directories) != {"build", "logs", "packages"}
            or (path / "build_exit.txt").read_text().strip() != "1"):
        raise RuntimeError("被拦目录不再是仅输入/日志、退出1且无对象的原状态。")
    return {"files": files, "directories": directories}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audit-metadata", required=True, help="根审计metadata，相对仓库根目录")
    parser.add_argument("--audit-metadata-sha256", required=True)
    args = parser.parse_args()
    metadata_path = relative_file(args.audit_metadata)
    metadata_path.resolve().relative_to(TASK.resolve())
    metadata_raw = regular(metadata_path)
    if args.audit_metadata_sha256 != AUDIT_SHA256 or sha(metadata_raw) != AUDIT_SHA256:
        raise RuntimeError("必须显式传根最终审过的恢复metadata SHA。")
    audit = json.loads(metadata_raw)
    if (audit.get("format") != "sn100-integration-06-attribute-recovery-audit-v1"
            or audit.get("paths_base") != "workspace" or audit.get("formal_modified_by_audit") is not False
            or audit.get("blocked_build_exit") != 1 or audit.get("compiled_objects") != 0):
        raise RuntimeError("根审计metadata格式未对接，未继续。")
    # 字段对应根的实际审计输出；最终metadata未固定前，不提供默认放行或猜测值。
    if RECORDS.exists() or RECORDS.is_symlink():
        raise RuntimeError("已有恢复记录，必须人工核对，不自动重跑。")
    promoter_path = TASK / "promote_integration_06_review.py"
    if sha(regular(promoter_path)) != PROMOTER_SHA256:
        raise RuntimeError("原失败晋级工具已变化。")
    helper = runpy.run_path(str(promoter_path), run_name="sn100_integration_06_recovery_support")
    patches = helper["PATCHES"]
    progress_path = TASK / "integration_06_review/promotion_progress.json"
    failed_raw = regular(progress_path)
    if sha(failed_raw) != FAILED_PROGRESS_SHA256:
        raise RuntimeError("原失败状态已变化，不能猜测恢复点。")
    progress = json.loads(failed_raw)
    if (progress.get("phase") != "failed_requires_review" or progress.get("complete") is not False
            or progress.get("applied") != [{"index": i, "patch": name, "sha256": value} for i, (name, value) in enumerate(patches[:3], 1)]
            or progress.get("current_patch") != {"index": 4, "patch": patches[3][0], "sha256": patches[3][1]}):
        raise RuntimeError("失败点不是第4片已写待核的固定状态。")
    if sha(regular(TASK / "integration_06_review/merge_review.json")) != MERGE_SHA256:
        raise RuntimeError("八片整合metadata已变化。")
    if audit["merge_review"]["sha256"] != MERGE_SHA256 or audit["promoter"]["sha256"] != PROMOTER_SHA256:
        raise RuntimeError("审计不是本次原工具和八片版本。")
    for name, wanted in patches:
        if sha(regular(relative_file("temp/sn100_host/workflow_night_20261102/" + name))) != wanted:
            raise RuntimeError("恢复前固定补丁已变化：" + name)
    original_path, original_raw = evidence(audit["failed_progress"])
    if original_raw != failed_raw or audit["failed_progress_sha256"] != FAILED_PROGRESS_SHA256:
        raise RuntimeError("审计保留的失败记录不等于当前失败记录。")
    diagnosis_path, diagnosis_raw = evidence(audit["diagnosis"])
    diagnosis = json.loads(diagnosis_raw)
    if (diagnosis.get("formal_equals_checkpoint_4") is not True
            or diagnosis.get("stage_equals_checkpoint_8_after_alignment") is not True
            or diagnosis.get("confirmed_applied_patch_count") != 4
            or diagnosis.get("program_executed") is not False
            or diagnosis.get("only_stage_difference") != PREFIX + "projects/copyprojectdialog.ui"):
        raise RuntimeError("审计诊断没有证明唯一换行差异及第4片完成。")
    _, base_raw = evidence(audit["source05_snapshot"])
    if sha(base_raw) != helper["BASE_SHA256"]:
        raise RuntimeError("不是固定05底稿。")
    base = json.loads(base_raw)
    evidence(audit["source05_archive"])
    for field in ("root_attributes", "audit_attributes", "stage_attributes"):
        _, attributes_raw = evidence(audit[field])
        if sha(attributes_raw) != ATTRIBUTES_SHA256:
            raise RuntimeError("正式、审计或stage路径属性不是同一原件。")
    evidence(audit["promoter"])
    evidence(audit["merge_review"])
    check_attributes()
    checkpoint_records = audit["checkpoints_0_to_8"]
    if len(checkpoint_records) != 9:
        raise RuntimeError("审计未保留0—8全部检查点。")
    checkpoints = []
    for item in checkpoint_records:
        _, raw = evidence(item)
        checkpoints.append(json.loads(raw))
    universe = set(checkpoints[8])
    if len(universe) != 649 or len(base) != 639 or any(set(value) != universe for value in checkpoints):
        raise RuntimeError("审计检查点没有覆盖全639底稿和10项新文件。")
    initial = {PREFIX + name: value for name, value in base.items()}
    initial.update({name: None for name in universe - set(initial)})
    if checkpoints[0] != initial or len(universe - {PREFIX + name for name in base}) != 10:
        raise RuntimeError("起点不等于05全部底稿和新增空目标。")
    for state in checkpoints:
        for name, wanted in state.items():
            helper["target_path"](ROOT, name)
            if wanted is not None and not re.fullmatch(r"[a-f0-9]{64}", wanted):
                raise RuntimeError("检查点指纹格式无效。")
    for index, (name, wanted) in enumerate(patches, 1):
        operations = helper["parse_patch"](regular(TASK / name))
        targets = {before or after for before, after in operations}
        for path in universe - targets:
            if checkpoints[index][path] != checkpoints[index - 1][path]:
                raise RuntimeError("审计检查点改了补丁之外的文件。")
        for before, after in operations:
            if ((before is None and checkpoints[index - 1][after] is not None)
                    or (before is not None and checkpoints[index - 1][before] is None)
                    or (after is None and checkpoints[index][before] is not None)
                    or (after is not None and checkpoints[index][after] is None)):
                raise RuntimeError("审计新增/删除/修改目标状态不符。")
    _, formal_stop_raw = evidence(audit["formal_at_stop"])
    if json.loads(formal_stop_raw) != checkpoints[4]:
        raise RuntimeError("根实际停止点不等于第4片检查点。")
    _, stage_snapshot_raw = evidence(audit["aligned_stage_snapshot"])
    final = checkpoints[8]
    if json.loads(stage_snapshot_raw) != final:
        raise RuntimeError("对齐后stage清单不等于审计最终检查点。")
    helper["verify_files"](ROOT, checkpoints[4])
    helper["verify_files"](helper["stage"], final)
    audit_directory = relative_file(audit["audit_directory"])
    audit_directory.resolve().relative_to(TASK.resolve())
    helper["verify_files"](audit_directory, final)
    for name, wanted in final.items():
        if wanted is not None and regular(helper["target_path"](audit_directory, name)) != regular(helper["target_path"](helper["stage"], name)):
            raise RuntimeError("对齐stage与按正式属性重演的audit逐字节不等。")
    alignment_originals = []
    originals = metadata_path.parent
    before_ui = originals / "stage_copyprojectdialog_before.ui"
    before_raw = regular(before_ui)
    if (sha(before_raw) != diagnosis["before_stage_ui_sha256"]
            or sha(before_raw.replace(b"\r\n", b"\n")) != diagnosis["original_ui_sha256"]
            or final[PREFIX + "projects/copyprojectdialog.ui"] != diagnosis["original_ui_sha256"]):
        raise RuntimeError("原temp表单不能由纯CRLF对齐回原候选字节。")
    before_snapshot = originals / "stage_before_alignment.json"
    before_map = json.loads(regular(before_snapshot))
    wanted_before = dict(final)
    wanted_before[PREFIX + "projects/copyprojectdialog.ui"] = diagnosis["before_stage_ui_sha256"]
    if before_map != wanted_before:
        raise RuntimeError("对齐前stage并非仅该表单换行不同。")
    for path in (before_ui, before_snapshot, originals / "apply.log"):
        alignment_originals.append({"path": path.relative_to(ROOT).as_posix(), "sha256": sha(regular(path))})
    blocked = TASK / "integration_06"
    blocked_state = scan_blocked(blocked)
    if relative_file(audit["blocked_prebuild_directory"]) != blocked:
        raise RuntimeError("审计的被拦构建目录不是固定路径。")
    recorded_blocked = {}
    for item in audit["blocked_prebuild_files"]:
        path, raw = evidence(item)
        recorded_blocked[path.relative_to(blocked).as_posix()] = sha(raw)
    if blocked_state["files"] != recorded_blocked:
        raise RuntimeError("被拦构建输入或日志已变化。")
    preserved = TASK / "integration_06_prebuild_blocked_before_attribute_recovery"
    preserved.resolve().relative_to(TASK.resolve())
    if preserved.exists() or preserved.is_symlink():
        raise RuntimeError("失败构建保留目录已存在，未覆盖。")
    # 所有外部输入再次复核后才登记恢复开始；此前不写正式文件或更改旧记录。
    if sha(regular(metadata_path)) != args.audit_metadata_sha256 or sha(regular(progress_path)) != FAILED_PROGRESS_SHA256:
        raise RuntimeError("恢复预核期间证据变化。")
    RECORDS.mkdir(exist_ok=False)
    original_record = write_new(RECORDS / "original_failed_progress.json", failed_raw)
    recovery = {"format": "sn100-integration-06-attribute-recovery-v1",
                "audit_metadata": {"path": metadata_path.relative_to(ROOT).as_posix(), "sha256": args.audit_metadata_sha256},
                "original_failed_progress": original_record, "diagnosis": audit["diagnosis"],
                "resume_script": {"path": Path(__file__).resolve().relative_to(ROOT).as_posix(), "sha256": sha(regular(Path(__file__)))},
                "alignment_originals": alignment_originals,
                "blocked_prebuild_move": {"original": blocked.relative_to(ROOT).as_posix(),
                                          "preserved": preserved.relative_to(ROOT).as_posix(), "checked_state": blocked_state},
                "recorded_without_reapply": [4], "resumed_patch_indices": [5, 6, 7, 8], "complete": False}
    write_new(RECORDS / "resume_started.json", recovery)
    progress["attribute_alignment_recovery"] = recovery
    progress["phase"] = "recovering_after_attribute_alignment"
    helper["save_progress"](progress)
    try:
        if scan_blocked(blocked) != blocked_state:
            raise RuntimeError("移动前被拦构建目录发生变化。")
        blocked.rename(preserved)
        recovery["blocked_prebuild_preservation"] = write_new(RECORDS / "blocked_prebuild_preservation.json",
            {"original": blocked.relative_to(ROOT).as_posix(), "preserved": preserved.relative_to(ROOT).as_posix(),
             "checked_state": blocked_state, "build_exit": 1, "objects_or_binaries": False})
        helper["verify_files"](ROOT, checkpoints[4])
        progress["applied"].append({"index": 4, "patch": patches[3][0], "sha256": patches[3][1]})
        progress["current_patch"] = None
        helper["save_progress"](progress)
        for index in range(5, 9):
            helper["verify_files"](ROOT, checkpoints[index - 1])
            check_attributes()
            for name, wanted in patches:
                if sha(regular(TASK / name)) != wanted:
                    raise RuntimeError("正式恢复前八片之一已变化。")
            name, wanted = patches[index - 1]
            progress["current_patch"] = {"index": index, "patch": name, "sha256": wanted}
            progress["formal_write_may_have_started"] = True
            helper["save_progress"](progress)
            helper["apply_patch"](TASK / name, wanted)
            helper["verify_files"](ROOT, checkpoints[index])
            progress["applied"].append(progress["current_patch"])
            progress["current_patch"] = None
            helper["save_progress"](progress)
        helper["compare_review"](ROOT, final)
        check_attributes()
        if sha(regular(metadata_path)) != args.audit_metadata_sha256:
            raise RuntimeError("恢复期间审计metadata变化。")
        for name, wanted in patches:
            if sha(regular(TASK / name)) != wanted:
                raise RuntimeError("恢复收尾时固定补丁变化。")
        completed = dict(recovery)
        completed.update({"complete": True, "formal_checkpoint": 8, "verified_source_paths": len(final),
                          "final_snapshot": checkpoint_records[8]})
        recovery["completion_evidence"] = write_new(RECORDS / "resume_complete.json", completed)
        recovery["complete"] = True
        progress["original_failure"] = progress.pop("error", "")
        progress.update({"phase": "complete", "complete": True, "verified_source_paths": len(final),
                         "changed_files": [name for name, value in final.items() if value != initial[name]],
                         "added_files": [name for name, value in final.items() if initial[name] is None and value is not None],
                         "deleted_files": [name for name, value in final.items() if initial[name] is not None and value is None],
                         "final_source_files": {name[len(PREFIX):]: value for name, value in final.items() if value is not None},
                         "promotion_script_sha256": PROMOTER_SHA256})
        helper["save_progress"](progress)
        print("RECOVERY_COMPLETED_AFTER_ATTRIBUTE_ALIGNMENT=1\nPATCH_4_REAPPLIED=0\nPATCHES_5_TO_8_APPLIED=4\nFORMAL_PATHS_MATCH_ALIGNED_REVIEW=649\nPRODUCT_EXECUTED=0")
    except BaseException as error:
        progress["phase"] = "failed_requires_review"
        progress["complete"] = False
        progress["error"] = str(error)
        recovery["complete"] = False
        helper["save_progress"](progress)
        raise


if __name__ == "__main__":
    main()
