"""只从已冻结原件生成总窗口待审脚本；本工具不连接板卡或执行生成物。"""
from pathlib import Path, PurePosixPath
import argparse
import base64
import hashlib
import importlib.util
import json
import os
import re
import shlex
import stat
import sys
import tarfile

sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
BASE = "/home/rkshare/programs/sn100_shell"
C7 = "c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e"
C7_APP = "50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258"
HELPER = "e3587bad320003bf1a2ea7b3176bf14d23527ebaae65b60ffb4bd5388d3866d2"
CONTROLLER = "ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff"
CONFIGURATION = "ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c"
SERIAL = "8314b1e8ffd0321f"
PHASES = ("metadata", "prepare", "preflight", "stop", "activate", "launch", "observe")
SHA = re.compile(r"[a-f0-9]{64}\Z")
BOOT = re.compile(r"[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}\Z")
IDENTIFIER = re.compile(r"[A-Za-z0-9_-]{1,64}\Z")
SOURCES = {
    "prepare02": "a6af67facbe29474c73fd887e071c4f5f93502a4b9b5628eeaa4dd1ba3087b67",
    "stop02": "349014eeca36ee90f30b34695fc498293bf06ee00a02874375afe3f0c4d88fdb",
    "activate01": "7237e7c7701b0766757a739199c133da1ff987bf436f051a5e4756c158ce97ce",
    "launch02": "fb864ad51c15f190f8385bcba42d20fa6874d83407bed824aefdca4476b57577",
    "formal_helper": HELPER,
    "waiter": "eff1fddabbb5d8bf89d54f3d2b45a78760e8d8a9738fd71047baa7730aad1de8",
    "parser": "a0ea76de798d3fee19e5bb0adf547d033d3619f5020e05a5f61ce6cbc1c46783",
}
CONFIG_PATHS = (
    BASE + "/runtime/data/config_Debug.ini", BASE + "/runtime/data/config.ini",
    BASE + "/runtime/data/theme_config.ini", "/userdata/shared/config_linux.ini",
    "/userdata/shared/consume.ini", "/userdata/shared/GL4A_config.cfg", "/userdata/shared/number.ini",
)
POINTER_PATHS = tuple(BASE + "/" + name for name in (
    "original_project_root", "instrument_configuration_file", "theme_configuration_file"))
INTENT_KEYS = (
    "format", "frozen", "release_id", "application_exe", "application_sha256", "application_sha_source",
    "observation_schema", "observation_dir", "observation_id", "observation_release", "root_decision_sha256",
    "start_owner", "supervise_seconds", "allow_prepare_flat", "allow_activation", "allow_old_app_stop",
    "allow_unique_start", "leave_running_required", "file_phase_config_path", "file_phase_config_sha256",
    "build_complete_path", "build_complete_sha256", "package_path", "package_sha256",
)
EMPTY_RECORD = {"path": "", "sha256": "", "bytes": None}
STOP_FILES = ("result", "exit_code", "stdout", "stderr", "actual_exit", "app_pid", "app_start_ticks", "app_exe", "boot_id")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def valid_sha(value):
    return isinstance(value, str) and SHA.fullmatch(value) is not None and value != "0" * 64


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "JSON字段重复：" + key)
        result[key] = value
    return result


def json_bytes(raw):
    def invalid_constant(value):
        raise ValueError("JSON不接受NaN或Infinity：" + value)
    value = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=invalid_constant)
    require(type(value) is dict, "原件须为JSON对象。")
    return value


def safe_relative(value):
    require(isinstance(value, str) and value and re.fullmatch(r"[A-Za-z0-9_./-]+", value), "仓库相对路径格式不明。")
    require(not value.startswith("/") and all(part not in ("", ".", "..") for part in value.split("/")), "仓库路径不规范。")
    return value


def board_path(value, parent=None):
    require(isinstance(value, str) and re.fullmatch(r"/[A-Za-z0-9_./-]+", value), "板端绝对路径格式不明。")
    require(all(part not in ("", ".", "..") for part in value[1:].split("/")), "板端路径不规范。")
    if parent is not None:
        require(value.startswith(parent + "/"), "板端路径越出约定目录。")
    return value


def local_path(value):
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / safe_relative(value)
    path.resolve().relative_to(ROOT.resolve())
    for component in (path, *path.parents):
        require(not component.is_symlink() and not (hasattr(component, "is_junction") and component.is_junction()), "原件路径经过链接。")
        if component == ROOT:
            break
    return path


class Inputs:
    """只读并登记原件字节；唯一校验模块由调用方固定路径加载。"""
    def __init__(self):
        self.seen = {}
        self.file_phase_verifier_record = None

    def raw(self, path):
        path = local_path(str(path))
        info = path.lstat()
        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, "原件不是单链接普通文件：" + str(path))
        raw = path.read_bytes()
        item = {"path": path.relative_to(ROOT).as_posix(), "sha256": digest(raw), "bytes": len(raw)}
        require(path not in self.seen or self.seen[path] == item, "读取期间原件变化。")
        self.seen[path] = item
        return raw, item

    def record(self, item):
        require(type(item) is dict and set(item) == {"path", "sha256", "bytes"}, "原件记录须只有path/sha256/bytes。")
        require(valid_sha(item["sha256"]) and type(item["bytes"]) is int and item["bytes"] >= 0, "原件记录未固定真实SHA/字节数。")
        raw, actual = self.raw(ROOT / safe_relative(item["path"]))
        require(actual == item, "原件指纹或字节数不符：" + item["path"])
        return raw

    def data(self, item):
        return json_bytes(self.record(item))

    def unchanged(self):
        for path, expected in list(self.seen.items()):
            _, actual = self.raw(path)
            require(actual == expected, "写输出前实际输入变化。")


def exact_keys(value, keys, label):
    require(type(value) is dict and set(value) == set(keys), label + "字段集合不符。")


def process_identity(value, camera=False):
    fields = {"pid", "start_ticks", "uid", "exe", "sha256"}
    if camera:
        fields.add("role")
    exact_keys(value, fields, "进程身份")
    for name in ("pid", "start_ticks"):
        require(isinstance(value[name], str) and re.fullmatch(r"[1-9][0-9]*", value[name]), "缺实际PID或启动计数。")
    require(isinstance(value["uid"], str) and re.fullmatch(r"0|[1-9][0-9]*", value["uid"]), "缺实际有效UID。")
    board_path(value["exe"])
    require(valid_sha(value["sha256"]), "缺实际进程映像SHA。")
    if camera:
        require(value["role"] in ("camera", "three_a"), "相机进程须区分camera与three_a。")


def configuration_rows(raw):
    require(raw.endswith(b"\n") and not raw.endswith(b"\n\n") and b"\r" not in raw, "七配置清单须为规范LF原文。")
    rows = raw.decode("ascii").split("\n")[:-1]
    matches = [re.fullmatch(r"([a-f0-9]{64})  (/[A-Za-z0-9_./-]+)", row) for row in rows]
    require(all(matches) and tuple(match[2] for match in matches) == CONFIG_PATHS, "七配置清单路径、顺序或SHA不符。")
    require(digest(raw) == CONFIGURATION, "七配置目标不是本次获准的ad11原件。")
    return [(match[1], match[2]) for match in matches]


def check_core(inputs, core, core_record):
    exact_keys(core, ("format", "frozen", "transition_id", "base", "metadata_directory", "audit_directory", "board_serial",
                      "file_phase", "target", "fresh_baseline", "root_decision", "stable_target_intent", "observation", "source_templates"), "核心输入")
    require(core.get("format") == "sn100-integration-12-01-runtime-core-v1" and core.get("frozen") is True, "不是已冻结的本轮核心输入。")
    require(IDENTIFIER.fullmatch(core.get("transition_id", "")) is not None and core.get("base") == BASE and core.get("board_serial") == SERIAL, "本轮标识、BASE或板卡不符。")
    for name in ("metadata_directory", "audit_directory"):
        board_path(core[name], BASE + "/verification")
        require(core[name].rsplit("/", 1)[0] == BASE + "/verification", "元数据和审计须是verification直接子目录。")
    require(core["metadata_directory"] != core["audit_directory"] and not core["audit_directory"].startswith(core["metadata_directory"] + "/")
            and not core["metadata_directory"].startswith(core["audit_directory"] + "/"), "元数据与切换审计目录须独立。")
    target = core["target"]
    exact_keys(target, ("release_id", "application_sha256", "renderer_sha256", "helper_sha256", "nested_namespace"), "新包目标")
    require(all(valid_sha(target[name]) for name in ("release_id", "application_sha256", "renderer_sha256", "helper_sha256"))
            and target["release_id"] != C7 and target["nested_namespace"] == "integration_12_01"
            and target["helper_sha256"] == HELPER, "新包目标未完整固定或helper发生未审变化。")
    baseline = core["fresh_baseline"]
    exact_keys(baseline, ("capture_record", "boot_id", "old_current", "old_application", "controller", "camera_processes", "camera_service", "configuration_manifest", "pointers"), "fresh基线")
    require(BOOT.fullmatch(baseline["boot_id"]) is not None and baseline["old_current"] == "releases/" + C7, "缺本次boot或旧版不是C7。")
    old = baseline["old_application"]
    exact_keys(old, ("run", "pid", "start_ticks", "uid", "exe", "sha256", "helper_sha256"), "旧C7身份")
    process_identity({name: old[name] for name in ("pid", "start_ticks", "uid", "exe", "sha256")})
    board_path(old["run"], BASE + "/runs")
    require(re.fullmatch(r"[0-9_]+", old["run"][len(BASE + "/runs/"):]) is not None
            and old["exe"] == BASE + "/releases/" + C7 + "/sn100_shell" and old["sha256"] == C7_APP and old["helper_sha256"] == HELPER, "旧运行不是准确C7。")
    controller = baseline["controller"]
    process_identity({name: controller[name] for name in ("pid", "start_ticks", "uid", "exe", "sha256")})
    exact_keys(controller, ("pid", "start_ticks", "uid", "exe", "sha256", "listen_endpoints"), "原主控")
    require(controller["exe"] == "/userdata/shared/ACA_V1.0OffLineD-500" and controller["sha256"] == CONTROLLER
            and controller["pid"] != old["pid"], "原主控不是当次独立准确身份。")
    endpoints = controller["listen_endpoints"].split(",")
    require(endpoints and all(re.fullmatch(r"[A-Za-z0-9_:.\[\]*%-]+:5000", endpoint) for endpoint in endpoints), "主控实际5000端点未固定。")
    cameras = baseline["camera_processes"]
    require(type(cameras) is list and len(cameras) >= 2, "须给出实际相机worker及3A身份。")
    for camera in cameras:
        process_identity(camera, camera=True)
    require({row["role"] for row in cameras} == {"camera", "three_a"}
            and len({row["pid"] for row in cameras}) == len(cameras)
            and all(row["pid"] not in (old["pid"], controller["pid"]) for row in cameras), "相机/3A身份不完整或重用进程。")
    service = baseline["camera_service"]
    exact_keys(service, ("unit", "active_state", "sub_state", "main_pid"), "相机服务")
    require(service["unit"] == "opto-camera.service" and re.fullmatch(r"[a-z-]+", service["active_state"])
            and re.fullmatch(r"[a-z-]+", service["sub_state"]) and re.fullmatch(r"0|[1-9][0-9]*", service["main_pid"]), "缺实际相机服务身份。")
    require(type(baseline["pointers"]) is list and tuple(row["path"] for row in baseline["pointers"]) == POINTER_PATHS
            and all(set(row) == {"path", "sha256"} and valid_sha(row["sha256"]) for row in baseline["pointers"]), "三指针fresh原文指纹不全。")
    configuration_rows(inputs.record(baseline["configuration_manifest"]))
    capture = inputs.data(baseline["capture_record"])
    expected_capture = {"format": "sn100-integration-12-01-fresh-baseline-v1", "board_serial": SERIAL,
                        **{name: value for name, value in baseline.items() if name != "capture_record"}}
    require(capture == expected_capture, "fresh捕获须逐项等于基线且排除自己的capture_record。")
    sources = core["source_templates"]
    exact_keys(sources, SOURCES, "已审来源")
    for name, expected in SOURCES.items():
        require(sources[name]["sha256"] == expected, "已审来源SHA不符：" + name)
        inputs.record(sources[name])
    observation = core["observation"]
    exact_keys(observation, ("directory", "id", "capture_parent", "capture_leaf", "timeout_seconds"), "被动观察设置")
    board_path(observation["directory"], core["metadata_directory"])
    board_path(observation["capture_parent"], core["metadata_directory"])
    board_path(observation["capture_leaf"], observation["capture_parent"])
    require(observation["directory"] == core["metadata_directory"] + "/observation"
            and observation["capture_parent"] == core["metadata_directory"] + "/captures"
            and observation["capture_leaf"].rsplit("/", 1)[0] == observation["capture_parent"], "观察与采集目录边界不符。")
    require(IDENTIFIER.fullmatch(observation["id"]) is not None and type(observation["timeout_seconds"]) is int
            and 1 <= observation["timeout_seconds"] <= 600, "观察ID或仅等待器时限未固定。")
    require(bool(inputs.record(core["root_decision"])), "总窗口裁定原件不能为空。")
    require(core["root_decision"] != core_record, "核心输入不能引用自身SHA。")
    verifier_path = HERE / "runtime_file_phase.py"
    _, inputs.file_phase_verifier_record = inputs.raw(verifier_path)
    specification = importlib.util.spec_from_file_location("_sn100_runtime_file_phase_12_01", verifier_path)
    require(specification is not None and specification.loader is not None
            and Path(specification.origin).resolve() == verifier_path.resolve(), "文件阶段校验模块不是同目录原件。")
    verifier = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(verifier)
    require(Path(verifier.__file__).resolve() == verifier_path.resolve(), "实际加载的校验模块位置不符。")
    phase_result = verifier.verify_file_phase(core, ROOT)
    require(type(phase_result.get("read_records")) is list and phase_result["read_records"], "文件阶段校验必须返回全部实读原件。")
    for item in phase_result["read_records"]:
        inputs.record(item)
    raw_intent = inputs.record(core["stable_target_intent"])
    intent = json_bytes(raw_intent)
    require(tuple(intent) == INTENT_KEYS and raw_intent == (json.dumps(intent, ensure_ascii=True, indent=2) + "\n").encode("ascii"), "稳定目标意图须为既有24字段规范原文。")
    phase = core["file_phase"]
    flat = BASE + "/releases/" + target["release_id"]
    expected = {
        "format": "sn100-root-launch-intent-v1", "frozen": True,
        "release_id": target["release_id"], "application_exe": flat + "/sn100_shell",
        "application_sha256": target["application_sha256"], "application_sha_source": "external_collector",
        "observation_schema": "sn100-lis-keep-running-observation-v1", "observation_dir": observation["directory"],
        "observation_id": observation["id"], "observation_release": target["release_id"],
        "root_decision_sha256": core["root_decision"]["sha256"], "start_owner": "root", "supervise_seconds": 0,
        "allow_prepare_flat": False, "allow_activation": False, "allow_old_app_stop": False, "allow_unique_start": False,
        "leave_running_required": True, "file_phase_config_path": phase["config"]["path"],
        "file_phase_config_sha256": phase["config"]["sha256"], "build_complete_path": phase["build_complete"]["path"],
        "build_complete_sha256": phase["build_complete"]["sha256"], "package_path": phase["package"]["path"], "package_sha256": phase["package"]["sha256"],
    }
    require(intent == expected and type(intent["supervise_seconds"]) is int
            and all(type(intent[name]) is bool for name in ("frozen", "allow_prepare_flat", "allow_activation", "allow_old_app_stop", "allow_unique_start", "leave_running_required")), "24字段目标意图未按本轮转换，或动作许可未保持false。")
    for value in intent.values():
        if isinstance(value, str):
            require(re.fullmatch(r"[A-Za-z0-9_./:-]*", value) is not None, "稳定意图含旧受限解析器不支持的字符串。")


def receipt_fields(raw):
    require(raw.endswith(b"\n") and not raw.endswith(b"\n\n") and b"\r" not in raw and len(raw) <= 65536, "收据须为有界LF原文。")
    result = {}
    for row in raw.decode("ascii").split("\n")[:-1]:
        key, separator, value = row.partition("=")
        require(separator and re.fullmatch(r"[A-Z][A-Z0-9_]*", key) and key not in result and value
                and all(32 <= ord(char) < 127 for char in value), "收据含重复、不完整或控制字段。")
        result[key] = value
    return result


def check_receipt(inputs, item, core, core_record, phase, authorization_record, exit_record, log_record):
    raw = inputs.record(item)
    capture_directory = "temp/sn100_host/root_manager_20261103/runtime_transition_captures/" + core["transition_id"]
    require(item["path"] == capture_directory + "/" + phase + ".receipt"
            and exit_record.get("path") == capture_directory + "/" + phase + ".exit"
            and log_record.get("path") == capture_directory + "/" + phase + ".log", "收据/log/exit不是同transition、同目录、同阶段文件组。")
    result = receipt_fields(raw)
    expected = {
        "FORMAT": "sn100-integration-12-01-runtime-receipt-v1", "PHASE": phase,
        "TRANSITION_ID": core["transition_id"], "BOARD": "board_b", "SERIAL": SERIAL,
        "BOOT_ID": core["fresh_baseline"]["boot_id"], "CORE_INPUTS_SHA256": core_record["sha256"],
        "AUTHORIZATION_SHA256": authorization_record["sha256"],
        "ROOT_LAUNCH_INTENT_SHA256": core["stable_target_intent"]["sha256"],
        "CONFIGURATION_MANIFEST_SHA256": CONFIGURATION, "EXIT_CODE": "0",
    }
    require(all(result.get(key) == value for key, value in expected.items()), "收据不属于同core、阶段、boot或授权。")
    baseline = core["fresh_baseline"]
    controller = baseline["controller"]
    expected_controller = ":".join((baseline["boot_id"], controller["pid"], controller["start_ticks"], controller["uid"], controller["exe"], controller["sha256"], controller["listen_endpoints"]))
    camera_rows = "".join("|".join(row[name] for name in ("role", "pid", "start_ticks", "uid", "exe", "sha256")) + "\n" for row in baseline["camera_processes"])
    service = baseline["camera_service"]
    camera_rows += "|".join(service[name] for name in ("unit", "active_state", "sub_state", "main_pid")) + "\n"
    current = baseline["old_current"] if PHASES.index(phase) <= PHASES.index("stop") else "releases/" + core["target"]["release_id"]
    require(result.get("CONTROLLER_IDENTITY") == expected_controller
            and result.get("CAMERA_IDENTITY_SHA256") == digest(camera_rows.encode("ascii"))
            and result.get("CURRENT_AFTER") == current, "前阶段原主控/相机/current不是同一基线。")
    facts = {
        "metadata": {"METADATA_INSTALLED": "1", "CURRENT_CHANGED": "0", "PROGRAM_STARTED": "0", "CLOSE_REQUESTED": "0"},
        "prepare": {"FLAT_PREPARED": "1", "NESTED_PRESERVED": "1", "CURRENT_CHANGED": "0", "PROGRAM_STARTED": "0", "CLOSE_REQUESTED": "0"},
        "preflight": {"NEW_RELEASE_PREFLIGHT_PASSED": "1", "OLD_APPLICATION_STILL_RUNNING": "1", "CURRENT_CHANGED": "0", "PROGRAM_STARTED": "0", "CLOSE_REQUESTED": "0"},
        "activate": {"ACTIVATED": "1", "PROGRAM_STARTED": "0", "NESTED_PRESERVED": "1", "CLOSE_REQUESTED": "0"},
        "launch": {"UNIQUE_START_REQUEST_EXIT": "0", "NEW_PROCESS_IDENTIFIED": "1", "SOFT_DEADLINE_SECONDS": "0", "APPLICATION_EXIT": "not_evaluated",
                   "NEW_EXE": BASE + "/releases/" + core["target"]["release_id"] + "/sn100_shell", "NEW_EXE_SHA256": core["target"]["application_sha256"]},
    }
    require(all(result.get(key) == value for key, value in facts.get(phase, {}).items()), "前阶段收据没有该阶段真实完成事实。")
    require(inputs.record(exit_record) == b"0\n", "前阶段真实外层退出不是0。")
    log_raw = inputs.record(log_record)
    log = log_raw.decode("utf-8").splitlines()
    require(log.count("RUNTIME_RECEIPT_SHA256=" + item["sha256"]) == 1, "前阶段日志没有准确单次收据SHA。")
    expected_tail = ("RUNTIME_RECEIPT_SHA256=" + item["sha256"] + "\n").encode("ascii") + raw
    require(log_raw.endswith(expected_tail), "前阶段日志末尾不是同一收据的完整原文及SHA。")
    for key in ("PHASE", "TRANSITION_ID", "CORE_INPUTS_SHA256", "AUTHORIZATION_SHA256"):
        require(log.count(key + "=" + result[key]) == 1, "前阶段日志阶段/core/授权事实缺失或重复：" + key)
    return result


def check_stop_files(inputs, authorization, receipt, core):
    records = authorization["old_stop_files"]
    exact_keys(records, STOP_FILES, "本轮旧停原件")
    old = core["fresh_baseline"]["old_application"]
    expected_bytes = {
        "result": b"APP_EXIT=0\nCLEAN_SHUTDOWN=1\n", "exit_code": b"0\n", "actual_exit": b"0\n",
        "app_pid": (old["pid"] + "\n").encode("ascii"), "app_start_ticks": (old["start_ticks"] + "\n").encode("ascii"),
        "app_exe": (old["exe"] + "\n").encode("ascii"), "boot_id": (core["fresh_baseline"]["boot_id"] + "\n").encode("ascii"),
    }
    sha_keys = {"result": "OLD_RESULT_SHA256", "exit_code": "OLD_EXIT_CODE_SHA256", "stdout": "STOP_STDOUT_SHA256",
                "stderr": "STOP_STDERR_SHA256", "actual_exit": "STOP_TOOL_EXIT_FILE_SHA256", "app_pid": "OLD_APP_PID_FILE_SHA256",
                "app_start_ticks": "OLD_APP_START_FILE_SHA256", "app_exe": "OLD_APP_EXE_FILE_SHA256", "boot_id": "OLD_BOOT_FILE_SHA256"}
    for name, item in records.items():
        raw = inputs.record(item)
        require(receipt.get(sha_keys[name]) == item["sha256"], "旧停收据与实际原件指纹不一致：" + name)
        if name in expected_bytes:
            require(raw == expected_bytes[name], "旧停原件不是当次准确C7的真实干净退出：" + name)
    expected = {"OLD_RUN": old["run"], "OLD_PID": old["pid"], "OLD_START_TICKS": old["start_ticks"],
                "OLD_EXE": old["exe"], "OLD_EXE_SHA256": old["sha256"], "OLD_HELPER_SHA256": old["helper_sha256"],
                "OLD_MANIFEST_SHA256": C7, "APP_EXIT": "0", "CLEAN_SHUTDOWN": "1", "STOP_TOOL_ACTUAL_EXIT": "0",
                "DISPLAY_RELEASED": "1", "CURRENT_AFTER": "releases/" + C7}
    require(all(receipt.get(key) == value for key, value in expected.items()), "停止收据不是本轮准确C7停止。")


def check_authorization(inputs, item, core, core_record, expected_phase):
    authorization = inputs.data(item)
    fields = ("format", "frozen", "phase", "allowed", "core_inputs", "root_approval", "previous_authorization", "previous_receipt",
              "previous_phase_exit", "previous_phase_log", "stop_intent_record", "old_stop_receipt", "old_stop_phase_exit", "old_stop_phase_log", "old_stop_files")
    exact_keys(authorization, fields, "阶段批准")
    require(authorization["format"] == "sn100-integration-12-01-runtime-phase-authorization-v1"
            and authorization["frozen"] is True and authorization["allowed"] is True and authorization["phase"] == expected_phase
            and authorization["core_inputs"] == core_record, "本阶段未单独冻结、批准或不是同一核心输入。")
    require(bool(inputs.record(authorization["root_approval"])), "本阶段总窗口批准原件不能为空。")
    require(authorization["root_approval"] not in (item, core_record), "批准原件不能引用自身或core代替批准。")
    index = PHASES.index(expected_phase)
    history = {}
    previous = None
    if index:
        previous_phase = PHASES[index - 1]
        history = check_authorization(inputs, authorization["previous_authorization"], core, core_record, previous_phase)
        previous = check_receipt(inputs, authorization["previous_receipt"], core, core_record, previous_phase,
                                 authorization["previous_authorization"], authorization["previous_phase_exit"], authorization["previous_phase_log"])
    else:
        require(all(authorization[name] == EMPTY_RECORD for name in ("previous_authorization", "previous_receipt", "previous_phase_exit", "previous_phase_log")), "metadata阶段不能借未来或历史前置收据。")
    if index >= PHASES.index("activate"):
        require(authorization["stop_intent_record"] == core["stable_target_intent"], "旧停意图SHA不能偷换成后续意图。")
        inputs.record(authorization["stop_intent_record"])
        stop_authorization_record = history["stop"]["record"]
        stopped = check_receipt(inputs, authorization["old_stop_receipt"], core, core_record, "stop", stop_authorization_record,
                                authorization["old_stop_phase_exit"], authorization["old_stop_phase_log"])
        check_stop_files(inputs, authorization, stopped, core)
        if expected_phase == "activate":
            require(authorization["previous_receipt"] == authorization["old_stop_receipt"], "activate必须直接接本轮stop。")
        else:
            require(previous.get("OLD_STOP_RECEIPT_SHA256") == authorization["old_stop_receipt"]["sha256"], "前阶段未绑定同一旧停收据。")
    else:
        require(all(authorization[name] == EMPTY_RECORD for name in ("stop_intent_record", "old_stop_receipt", "old_stop_phase_exit", "old_stop_phase_log")), "旧停发生前不得预填停止收据。")
        exact_keys(authorization["old_stop_files"], STOP_FILES, "未发生旧停的空槽")
        require(all(value == EMPTY_RECORD for value in authorization["old_stop_files"].values()), "旧停发生前不得预填原件。")
    if expected_phase == "observe":
        old = core["fresh_baseline"]["old_application"]
        require(previous.get("NEW_RUN", "").startswith(BASE + "/runs/") and re.fullmatch(r"[0-9_]+", previous["NEW_RUN"][len(BASE + "/runs/"):])
                and re.fullmatch(r"[1-9][0-9]*", previous.get("NEW_PID", "")) and re.fullmatch(r"[1-9][0-9]*", previous.get("NEW_START_TICKS", ""))
                and previous["NEW_RUN"] != old["run"] and previous["NEW_PID"] != old["pid"], "launch收据缺本轮新身份。")
    history[expected_phase] = {"record": item, "value": authorization, "previous": previous}
    return history


def encoded(raw):
    return base64.b64encode(raw).decode("ascii")


def shell_globals(inputs, core, core_record, authorization, authorization_record, phase, previous):
    baseline, target, obs = core["fresh_baseline"], core["target"], core["observation"]
    old, controller = baseline["old_application"], baseline["controller"]
    values = {
        "template_frozen": "true", "phase_allowed": "true", "phase": phase,
        "base": BASE, "meta": core["metadata_directory"], "audit": core["audit_directory"],
        "transition_id": core["transition_id"], "expected_boot": baseline["boot_id"], "expected_current": baseline["old_current"],
        "release_id": target["release_id"], "flat": BASE + "/releases/" + target["release_id"],
        "nested": BASE + "/releases/integration_12_01/" + target["release_id"],
        "expected_app_sha256": target["application_sha256"], "expected_renderer_sha256": target["renderer_sha256"],
        "expected_board_script_sha256": target["helper_sha256"], "expected_controller_sha256": controller["sha256"],
        "core_sha256": core_record["sha256"], "authorization_sha256": authorization_record["sha256"],
        "root_launch_intent_sha256": core["stable_target_intent"]["sha256"], "configuration_manifest_sha256": CONFIGURATION,
        "oldrun": old["run"], "old_pid": old["pid"], "old_ticks": old["start_ticks"], "old_uid": old["uid"],
        "old_exe": old["exe"], "old_exe_sha": old["sha256"], "old_helper_sha": old["helper_sha256"],
        "controller_expected_identity": ":".join((baseline["boot_id"], controller["pid"], controller["start_ticks"], controller["uid"], controller["exe"], controller["sha256"], controller["listen_endpoints"])),
        "camera_service_unit": baseline["camera_service"]["unit"], "camera_active_state": baseline["camera_service"]["active_state"],
        "camera_sub_state": baseline["camera_service"]["sub_state"], "camera_main_pid": baseline["camera_service"]["main_pid"],
        "observation_dir": obs["directory"], "observation_id": obs["id"], "capture_parent": obs["capture_parent"], "capture_leaf": obs["capture_leaf"],
        "waiter_timeout": str(obs["timeout_seconds"]), "waiter_sha256": core["source_templates"]["waiter"]["sha256"],
        "previous_phase": PHASES[PHASES.index(phase) - 1] if phase != "metadata" else "",
        "previous_receipt_sha256": authorization["previous_receipt"]["sha256"],
        "previous_authorization_sha256": authorization["previous_authorization"]["sha256"],
        "old_stop_receipt_sha256": authorization["old_stop_receipt"]["sha256"],
        "new_run": previous["NEW_RUN"] if phase == "observe" else "",
        "new_pid": previous["NEW_PID"] if phase == "observe" else "",
        "new_ticks": previous["NEW_START_TICKS"] if phase == "observe" else "",
    }
    payloads = {
        "core_base64": inputs.record(core_record), "authorization_base64": inputs.record(authorization_record),
        "approval_base64": inputs.record(authorization["root_approval"]),
        "stable_intent_base64": inputs.record(core["stable_target_intent"]),
        "configuration_base64": inputs.record(baseline["configuration_manifest"]),
        "waiter_base64": inputs.record(core["source_templates"]["waiter"]),
    }
    values["approval_sha256"] = authorization["root_approval"]["sha256"]
    for name, raw in payloads.items():
        values[name] = encoded(raw)
    result = "".join(name + "=" + shlex.quote(value) + "\n" for name, value in values.items())
    result += "configuration_paths=(" + " ".join(shlex.quote(path) for path in CONFIG_PATHS) + ")\n"
    result += "pointer_rows=(" + " ".join(shlex.quote(row["sha256"] + "  " + row["path"]) for row in baseline["pointers"]) + ")\n"
    result += "camera_rows=(" + " ".join(shlex.quote("|".join(row[name] for name in ("role", "pid", "start_ticks", "uid", "exe", "sha256"))) for row in baseline["camera_processes"]) + ")\n"
    return result


def write_exclusive(path, raw, mode=0o640):
    with path.open("xb") as output:
        output.write(raw)
        output.flush()
        os.fsync(output.fileno())
    path.chmod(mode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=PHASES, required=True)
    parser.add_argument("--inputs", required=True)
    parser.add_argument("--authorization", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    inputs = Inputs()
    core_raw, core_record = inputs.raw(local_path(args.inputs))
    core = json_bytes(core_raw)
    # 此门在任何输出、模板读取、包读取前；原未冻结整理表也在这里拒绝。
    require(core.get("frozen") is True, "输入frozen不是true；未创建输出、未生成可执行脚本。")
    authorization_raw, authorization_record = inputs.raw(local_path(args.authorization))
    require(json_bytes(authorization_raw).get("frozen") is True, "阶段批准未冻结；没有输出。")
    check_core(inputs, core, core_record)
    history = check_authorization(inputs, authorization_record, core, core_record, args.phase)
    authorization = history[args.phase]["value"]
    previous = history[args.phase]["previous"]
    output = local_path(args.output)
    generated = HERE / "generated"
    require(output.parent == generated and IDENTIFIER.fullmatch(output.name) is not None, "输出须是本候选generated下全新单层目录。")
    require(not output.exists() and not output.is_symlink(), "输出目录已存在，保留原现场。")
    template_raw, template_record = inputs.raw(HERE / "runtime_common.sh.in")
    body_raw, body_record = inputs.raw(HERE / ("phase_" + args.phase + ".sh.in"))
    source = inputs.record(core["source_templates"]["prepare02"]).decode("utf-8")
    start, end = "checked_record() {", 'checked_record "$root_transition_approval"'
    # 冻结02在顶层和activate分支均有批准调用；取函数定义后的首个顶层调用为边界。
    require(source.count(start) == 1 and end in source, "已审02函数边界缺失或定义重复。")
    legacy_start = source.index(start)
    legacy = source[legacy_start:source.index(end, legacy_start)]
    text = template_raw.decode("utf-8")
    substitutions = {"@@GLOBALS@@": shell_globals(inputs, core, core_record, authorization, authorization_record, args.phase, previous),
                     "@@LEGACY_FUNCTIONS@@": legacy, "@@PHASE_BODY@@": body_raw.decode("utf-8")}
    for marker, value in substitutions.items():
        require(text.count(marker) == 1, "静态模板占位边界不唯一。")
        text = text.replace(marker, value, 1)
    require("@@" not in text and "\r" not in text, "生成脚本有未替换标记或非LF字节。")
    script = text.encode("utf-8")
    _, generator_record = inputs.raw(Path(__file__).resolve())
    inputs.unchanged()
    # 从这里才开始写；校验失败不会留下可执行输出。
    if not generated.exists():
        generated.mkdir(mode=0o750)
    require(generated.is_dir() and not generated.is_symlink(), "输出父目录不是规范普通目录。")
    output.mkdir(mode=0o750)
    write_exclusive(output / "control.sh", script, 0o750)
    write_exclusive(output / "core_inputs.json", core_raw)
    write_exclusive(output / "phase_authorization.json", authorization_raw)
    manifest = {"format": "sn100-integration-12-01-runtime-generation-v1", "phase": args.phase,
                "script_generated": True, "board_executed": False, "core_inputs": core_record,
                "authorization": authorization_record, "generator": generator_record,
                "file_phase_verifier": inputs.file_phase_verifier_record,
                "common_template": template_record, "phase_template": body_record,
                "script": {"path": (output / "control.sh").relative_to(ROOT).as_posix(), "sha256": digest(script), "bytes": len(script)},
                "read_inputs": sorted(inputs.seen.values(), key=lambda item: item["path"])}
    write_exclusive(output / "generation_manifest.json", (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    print(json.dumps({"script_generated": True, "board_executed": False, "script": manifest["script"]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, OSError, KeyError, TypeError, tarfile.TarError) as error:
        print("拒绝生成：" + str(error), file=sys.stderr)
        sys.exit(2)
