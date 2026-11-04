#!/usr/bin/env python3
"""只读核对已采集的LIS快照；不连接板卡，不运行或关闭业务程序。

Root须在外层保存本工具的真实退出。退出0只表示采集、短段留屏证据和
已评估软件项未失败；pending继续待验，不能据此填写应用退出0或LIS业务通过。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path, PurePosixPath


HASH = re.compile(r"[0-9a-f]{64}\Z")
DIGITS = re.compile(r"[0-9]+\Z")
POSITIVE = re.compile(r"[1-9][0-9]*\Z")
OBSERVATION_ID = re.compile(r"[A-Za-z0-9_-]{1,64}\Z")
BOOT_ID = re.compile(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\Z")
TOOLTIP = re.compile(
    r"LIS：[^；。\r\n]{1,60}；配置版本([0-9]+)，连接代次([0-9]+)，已连接对端([0-9]+)。\Z"
)
STATES = {
    "Unconfigured", "Configured", "LocalSettingsOnly", "SerialPending", "SettingsUnconfirmed",
    "Listening", "Connecting", "Connected", "ReconnectWaiting", "Disconnected", "Failed", "Stopped",
}
CAPTURE_FILES = {
    "capture_identity_after.json", "capture_identity_before.json", "capture_status.json",
    "lis_observation.json", "lis_observation.ready",
}
EARLY_NOT_GUI_REASON = "实际应用不是QApplication，未读取GUI对象。"
STARTUP_WAIT_REASONS = {
    "正式界面和LIS对象尚未就绪。",
    "正式frmMain对象缺失或不唯一，未选取任意窗口观察。",
    "正式窗口、启动画面结束、已提交LIS设置或真实连接对象尚未同时就绪。",
}


class ReviewError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ReviewError(message)


def safe_board_path(value: str) -> str:
    require(bool(re.fullmatch(r"/[A-Za-z0-9_./-]+", value)), "板端路径须为无空格安全绝对路径。")
    path = PurePosixPath(value)
    require(str(path) == value and ".." not in path.parts, "板端路径必须规范，不能有重定向部分。")
    return value


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-directory", required=True, type=Path)
    for name in (
        "board-serial", "boot-id", "pid", "start-ticks", "release-id", "executable",
        "executable-sha256", "current-link", "expected-current-target", "release-id-file", "observation-id",
    ):
        parser.add_argument("--" + name, required=True)
    return parser.parse_args()


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    value = {}
    for key, item in pairs:
        require(key not in value, "JSON存在重复键。")
        value[key] = item
    return value


def decode_json(raw: bytes) -> dict:
    def invalid_constant(_: str) -> object:
        raise ReviewError("JSON含非标准数值。")

    value = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=invalid_constant)
    require(type(value) is dict, "JSON顶层必须为对象。")
    return value


def read_file(directory: Path, name: str, upper_bound: int) -> bytes:
    path = directory / name
    require(path.is_file() and not path.is_symlink(), f"缺少普通文件：{name}")
    require(0 < path.stat().st_size < upper_bound, f"文件大小不符：{name}")
    data = path.read_bytes()
    require(0 < len(data) < upper_bound, f"读取后文件大小不符：{name}")
    return data


def expect_bool(obj: dict, key: str, expected: bool | None = None) -> bool:
    require(key in obj and type(obj[key]) is bool, f"{key}必须为布尔值。")
    if expected is not None:
        require(obj[key] is expected, f"{key}与本观察合同不符。")
    return obj[key]


def expect_nonnegative_int(obj: dict, key: str) -> int:
    value = obj.get(key)
    require(type(value) is int and 0 <= value <= 2147483647, f"{key}必须为非负整数。")
    return value


def expected_identity(args: argparse.Namespace) -> dict:
    require(args.board_serial == "8314b1e8ffd0321f", "本模板只接受已确认的板B。")
    require(bool(BOOT_ID.fullmatch(args.boot_id)), "预期boot_id格式错误。")
    require(bool(POSITIVE.fullmatch(args.pid)) and bool(POSITIVE.fullmatch(args.start_ticks)), "预期进程身份格式错误。")
    require(bool(HASH.fullmatch(args.release_id)) and bool(HASH.fullmatch(args.executable_sha256)), "预期SHA格式错误。")
    require(bool(OBSERVATION_ID.fullmatch(args.observation_id)), "预期观察ID格式错误。")
    for value in (args.executable, args.current_link, args.expected_current_target, args.release_id_file):
        safe_board_path(value)
    require(args.executable.startswith(args.expected_current_target + "/")
            and args.release_id_file.startswith(args.expected_current_target + "/"),
            "预期程序和发布文件必须属于同一current目标包。")
    return {
        "schema": "sn100-lis-live-capture-identity-v1", "board_serial": args.board_serial,
        "boot_id": args.boot_id, "pid": args.pid, "start_ticks": args.start_ticks,
        "release_id": args.release_id, "executable": args.executable,
        "executable_sha256": args.executable_sha256, "current_link": args.current_link,
        "current_target": args.expected_current_target, "release_id_file": args.release_id_file,
        "release_file_value": args.release_id, "observation_id": args.observation_id,
    }


def validate_snapshot(snapshot: dict, args: argparse.Namespace) -> tuple[str, bool, dict]:
    require(snapshot.get("schema") == "sn100-lis-keep-running-observation-v1", "观察结果schema不符。")
    require(snapshot.get("observation_id") == args.observation_id and snapshot.get("release_id") == args.release_id,
            "观察结果的观察ID或发布ID不符。")
    require(snapshot.get("release_identity_source") == "runtime_expected_not_independently_verified_here",
            "观察入口不得将环境预期发布号冒称自行核包的结果。")
    require(snapshot.get("process") == {"pid": args.pid, "start_ticks": args.start_ticks}, "观察进程身份不符。")
    expect_bool(snapshot, "observation_complete", True)
    require(snapshot.get("completion_kind") == "snapshot_saved_keep_running", "完成标记不属于留屏观察。")
    require("application_exit" in snapshot and snapshot["application_exit"] is None, "未执行的应用退出必须为null。")
    for key in ("application_exit_evaluated", "shutdown_evaluated", "close_verified", "qk_query_requested", "physical_screen_verified"):
        expect_bool(snapshot, key, False)
    for key in ("observer_exit", "waiter_actual_exit", "parser_actual_exit"):
        require(key not in snapshot, "观察快照不能预写工具或不存在的观察进程退出。")
    startup_ready = expect_bool(snapshot, "startup_ready")
    window_alive = expect_bool(snapshot, "window_alive_at_snapshot")
    window_visible = expect_bool(snapshot, "window_visible_at_snapshot")
    require(not window_visible or window_alive, "窗口可见不能同时声明窗口不存在。")
    duration_ms = expect_nonnegative_int(snapshot, "duration_ms")
    startup_wait_ms = expect_nonnegative_int(snapshot, "startup_wait_ms")
    if startup_ready:
        require(duration_ms >= 15000 and startup_wait_ms < 60000,
                "已就绪观察须实际满15秒，且必须在等待达到60秒之前开始。")
    evaluated = expect_bool(snapshot, "software_state_evaluated")
    passed = expect_bool(snapshot, "software_state_passed")
    failed = expect_nonnegative_int(snapshot, "failed")
    checks = snapshot.get("checks")
    require(type(checks) is list and len(checks) <= 1, "本次检查数组只能含0或1个LIS软件项。")
    for check in checks:
        require(type(check) is dict and set(check) == {"name", "passed"}, "检查项字段不符。")
        require(check["name"] == "顶部LIS对象、结构提示与原配置来源可核对", "检查项名称不符。")
        expect_bool(check, "passed")
    require(failed == sum(check["passed"] is False for check in checks), "failed不是实际false项数量。")
    pending = snapshot.get("pending_items")
    require(type(pending) is list and all(type(item) is str and item for item in pending), "pending_items必须为原因文字数组。")
    lis = snapshot.get("lis_connection_observation")
    require(type(lis) is dict, "缺少实际LIS观察快照。")
    require(all(key in lis for key in ("status", "connected", "state", "business_verified", "close_verified_here")),
            "LIS快照缺少必要字段。")
    status = lis.get("status")
    require(type(status) is str and status in {"integration_error", "pending", "observed"}, "LIS快照状态不在合同范围内。")
    expect_bool(lis, "business_verified", False)
    expect_bool(lis, "close_verified_here", False)
    require(lis.get("connected") is None or type(lis.get("connected")) is bool, "LIS connected字段类型错误。")

    state = lis.get("state")
    state_is_object = type(state) is dict
    state_legal = False
    revision = generation = None
    peers = None
    if state_is_object:
        config = state.get("configuration")
        revision = config.get("revision") if type(config) is dict else None
        generation = state.get("generation")
        peers = state.get("connected_peers")
        state_legal = (type(state.get("state")) is str and state["state"] in STATES
                       and type(revision) is str and bool(DIGITS.fullmatch(revision))
                       and type(generation) is str and bool(DIGITS.fullmatch(generation))
                       and type(peers) is int and peers >= 0 and type(state.get("closing")) is bool)
        if status != "integration_error":
            require(state_legal, "非错误LIS快照的状态、版本、代次或对端数不合法。")
        if state_legal:
            connected = not state["closing"] and peers > 0
            require(type(lis.get("connected")) is bool and lis["connected"] is connected, "真实连接标志与状态不符。")
            if peers == 0 or state["closing"]:
                require(status != "observed", "无可采用真实对端不能记observed。")
    elif status != "integration_error":
        require(state is None, "未就绪LIS状态必须为对象或null。")

    configuration_matches = lis.get("configuration_matches_requested_context")
    require(configuration_matches is None or type(configuration_matches) is bool, "配置匹配字段类型错误。")
    if configuration_matches is True:
        committed = lis.get("committed_configuration")
        require(type(committed) is dict, "已匹配配置缺少已提交值。")
        expected_values = {
            "original_profile": True, "type": "tcp", "ip": "127.0.0.1", "port": "3800", "tcp_type": "client",
            "link": True, "auto_get": False, "auto_send": False, "simulation": False,
        }
        for key, value in expected_values.items():
            require(type(committed.get(key)) is type(value) and committed[key] == value, "已提交配置与获准条件不符。")
    committed = lis.get("committed_configuration")
    state_config = state.get("configuration") if state_is_object else None
    source = committed.get("source_fingerprint") if type(committed) is dict else None
    connected_source = state_config.get("source_fingerprint") if type(state_config) is dict else None
    both_sources_are_hashes = (type(source) is str and bool(HASH.fullmatch(source))
                              and type(connected_source) is str and bool(HASH.fullmatch(connected_source)))
    fingerprint_equal = lis.get("connection_and_settings_source_fingerprint_equal")
    require(fingerprint_equal is None or type(fingerprint_equal) is bool, "来源相等字段必须为bool或null。")
    if type(fingerprint_equal) is bool:
        require(both_sources_are_hashes, "来源相等判断缺少实际SHA。")
        require(fingerprint_equal is (source == connected_source), "来源相等字段与实际两个SHA不符。")

    icon = lis.get("icon_lookup")
    icon_matched = type(icon) is dict and icon.get("matched") is True
    if type(icon) is dict and "matched" in icon:
        expect_bool(icon, "matched")
    if icon_matched:
        require(icon.get("scope") == "frmMain/frmBg/statusArea/btnLis", "图标定位范围不符。")
        for key in ("background_count", "status_area_count", "button_count"):
            require(type(icon.get(key)) is int and icon[key] == 1, "图标定位各层数量必须实际为1。")
        expected_widgets = {
            "background": {"object_name": "frmBg", "class": "QFrame", "parent_object_name": "frmMain", "parent_class": "frmMain"},
            "status_area": {"object_name": "statusArea", "class": "CusView", "parent_object_name": "frmBg", "parent_class": "QFrame"},
            "button": {"object_name": "btnLis", "class": "CusBadgeButton", "parent_object_name": "statusArea", "parent_class": "CusView"},
        }
        for key, identity in expected_widgets.items():
            require(icon.get(key) == identity, "图标各层实际类型、名称或父对象不符。")
    prefix = lis.get("button_tooltip_status_prefix")
    require(prefix is None or type(prefix) is str, "图标提示前缀类型错误。")
    match = TOOLTIP.fullmatch(prefix) if type(prefix) is str and prefix else None
    numeric_identity_matches = None
    if match is not None:
        numeric_identity_matches = (state_is_object and type(revision) is str and type(generation) is str
                                    and type(peers) is int and match.group(1) == revision
                                    and match.group(2) == generation and match.group(3) == str(peers))
    declared_tooltip_match = lis.get("tooltip_identity_matches_state")
    require(declared_tooltip_match is None or type(declared_tooltip_match) is bool, "提示身份匹配字段类型错误。")
    require(declared_tooltip_match is numeric_identity_matches, "提示身份匹配字段与独立解析不符。")

    # 能评估与能通过分开：来源为false或提示数值不符是失败，不能降格成pending。
    for key in ("settings_ready", "settings_busy", "settings_dirty"):
        if key in lis:
            expect_bool(lis, key)
    settings_confirmed = (lis.get("settings_ready") is True and lis.get("settings_busy") is False
                          and lis.get("settings_dirty") is False)
    revision_is_positive = type(revision) is str and bool(POSITIVE.fullmatch(revision))
    prerequisites = (configuration_matches is True and state_is_object and type(fingerprint_equal) is bool
                     and settings_confirmed and revision_is_positive and both_sources_are_hashes
                     and icon_matched and match is not None)
    expected_evaluated = status == "integration_error" or prerequisites
    expected_passed = (status != "integration_error" and prerequisites and fingerprint_equal is True
                       and numeric_identity_matches is True)
    require(evaluated is expected_evaluated, "软件评估标志与实际前置条件不符。")
    require(passed is expected_passed, "软件通过标志与独立核对不符。")
    require(len(checks) == int(evaluated), "检查数组数量与评估状态不符。")
    if evaluated:
        require(checks[0]["passed"] is passed, "检查项与软件结论不符。")
    else:
        require(not passed and failed == 0 and pending, "未评估项必须保留pending原因，不填写通过。")
    if not startup_ready:
        require(not evaluated and not passed and not checks and failed == 0 and duration_ms == 0,
                "启动未就绪不能预填软件评估或观测时长。")
        require(status == "pending" and lis.get("connected") is None and state is None,
                "未进入观察时只能保存未读取真实LIS状态的pending快照。")
        # 源码仅允许非GUI应用提前封口；其他未就绪原因须实际等到截止。
        reason = lis.get("reason")
        require(type(reason) is str and (reason == EARLY_NOT_GUI_REASON
                or (startup_wait_ms >= 60000 and reason in STARTUP_WAIT_REASONS)),
                "未就绪原因或等待截止与实际观察入口不一致。")
    software = "pass" if evaluated and passed else "fail" if evaluated else "pending"
    return software, window_alive and window_visible, {
        "software_state_evaluated": evaluated, "software_state_passed": passed, "failed": failed,
        "check_count": len(checks), "connected": lis.get("connected"), "lis_status": status,
        "startup_ready": startup_ready, "pending_items": pending,
        "duration_ms": duration_ms, "startup_wait_ms": startup_wait_ms,
    }


def review(args: argparse.Namespace) -> dict:
    expected = expected_identity(args)
    directory = args.capture_directory
    require(directory.is_absolute() and directory.is_dir() and not directory.is_symlink(), "采集目录须为实际绝对目录。")
    require(directory.resolve(strict=True) == directory.absolute(), "采集目录不能经符号链接重定向。")
    manifest_raw = read_file(directory, "capture_manifest.sha256", 4096)
    manifest_text = manifest_raw.decode("ascii")
    require(manifest_text.endswith("\n"), "指纹清单必须以换行结束。")
    hashes = {}
    for line in manifest_text.splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_.-]+)", line)
        require(match is not None, "指纹清单格式错误。")
        digest, name = match.groups()
        require(name not in hashes, "指纹清单存在重复文件。")
        hashes[name] = digest
    require(set(hashes) == CAPTURE_FILES, "指纹清单必须恰好列出5个封口采集文件。")
    files = {}
    for name in CAPTURE_FILES:
        files[name] = read_file(directory, name, 1048576 if name == "lis_observation.json" else 4096)
        require(hashlib.sha256(files[name]).hexdigest() == hashes[name], f"采集文件SHA不符：{name}")
    require(len(files["lis_observation.ready"]) < 512, "ready标记大小不符。")
    expected_ready = (f"SN100_LIS_OBSERVATION_READY_V1 {args.observation_id} {args.release_id} "
                      f"{args.pid} {hashes['lis_observation.json']}\n").encode("ascii")
    require(files["lis_observation.ready"] == expected_ready, "ready标记的观察身份或实际JSON SHA不符。")
    before = decode_json(files["capture_identity_before.json"])
    after = decode_json(files["capture_identity_after.json"])
    require(before == expected and after == expected, "采集前后身份与Root明确预期不一致。")
    require(files["capture_identity_before.json"] == files["capture_identity_after.json"], "前后身份原件字节不一致。")
    status = decode_json(files["capture_status.json"])
    require(status == {
        "schema": "sn100-lis-live-capture-status-v1", "collection_complete": True,
        "application_exit": None, "application_exit_evaluated": False, "shutdown_evaluated": False,
        "reason": "sealed_snapshot_copied_without_application_stop",
    }, "采集状态未完成或预写了不属于本次的退出结论。")
    # dict相等会把0与False视作相等，关键协议字段仍单独按真实类型核对。
    expect_bool(status, "collection_complete", True)
    expect_bool(status, "application_exit_evaluated", False)
    expect_bool(status, "shutdown_evaluated", False)
    require(status["application_exit"] is None, "应用退出字段必须为null。")
    snapshot = decode_json(files["lis_observation.json"])
    software, windows_visible, details = validate_snapshot(snapshot, args)
    # 本地文件也须在整次解析中保持；不读取持续增长的应用日志或资源表。
    require(read_file(directory, "capture_manifest.sha256", 4096) == manifest_raw, "解析期间清单改变。")
    for name, original in files.items():
        require(read_file(directory, name, 1048576 if name == "lis_observation.json" else 4096) == original,
                "解析期间采集文件改变。")
    return {
        "schema": "sn100-lis-live-capture-review-v1", "observation_id": args.observation_id,
        "release_id": args.release_id, "collection_passed": True,
        "keep_running_evidence_passed": windows_visible, "software_result": software,
        "lis_business_result": "pending", "application_exit": None, "shutdown_evaluated": False,
        "close_verified": False, "physical_screen_verified": False,
        "evidence_scope": "sealed_snapshot_and_same_live_process_identity_before_after",
        "details": details,
    }


def main() -> int:
    args = arguments()
    try:
        result = review(args)
    except (ReviewError, OSError, ValueError, UnicodeError) as error:
        result = {
            "schema": "sn100-lis-live-capture-review-v1", "collection_passed": False,
            "keep_running_evidence_passed": False, "software_result": "pending", "lis_business_result": "pending",
            "application_exit": None, "shutdown_evaluated": False, "close_verified": False,
            "physical_screen_verified": False, "reason": str(error),
        }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if (result["collection_passed"] and result["keep_running_evidence_passed"]
                 and result["software_result"] != "fail") else 1


if __name__ == "__main__":
    sys.exit(main())
