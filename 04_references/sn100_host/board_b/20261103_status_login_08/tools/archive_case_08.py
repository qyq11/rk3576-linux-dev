#!/usr/bin/env python3
"""真实08采集完成后，在电脑保存有限证据和同版检查器；不联网、不运行产品。

用法：archive_case_08.py ABSOLUTE_COLLECTION_JSON ABSOLUTE_PARSED_SUMMARY_JSON
归档成功只表示文件已经核对和保存。失败、缺结果或待独立复核均按原状记录，
不会因为生成了归档就把软件、实屏、真实账户或仪器动作写成通过。
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import tarfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
# 由本批prepared/link/execution和最终handoff交叉填写，不沿用旧轮次数值。
RELEASE = APP = CHECKER = CONTRACT_SHA = SOURCE_ARCHIVE_SHA = None
SOURCE_COUNT = OBJECT_COUNT = None
SERIAL = "8314b1e8ffd0321f"
N_READY = "REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false"
TASK_READY = "TASK_REAGENT_SHUTDOWN_READY"
EMERGENCY_CHECK = "本次已验原INI布尔值与干净急诊初建孔的实际container一致"
EVIDENCE = ROOT / "04_references/sn100_host/board_b/20261103_status_login_08"
BACKUP = ROOT / "07_backups/sn100_host/board_b/status_login_08_20261103"
OFFICIAL = ROOT / "07_backups/sn100_host/board_b/workflow_integration_20261102/integration_08"
LIVE = ROOT / "temp/sn100_host/workflow_night_20261102/integration_08_live"
PRECHECK = ROOT / "temp/sn100_host/workflow_night_20261102/live_08_readonly_precheck"
PRECHECK_REQUIRED = tuple(stem + suffix for stem in ("precheck", "diagnose", "boot", "resume_controller")
                          for suffix in (".log", ".exit")) + ("manifest_access.log", "manifest_access.exit", "manifest_access.sh")
PRECHECK_OPTIONAL = ("after_resume.log", "after_resume.exit")
EXISTING = {"current_before.json", "README.md", "SHA256SUMS"}
CONTRACT_FILES = {"shutdown_contract.json", "required_shutdown_markers.txt",
                  "forbidden_shutdown_markers.txt", "shutdown_contract_release_id"}
RUN_FILES = {
    "application_exit.txt", "app.log", "case_exit.txt", "clock_ticks_per_second.txt",
    "controller_ini_before.sha256", "controller_ini_check.txt", "ended_at.txt", "identity.txt",
    "ini_before.sha256", "ini_check.txt", "preflight.log", "resources.tsv", "started_at.txt",
    "wrapper_exit.txt", "runtime/kms.json", "results/check_exit.txt", "results/result.json",
    "results/connected_header.png", "results/disconnected_header.png", "results/disconnected_lamp_first.png",
    "results/disconnected_lamp.png", "results/disconnected_lamp_second.png",
    "results/instrument_actual_status.png", "results/ad_current_generation.png",
    "shutdown_contract_files.sha256",
} | CONTRACT_FILES
PC_FILES = {
    "collection_authorization.json", "parser.log", "parser.exit", "remote_hashes_before.txt",
    "remote_hashes_after.txt", "remote_hashes_before.stderr", "remote_hashes_before.exit",
    "remote_hashes_after.stderr", "remote_hashes_after.exit", "remote_raw.stderr", "remote_raw.exit",
    "postcheck.txt", "postcheck.stderr", "postcheck.exit", "independent_ad_review.json",
    "independent_ad_review.log", "independent_ad_review.exit", "independent_wire_review.md", "independent_exit_scope_review.md",
    "actual_run_analysis.json", "ad_actual_result_review.md", "transport_steps.json",
}
CHECKER_FILES = (
    "status_login_review_08", "status_login_review_08.cpp", "ad_wire_observer.h",
    "run_case_on_board.sh", "capture_application_exit.sh", "summarize_run.py",
    "build_review_08.sh", "bind_build_08.py", "freeze_release_08.py", "shutdown_contract_08.py",
    "prepared_inputs.json", "source_snapshot_08.json", "link_provenance.json", "link_objects.txt",
    "review_build_provenance.json", "review_elf_header.txt", "review_binary.sha256", "review_release_id",
    "shutdown_contract.json", "required_shutdown_markers.txt", "forbidden_shutdown_markers.txt",
    "shutdown_contract_release_id", "execute_final_08_build.py", "execution_08_inputs_before.json",
    "execution_08_inputs_after.json", "execution_08_result.json", "freeze_08.log", "freeze_08.exit",
    "build_review_08.log", "build_review_08.exit",
    "checker_dependency_check.log", "checker_dependency_check.exit",
    "checker_dependencies/dependency_report.json", "checker_dependencies/dependency_summary.txt",
    "checker_dependencies/SHA256SUMS", "final_08_handoff.json", "final_08_build_handoff.md",
    "upload_manifest_08.sha256", "execution_08_driver_exit.txt", "create_handoff_08.py",
    "official_qa_handoff.json", "official_object_binding.json", "approved_final_merge_review.json",
    "task_shutdown_contract_adapter_candidate.py", "observation_scope_08.json",
    "initial_snapshot_provenance_review.md", "review_actual_ad_08.py",
    "collect_case_08.py", "collection_scope_08.md",
)
OPTIONAL_CHECKER_FILES = ("final_task_shutdown_contract_review.md", "emergency_container_observation_scope.md")


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def required_digest(value, name):
    require(isinstance(value, str) and re.fullmatch(r"[a-f0-9]{64}", value), name + "不是完整SHA-256")
    return value


def positive_count(value, name):
    require(type(value) is int and value > 0, name + "不是实际正整数")
    return value


def precheck_interruption_inputs():
    # 已审collector导入时只有常量和定义；只调用内存scrub，不调用main或SSH。
    from collect_case_08 import scrub
    require(PRECHECK.is_dir() and PRECHECK.resolve() == PRECHECK, "前置中断证据目录缺失或重定向")
    names = list(PRECHECK_REQUIRED)
    optional_present = [(PRECHECK / name).exists() for name in PRECHECK_OPTIONAL]
    require(all(optional_present) or not any(optional_present), "恢复后可选日志与退出记录须成对保存")
    if all(optional_present):
        names.extend(PRECHECK_OPTIONAL)
    records, contents = [], {}
    for name in names:
        source = regular(PRECHECK / name)
        require(source.stat().st_size <= 8 * 1024 * 1024, "前置文字证据超过8MiB预算：" + name)
        raw = source.read_bytes()
        local, redactions = scrub(raw)
        exact = raw == local
        require(exact or bool(redactions), "前置证据字节变化却没有脱敏原因：" + name)
        raw_sha, local_sha = hashlib.sha256(raw).hexdigest(), hashlib.sha256(local).hexdigest()
        require(sha(source) == raw_sha, "读取前置证据期间源文件变化：" + name)
        target = "precheck_interruption/" + name
        records.append({"source": rel(source), "target": target, "source_bytes": len(raw),
                        "local_bytes": len(local), "raw_sha256": raw_sha, "local_sha256": local_sha,
                        "stored_exactly_as_source": exact, "redactions": [] if exact else redactions,
                        "source_bytes_preserved": True})
        contents[target] = local
    return records, contents


def regular(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink() and path.resolve() == path,
            "需要未重定向的普通文件：" + str(path))
    return path


def sha(path):
    digest = hashlib.sha256()
    with regular(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            digest.update(block)
    return digest.hexdigest()


def read(path):
    return json.loads(regular(path).read_text(encoding="utf-8"))


def rel(path):
    return Path(path).relative_to(ROOT).as_posix()


def safe_name(name):
    require(isinstance(name, str), "清单路径不是文字")
    name = name.removeprefix("./")
    path = PurePosixPath(name)
    require(name and re.fullmatch(r"[A-Za-z0-9_./-]+", name) is not None
            and not path.is_absolute() and all(p not in {"", ".", ".."} for p in name.split("/")),
            "清单路径不安全：" + repr(name))
    return name


def sums(path):
    result = {}
    for line in regular(path).read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([a-f0-9]{64})  (.+)", line)
        require(match is not None, "校验清单格式错误：" + str(path))
        name = safe_name(match[2])
        require(name not in result, "校验清单重名：" + name)
        result[name] = match[1]
    require(result, "校验清单为空：" + str(path))
    return result


def new_text(path, value):
    path = Path(path)
    require(not path.exists() and not path.is_symlink(), "不能覆盖：" + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(value)


def new_json(path, value):
    new_text(path, json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n")


def file_code(path):
    if not path.exists():
        return None
    value = regular(path).read_text(encoding="utf-8").strip()
    return int(value) if re.fullmatch(r"-?[0-9]+", value) else None


def preserve_pointer_inputs():
    require(BACKUP.is_dir() and BACKUP.resolve() == BACKUP, "先由总协调保存08原指针恢复输入")
    conflicts = sorted(p.name for p in BACKUP.iterdir() if p.name not in EXISTING)
    require(not conflicts, "08已有其他归档条目，不能覆盖：" + ", ".join(conflicts))
    hashes = {name: sha(BACKUP / name) for name in EXISTING}
    existing_sums = sums(BACKUP / "SHA256SUMS")
    require(set(existing_sums) == EXISTING - {"SHA256SUMS"}, "原指针校验清单范围不符，先审查原件")
    require(all(hashes[name] == value for name, value in existing_sums.items()), "原指针恢复输入校验失败")
    pointer = read(BACKUP / "current_before.json")
    require(pointer.get("board") == "board_b" and pointer.get("serial") == SERIAL
            and isinstance(pointer.get("current_before"), str)
            and re.fullmatch(r"releases/[a-f0-9]{64}", pointer["current_before"])
            and pointer.get("intended_current") == "releases/" + RELEASE,
            "current_before不是板B本次实际原指针与08目标记录")
    return hashes


def final_build_inputs():
    global RELEASE, APP, CHECKER, CONTRACT_SHA, SOURCE_ARCHIVE_SHA, SOURCE_COUNT, OBJECT_COUNT
    prepared, bound, built, handoff = (read(HERE / name) for name in (
        "prepared_inputs.json", "link_provenance.json", "execution_08_result.json", "final_08_handoff.json"))
    RELEASE = required_digest(prepared.get("release_id"), "08发布身份")
    APP = required_digest(prepared.get("formal_identity", {}).get("application_sha256"), "08正式应用")
    CHECKER = required_digest(built.get("checker_sha256"), "08检查程序")
    SOURCE_ARCHIVE_SHA = required_digest(prepared.get("source_archive_sha256"), "08源码包")
    CONTRACT_SHA = required_digest(prepared.get("contract_sha256", {}).get("shutdown_contract.json"), "08关闭合同")
    SOURCE_COUNT = positive_count(prepared.get("source_count"), "08源码数")
    OBJECT_COUNT = positive_count(bound.get("object_count"), "08正式对象数")
    require(prepared.get("schema") == bound.get("schema") == "sn100-review-08-v1"
            and prepared.get("review_generation") == bound.get("review_generation") == "08"
            and prepared.get("status") == "bound_to_final_08_archive"
            and handoff.get("schema") == "sn100-final-08-checker-handoff-v1", "不是最终08输入")
    require(all(item.get("release_id") == RELEASE for item in (prepared, bound, built, handoff)), "检查器不是最终08")
    require(bound.get("source_count") == built.get("source_count") == handoff.get("source_count") == SOURCE_COUNT
            and built.get("object_count") == handoff.get("object_count") == OBJECT_COUNT
            and bound.get("main_object_kept") is True, "08实际源码/对象数或正式main绑定不符")
    objects = bound.get("objects", [])
    require(len(objects) == OBJECT_COUNT and [o.get("index") for o in objects] == list(range(OBJECT_COUNT))
            and bound.get("main_object", {}).get("input") == "shell/main.cpp", "对象完整记录或真实入口缺失")
    require(bound.get("main_object") == objects[0] == built.get("main_object") == handoff.get("main_object")
            and bound.get("formal_identity") == prepared.get("formal_identity")
            and bound.get("prepared_inputs_sha256") == sha(HERE / "prepared_inputs.json")
            and built.get("link_provenance_sha256") == sha(HERE / "link_provenance.json")
            and built.get("link_record_sha256") == handoff.get("link_record_sha256") == bound.get("record_sha256"),
            "08实际入口、prepared或链接记录不一致")
    require(prepared["formal_identity"]["application_sha256"] == APP
            and sha(HERE / "status_login_review_08") == built.get("checker_sha256") == handoff.get("checker_sha256") == CHECKER,
            "正式程序或新检查器SHA不符")
    require(handoff.get("formal_application_sha256") == APP
            and handoff.get("checker_source_sha256") == built.get("checker_source_sha256") == sha(HERE / "status_login_review_08.cpp")
            and handoff.get("observer_sha256") == built.get("observer_sha256") == sha(HERE / "ad_wire_observer.h"),
            "最终08handoff没有绑定本次正式应用和观察源码")
    require(built.get("checker_built") is True and built.get("complete_source_archive_dependencies_objects_unchanged") is True
            and handoff.get("actual_driver_exit_code") == 0 and handoff.get("before_after_identical") is True
            and all(handoff.get("actual_step_exit_codes", {}).get(step) == 0
                    for step in ("build_review", "freeze", "checker_dependency_check")), "实际构建/依赖结果不完整")
    require(sha(HERE / "execution_08_inputs_before.json") == sha(HERE / "execution_08_inputs_after.json"), "构建前后输入变化")
    for group in (prepared["template_sha256"], prepared["contract_sha256"], handoff["record_files_sha256"]):
        require(all(sha(HERE / name) == digest for name, digest in group.items()), "已冻结的工具/构建证据发生变化")
    snapshot = read(HERE / "source_snapshot_08.json")
    require(len(snapshot) == SOURCE_COUNT and snapshot == read(OFFICIAL / "source_snapshot.json"), "08源码清单与官方归档不符")
    package = OFFICIAL / ("sn100_sources_" + RELEASE + ".tar.gz")
    require(sha(package) == prepared["source_archive_sha256"] == SOURCE_ARCHIVE_SHA, "官方源码包SHA不符")
    official_handoff = read(HERE / "official_qa_handoff.json")
    require(official_handoff.get("release_id") == RELEASE and official_handoff.get("source_count") == SOURCE_COUNT
            and official_handoff.get("main_objects") == OBJECT_COUNT
            and ROOT / safe_name(official_handoff["source_archive"]["path"]) == package
            and official_handoff["source_archive"]["sha256"] == SOURCE_ARCHIVE_SHA
            and handoff.get("source_archive_sha256") == SOURCE_ARCHIVE_SHA,
            "正式08交接未绑定匹配的源码包与对象数")
    actual = {}
    with tarfile.open(package, "r:gz") as bundle:
        for member in bundle.getmembers():
            require(member.isfile() and not member.issym() and not member.islnk()
                    and member.name.startswith("06_programs/sn100_host/"), "官方源码包条目类型或前缀不符")
            name = safe_name(member.name.removeprefix("06_programs/sn100_host/"))
            require(name not in actual and name in snapshot, "官方源码包重名或多余：" + name)
            digest = hashlib.sha256()
            with bundle.extractfile(member) as stream:
                for block in iter(lambda: stream.read(1048576), b""):
                    digest.update(block)
            actual[name] = digest.hexdigest()
    require(actual == snapshot, "官方源码包逐文件回读与最终snapshot不符")
    contract = read(HERE / "shutdown_contract.json")
    require(sha(HERE / "shutdown_contract.json") == CONTRACT_SHA and contract.get("release_id") == RELEASE
            and contract.get("main_sha256") == snapshot["shell/main.cpp"]
            and isinstance(contract.get("required_markers"), list) and contract["required_markers"]
            and len(set(contract["required_markers"])) == len(contract["required_markers"])
            and handoff.get("required_markers") == contract["required_markers"]
            and handoff.get("required_marker_count") == len(contract["required_markers"])
            and "ORIGINAL_CHANNEL_WORKER_STOPPED true" in contract["required_markers"]
            and N_READY in contract["required_markers"], "实际08关闭条件不属于本轮")
    n_guard = contract.get("n_shutdown_guard", {})
    n_paths = {"shell/reagent_level_shell.cpp", "shell/frmmain.cpp",
               "reagents/original_reagent_level_worker.cpp", "reagents/original_reagent_level_worker.h"}
    require(set(n_guard.get("source_sha256", {})) == n_paths
            and all(n_guard["source_sha256"][name] == snapshot[name] for name in n_paths)
            and n_guard.get("required_ready_marker") == N_READY
            and n_guard.get("n_function_verified") is False and n_guard.get("in_flight_n_close_verified") is False,
            "N关闭来源或验收边界不属于本次最终08")
    require(sha(HERE / "run_case_on_board.sh") == contract["runner_sha256"], "实际runner与关闭条件不符")
    task_guard = contract.get("task_shutdown_guard", {})
    task_sources = task_guard.get("source_sha256", {})
    require(isinstance(task_sources, dict) and task_sources
            and all(snapshot.get(name) == value for name, value in task_sources.items())
            and task_guard.get("required_marker") == TASK_READY and TASK_READY in contract["required_markers"]
            and task_guard.get("approved_review_sha256") == sha(HERE / "approved_final_merge_review.json")
            and task_guard.get("task_business_verified") is False
            and task_guard.get("in_flight_task_single_outer_close_verified") is False, "Task实际关门条件未绑定或越界")
    scope = read(HERE / "observation_scope_08.json")
    require(scope == prepared.get("observation_scope") == handoff.get("observation_scope")
            and scope.get("release_id") == RELEASE
            and scope.get("checker_source_sha256") == built.get("checker_source_sha256")
            and all(scope.get(key) is False and built.get(key) is False and handoff.get(key) is False
                    for key in ("new_construction_proven", "same_main_memory_bool_read")), "急诊当前视图范围扩大")
    hashes = {name: sha(HERE / name) for name in CHECKER_FILES}
    hashes.update({name: sha(HERE / name) for name in OPTIONAL_CHECKER_FILES if (HERE / name).is_file()})
    return prepared, contract, hashes, package


def collection_inputs(collection_path, summary_path, prepared):
    metadata = read(collection_path)
    require(metadata.get("schema") == "sn100-status-login-08-collection-v1", "不是08采集元数据")
    require(metadata.get("board") == {"name": "board_b", "serial": SERIAL}
            and metadata.get("release_id") == RELEASE and metadata.get("application_sha256") == APP
            and metadata.get("checker_sha256") == CHECKER
            and metadata.get("source_count") == SOURCE_COUNT and metadata.get("object_count") == OBJECT_COUNT,
            "采集身份或版本不符")
    require(metadata.get("collection_complete") is True and metadata.get("collector_exit_code") == 0
            and metadata.get("all_selected_files_downloaded_and_verified") is True
            and metadata.get("all_remote_files_downloaded_and_verified") is False
            and metadata.get("read_only_remote_commands") is True
            and metadata.get("product_started_or_stopped") is False and metadata.get("current_changed") is False,
            "只读采集尚未完整完成")
    case, mode = metadata.get("case_name"), metadata.get("mode")
    require(case == "auto_01" and mode == "auto",
            "当前工具只归档已结束的08自动专项")
    run = Path(metadata["local_run_directory"])
    require(run.is_absolute() and run.resolve() == run and run == LIVE / case
            and run.name == case and collection_path == run / "collection.json", "本地采集路径越界或错配")
    require(metadata.get("remote_run_directory") ==
            "/home/rkshare/programs/sn100_shell/verification/status_login_08_20261103/runs/" + case, "远端case路径错配")
    manifest_info = metadata["remote_manifest"]
    require(manifest_info.get("path") == "remote_capture.sha256"
            and sha(run / "remote_capture.sha256") == manifest_info.get("sha256"), "原始采集全清单SHA不符")
    manifest = sums(run / "remote_capture.sha256")
    require(len(manifest) == manifest_info.get("file_count"), "远端清单实际数量不符")
    before = regular(run / "remote_hashes_before.txt").read_bytes()
    after = regular(run / "remote_hashes_after.txt").read_bytes()
    require(before == after and before.count(b"HASHES_BEGIN\n") == 1, "采集期间完整远端元数据发生变化")
    sizes_text, hashes_text = before.decode("utf-8").split("HASHES_BEGIN\n", 1)
    require(hashes_text.encode("utf-8") == (run / "remote_capture.sha256").read_bytes(), "完整远端表的SHA段与原清单不一致")
    remote_sizes = {}
    for line in sizes_text.splitlines():
        fields = line.split("\t")
        require(len(fields) == 2 and fields[0].isdigit(), "完整远端大小表格式错误")
        name = safe_name(fields[1])
        require(name not in remote_sizes, "完整远端大小表重名：" + name)
        remote_sizes[name] = int(fields[0])
    require(set(remote_sizes) == set(manifest), "远端大小表和SHA表路径范围不一致")
    partition, selected, excluded = {}, {}, {}
    for group, target in (("files", selected), ("excluded", excluded)):
        require(isinstance(metadata.get(group), list), "采集分区不是列表：" + group)
        for item in metadata[group]:
            name = safe_name(item["path"])
            require(name not in partition and item.get("source") == "remote", "采集分区重名或来源不符：" + name)
            require(name in manifest and item.get("sha256") == manifest[name]
                    and type(item.get("size")) is int and item["size"] == remote_sizes[name], "采集元数据与完整远端清单不符：" + name)
            if group == "files":
                require(name in RUN_FILES, "有限归档白名单外的远端对象，先审查：" + name)
                raw_hash, local_hash = item.get("raw_sha", item["sha256"]), item.get("local_sha", item["sha256"])
                require(item.get("local_verified") is True and raw_hash == manifest[name]
                        and isinstance(local_hash, str) and re.fullmatch(r"[a-f0-9]{64}", local_hash)
                        and sha(run / name) == local_hash, "选定文件没有与声明的本地字节匹配：" + name)
                if raw_hash == local_hash:
                    require(item.get("stored_exactly_as_remote") is True
                            and regular(run / name).stat().st_size == item["size"]
                            and item.get("redactions") == [], "原样选定文件大小或记录不符：" + name)
                else:
                    require(item.get("stored_exactly_as_remote") is False
                            and isinstance(item.get("redactions"), list) and bool(item["redactions"]),
                            "字节已经变化却没有明确脱敏记录：" + name)
                target[name] = local_hash
            else:
                require(name not in RUN_FILES and item.get("local_verified") is False
                        and isinstance(item.get("reason"), str) and item["reason"],
                        "未取回文件缺少明确原因，或把必要白名单文件当作缓存排除：" + name)
                target[name] = manifest[name]
            partition[name] = manifest[name]
    require(partition == manifest, "收集与排除没有完整覆盖原始远端清单")
    require({"app.log", "identity.txt", "wrapper_exit.txt"} | CONTRACT_FILES <= set(selected), "关键运行身份、日志或合同缺失")
    for name in CONTRACT_FILES:
        require(selected.get(name) == prepared["contract_sha256"][name], "板端实际关闭条件不是本轮最终合同：" + name)
    parser = metadata["parser"]
    require(parser.get("path") == "parsed_summary.json" and parser.get("encoding") == "utf-8"
            and type(parser.get("exit_code")) is int and summary_path == run / "parsed_summary.json"
            and sha(summary_path) == parser.get("sha256"), "UTF-8汇总或解析器记录错配")
    require(file_code(run / "parser.exit") == parser["exit_code"], "解析器真实退出记录与元数据不符")
    postcheck = metadata.get("postcheck", {})
    require(postcheck.get("path") == "postcheck.txt" and type(postcheck.get("exit_code")) is int
            and sha(run / "postcheck.txt") == postcheck.get("sha256")
            and file_code(run / "postcheck.exit") == postcheck["exit_code"], "事后只读核对记录与采集元数据不符")
    summary = read(summary_path)
    require(summary.get("release_id") == RELEASE, "本地汇总不属于最终08")
    identity = {}
    for line in regular(run / "identity.txt").read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            require(key not in identity, "身份记录重名：" + key)
            identity[key] = value
    require(all(identity.get(key) == value for key, value in {
        "MODE": mode, "RELEASE_ID": RELEASE, "APP_SHA256": APP, "PROBE_SHA256": CHECKER}.items()), "原始运行身份与采集元数据不符")
    return metadata, summary, run, selected, excluded, identity


def ad_text_proof(probe):
    """只重核已保存的本次上报文字和96个控件单元格，不解释物理测量准确性。"""
    ad = probe.get("independent_ad", {}) if isinstance(probe, dict) else {}
    try:
        old, new = int(ad["old_generation"]), int(ad["new_generation"])
        raw, comparison, coverage = ad["raw_observation"], ad["display_comparison"], ad["current_qq_coverage"]
        basic = (ad.get("passed") is True and ad.get("status") == "pass" and ad.get("same_new_connection") is True
                 and new > old >= 0 and int(raw["generation"]) == int(comparison["generation"]) == new
                 and raw.get("current_qq_received") is True and raw.get("valid_qq_count", 0) >= 1
                 and raw.get("error") == "" and raw.get("error_count") == 0 and raw.get("rejected_generation_feeds") == 0
                 and coverage.get("ok") is True and coverage.get("supplied_cells") == 96
                 and coverage.get("missing_or_short_keys") == [] and comparison.get("ok") is True
                 and comparison.get("status") == "pass" and comparison.get("checked_rows") == 24
                 and comparison.get("checked_cells") == 96 and comparison.get("mismatches") == [])
        rows = {}
        for row in ad["table_rows_at_comparison"]:
            key = row.get("key")
            if key in {str(i) for i in range(1, 25)}:
                if key in rows:
                    return False
                rows[key] = row
        if not basic or set(rows) != {str(i) for i in range(1, 25)}:
            return False
        channels = raw["channels_first_declared_pairs"]
        for key, row in rows.items():
            values = channels[key].split(" ")
            if (len(values) < 4 or row.get("values") != values[:4]
                    or not isinstance(row.get("reported"), list) or len(row["reported"]) != 4
                    or not all(flag is True for flag in row["reported"])):
                return False
        return True
    except (KeyError, TypeError, ValueError, AttributeError):
        return False


def independent_ad_status(run):
    path = run / "independent_ad_review.json"
    if not path.exists():
        return "pending"
    try:
        review = read(path)
        binding = review.get("binding", {})
        formal_binding = read(HERE / "link_provenance.json")
        snapshot = read(HERE / "source_snapshot_08.json")
        expected = {"release_id": RELEASE, "application_sha256": APP, "checker_sha256": CHECKER,
                    "prepared_sha256": sha(HERE / "prepared_inputs.json"), "link_sha256": sha(HERE / "link_provenance.json"),
                    "contract_sha256": CONTRACT_SHA, "source_count": SOURCE_COUNT, "formal_object_count": OBJECT_COUNT,
                    "checker_source_sha256": sha(HERE / "status_login_review_08.cpp"),
                    "observer_sha256": sha(HERE / "ad_wire_observer.h"), "main_source_sha256": snapshot["shell/main.cpp"],
                    "main_object_sha256": formal_binding["main_object"]["sha256"],
                    "source_archive_sha256": SOURCE_ARCHIVE_SHA, "link_record_sha256": formal_binding["record_sha256"],
                    "execution_sha256": sha(HERE / "execution_08_result.json"),
                    "inputs_before_and_after_sha256": sha(HERE / "execution_08_inputs_before.json"),
                    "required_shutdown_marker_count": len(read(HERE / "shutdown_contract.json")["required_markers"]),
                    "observation_scope_sha256": sha(HERE / "observation_scope_08.json")}
        probe = read(run / "results/result.json")
        evidence = review.get("evidence_sha256", {})
        expected_paths = {"app.log", "identity.txt", "results/result.json", "shutdown_contract.json"}
        valid = (review.get("schema") == "sn100-actual-ad-independent-review-08-v1"
                 and review.get("status") == "pass" and review.get("ad_evidence_consistent") is True
                 and review.get("checked_value_cells") == 96 and review.get("mismatches") == []
                 and review.get("n_function_verified") is False and review.get("in_flight_n_close_verified") is False
                 and review.get("task_business_verified") is False
                 and review.get("in_flight_task_single_outer_close_verified") is False
                 and review.get("new_construction_proven") is False and review.get("same_main_memory_bool_read") is False
                 and review.get("raw_emergency_default_container_observation") == probe.get("emergency_default_container_observation")
                 and review.get("software_checks_recorded") == len(probe.get("checks", []))
                 and all(binding.get(key) == value for key, value in expected.items())
                 and set(evidence) == expected_paths and all(sha(run / key) == evidence[key] for key in expected_paths)
                 and file_code(run / "independent_ad_review.exit") == 0)
        return "pass" if valid else "failed_or_incomplete"
    except (KeyError, TypeError, ValueError, UnicodeDecodeError, AttributeError, OSError, RuntimeError):
        return "invalid_or_incomplete"


def emergency_view_proof(probe):
    """只解释原记录和实际数组；空孔不提供创建来源或main内存bool证明。"""
    checks = probe.get("checks", []) if isinstance(probe, dict) else []
    raw = probe.get("emergency_default_container_observation") if isinstance(probe, dict) else None
    valid = isinstance(raw, dict) and isinstance(checks, list) and all(isinstance(item, dict) for item in checks)
    base, optional = [], []
    if valid:
        optional = [item for item in checks if item.get("name") == EMERGENCY_CHECK]
        base = [item for item in checks if item.get("name") != EMERGENCY_CHECK]
        evaluated = raw.get("evaluated")
        valid = (type(evaluated) is bool and type(raw.get("passed")) is bool
                 and len(base) == 24 and len(optional) == int(evaluated)
                 and type(probe.get("check_count")) is int and probe["check_count"] == len(checks)
                 and raw.get("same_main_memory_bool_read") is False)
        if valid and evaluated:
            valid = (raw.get("status") in {"pass", "fail"} and raw["passed"] is (raw["status"] == "pass")
                     and optional[0].get("passed") is raw["passed"])
        elif valid:
            valid = (raw.get("status") == "pending" and raw["passed"] is False
                     and isinstance(raw.get("reason"), str) and bool(raw["reason"]))
    return {
        "record_and_actual_check_count_consistent": bool(valid),
        "status": raw.get("status", "pending") if isinstance(raw, dict) else "missing",
        "actual_check_count": len(checks) if isinstance(checks, list) else None,
        "base_check_count": len(base), "optional_check_count": len(optional),
        "current_view_comparison_accepted": bool(valid and raw.get("evaluated") is True and raw.get("passed") is True),
        "new_construction_proven": False, "same_main_memory_bool_read": False,
        "raw_emergency_default_container_observation": raw,
        "scope": "只核当前急诊视图记录与未变化INI映射；不证明创建来源、创建时点或main同一内存bool。",
    }


def postcheck_proof(metadata, run, identity):
    text = regular(run / "postcheck.txt").read_text(encoding="utf-8", errors="replace")
    fields = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    controller = identity.get("CONTROLLER_PID", "")
    expected = {
        "BOARD_SERIAL": SERIAL, "CURRENT": "/home/rkshare/programs/sn100_shell/releases/" + RELEASE,
        "CONTROLLER_PID": controller, "CONTROLLER_EXE": "/userdata/shared/ACA_V1.0OffLineD-500",
        "CONTROLLER_SHA256": "ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff",
        "APPLICATION_COUNT": "0", "CHECKER_COUNT": "0", "CONTROLLER_COUNT": "1",
        "POSTCHECK_INI_EXIT": "0", "POSTCHECK_CONTROLLER_INI_EXIT": "0",
    }
    return (metadata["postcheck"]["exit_code"] == 0 and bool(re.fullmatch(r"[1-9][0-9]*", controller))
            and all(fields.get(key) == value for key, value in expected.items())
            and ": FAILED" not in text and ": OK" in text
            and re.search(r"pid=" + re.escape(controller) + r",", text) is not None)


def assess_result(metadata, summary, run, identity, contract):
    text = regular(run / "app.log").read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
    markers = {name: bool(re.search(r"^" + re.escape(name) + r"\s*$", text, re.M)) for name in contract["required_markers"]}
    failures = [pattern for pattern in contract["failure_patterns"] if re.search(pattern, text, re.M)]
    codes = {"wrapper": file_code(run / "wrapper_exit.txt"), "application": file_code(run / "application_exit.txt"),
             "check": file_code(run / "results/check_exit.txt"), "case": file_code(run / "case_exit.txt"),
             "parser": metadata["parser"]["exit_code"]}
    probe_path = run / "results/result.json"
    parse_error = None
    try:
        probe = read(probe_path) if probe_path.exists() else None
    except (ValueError, UnicodeDecodeError) as error:
        probe, parse_error = None, str(error)
    checks = probe.get("checks", []) if isinstance(probe, dict) else []
    emergency = emergency_view_proof(probe)
    probe_ok = (isinstance(probe, dict) and probe.get("release_id") == RELEASE and probe.get("completed") is True
                and type(probe.get("failed")) is int and probe["failed"] == 0
                and isinstance(checks, list) and bool(checks)
                and all(isinstance(c, dict) and c.get("passed") is True for c in checks)
                and emergency["record_and_actual_check_count_consistent"])
    ad_ok = ad_text_proof(probe)
    independent_status = independent_ad_status(run)
    postcheck_ok = postcheck_proof(metadata, run, identity)
    critical = {"app.log", "identity.txt", "results/result.json"} | CONTRACT_FILES
    exact_critical = all(item.get("stored_exactly_as_remote") is True and item.get("raw_sha") == item.get("local_sha")
                         for item in metadata["files"] if item["path"] in critical)
    consistent = (summary.get("mode") == metadata["mode"] and summary.get("wrapper_exit") == codes["wrapper"]
                  and summary.get("application_wait_exit") == codes["application"]
                  and summary.get("automatic_check_exit") == codes["check"]
                  and summary.get("shutdown_contract_release") == RELEASE
                  and summary.get("shutdown_markers") == markers and summary.get("current_shutdown_failures") == failures
                  and summary.get("probe_result") == probe)
    accepted = (summary.get("software_checks_passed") is True and consistent
                and summary.get("schema") != "sn100-status-login-08-parser-unavailable-v1"
                and all(type(code) is int and code == 0 for code in codes.values())
                and all(markers.values()) and not failures and probe_ok and ad_ok
                and summary.get("scope_errors") == [] and summary.get("ini_hashes_unchanged") is True
                and summary.get("same_controller_alive") is True and identity.get("SAME_CONTROLLER_ALIVE") == "1"
                and postcheck_ok
                and exact_critical
                and bool(re.search(r"^EXIT code=\s*0\s*$", text, re.M)))
    return {
        "automatic_checker_scope_accepted": bool(accepted), "summary_claimed_software_pass": summary.get("software_checks_passed") is True,
        "summary_matches_raw_exit_markers_and_probe": bool(consistent), "actual_exit_codes": codes,
        "required_marker_count": len(markers), "actual_required_markers": markers, "actual_failure_patterns": failures,
        "result_present": probe_path.exists(), "probe_result_parse_error": parse_error,
        "probe_complete_and_no_failed_checks": bool(probe_ok),
        "current_generation_96_ad_text_rechecked": bool(ad_ok),
        "postcheck_facts_accepted": bool(postcheck_ok), "independent_ad_review_status": independent_status,
        "automatic_ad_evidence_accepted": bool(accepted and independent_status == "pass"),
        "critical_evidence_stored_exactly_as_remote": exact_critical,
        "n_shutdown_ready_observed": markers.get(N_READY) is True,
        "n_accepted_log_records": len(re.findall(r"^REAGENT_LEVEL_ACCEPTED\s", text, re.M)),
        "n_processed_log_records": len(re.findall(r"^REAGENT_LEVEL_PROCESSED\s", text, re.M)),
        "n_display_log_records": len(re.findall(r"^REAGENT_LEVEL_DISPLAY\s", text, re.M)),
        "n_function_verified": False, "in_flight_n_close_verified": False,
        "task_shutdown_ready_observed": markers.get(TASK_READY) is True,
        "task_business_verified": False, "in_flight_task_single_outer_close_verified": False,
        "new_construction_proven": False, "same_main_memory_bool_read": False,
        "emergency_current_view_observation": emergency,
        "overall_instrument_acceptance": "not_concluded_by_archiver",
    }


def verify_bundle(path, entries):
    actual = {}
    with tarfile.open(path, "r:gz") as bundle:
        for member in bundle.getmembers():
            require(member.isfile() and not member.issym() and not member.islnk()
                    and member.name.startswith("checker/"), "新检查器包条目越界或不是普通文件")
            name = safe_name(member.name.removeprefix("checker/"))
            require(name in entries and name not in actual, "新检查器包多余或重名：" + name)
            digest = hashlib.sha256()
            with bundle.extractfile(member) as stream:
                for block in iter(lambda: stream.read(1048576), b""):
                    digest.update(block)
            actual[name] = digest.hexdigest()
    require(actual == entries, "新检查器包逐文件回读不符")


def final_manifest(base, name):
    entries = {}
    for path in sorted(base.rglob("*")):
        require(not path.is_symlink(), "新归档含链接：" + str(path))
        if path.is_file() and path != base / name:
            entries[path.relative_to(base).as_posix()] = sha(path)
    new_text(base / name, "".join(value + "  " + key + "\n" for key, value in entries.items()))
    require(sums(base / name) == entries and all(sha(base / key) == value for key, value in entries.items()), "最终校验清单回读不符")
    return len(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("collection_json", type=Path)
    parser.add_argument("parsed_summary_json", type=Path)
    args = parser.parse_args()
    require(args.collection_json.is_absolute() and args.parsed_summary_json.is_absolute(), "两个输入都必须是绝对本地路径")
    require(not EVIDENCE.exists() and not EVIDENCE.is_symlink(), "04目标已存在，不能覆盖：" + str(EVIDENCE))
    prepared, contract, checker_hashes, source_package = final_build_inputs()
    original_hashes = preserve_pointer_inputs()
    metadata, summary, run, selected, excluded, identity = collection_inputs(args.collection_json, args.parsed_summary_json, prepared)
    assessment = assess_result(metadata, summary, run, identity, contract)
    precheck_records, precheck_contents = precheck_interruption_inputs()
    # 全部身份与范围检查先完成；之后只创建新文件，不修改原输入或三份原指针文件。
    EVIDENCE.mkdir(parents=True)
    copied = []

    def copy(source, target):
        source = regular(source)
        target = Path(target)
        require(target.is_relative_to(EVIDENCE) or target.is_relative_to(BACKUP), "复制目标越界")
        require(not target.exists() and not target.is_symlink(), "复制目标已存在：" + str(target))
        target.parent.mkdir(parents=True, exist_ok=True)
        require(target.parent.resolve() == target.parent, "复制目标目录被重定向")
        before = sha(source)
        with source.open("rb") as origin, target.open("xb") as destination:
            for block in iter(lambda: origin.read(1048576), b""):
                destination.write(block)
        require(sha(source) == sha(target) == before, "复制期间内容改变：" + str(source))
        copied.append({"source": rel(source), "target": rel(target), "sha256": before})

    for name in sorted(selected):
        target = EVIDENCE / metadata["case_name"] / name
        copy(run / name, target)
        require(sha(target) == selected[name], "选定本地文件在校验和复制之间变化：" + name)
    local_names = {"collection.json", "parsed_summary.json", "remote_capture.sha256"}
    local_names |= {name for name in PC_FILES if (run / name).is_file()}
    for name in sorted(local_names):
        if name in {"collection.json", "parsed_summary.json", "collection_authorization.json"}:
            read(run / name)  # 必须是有效UTF-8 JSON；失败汇总按原字节保存，不补写结论。
        copy(run / name, EVIDENCE / metadata["case_name"] / name)
    for name in checker_hashes:
        copy(HERE / name, EVIDENCE / "checker" / name)
    for record in precheck_records:
        target = EVIDENCE / record["target"]
        require(not target.exists() and not target.is_symlink(), "前置证据目标已存在")
        target.parent.mkdir(parents=True, exist_ok=True)
        require(target.parent.resolve() == target.parent, "前置证据目标目录重定向")
        with target.open("xb") as stream:
            stream.write(precheck_contents[record["target"]])
        require(sha(target) == record["local_sha256"] and sha(ROOT / record["source"]) == record["raw_sha256"],
                "保存前置证据期间源或本地字节变化")
    copy(Path(__file__).resolve(), EVIDENCE / "tools/archive_case_08.py")
    if (HERE / "collect_case_08.py").is_file():
        copy(HERE / "collect_case_08.py", EVIDENCE / "tools/collect_case_08.py")
    bundle_path = BACKUP / ("status_login_review_08_" + CHECKER + ".tar.gz")
    entries = dict(checker_hashes)
    entries["archive_case_08.py"] = sha(Path(__file__).resolve())
    require(not bundle_path.exists() and not bundle_path.is_symlink(), "检查器包已存在，不能覆盖")
    with bundle_path.open("xb") as output, tarfile.open(fileobj=output, mode="w:gz") as bundle:
        for name in sorted(entries):
            source = HERE / name
            info = tarfile.TarInfo("checker/" + name)
            info.size = regular(source).stat().st_size
            info.mode = 0o755 if name == "status_login_review_08" else 0o644
            info.mtime = int(source.stat().st_mtime)
            with source.open("rb") as stream:
                bundle.addfile(info, stream)
    verify_bundle(bundle_path, entries)
    new_json(BACKUP / "checker_bundle_manifest.json", {
        "archive": bundle_path.name, "sha256": sha(bundle_path), "files": entries,
        "readback_verified_files": len(entries), "formal_source_archive_reference": rel(source_package),
        "formal_source_archive_sha256": sha(source_package), "formal_sources_duplicated": False,
    })
    scope = ("本次只保存已结束的08自动专项有限证据及同版检查器，不联网、不运行或重跑产品。\n"
             "完整远端大小及SHA表在采集前后相同；只取回白名单必要日志、JSON与数字/状态图并逐份核对。\n"
             "缓存、登录画面等排除项只保存远端元数据，未取回、未在电脑核其内容；不宣称全部远端文件本地核过。\n"
             "选定文件若脱敏，分别保存raw_sha/local_sha与修改说明；本地副本不冒充原样，关键证据字节变化时不自动接受完整原始通信结论。\n"
             "本轮实际源码仅引用匹配的官方归档；不复制业务库、INI内容、账户凭据或整机环境。\n"
             "08原current_before.json、README.md、SHA256SUMS字节保留；新清单叫archive_SHA256SUMS。\n"
             "检查器恢复包只供同一正式08发布配套使用，不能替代主程序、依赖或指针恢复记录。\n"
             "归档校验成功不代表功能通过。缺结果、解析失败、AD不齐或退出不完整均保留实际状态。\n"
             "AD文字比对不证明物理读数准确；实体键鼠/实屏、管理员口令、测试绿灯、机械动作、42/48位置及800/405差异仍按各自证据验收。\n")
    scope += "N READY仅证明关闭保护检查为空；N功能及在途N一次外层退出仍待真实场景验证。\n"
    scope += ("Task READY只核真实关门等待结束，不证明事务结果、页面采用或在途一次退出。\n"
              "急诊项按实际checks数组及evaluated/pending分别记录，不预填总数。原始结果字节保留。\n"
              "急诊比较通过仅指当前快照与本次未变化INI重验值一致；new_construction_proven=false、"
              "same_main_memory_bool_read=false，不证明新孔来源、时点、main同一内存bool、普通建架、另一bool、手选保存或CAL清空。\n")
    scope += ("前置证据仅取precheck、diagnose、boot、resume_controller的log/exit，manifest_access的log/exit/sh及已有的after_resume配对文件，"
              "不取整个目录；先在内存沿用collect08.scrub检查，逐份记录源raw与本地SHA和必要脱敏原因，源原件不改。\n"
              "这组资料分别说明板B重启、恢复原launcher及启动前指纹清单访问条件；清单不可读不等于INI变化，不能拼接为08运行时长、长稳或业务通过。\n")
    new_text(EVIDENCE / "collection_scope.md", scope)
    new_text(BACKUP / "checker_restore_scope.md", scope)
    provenance = {
        "schema": "sn100-status-login-08-limited-archive-v1", "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "board": "board_b", "serial": SERIAL, "release_id": RELEASE, "application_sha256": APP,
        "checker_sha256": CHECKER, "source_count": SOURCE_COUNT, "object_count": OBJECT_COUNT,
        "formal_source_archive": rel(source_package), "formal_source_archive_sha256": sha(source_package),
        "formal_source_readback_verified_files": SOURCE_COUNT, "source_snapshot_sha256": sha(HERE / "source_snapshot_08.json"),
        "shutdown_contract_sha256": CONTRACT_SHA, "checker_archive": rel(bundle_path), "checker_archive_sha256": sha(bundle_path),
        "checker_archive_readback_verified_files": len(entries), "case_name": metadata["case_name"],
        "collection_metadata_sha256": sha(args.collection_json), "parsed_summary_sha256": sha(args.parsed_summary_json),
        "complete_remote_manifest_sha256": metadata["remote_manifest"]["sha256"],
        "remote_manifest_entry_count": len(selected) + len(excluded),
        "all_remote_files_locally_verified": False, "selected_local_files_verified": len(selected),
        "selected_remote_files": sorted(selected), "selected_file_records": metadata["files"],
        "selected_files_stored_exactly_as_remote": sum(item["stored_exactly_as_remote"] is True for item in metadata["files"]),
        "selected_files_with_redactions": [dict(item, actual_local_size=(run / item["path"]).stat().st_size)
                                          for item in metadata["files"] if item["stored_exactly_as_remote"] is False],
        "excluded_remote_files": metadata["excluded"], "excluded_remote_file_count": len(excluded),
        "excluded_contents_downloaded_or_locally_verified": False,
        "pc_derived_and_collection_records": sorted(local_names), "result_assessment": assessment,
        "preserved_existing_pointer_files": original_hashes, "existing_pointer_bytes_preserved": True,
        "precheck_interruption_records": precheck_records,
        "precheck_interruption_scope": "板B重启、恢复与指纹清单访问前置证据；清单不可读不等于INI变化。不拼接为08长稳或业务通过。只白名单文字，无整目录归档。",
        "resource_scope": "仅本轮上位机短窗采样，不作同板控制、温度降频、资源预算或长稳通过结论。",
        "physical_input_screen_verified": False, "administrator_credentials_verified": False,
        "normal_ui_close_reopen_verified": False, "real_testing_green_verified": False,
        "mechanical_or_detection_functions_verified": False, "copied_inputs": copied,
        "n_function_verified": False, "in_flight_n_close_verified": False,
        "task_business_verified": False, "in_flight_task_single_outer_close_verified": False,
        "new_construction_proven": False, "same_main_memory_bool_read": False,
        "board_accessed_during_archive": False, "product_rerun_during_archive": False,
    }
    require({name: sha(BACKUP / name) for name in EXISTING} == original_hashes, "原指针恢复文件在归档期间变化")
    require(sha(source_package) == prepared["source_archive_sha256"], "归档期间引用的官方源码包发生变化")
    require(all(sha(HERE / name) == digest for name, digest in checker_hashes.items()), "归档期间检查器或构建证据变化")
    require(all(sha(ROOT / item["source"]) == sha(ROOT / item["target"]) == item["sha256"] for item in copied), "归档源或副本变化")
    require(all(sha(ROOT / item["source"]) == item["raw_sha256"]
                and sha(EVIDENCE / item["target"]) == item["local_sha256"] for item in precheck_records),
            "归档期间前置中断源或经检查的本地证据变化")
    for base in (EVIDENCE, BACKUP):
        new_json(base / "provenance.json", provenance)
    counts = {rel(EVIDENCE): final_manifest(EVIDENCE, "SHA256SUMS"),
              rel(BACKUP): final_manifest(BACKUP, "archive_SHA256SUMS")}
    require({name: sha(BACKUP / name) for name in EXISTING} == original_hashes, "最终校验后原指针文件变化")
    targets = [str(p.relative_to(ROOT)) for base in (EVIDENCE, BACKUP) for p in base.rglob("*") if p.is_file()]
    ignored = subprocess.run(["git", "check-ignore", "--stdin"], cwd=ROOT, input="\n".join(targets) + "\n", text=True, capture_output=True)
    require(ignored.returncode == 1 and not ignored.stdout, "新归档文件被忽略或检查异常：" + ignored.stdout + ignored.stderr)
    attributes = subprocess.run(["git", "check-attr", "filter", "diff", "merge", "--", rel(bundle_path)], cwd=ROOT, text=True, capture_output=True)
    require(attributes.returncode == 0 and len(attributes.stdout.splitlines()) == 3
            and all(line.endswith(": lfs") for line in attributes.stdout.splitlines()), "新检查器包缺少完整LFS属性")
    print(json.dumps({"archive_integrity_verified": True, "manifest_entries": counts,
                      "automatic_checker_scope_accepted": assessment["automatic_checker_scope_accepted"],
                      "independent_ad_review_status": assessment["independent_ad_review_status"],
                      "existing_pointer_bytes_preserved": True, "board_accessed": False}, ensure_ascii=False))


if __name__ == "__main__":
    main()
