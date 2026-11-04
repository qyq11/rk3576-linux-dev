"""下一批工具共用的文件核对；只有总控给出冻结清单后才允许执行各阶段。"""
from pathlib import Path
from datetime import datetime
import hashlib
import base64
import json
import os
import re
import stat
import sys

TOOLS = Path(__file__).resolve().parent
TASK = TOOLS.parent
ROOT = TASK.parents[2]
WORK = TASK / "integration_11_01"
PROGRAM = ROOT / "06_programs/sn100_host"
ARCHIVE = ROOT / "07_backups/sn100_host/board_b/workflow_integration_20261103/integration_11_01"
EVIDENCE = ROOT / "04_references/sn100_host/workflow_next_20261103/integration_11_01"
CONFIG = TOOLS / "integration_11_01_tools_config.json"
SHA = re.compile(r"[a-f0-9]{64}\Z")
LIVE_GUARD_FORMAT = "sn100-integration-11-01-live-guard-v2"
LIVE_GUARD_KEYS = (
    "FORMAT", "BOARD", "SERIAL", "BOOT_ID", "CURRENT_RELEASE",
    "MAIN_PID", "MAIN_UID", "MAIN_START_TICKS", "MAIN_EXE", "MAIN_EXE_SHA256",
    "CONTROLLER_PID", "CONTROLLER_UID", "CONTROLLER_START_TICKS", "CONTROLLER_EXE", "CONTROLLER_EXE_SHA256",
    "CONTROLLER_LISTEN_ENDPOINTS", "POINTER_ORIGINAL_PROJECT_ROOT_PATH", "POINTER_ORIGINAL_PROJECT_ROOT_SHA256",
    "POINTER_INSTRUMENT_CONFIGURATION_FILE_PATH", "POINTER_INSTRUMENT_CONFIGURATION_FILE_SHA256",
    "POINTER_THEME_CONFIGURATION_FILE_PATH", "POINTER_THEME_CONFIGURATION_FILE_SHA256", "CONFIGURATION_SHA256",
)
CONFIGURATION_GUARD_FORMAT = "sn100-integration-11-01-configuration-guard-v1"
# 只保存总控已冻结的七个配置路径和指纹，不复制配置内容。
PROTECTED_CONFIGURATION_PATHS = (
    "/home/rkshare/programs/sn100_shell/runtime/data/config_Debug.ini",
    "/home/rkshare/programs/sn100_shell/runtime/data/config.ini",
    "/home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini",
    "/userdata/shared/config_linux.ini",
    "/userdata/shared/consume.ini",
    "/userdata/shared/GL4A_config.cfg",
    "/userdata/shared/number.ini",
)



def records_in(value):
    if isinstance(value, dict):
        if "path" in value and "sha256" in value:
            yield value
        for child in value.values():
            yield from records_in(child)
    elif isinstance(value, list):
        for child in value:
            yield from records_in(child)


def safe_name(value):
    if (not isinstance(value, str) or not value or "\\" in value or ":" in value
            or value.startswith("/") or any(x in value for x in "\0\r\n")
            or any(part in ("", ".", "..") for part in value.split("/"))):
        raise RuntimeError("文件名必须是范围内的相对路径：" + repr(value))
    return value


def ordinary_path(path, within=ROOT):
    path = Path(path)
    path.resolve().relative_to(Path(within).resolve())
    for parent in (path, *path.parents):
        if parent.is_symlink() or (hasattr(parent, "is_junction") and parent.is_junction()):
            raise RuntimeError("路径经过链接，未继续：" + str(parent))
        if parent == Path(within):
            break
    return path


def require_regular(path, within=ROOT):
    path = ordinary_path(path, within)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise RuntimeError("不是单链接普通原件：" + str(path))
    return path


def sha(path):
    require_regular(path)
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path):
    return json.loads(require_regular(path).read_text(encoding="utf-8"))


def write_new(path, value):
    path = ordinary_path(path)
    if not path.parent.is_dir():
        raise RuntimeError("先明确创建输出父目录：" + str(path.parent))
    data = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    with path.open("xb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def record(path):
    path = require_regular(path)
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path), "bytes": path.stat().st_size}


def local_path(value):
    if os.name == "nt" and value.startswith("/mnt/c/"):
        value = "C:/" + value[7:]
    return Path(value)


def check_record(item):
    path = ROOT / safe_name(item["path"])
    if not isinstance(item.get("sha256"), str) or not SHA.fullmatch(item["sha256"]) or sha(path) != item["sha256"]:
        raise RuntimeError("冻结原件指纹不符：" + str(path))
    if "bytes" in item and item["bytes"] != path.stat().st_size:
        raise RuntimeError("冻结原件字节数不符：" + str(path))
    return path


def validate_configuration_table(raw):
    if (not isinstance(raw, bytes) or not raw.endswith(b"\n") or raw.endswith(b"\n\n")
            or b"\r" in raw or b"\0" in raw or len(raw) > 8192):
        raise RuntimeError("配置指纹表必须是七行LF原文。")
    rows = raw.decode("ascii").split("\n")[:-1]
    paths = []
    for row in rows:
        match = re.fullmatch(r"([a-f0-9]{64})  (/[A-Za-z0-9_./-]+)", row)
        if not match:
            raise RuntimeError("配置指纹表含不规范的SHA或绝对路径。")
        paths.append(match.group(2))
    if tuple(paths) != PROTECTED_CONFIGURATION_PATHS or len(set(paths)) != len(paths):
        raise RuntimeError("配置指纹表不是总控指定的七个唯一路径和顺序。")
    return raw


def protected_configuration_bytes(config):
    item = config["protected_configuration_manifest"]
    path = check_record(item)
    if item != record(path):
        raise RuntimeError("配置指纹清单须明确记录原件路径、SHA和字节数。")
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != item["sha256"] or len(raw) != item["bytes"]:
        raise RuntimeError("读取配置指纹清单期间原件变化。")
    return validate_configuration_table(raw)


def decode_guard_markers(lines, prefix):
    values = {}
    for suffix in ("SHA256", "BASE64"):
        marker = prefix + "_" + suffix + "="
        found = [line[len(marker):] for line in lines if line.startswith(marker)]
        if len(found) != 1:
            raise RuntimeError("板端保护标记缺失或重复：" + marker)
        values[suffix] = found[0]
    if not SHA.fullmatch(values["SHA256"]) or len(values["BASE64"]) > 32768:
        raise RuntimeError("板端保护标记格式或大小不符。")
    raw = base64.b64decode(values["BASE64"], validate=True)
    if (len(raw) > 16384 or hashlib.sha256(raw).hexdigest() != values["SHA256"]
            or base64.b64encode(raw).decode("ascii") != values["BASE64"]):
        raise RuntimeError("板端保护原文、编码或指纹不符。")
    return raw


def decode_configuration_guards(lines, config):
    expected = protected_configuration_bytes(config)
    actual = tuple(validate_configuration_table(decode_guard_markers(lines, "CONFIG_GUARD_" + phase))
                   for phase in ("BEFORE", "AFTER"))
    if any(raw != expected for raw in actual):
        raise RuntimeError("板端实际七份配置指纹不等于总控冻结清单。")
    return actual


def configuration_guard(config):
    expected = protected_configuration_bytes(config)
    for name in ("configuration_guard_before.sha256", "configuration_guard_after.sha256"):
        if validate_configuration_table(require_regular(WORK / name).read_bytes()) != expected:
            raise RuntimeError("保存的实际配置指纹表不等于冻结原件：" + name)
    return {"format": CONFIGURATION_GUARD_FORMAT,
            "expected_manifest": record(check_record(config["protected_configuration_manifest"])),
            "before": record(WORK / "configuration_guard_before.sha256"),
            "after": record(WORK / "configuration_guard_after.sha256"), "unchanged": True}


def verify_configuration_guard(config, declared, lines, evidence=None):
    expected = configuration_guard(config)
    if declared != expected:
        raise RuntimeError("配置保护记录不是同一冻结清单和两份板端原文。")
    actual = decode_configuration_guards(lines, config)
    for index, name in enumerate(("configuration_guard_before.sha256", "configuration_guard_after.sha256")):
        if require_regular(WORK / name).read_bytes() != actual[index]:
            raise RuntimeError("配置保护原文不等于板端单次输出：" + name)
        if evidence is not None and require_regular(evidence / name).read_bytes() != actual[index]:
            raise RuntimeError("配置保护证据副本变化：" + name)
    if (evidence is not None and require_regular(evidence / "protected_configuration_manifest.sha256").read_bytes()
            != protected_configuration_bytes(config)):
        raise RuntimeError("证据中的冻结配置指纹清单变化。")
    return expected


def cfg():
    value = read_json(CONFIG)
    if value.get("format") != "sn100-integration-11-01-tools-config-v2" or value.get("frozen") is not True:
        raise RuntimeError("下一批最终值尚未由根冻结；本工具只是模板，未执行。")
    permissions = value.get("operation_permissions")
    required_permissions = {"allow_full_build", "allow_stage"}
    if (not isinstance(permissions, dict) or set(permissions) != required_permissions
            or any(type(flag) is not bool for flag in permissions.values())):
        raise RuntimeError("缺少总控逐项固定的完整构建与文件暂存范围；切换许可另见包后意图。")
    observation = value.get("observation_contract", {})
    if (not isinstance(observation, dict) or observation.get("format") != "sn100-lis-keep-running-observation-v1"
            or observation.get("actual_start_owner") != "root"
            or observation.get("runtime_environment_names") != ["SN100_LIS_OBSERVATION_DIR", "SN100_LIS_OBSERVATION_ID", "SN100_LIS_OBSERVATION_RELEASE"]
            or observation.get("runtime_values_location") != "separate_root_launch_intent_after_package"):
        raise RuntimeError("观察合同不是总控裁定的同进程被动观察和包后启动交接。")
    check_record(observation["root_decision"])
    if not any(item == observation["root_decision"] for item in value.get("evidence", [])):
        raise RuntimeError("总控观察裁定须作为固定证据保留；包后运行值不可写回本配置。")
    if value.get("approved_final_review_sha256") != value["final_review"]["sha256"]:
        raise RuntimeError("最终整合元数据不是根明确确认的那一份。")
    for name in ("final_review", "promotion_progress", "promotion_script", "baseline_archive", "baseline_snapshot", "final_source_manifest"):
        check_record(value[name])
    if not re.fullmatch(r"releases/[a-f0-9]{64}", value.get("expected_current", "")):
        raise RuntimeError("缺少根冻结的板端当前版本保护值。")
    protected_configuration_bytes(value)
    if not any(item == value["protected_configuration_manifest"] for item in value.get("evidence", [])):
        raise RuntimeError("总控冻结的配置指纹清单须作为完整证据记录保留。")
    for name in ("expected_running_application_sha256", "expected_controller_sha256"):
        if not isinstance(value.get(name), str) or not SHA.fullmatch(value[name]):
            raise RuntimeError("缺少总控冻结的现有应用或主控实际SHA：" + name)
    controller = value.get("expected_controller_exe", "")
    if (not isinstance(controller, str) or not controller.startswith("/")
            or not re.fullmatch(r"/[A-Za-z0-9_./-]+", controller)
            or any(part in ("", ".", "..") for part in controller[1:].split("/"))):
        raise RuntimeError("缺少总控核定的实际主控可执行文件绝对路径。")
    formats = value["metadata_schema"]
    if not all(isinstance(formats.get(key), str) and formats[key] for key in ("review_format", "promotion_format")):
        raise RuntimeError("最终元数据与晋级记录的格式名称尚未固定。")
    review = read_json(check_record(value["final_review"]))
    if review.get("format") != formats["review_format"]:
        raise RuntimeError("不是下一批最终整合元数据。")
    snapshot_map(review["base_source_files"])
    snapshot_map(review["final_source_files"])
    if review.get("source_count") != len(review["final_source_files"]) or not review.get("patches"):
        raise RuntimeError("最终源码数量或动态补丁清单不完整。")
    for item in records_in(review):
        check_record(item)
    if read_json(check_record(value["baseline_snapshot"])) != review["base_source_files"]:
        raise RuntimeError("冻结底稿不等于最终整合记录的起点。")
    if read_json(check_record(value["final_source_manifest"])) != review["final_source_files"]:
        raise RuntimeError("完整最终源码清单与元数据内的文件表不相同。")
    indexes = []
    for item in review["patches"]:
        indexes.append(item["index"])
        check_record({"path": item["patch"], "sha256": item["sha256"]})
        check_record(item["before_checkpoint"])
        check_record(item["after_checkpoint"])
    if indexes != list(range(1, len(indexes) + 1)):
        raise RuntimeError("动态补丁顺序不连续。")
    value["patches"] = [{"path": item["patch"], "sha256": item["sha256"]} for item in review["patches"]]
    value["review_metadata"] = review
    for name in ("evidence", "tools"):
        if not isinstance(value.get(name), list) or not value[name]:
            raise RuntimeError("缺少本批动态冻结清单：" + name)
        seen = set()
        for item in value[name]:
            path = check_record(item)
            if path in seen:
                raise RuntimeError("同一清单出现重复原件：" + str(path))
            seen.add(path)
    required_tools = {
        "integration_11_01_tool_support.py", "build_integration_11_01.sh", "verify_integration_11_01_inputs.py",
        "archive_integration_11_01.py", "stage_board_b_release_11_01.py", "stage_board_b_release_11_01.sh",
        "verify_and_save_integration_11_01.py", "finish_integration_11_01_archive.py",
        "integration_11_01_object_binding.py",
    }
    if not {TOOLS / name for name in required_tools}.issubset({ROOT / item["path"] for item in value["tools"]}):
        raise RuntimeError("工具冻结清单没有覆盖完整下一批入口。")
    # 收集阶段也读取这些字段；缺字段不能与另一份缺字段的记录相等后放行。
    for name in ("changed_files", "added_files", "deleted_files"):
        if not isinstance(review.get(name), list):
            raise RuntimeError("最终审查缺少明确的变更路径清单：" + name)
    if (not isinstance(review.get("git_apply_config"), dict) or not review["git_apply_config"]
            or not isinstance(review.get("root_attributes"), dict)):
        raise RuntimeError("最终审查缺少实际Git选项或根属性原件记录。")
    check_record(review["root_attributes"])
    failures = review.get("historical_failed_checks")
    if not isinstance(failures, list):
        raise RuntimeError("前期失败记录必须是明确的原件列表。")
    evidence = {(item["path"], item["sha256"]) for item in value["evidence"]}
    for item in records_in(failures):
        check_record(item)
        if (item["path"], item["sha256"]) not in evidence:
            raise RuntimeError("前期失败没有同时进入最终元数据和保留证据清单。")
    return value


def snapshot_map(value):
    if not isinstance(value, dict) or not value:
        raise RuntimeError("缺少完整源码指纹表。")
    for name, fingerprint in value.items():
        safe_name(name)
        if not isinstance(fingerprint, str) or not SHA.fullmatch(fingerprint):
            raise RuntimeError("源码指纹格式无效：" + name)
    return value


def source_snapshot():
    config = cfg()
    return snapshot_map(read_json(check_record(config["final_source_manifest"])))


def promotion():
    config = cfg()
    review = config["review_metadata"]
    value = read_json(check_record(config["promotion_progress"]))
    if (value.get("format") != config["metadata_schema"]["promotion_format"] or value.get("complete") is not True
            or value.get("phase") != "complete" or value.get("current_patch") is not None
            or value.get("final_source_files") != review["final_source_files"]
            or value.get("base_source_files") != review["base_source_files"]
            or value.get("patches") != review["patches"]
            or value.get("final_merge_review_sha256") != config["final_review"]["sha256"]
            or value.get("merge_review_sha256") != config["final_review"]["sha256"]
            or value.get("promotion_script_sha256") != config["promotion_script"]["sha256"]):
        raise RuntimeError("正式晋级没有完整结束或不等于冻结最终源码，未开始下一阶段。")
    expected = [{key: item[key] for key in ("index", "patch", "sha256")} for item in review["patches"]]
    if (value.get("applied") != expected or value.get("preflight_applied") != [item["index"] for item in expected]
            or value.get("verified_source_paths") != len(review["final_source_files"])
            or value.get("source_snapshot_sha256") != config["baseline_snapshot"]["sha256"]
            or value.get("git_apply_config") != review.get("git_apply_config")
            or value.get("root_attributes") != review.get("root_attributes")):
        raise RuntimeError("逐片晋级或完整核对终态不完整，未开始编译。")
    for name in ("changed_files", "added_files", "deleted_files"):
        if value.get(name) != review.get(name):
            raise RuntimeError("晋级变更范围与最终整合记录不符。")
    return value


def verify_sources(snapshot=None):
    snapshot = source_snapshot() if snapshot is None else snapshot_map(snapshot)
    for name, wanted in snapshot.items():
        if sha(PROGRAM / name) != wanted:
            raise RuntimeError("正式源码已漂移，未混入其他修改：" + name)
    return snapshot


def verify_build():
    config = cfg()
    promotion()
    snapshot = verify_sources()
    complete = read_json(WORK / "build_complete.json")
    inputs = read_json(WORK / "input_verification.json")
    if (require_regular(WORK / "build_exit.txt").read_text().strip() != "0"
            or complete.get("format") != "sn100-integration-11-01-build-complete-v1"
            or complete.get("arm64_build_and_package_passed") is not True
            or complete.get("source_count") != len(snapshot)
            or read_json(WORK / "source_snapshot.json") != snapshot
            or complete.get("source_snapshot_sha256") != sha(WORK / "source_snapshot.json")
            or complete.get("tools_config_sha256") != sha(CONFIG)
            or complete.get("promotion_progress_sha256") != config["promotion_progress"]["sha256"]
            or complete.get("final_merge_review_sha256") != config["final_review"]["sha256"]
            or complete.get("dependency_report_sha256") != sha(WORK / "dependencies/dependency_report.json")):
        raise RuntimeError("下一批真实构建终态不完整或不是冻结源码。")
    lines = require_regular(WORK / "logs/build.log").read_text(encoding="utf-8").splitlines()
    if lines.count("ARM64_INTEGRATION_11_01_BUILD_AND_PACKAGE_PASSED=1") != 1:
        raise RuntimeError("构建没有实际完整收尾标记。")
    if require_regular(WORK / "merge_inputs.json").read_bytes() != check_record(config["promotion_progress"]).read_bytes():
        raise RuntimeError("构建冻结的晋级原文已变化。")
    for name, value in inputs.items():
        if name != "format" and complete.get(name) != value:
            raise RuntimeError("构建前后输入核对结果不一致：" + name)
    if set(complete.get("binaries", {})) != {"sn100_shell", "sn100_report_renderer"}:
        raise RuntimeError("缺少本批两份真实程序。")
    for name, wanted in complete["binaries"].items():
        if sha(WORK / "build" / name) != wanted:
            raise RuntimeError("真实程序已变化：" + name)
    package = WORK / "packages" / safe_name(complete["package"])
    if (package.parent != WORK / "packages" or package.name != "sn100_shell_" + complete["release_id"] + ".tar.gz"
            or not SHA.fullmatch(complete["release_id"]) or sha(package) != complete["package_sha256"]):
        raise RuntimeError("本批包身份或实际字节不符。")
    dependency = read_json(WORK / "dependencies/dependency_report.json")
    if dependency.get("errors") != [] or dependency.get("offline_link_and_sql_closure_passed") is not True:
        raise RuntimeError("本批静态依赖检查未通过。")
    objects = list((WORK / "arm64_build").glob("*.o"))
    if len(objects) != complete["objects"]["total"] or complete["objects"]["aarch64"] != len(objects):
        raise RuntimeError("实际对象数量不等于构建终态。")
    for path in objects:
        with require_regular(path).open("rb") as source:
            header = source.read(20)
        if header[:6] != b"\x7fELF\x02\x01" or int.from_bytes(header[16:18], "little") != 1 or int.from_bytes(header[18:20], "little") != 183:
            raise RuntimeError("本批对象不是ARM64：" + str(path))
    # 按冻结脚本和实际输入顺序重建对应关系，重新读每个对象与生成文件SHA。
    # 这是构建后保存的直接输入绑定，不冒充编译器原始.d或完整头文件依赖。
    from integration_11_01_object_binding import read_build_rows, verify_binding
    binding_path = check_record(complete["object_binding"])
    if binding_path != WORK / "object_binding.json":
        raise RuntimeError("构建终态的对象绑定不是本批固定位置。")
    verify_binding(read_json(binding_path), snapshot, read_build_rows(), complete["objects"])
    return complete


def verify_stage():
    config = cfg()
    if config["operation_permissions"]["allow_stage"] is not True:
        raise RuntimeError("本批未授予网络暂存范围，不能核作已获准暂存。")
    current = config["expected_current"]
    build = verify_build()
    complete = read_json(WORK / "stage_complete.json")
    started = read_json(WORK / "stage_started.json")
    fixed = {
        "board": "board_b", "serial": "8314b1e8ffd0321f", "ssh_host": "root@192.168.100.123",
        "host_key_alias": "rk3576-board-b", "host_fingerprint": "SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg",
        "current_before": current, "release_id": build["release_id"], "package_sha256": build["package_sha256"],
        "remote_release": "/home/rkshare/programs/sn100_shell/releases/integration_11_01/" + build["release_id"],
        "storage_lock_path": "/home/rkshare/programs/sn100_shell/immutable_store.lock",
        "storage_lock_kind": "exclusive_immutable_store_only",
        "live_guard_format": LIVE_GUARD_FORMAT,
        "live_guard_scope": "locked_unpack_and_commit",
        "expected_controller_exe": config["expected_controller_exe"],
        "expected_running_application_sha256": config["expected_running_application_sha256"],
        "expected_controller_sha256": config["expected_controller_sha256"],
    }
    if (started.get("format") != "sn100-integration-11-01-stage-started-v1" or started.get("complete") is not False
            or any(started.get(key) != value for key, value in fixed.items())
            or not re.fullmatch(r"/home/rkshare/programs/sn100_shell/\.workflow_upload_11_01_[a-f0-9]{32}\.tar\.gz", started.get("remote_upload", ""))):
        raise RuntimeError("暂存开始记录不是本批同板、同包、同当前版本。")
    paths = {
        "config": CONFIG, "build_complete": WORK / "build_complete.json", "build_exit": WORK / "build_exit.txt",
        "build_log": WORK / "logs/build.log", "source_snapshot": WORK / "source_snapshot.json",
        "dependency_report": WORK / "dependencies/dependency_report.json", "package": WORK / "packages" / build["package"],
        "stage_script": TOOLS / "stage_board_b_release_11_01.sh", "stage_tool": TOOLS / "stage_board_b_release_11_01.py",
        "upload_script": WORK / "stage_upload.sh",
        "support_tool": TOOLS / "integration_11_01_tool_support.py",
    }
    expected_inputs = {key: record(path) for key, path in paths.items()}
    expected_inputs["protected_configuration_manifest"] = dict(config["protected_configuration_manifest"])
    expected_inputs.update({name: dict(config[name]) for name in ("final_review", "promotion_progress")})
    for group in ("patches", "evidence", "tools"):
        expected_inputs.update({group + "_" + str(index): dict(item) for index, item in enumerate(config[group], 1)})
    if (started.get("inputs") != expected_inputs or started.get("stage_script_sha256") != expected_inputs["stage_script"]["sha256"]
            or started.get("upload_script_sha256") != expected_inputs["upload_script"]["sha256"]):
        raise RuntimeError("暂存全过程没有绑定当前冻结输入。")
    results = {name: record(WORK / name) for name in ("logs/upload.log", "upload.exit", "logs/board_b_stage.log", "board_b_stage.exit",
                                                     "live_guard_before.txt", "live_guard_after.txt",
                                                     "configuration_guard_before.sha256", "configuration_guard_after.sha256")}
    guard = {"format": fixed["live_guard_format"], "before": record(WORK / "live_guard_before.txt"),
             "after": record(WORK / "live_guard_after.txt"), "unchanged": True}
    expected = {**started, "format": "sn100-integration-11-01-stage-complete-v1", "complete": True,
                "completed_at": complete.get("completed_at"), "current_after": current, "actual_upload_exit": 0,
                "actual_stage_exit": 0, "activation_performed": False, "program_started": False,
                "started_record": record(WORK / "stage_started.json"), "results": results, "live_guard": guard,
                "configuration_guard": configuration_guard(config)}
    if not complete.get("completed_at") or complete != expected:
        raise RuntimeError("暂存开始、实际结果和完整终态不一致。")
    if any(require_regular(WORK / name).read_text().strip() != "0" for name in ("upload.exit", "board_b_stage.exit")):
        raise RuntimeError("实际上传或暂存退出码不是0。")
    upload = require_regular(WORK / "logs/upload.log").read_text(encoding="utf-8").splitlines()
    for marker in ("BOARD=board_b", "SERIAL=8314b1e8ffd0321f", "PACKAGE_SHA256=" + build["package_sha256"], "UPLOAD_SCRIPT_EXIT=0"):
        if upload.count(marker) != 1:
            raise RuntimeError("上传日志缺准确单次标记：" + marker)
    lines = require_regular(WORK / "logs/board_b_stage.log").read_text(encoding="utf-8").splitlines()
    verify_configuration_guard(config, complete["configuration_guard"], lines)
    raw_before = require_regular(WORK / "live_guard_before.txt").read_bytes()
    raw_after = require_regular(WORK / "live_guard_after.txt").read_bytes()
    if not raw_before or raw_before != raw_after:
        raise RuntimeError("持存储锁期间的现有应用、主控或指针身份变化。")
    observed = {}
    for row in raw_before.decode("utf-8").splitlines():
        key, separator, value = row.partition("=")
        if (not separator or not re.fullmatch(r"[A-Z][A-Z0-9_]*", key) or key in observed
                or not value or any(ord(char) < 32 for char in value)):
            raise RuntimeError("板端运行保护原文含重复或不完整字段。")
        observed[key] = value
    pointer_fields = {
        "ORIGINAL_PROJECT_ROOT": "original_project_root",
        "INSTRUMENT_CONFIGURATION_FILE": "instrument_configuration_file",
        "THEME_CONFIGURATION_FILE": "theme_configuration_file",
    }
    required_guard_fields = {
        "FORMAT", "BOARD", "SERIAL", "BOOT_ID", "CURRENT_RELEASE", "MAIN_PID", "MAIN_UID",
        "MAIN_START_TICKS", "MAIN_EXE", "MAIN_EXE_SHA256", "CONTROLLER_PID", "CONTROLLER_UID",
        "CONTROLLER_START_TICKS", "CONTROLLER_EXE", "CONTROLLER_EXE_SHA256", "CONTROLLER_LISTEN_ENDPOINTS",
    }
    for prefix in pointer_fields:
        required_guard_fields.update({"POINTER_" + prefix + "_PATH", "POINTER_" + prefix + "_SHA256"})
    required_guard_fields.add("CONFIGURATION_SHA256")
    if (set(observed) != required_guard_fields or tuple(observed) != LIVE_GUARD_KEYS
            or not raw_before.endswith(b"\n") or b"\r" in raw_before):
        raise RuntimeError("板端运行保护原文不是本合同完整字段集合和LF原文。")
    for name in ("MAIN_UID", "CONTROLLER_UID"):
        if not re.fullmatch(r"0|[1-9][0-9]*", observed[name]):
            raise RuntimeError("板端运行保护缺实际进程用户身份：" + name)
    for prefix, basename in pointer_fields.items():
        if (observed["POINTER_" + prefix + "_PATH"] != "/home/rkshare/programs/sn100_shell/" + basename
                or not SHA.fullmatch(observed["POINTER_" + prefix + "_SHA256"])):
            raise RuntimeError("板端运行保护缺实际资料/配置指针摘要：" + prefix)
    if (observed.get("FORMAT") != fixed["live_guard_format"] or observed.get("BOARD") != "board_b"
            or observed.get("SERIAL") != fixed["serial"] or observed.get("CURRENT_RELEASE") != current
            or observed.get("MAIN_EXE") != "/home/rkshare/programs/sn100_shell/" + current + "/sn100_shell"
            or observed.get("CONTROLLER_EXE") != config["expected_controller_exe"]
            or observed.get("MAIN_EXE_SHA256") != config["expected_running_application_sha256"]
            or observed.get("CONTROLLER_EXE_SHA256") != config["expected_controller_sha256"]
            or observed.get("CONFIGURATION_SHA256") != config["protected_configuration_manifest"]["sha256"]):
        raise RuntimeError("板端保护原文不是本批原有正式应用和主控。")
    for name in ("MAIN_PID", "MAIN_START_TICKS", "CONTROLLER_PID", "CONTROLLER_START_TICKS"):
        if not re.fullmatch(r"[1-9][0-9]*", observed.get(name, "")):
            raise RuntimeError("板端保护原文缺实际进程身份：" + name)
    for name in ("MAIN_EXE_SHA256", "CONTROLLER_EXE_SHA256"):
        if not SHA.fullmatch(observed.get(name, "")):
            raise RuntimeError("板端保护原文缺实际程序SHA：" + name)
    if not re.fullmatch(r"[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}", observed.get("BOOT_ID", "")):
        raise RuntimeError("板端保护原文缺实际启动身份。")
    for phase, raw in (("BEFORE", raw_before), ("AFTER", raw_after)):
        for marker in ("LIVE_GUARD_" + phase + "_SHA256=" + hashlib.sha256(raw).hexdigest(),
                       "LIVE_GUARD_" + phase + "_BASE64=" + base64.b64encode(raw).decode("ascii")):
            if lines.count(marker) != 1:
                raise RuntimeError("保护原文不等于板端实际输出：" + phase)
    for marker in ("BOARD=board_b", "SERIAL=8314b1e8ffd0321f", "STAGED_RELEASE=" + build["release_id"],
                   "CURRENT_RELEASE=" + current, "PACKAGE_SHA256=" + build["package_sha256"],
                   "REMOTE_RELEASE=" + fixed["remote_release"],
                   "STORAGE_LOCK_PATH=" + fixed["storage_lock_path"], "STORAGE_LOCK_KIND=" + fixed["storage_lock_kind"],
                   "ACTIVATED=0", "PROGRAM_STARTED=0", "STAGE_BATCH=integration_11_01", "STAGE_SCRIPT_EXIT=0",
                   "DEPENDENCIES_BEGIN", "DEPENDENCIES_END"):
        if lines.count(marker) != 1:
            raise RuntimeError("暂存日志缺准确单次标记：" + marker)
    if any("not found" in line for line in lines):
        raise RuntimeError("板端实际动态库缺失。")
    for name, wanted in build["binaries"].items():
        if lines.count(wanted + "  " + fixed["remote_release"] + "/" + name) != 1:
            raise RuntimeError("板端实际程序SHA不符：" + name)
    return complete


def checksums(directory):
    directory = ordinary_path(directory)
    rows = []
    for path in sorted(directory.rglob("*")):
        ordinary_path(path)
        if path.is_file() and path != directory / "SHA256SUMS":
            rows.append(sha(path) + "  " + path.relative_to(directory).as_posix())
    target = directory / "SHA256SUMS"
    with target.open("xb") as output:
        output.write(("\n".join(rows) + "\n").encode("utf-8"))
    verify_checksums(directory)
    return len(rows)


def verify_checksums(directory):
    directory = ordinary_path(directory)
    listed = set()
    for row in require_regular(directory / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        wanted, name = row.split("  ", 1)
        safe_name(name)
        if not SHA.fullmatch(wanted) or name in listed or sha(directory / name) != wanted:
            raise RuntimeError("归档清单缺失、重复或指纹不符：" + name)
        listed.add(name)
    entries = list(directory.rglob("*"))
    for path in entries:
        ordinary_path(path)
    actual = {path.relative_to(directory).as_posix() for path in entries if path.is_file() and path != directory / "SHA256SUMS"}
    if actual != listed:
        raise RuntimeError("归档文件集合不等于完整清单。")
    return len(listed)


def now():
    return datetime.now().astimezone().isoformat()


if __name__ == "__main__":
    if sys.argv[1:] != ["preflight"]:
        raise SystemExit("只支持 preflight 文件核对。")
    if cfg()["operation_permissions"]["allow_full_build"] is not True:
        raise SystemExit("总控尚未允许本批完整构建，未创建输出目录。")
    promotion()
    verify_sources()
    print("INTEGRATION_11_01_FROZEN_INPUT_PREFLIGHT=1\nPRODUCT_EXECUTED=0")
