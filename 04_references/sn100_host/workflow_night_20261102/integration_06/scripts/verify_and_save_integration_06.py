"""逐份核第06批源码/程序包和板B暂存证据，再新建本批证据目录。"""
from pathlib import Path
from datetime import datetime
import argparse
import hashlib
import json
import re
import runpy
import shutil
import stat
import tarfile

TASK = Path(__file__).resolve().parent
ROOT = TASK.parents[2]
WORK = TASK / "integration_06"
NETWORK_COMPLETE_SHA256 = "3ed80114ad19a2fa531edb8c08e73bb7081c0888a152c715224b37647a4130b8"
CURRENT_05 = "releases/e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270"
REVIEWS = (
    "integration_06_review/merge_review.json", "integration_06_review/cross_patch_review.md",
    "integration_06_review/cross_patch_source_snapshot.json", "integration_06_review/cross_patch_build_inputs.tsv",
    "integration_06_bridges/README.md", "integration_06_bridges/independent_review.md",
    "report_lis/channel_switch_candidate_v2/handoff.md", "report_lis/channel_switch_candidate_v2/independent_fix_review.md",
    "report_lis/channel_switch_candidate_v2/verification_result.md", "report_lis/channel_switch_candidate_v2/patches.sha256",
    "project_update_intents/handoff.md", "project_update_intents/interface_contract.md",
    "project_update_intents/candidate_manifest.json", "project_update_intents/final_independent_review.md",
    "project_update_intents_v2/handoff.md", "project_update_intents_v2/candidate_manifest.json",
    "project_update_intents_v2/independent_fix_review.md",
    "report_lis/qq_actual_count_review/handoff.md", "report_lis/qq_actual_count_review/source_review.md",
    "report_lis/qq_actual_count_review/independent_review.md", "report_lis/qq_actual_count_review/handoff_files.sha256",
    "project_copy_v2/handoff.md", "project_copy_v2/proof_contract.md", "project_copy_v2/reader_boundaries.md",
    "project_copy_v2/review_manifest.json", "project_copy_v2/independent_recovery_review.md", "project_copy_v2/patches.sha256",
)
SCRIPTS = (
    "prepare_integration_06_promotion.py", "promote_integration_06_template.py.in", "promote_integration_06_review.py",
    "build_integration_06.sh", "verify_integration_06_inputs.py", "archive_integration_06.py",
    "stage_board_b_release_06.py", "stage_board_b_release_06.sh", "verify_and_save_integration_06.py",
    "finish_integration_06_archive.py", "integration_06_tools_handoff.md",
    "resume_integration_06_after_attribute_alignment.py", "verify_integration_06_inputs_before_attribute_recovery.py",
    "verify_integration_06_inputs_optional_recovery_83d9.py",
    "archive_integration_06.py.before_attribute_recovery", "verify_and_save_integration_06.py.before_attribute_recovery",
    "finish_integration_06_archive.py.before_attribute_recovery", "integration_06_tools_handoff.md.before_attribute_recovery",
    "verify_and_save_integration_06.py.before_network_recovery", "finish_integration_06_archive.py.before_network_recovery",
    "resume_stage_06_wired.py", "run_board_b_script_06_wired.py",
    "inspect_stage_06_interruption_01.sh", "inspect_stage_06_interruption_02_wired.sh",
    "verify_and_save_integration_06.py.before_network_copy_directory_fix",
    "recover_integration_06_collector_directory_fix.py", "collector_attempt_01_tool_result.json",
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe(name):
    if (not name or "\\" in name or ":" in name or name.startswith("/")
            or any(x in ("", ".", "..") for x in name.split("/"))):
        raise RuntimeError("归档路径越界。")
    return name


def checksums(directory):
    rows = []
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise RuntimeError("归档中出现链接，停止：" + str(path))
        if path.is_file() and path.name != "SHA256SUMS":
            rows.append(digest(path.read_bytes()) + "  " + path.relative_to(directory).as_posix())
    (directory / "SHA256SUMS").write_text("\n".join(rows) + "\n", encoding="ascii")
    for row in rows:
        expected, name = row.split("  ", 1)
        if digest((directory / name).read_bytes()) != expected:
            raise RuntimeError("归档逐份复算失败：" + name)
    return len(rows)


def network_record(item, expected_path):
    path = ROOT / safe(item["path"])
    if path != expected_path:
        raise RuntimeError("网络恢复证据不在固定原路径：" + item["path"])
    path.resolve().relative_to(TASK.resolve())
    for entry in (path, *path.parents):
        if entry.is_symlink() or (hasattr(entry, "is_junction") and entry.is_junction()):
            raise RuntimeError("网络恢复证据经过链接。")
        if entry == TASK:
            break
    if not path.is_file() or digest(path.read_bytes()) != item["sha256"]:
        raise RuntimeError("网络恢复证据指纹不符：" + item["path"])
    return path


def resolve_board_stage(provenance, explicit_recovery_sha256):
    if (WORK / "upload.exit").read_text().strip() != "0":
        raise RuntimeError("原上传未成功。")
    original_exit = (WORK / "board_b_stage.exit").read_text().strip()
    if original_exit == "0":
        if explicit_recovery_sha256:
            raise RuntimeError("原保存已成功，不接受额外网络恢复代替它。")
        return WORK / "logs/board_b_stage.log", None, {}
    if original_exit != "255" or explicit_recovery_sha256 != NETWORK_COMPLETE_SHA256:
        raise RuntimeError("原保存未成功；必须显式指定已核网络恢复complete的SHA。")
    folder = WORK / "stage_network_recovery"
    complete_path = folder / "complete.json"
    complete_item = {"path": complete_path.relative_to(ROOT).as_posix(), "sha256": NETWORK_COMPLETE_SHA256}
    network_record(complete_item, complete_path)
    record = json.loads(complete_path.read_text(encoding="utf-8"))
    if (record.get("format") != "sn100-integration-06-stage-network-recovery-v1"
            or record.get("complete") is not True or record.get("original_stage_exit") != 255
            or record.get("actual_resumed_stage_exit") != 0 or record.get("activation_performed") is not False
            or record.get("program_started") is not False or record.get("board") != "board_b"
            or record.get("serial") != "8314b1e8ffd0321f" or record.get("ssh_host") != "root@192.168.100.123"
            or record.get("host_key_alias") != "rk3576-board-b" or record.get("current_before") != CURRENT_05
            or record.get("release_id") != provenance["release_id"]
            or record.get("package_sha256") != provenance["package_sha256"]):
        raise RuntimeError("网络恢复未完整成功，或原板卡/程序包/current身份不符。")
    expected_inputs = {
        "original_started": WORK / "stage_started.json", "original_stage_log": WORK / "logs/board_b_stage.log",
        "original_stage_exit": WORK / "board_b_stage.exit", "upload_exit": WORK / "upload.exit",
        "wired_inspection_log": TASK / "inspect_stage_06_interruption_02_wired.log",
        "wired_inspection_exit": TASK / "inspect_stage_06_interruption_02_wired.exit",
        "wired_inspection_script": TASK / "inspect_stage_06_interruption_02_wired.sh",
        "stage_script": TASK / "stage_board_b_release_06.sh", "resume_tool": TASK / "resume_stage_06_wired.py",
    }
    if set(record["inputs"]) != set(expected_inputs):
        raise RuntimeError("网络恢复缺完整原输入链。")
    copies = {complete_path: "stage_network_recovery/complete.json"}
    for name, expected_path in expected_inputs.items():
        path = network_record(record["inputs"][name], expected_path)
        copies[path] = "stage_network_recovery/inputs/" + path.relative_to(TASK).as_posix()
    pinned = {
        "stage_script": "8af975382001058b0eb0d52b2f021d992cad4199cfd0b9bbbc26d7f6ae944809",
        "resume_tool": "c50020b25d203d6d697eddb7f5f3b7b2da76c8353efe9e6f87c551cb90fc3c76",
        "wired_inspection_log": "79ce382678800b9709c57d4afd94b4370e2bc1f0ff39b403142e3fe77b522903",
        "original_stage_log": "122fe5b07412700f17cc10f788d008f45925ff06641c2bb79e4b02ef9c48a49e",
        "original_stage_exit": "a730e9f4fecef62a7023b1b3b6b11312e9689ef5a5c2109737bc7e12db929948",
    }
    if any(record["inputs"][name]["sha256"] != value for name, value in pinned.items()):
        raise RuntimeError("原脚本、无线255原件或已核有线检查身份不符。")
    initial = json.loads((WORK / "stage_started.json").read_text(encoding="utf-8"))
    if (initial["remote_upload"] != record["remote_upload"] or initial["release_id"] != record["release_id"]
            or initial["package_sha256"] != record["package_sha256"]):
        raise RuntimeError("恢复没有沿同一已上传包继续。")
    inspection = (TASK / "inspect_stage_06_interruption_02_wired.log").read_text(encoding="utf-8")
    if ("FINAL_RELEASE_PRESENT=0" not in inspection.splitlines() or "STAGE_PARENT=" in inspection
            or "CURRENT_RELEASE=" + CURRENT_05 not in inspection.splitlines()
            or (TASK / "inspect_stage_06_interruption_02_wired.exit").read_text().strip() != "0"):
        raise RuntimeError("有线检查未证明原current和没有发布/临时保存结果。")
    expected_results = {name: folder / name for name in ("preflight.sh", "preflight.log", "preflight.exit", "stage.log", "stage.exit")}
    if set(record["results"]) != set(expected_results):
        raise RuntimeError("网络恢复缺完整预检和实际保存结果。")
    for name, path in expected_results.items():
        network_record(record["results"][name], path)
        copies[path] = "stage_network_recovery/" + name
    if (folder / "preflight.exit").read_text().strip() != "0" or (folder / "stage.exit").read_text().strip() != "0":
        raise RuntimeError("有线预检或实际保存退出码不为0。")
    preflight = (folder / "preflight.log").read_text(encoding="utf-8").splitlines()
    if any(line not in preflight for line in ("BOARD=board_b", "SERIAL=8314b1e8ffd0321f", "EXISTING_UPLOAD_HASH_OK=1", "NO_STAGE_OR_RELEASE=1")):
        raise RuntimeError("有线续做前没有核同板/原包/空目标现场。")
    started_path = network_record(record["started_record"], folder / "started.json")
    started = json.loads(started_path.read_text(encoding="utf-8"))
    expected_started = {key: value for key, value in record.items() if key not in
                        ("completed_at", "original_stage_exit", "actual_resumed_stage_exit", "activation_performed", "program_started", "started_record", "results")}
    expected_started["complete"] = False
    if started != expected_started:
        raise RuntimeError("续做前在途记录和实际终态不一致。")
    copies[started_path] = "stage_network_recovery/started.json"
    extras = {
        "inspect_stage_06_interruption_01.sh": "427ec42e5e9db95e145507cc101ec5da5c2670680277d924b94f0f506bd09a30",
        "inspect_stage_06_interruption_01.log": "8005cf63d96c2780b0e335f9dd8e495945c2bfcf0dc6511139e99b2ea37d846b",
        "inspect_stage_06_interruption_01.exit": "a730e9f4fecef62a7023b1b3b6b11312e9689ef5a5c2109737bc7e12db929948",
        "run_board_b_script_06_wired.py": "c16ac85fed6a9f49204337dd95239d6e95d54ea3e224f747cd04f37f6dcd88fd",
    }
    for name, wanted in extras.items():
        path = TASK / name
        network_record({"path": path.relative_to(ROOT).as_posix(), "sha256": wanted}, path)
        copies[path] = "stage_network_recovery/inputs/" + name
    summary = {"complete_record": complete_item, "original_stage_exit": 255, "actual_resumed_stage_exit": 0,
               "board_log": record["results"]["stage.log"], "stage_script_sha256": pinned["stage_script"],
               "inspection_sha256": pinned["wired_inspection_log"], "current_before": CURRENT_05,
               "activation_performed": False, "program_started": False}
    return folder / "stage.log", summary, copies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage-network-recovery-sha256", default="")
    args = parser.parse_args()
    archive = ROOT / "07_backups/sn100_host/board_b/workflow_integration_20261102/integration_06"
    recorded = Path((WORK / "archive_path.txt").read_text(encoding="utf-8").strip())
    # 根可能在Windows归档，路径记录仅作对照；实际读写位置始终是固定06目录。
    if recorded.name != "integration_06" or archive.is_symlink():
        raise RuntimeError("不是本批固定归档目录。")
    for row in (archive / "SHA256SUMS").read_text(encoding="ascii").splitlines():
        wanted, name = row.split("  ", 1)
        if digest((archive / safe(name)).read_bytes()) != wanted:
            raise RuntimeError("原归档指纹不符：" + name)
    provenance = json.loads((archive / "provenance.json").read_text(encoding="utf-8"))
    snapshot = json.loads((archive / "source_snapshot.json").read_text(encoding="utf-8"))
    complete = json.loads((archive / "build_complete.json").read_text(encoding="utf-8"))
    progress = json.loads((WORK / "merge_inputs.json").read_text(encoding="utf-8"))
    if (WORK / "merge_inputs.json").read_bytes() != (TASK / "integration_06_review/promotion_progress.json").read_bytes():
        raise RuntimeError("构建后逐片晋级记录已变化。")
    release = provenance["release_id"]
    if not re.fullmatch(r"[a-f0-9]{64}", release):
        raise RuntimeError("发布标识无效。")
    source = archive / ("sn100_sources_" + release + ".tar.gz")
    if digest(source.read_bytes()) != provenance["source_archive_sha256"]:
        raise RuntimeError("源码包指纹不符。")
    seen = set()
    with tarfile.open(source, "r:gz") as bundle:
        for member in bundle:
            prefix = "06_programs/sn100_host/"
            if not member.isfile() or not member.name.startswith(prefix):
                raise RuntimeError("源码包包含非预期条目。")
            name = safe(member.name[len(prefix):])
            if name in seen or name not in snapshot or digest(bundle.extractfile(member).read()) != snapshot[name]:
                raise RuntimeError("源码条目重复、缺失或指纹不符：" + name)
            seen.add(name)
    if seen != set(snapshot) or snapshot != progress["final_source_files"]:
        raise RuntimeError("源码恢复包未覆盖全批审阅结果。")
    package = archive / ("sn100_shell_" + release + ".tar.gz")
    if digest(package.read_bytes()) != provenance["package_sha256"]:
        raise RuntimeError("程序包指纹不符。")
    files, special = {}, {}
    with tarfile.open(package, "r:gz") as bundle:
        for member in bundle:
            name = safe(member.name)
            if member.isdir():
                continue
            if not member.isfile() or name in files:
                raise RuntimeError("程序包有重复或非普通条目。")
            data = bundle.extractfile(member).read()
            files[name] = digest(data)
            if name in ("SHA256SUMS", "release_id", "release.json"):
                special[name] = data
    if digest(special["SHA256SUMS"]) != release or special["release_id"].decode("ascii").strip() != release:
        raise RuntimeError("程序包内发布标识不符。")
    wanted_files = {}
    for row in special["SHA256SUMS"].decode("ascii").splitlines():
        wanted, name = row.split("  ", 1)
        name = safe(name)
        if name in wanted_files or not re.fullmatch(r"[a-f0-9]{64}", wanted):
            raise RuntimeError("程序包清单重复或格式无效。")
        wanted_files[name] = wanted
    if {name: value for name, value in files.items() if name not in ("SHA256SUMS", "release_id")} != wanted_files:
        raise RuntimeError("程序包逐份解包后的清单不符。")
    metadata = json.loads(special["release.json"])
    if (metadata["sources"] != snapshot or metadata["binary_sha256"] != provenance["application_sha256"]
            or metadata["renderer_sha256"] != provenance["report_renderer_sha256"]
            or metadata["dependencies_sha256"] != complete["dependency_report_sha256"]
            or files["sn100_shell"] != provenance["application_sha256"]
            or files["sn100_report_renderer"] != provenance["report_renderer_sha256"]):
        raise RuntimeError("程序包内源码/二进制/依赖身份不符。")
    board_log_path, network_recovery, network_copies = resolve_board_stage(provenance, args.stage_network_recovery_sha256)
    started = json.loads((WORK / "stage_started.json").read_text(encoding="utf-8"))
    if (started.get("batch") != "integration_06" or started.get("release_id") != release
            or started.get("package_sha256") != provenance["package_sha256"]
            or started.get("stage_script_sha256") != digest((TASK / "stage_board_b_release_06.sh").read_bytes())):
        raise RuntimeError("板B保存的实际脚本或程序包身份不符。")
    board_log = board_log_path.read_text(encoding="utf-8")
    for expected in ("BOARD=board_b", "SERIAL=8314b1e8ffd0321f", "STAGE_BATCH=integration_06",
                     "STAGED_RELEASE=" + release, "ACTIVATED=0", "PROGRAM_STARTED=0",
                     "PACKAGE_SHA256=" + provenance["package_sha256"]):
        if expected not in board_log.splitlines():
            raise RuntimeError("板B日志缺少准确身份/范围：" + expected)
    for name, expected in (("sn100_shell", provenance["application_sha256"]),
                           ("sn100_report_renderer", provenance["report_renderer_sha256"])):
        pattern = re.escape(expected) + r"\s+" + re.escape("/home/rkshare/programs/sn100_shell/releases/" + release + "/" + name)
        if not re.search(r"(?m)^" + pattern + r"$", board_log):
            raise RuntimeError("板B实际程序指纹不符：" + name)
    if "not found" in board_log:
        raise RuntimeError("板B实际动态库缺失。")
    current = [line[len("CURRENT_RELEASE="):] for line in board_log.splitlines() if line.startswith("CURRENT_RELEASE=")]
    if len(current) != 1:
        raise RuntimeError("板B日志未明确保存时的current。")
    if network_recovery is not None and current[0] != CURRENT_05:
        raise RuntimeError("有线实际保存改变了原current，未记为只保存通过。")
    destination = ROOT / "04_references/sn100_host/workflow_night_20261102/integration_06"
    if destination.exists() or destination.is_symlink():
        raise RuntimeError("已有06证据目录，请人工核查，不覆盖。")
    copies = {
        WORK / "logs/build.log": "build.log", WORK / "build_exit.txt": "build_exit.txt",
        WORK / "build_inputs.tsv": "build_inputs.tsv", WORK / "merge_inputs.json": "merge_inputs.json",
        WORK / "input_verification.json": "input_verification.json", WORK / "build_complete.json": "build_complete.json",
        TASK / "integration_06_review/promotion_progress.json": "promotion_progress.json",
        WORK / "logs/board_b_stage.log": "board_b_stage.log", WORK / "board_b_stage.exit": "board_b_stage.exit",
        WORK / "logs/upload.log": "upload.log", WORK / "upload.exit": "upload.exit", WORK / "stage_started.json": "stage_started.json",
        archive / "source_snapshot.json": "source_snapshot.json", archive / "dependency_report.json": "dependency_report.json",
    }
    for path in [*copies, *network_copies, *(TASK / name for name in REVIEWS), *(TASK / name for name in SCRIPTS)]:
        if not path.is_file() or path.is_symlink():
            raise RuntimeError("缺少固定证据原文：" + str(path))
    for item in progress["patches"]:
        if digest((TASK / safe(item["patch"])).read_bytes()) != item["sha256"]:
            raise RuntimeError("补丁已漂移，未保存混合证据。")
    recovery_copies = {}
    recovery = progress.get("attribute_alignment_recovery")
    if recovery is not None:
        if recovery.get("complete") is not True or complete.get("attribute_alignment_recovery", {}).get("audit_metadata_sha256") != recovery["audit_metadata"]["sha256"]:
            raise RuntimeError("本批构建没有核准换行属性恢复。")
        audit_path = ROOT / safe(recovery["audit_metadata"]["path"])
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        preserved_record = json.loads((ROOT / recovery["blocked_prebuild_preservation"]["path"]).read_text(encoding="utf-8"))
        preserved = ROOT / safe(preserved_record["preserved"])
        preserved.resolve().relative_to(TASK.resolve())
        blocked_original = ROOT / safe(audit["blocked_prebuild_directory"])
        blocked_entries = {item["path"] for item in audit["blocked_prebuild_files"]}
        records = []

        def collect(value):
            if isinstance(value, dict):
                if isinstance(value.get("path"), str) and isinstance(value.get("sha256"), str):
                    records.append(value)
                for nested in value.values():
                    collect(nested)
            elif isinstance(value, list):
                for nested in value:
                    collect(nested)

        collect(recovery)
        collect(audit)
        for item in records:
            original = ROOT / safe(item["path"])
            if item["path"] in blocked_entries:
                name = original.relative_to(blocked_original).as_posix()
                original = preserved / name
                destination_name = "attribute_alignment/blocked_prebuild/" + name
            elif item == audit["root_attributes"]:
                destination_name = "attribute_alignment/root.gitattributes"
            else:
                destination_name = None
            original.resolve().relative_to(ROOT.resolve())
            if original.is_symlink() or not original.is_file() or digest(original.read_bytes()) != item["sha256"]:
                raise RuntimeError("恢复/审计原件已变化：" + item["path"])
            # 第05批源码包保持在原归档；这里只核并保留来源引用，不复制旧版本大包。
            if item == audit["source05_archive"]:
                continue
            if destination_name is None:
                original.resolve().relative_to(TASK.resolve())
                destination_name = "attribute_alignment/" + original.relative_to(TASK).as_posix()
            recovery_copies[original] = destination_name
        for name, wanted in preserved_record["checked_state"]["files"].items():
            original = preserved / safe(name)
            original.resolve().relative_to(preserved.resolve())
            if digest(original.read_bytes()) != wanted:
                raise RuntimeError("首次被拦预构建证据已变化。")
            recovery_copies[original] = "attribute_alignment/blocked_prebuild/" + name
    directory_fix = WORK / "collector_directory_fix"
    fix_record = json.loads((directory_fix / "complete.json").read_text(encoding="utf-8"))
    if (fix_record.get("complete") is not True or fix_record.get("original_collector_exit") != 1
            or fix_record.get("fixed_collector_sha256") != digest(Path(__file__).read_bytes())
            or fix_record.get("original_failed_tool_sha256") != "a7bf5381fcaf107592795ea81d965d2a65cccf9d8d2e496f159cfdd62e851f94"
            or fix_record.get("failure_tool_result_sha256") != "c7c892a190de7680d26faf9dbe148ec72f016b539bf7fb96346ba6f3e39bdb12"):
        raise RuntimeError("本次归档器失败后的目录保留证据不完整。")
    expected_partial = {
        "build.log": "440e1756494dfdab53de938ed038e60c789a9b1def9277fd8941833a4bfd1c8b",
        "build_exit.txt": "9a271f2a916b0b6ee6cecb2426f0b3206ef074578be55d9bc94f6f3fe3ab86aa",
        "build_inputs.tsv": "315bf3b463a230e51c772e21d7e709fce2a43bac7820b7ba0ebe5385a59ba0fb",
        "merge_inputs.json": "079ad9dabd9acd28b5db8d3b1b7a19cf9b9eb45aa2290d5ea8ed92bfeab298d2",
        "input_verification.json": "b4e06c647cfb98310032d489443f82cc01d87a228bc77586624c559c2318286e",
        "build_complete.json": "e90806afa8b096adf086aa2498fdb559de1bbde1685761f8f27bcc197c21a02f",
        "promotion_progress.json": "079ad9dabd9acd28b5db8d3b1b7a19cf9b9eb45aa2290d5ea8ed92bfeab298d2",
    }
    preserved_partial = ROOT / "04_references/sn100_host/workflow_night_20261102/integration_06_collector_incomplete_01"
    if (fix_record.get("partial_files") != expected_partial
            or fix_record.get("preserved_directory") != preserved_partial.relative_to(ROOT).as_posix()
            or fix_record.get("original_directory") != destination.relative_to(ROOT).as_posix()
            or fix_record.get("rename_performed") is not True or fix_record.get("verification_was_present") is not False
            or fix_record.get("new_evidence_directory_created") is not False):
        raise RuntimeError("目录恢复不是固定七文件和精确保留路径。")
    recovery_tool = TASK / "recover_integration_06_collector_directory_fix.py"
    network_record({"path": recovery_tool.relative_to(ROOT).as_posix(), "sha256": fix_record["recovery_tool_sha256"]}, recovery_tool)
    if recovery_tool.lstat().st_nlink != 1:
        raise RuntimeError("恢复工具不是单链接原件。")
    helper = runpy.run_path(str(recovery_tool), run_name="sn100_collector_partial_readonly_check")
    if (helper["FIXED_TOOL_SHA"] != digest(Path(__file__).read_bytes())
            or helper["OLD_TOOL_SHA"] != fix_record["original_failed_tool_sha256"]
            or helper["FAILURE_SHA"] != fix_record["failure_tool_result_sha256"]):
        raise RuntimeError("恢复工具与本次固定工具/失败身份不符。")
    if (digest(helper["raw"](TASK / "verify_and_save_integration_06.py.before_network_copy_directory_fix")) != helper["OLD_TOOL_SHA"]
            or digest(helper["raw"](TASK / "collector_attempt_01_tool_result.json")) != helper["FAILURE_SHA"]):
        raise RuntimeError("旧工具或根转存的真实工具返回已变化。")
    if json.loads(helper["raw"](directory_fix / "complete.json")) != fix_record:
        raise RuntimeError("恢复终态读取期间变化。")
    fix_started = json.loads(helper["raw"](directory_fix / "started.json"))
    expected_complete = dict(fix_started)
    expected_complete.update({"complete": True, "rename_performed": True})
    if fix_started.get("complete") is not False or fix_started.get("rename_performed") is not False or expected_complete != fix_record:
        raise RuntimeError("恢复started与complete不只两项完成标志差异。")
    if helper["check_partial"](preserved_partial) != expected_partial:
        raise RuntimeError("保留目录的完整集合、单链接和七份原来源未全部核准。")
    for name in expected_partial:
        source = preserved_partial / name
        copies[source] = "collector_directory_fix/partial/" + name
    copies[directory_fix / "started.json"] = "collector_directory_fix/started.json"
    copies[directory_fix / "complete.json"] = "collector_directory_fix/complete.json"
    # 包括刚追加的恢复凭据在内，全部源原件须在创建新归档前存在并核普通单链接/无路径链接。
    for source in [*copies, *network_copies, *recovery_copies, *(TASK / name for name in REVIEWS), *(TASK / name for name in SCRIPTS)]:
        helper["ordinary_path"](source)
        info = source.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise RuntimeError("归档所需原件缺失或不是单链接普通文件：" + str(source))
    destination.mkdir(parents=True, exist_ok=False)
    for original, name in copies.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    # 相同源文件需要同时保留根级原255与网络恢复输入副本，不能用Path-key合并覆盖。
    for original, name in network_copies.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    for name in REVIEWS:
        target = destination / "source_review" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(TASK / name, target)
    for item in progress["patches"]:
        target = destination / "patches" / item["patch"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(TASK / item["patch"], target)
    (destination / "scripts").mkdir()
    for name in SCRIPTS:
        shutil.copyfile(TASK / name, destination / "scripts" / name)
    for original, name in recovery_copies.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    provenance.update({"board_staging": "passed_manifest_sha256_and_runtime_dependencies",
                       "source_archive_readback_files": len(seen), "package_archive_readback_files": len(files),
                       "current_release_at_staging": current[0]})
    if network_recovery is not None:
        provenance["board_staging"] = "passed_after_explicit_wired_network_recovery"
        provenance["stage_network_recovery"] = network_recovery
    provenance["collector_directory_recovery"] = {
        "original_exit": 1, "preserved_directory": fix_record["preserved_directory"],
        "failure_tool_result_sha256": fix_record["failure_tool_result_sha256"],
        "evidence_origin": "根转存当次exec_command返回，不是collector自行生成的原日志或重跑结果。"}
    text = json.dumps(provenance, ensure_ascii=False, indent=2) + "\n"
    (archive / "provenance.json").write_text(text, encoding="utf-8")
    (destination / "provenance.json").write_text(text, encoding="utf-8")
    verification = {
        "captured_at": datetime.now().astimezone().isoformat(), "batch": "integration_06", "release_id": release,
        "arm64_build_exit": 0, "source_count": len(snapshot), "unique_build_inputs": complete["unique_build_inputs"],
        "compiled_objects": len(list((WORK / "arm64_build").glob("obj_*.o"))),
        "helper_objects": len(list((WORK / "arm64_build").glob("helper_*.o"))),
        "source_archive_readback_files": len(seen), "package_archive_readback_files": len(files),
        "board": "board_b", "serial": "8314b1e8ffd0321f", "board_stage_exit": 0,
        "current_release_changed": False, "new_program_executed": False, "controller_restarted": False,
        "rk_function_verified": False, "instrument_verified": False, "runtime_data_or_credentials_archived": False,
    }
    if recovery is not None:
        verification["initial_promotion_failed_at_patch"] = 4
        verification["resumed_without_reapplying_patch4"] = True
        verification["attribute_alignment_recovery"] = complete["attribute_alignment_recovery"]
    if network_recovery is not None:
        verification["initial_board_stage_exit"] = 255
        verification["stage_network_recovery"] = network_recovery
    verification["collector_directory_recovery"] = provenance["collector_directory_recovery"]
    (destination / "verification.json").write_text(json.dumps(verification, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("BACKUP_CHECKSUM_FILES=" + str(checksums(archive)))
    print("EVIDENCE_CHECKSUM_FILES=" + str(checksums(destination)))
    print(json.dumps(verification, ensure_ascii=False))


if __name__ == "__main__":
    main()
