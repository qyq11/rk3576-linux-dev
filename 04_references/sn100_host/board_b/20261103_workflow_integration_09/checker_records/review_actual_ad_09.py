#!/usr/bin/env python3
"""独立只读核对最终09实际联机结果中的AD生效证据。

用法：review_actual_ad_09.py ABSOLUTE_CASE_DIRECTORY ABSOLUTE_BINDING_DIRECTORY
                         [--output ABSOLUTE_NEW_REPORT_JSON]
只读本次取回的app.log、results/result.json及绑定记录，不运行产品、不联网。
默认只输出JSON；只有明确给--output时才新建报告，绝不覆盖输入或已有报告。
发布身份、源码与对象数量从本批09准备、链接及实际构建记录交叉读取，不写入既往实际值。
原24项与急诊、LIS软件状态、当前告警三个可选项按实际数组和评估记录判断，不预写总数。
急诊只核当前快照记录一致，不证明本次新建或直接取得main中的同一内存bool。
完整TX范围和最终09实际退出合同由既有summarize_run.py验收，本工具不代替它。
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys
import unicodedata


SCHEMA = "sn100-actual-ad-independent-review-09-v1"
CHECKER_NAME = "status_login_review_09"
N_READY_MARKER = "REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false"
TASK_READY_MARKER = "TASK_REAGENT_SHUTDOWN_READY"
EMERGENCY_CHECK = "本次已验原INI布尔值与干净急诊初建孔的实际container一致"
LIS_CHECK = "原LIS配置来源与实际连接状态可读；无对端不冒称通信通过"
ALARM_CHECK = "明确一次Qk的真实当前告警回包与当前页一致并正常关闭"
CLEAR_CHECK = "旧连接失效后AD数值列为缺值且本次上报标记已清除"
REOPEN_CHECK = "恢复原INI中的实际主控连接，收到新状态"
AD_CHECK = "本次新连接实际Qq的1至24通道四项原文字与96个本次上报单元格一致"
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


class AuditError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise AuditError(message)


def sha(path):
    require(path.is_file() and not path.is_symlink(), "需要普通证据文件：" + str(path))
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "JSON含重复键，不能任取一个值：" + key)
        result[key] = value
    return result


def read_json(path):
    require(path.is_file() and not path.is_symlink(), "缺少普通JSON证据文件：" + str(path))
    require(path.stat().st_size <= 32 * 1024 * 1024, "JSON超过本检查器32MiB读取预算。")
    def invalid_constant(value):
        raise AuditError("JSON含非标准数值常量：" + value)
    return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=unique_object,
                      parse_constant=invalid_constant)


def integer(value, name, minimum=0):
    require(type(value) is int and value >= minimum, name + "必须是已确认的整数。")
    return value


def digest(value, name):
    require(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value),
            name + "必须是完整的小写SHA-256。")
    return value


def read_text(path, limit=128 * 1024 * 1024):
    require(path.is_file() and not path.is_symlink(), "缺少普通文字证据文件：" + str(path))
    require(path.stat().st_size <= limit, "文字证据超过本检查器读取预算：" + path.name)
    return path.read_text(encoding="utf-8-sig")


def generation(value, name):
    require(isinstance(value, str) and re.fullmatch(r"[0-9]+", value), name + "必须是本次JSON记录的完整十进制代次。")
    number = int(value)
    require(number <= 2**64 - 1, name + "超出uint64范围。")
    return number


class RxStream:
    """从原Windows帧格式独立重写；不导入正式代码或观察器头的计算结果。"""
    def __init__(self):
        self.buffer = bytearray()
        self.assembly = bytearray()
        self.total = self.next = 0
        self.first_line = None
        self.frames = self.messages = self.fragmented = 0
        self.completed = []

    def feed(self, raw, line):
        require(len(self.buffer) + len(raw) <= 2 * 1024 * 1024, "RX缓冲超过2MiB检查预算。")
        self.buffer.extend(raw)
        while self.buffer:
            if self.buffer == b"\x06":
                break
            require(self.buffer[:2] == b"\x06\x07", "第%d行后的RX无法从06/07帧头连续解读。" % line)
            if len(self.buffer) < 4:
                break
            length = int.from_bytes(self.buffer[2:4], "big")
            require(length > 4, "RX长度不足四字节分片头和非空内容。")
            if len(self.buffer) < length + 7:
                break
            frame = bytes(self.buffer[:length + 7])
            del self.buffer[:length + 7]
            require(frame[-2:] == b"\x08\x09", "第%d行RX长度所指帧尾不是08/09。" % line)
            packet = frame[4:-3]
            checksum = 0
            for value in packet:
                checksum ^= value
            require(checksum == frame[-3], "第%d行RX异或校验不一致。" % line)
            self.frames += 1
            index, total = int.from_bytes(packet[:2], "big"), int.from_bytes(packet[2:4], "big")
            require(total > 0 and index < total, "RX分片序号/总数无效。")
            if index == 0:
                require(self.total == 0, "旧业务消息缺片时又收到新起始片，不能合并。")
                self.total, self.next, self.first_line = total, 0, line
            require(self.total == total and self.next == index, "RX分片孤立、重复或乱序。")
            require(len(self.assembly) + len(packet[4:]) <= 4 * 1024 * 1024, "RX业务消息超过4MiB检查预算。")
            self.assembly.extend(packet[4:])
            self.next += 1
            if self.next == self.total:
                self.messages += 1
                self.fragmented += self.total > 1
                self.completed.append({"raw": bytes(self.assembly), "first_line": self.first_line,
                                       "last_line": line, "fragments": total})
                self.assembly.clear()
                self.total = self.next = 0
                self.first_line = None


def parse_qq(raw):
    require(raw.startswith(b"Qq"), "内部审查入口不是Qq。")
    fields = []
    cursor = 2
    while cursor < len(raw):
        require(raw[cursor:cursor + 1] == b"[", "Qq字段没有完整、连续的方括号。")
        end = raw.find(b"]", cursor + 1)
        require(end >= 0 and len(fields) < 8192, "Qq未闭合或超过8192字段检查预算。")
        field = raw[cursor + 1:end]
        require(all(0x20 <= value <= 0x7e and value != 0x5b for value in field),
                "Qq含无法独立确认的非ASCII、控制字符或嵌套括号。")
        fields.append(field.decode("ascii"))
        cursor = end + 1
    require(fields and re.fullmatch(r"[ \t]*\+?[0-9]+[ \t]*", fields[0]), "Qq声明数量无法确认。")
    count = int(fields[0])
    require(count <= 4095 and len(fields) >= 1 + count * 2, "Qq前N对字段不足或超过检查预算。")
    channels, duplicates = {}, 0
    for index in range(count):
        key, value = fields[1 + 2 * index:3 + 2 * index]
        if key in channels:
            duplicates += 1
        else:
            channels[key] = value
    # 数值串不转int/float，不把65535判成物理有效或无效；尾部Base等保持原文另存。
    return {"declared_count": count, "channels": channels, "duplicate_keys": duplicates,
            "ignored_tail_fields": fields[1 + count * 2:], "raw_qq": raw.decode("ascii"),
            "raw_qq_hex": raw.hex(" ")}


def check_lifecycle_sources(guard, snapshot, binding, label):
    sources = guard.get("source_sha256")
    require(isinstance(sources, dict) and sources, label + "缺少本批源码校验清单。")
    frozen = binding / "frozen_09/sn100_host"
    require(frozen.is_dir() and not frozen.is_symlink(), "缺少本批09冻结源码。")
    for name, expected in sources.items():
        require(isinstance(name, str) and name and "\\" not in name
                and not Path(name).is_absolute() and ".." not in Path(name).parts,
                label + "源码路径不是冻结目录内的相对文件。")
        require(snapshot.get(name) == digest(expected, label + "." + name)
                and sha(frozen / name) == expected, label + "与本批源码快照/实际文件不一致：" + name)
    return len(sources)


def check_binding(case, binding):
    prepared = read_json(binding / "prepared_inputs.json")
    link = read_json(binding / "link_provenance.json")
    contract = read_json(binding / "shutdown_contract.json")
    execution = read_json(binding / "execution_09_result.json")
    before = read_json(binding / "execution_09_inputs_before.json")
    after = read_json(binding / "execution_09_inputs_after.json")
    require(all(isinstance(value, dict) for value in (prepared, link, contract, execution, before, after)),
            "本批绑定记录必须是JSON对象。")
    release = digest(prepared.get("release_id"), "prepared.release_id")
    formal = prepared.get("formal_identity")
    require(isinstance(formal, dict), "prepared缺少正式来源身份。")
    application = digest(formal.get("application_sha256"), "formal.application_sha256")
    main_source = digest(formal.get("main_sha256"), "formal.main_sha256")
    source_archive = digest(prepared.get("source_archive_sha256"), "prepared.source_archive_sha256")
    source_count = integer(prepared.get("source_count"), "prepared.source_count", 1)
    object_count = integer(link.get("object_count"), "link.object_count", 1)
    require(formal.get("release_id") == release and formal.get("source_count") == source_count,
            "prepared正式来源的发布身份或源码数量不一致。")
    for name, value in (("prepared", prepared), ("link", link)):
        require(value.get("schema") == "sn100-review-09-v1" and value.get("review_generation") == "09",
                name + "不是09绑定记录，不能沿用其他批次结果。")
        require(value.get("release_id") == release and value.get("source_count") == source_count
                and value.get("source_archive_sha256") == source_archive,
                name + "与本批发布、源码数量或源码包身份不一致。")
    require(prepared.get("status") == "bound_to_final_09_archive", "准备输入尚未绑定最终09归档。")
    require(link.get("formal_identity") == formal, "prepared与link正式身份不一致。")
    objects = link.get("objects")
    require(isinstance(objects, list) and len(objects) == object_count, "链接对象数量不完整。")
    for index, item in enumerate(objects):
        require(isinstance(item, dict) and type(item.get("index")) is int and item["index"] == index
                and item.get("kind") in ("source", "moc", "resource") and isinstance(item.get("input"), str)
                and isinstance(item.get("path"), str) and Path(item["path"]).name == "obj_%d.o" % index,
                "链接对象索引、种类或路径不连续。")
        digest(item.get("sha256"), "link.objects[%d].sha256" % index)
    require(link.get("main_object_kept") is True and link.get("main_object") == objects[0]
            and objects[0].get("kind") == "source" and objects[0].get("input") == "shell/main.cpp"
            and sum(item["input"] == "shell/main.cpp" for item in objects) == 1,
            "链接记录未保留唯一的正式main对象。")
    require(link.get("prepared_inputs_sha256") == sha(binding / "prepared_inputs.json"), "prepared文件不等于链接时输入。")
    record_without_sha = dict(link)
    recorded_sha = record_without_sha.pop("record_sha256", None)
    canonical = json.dumps(record_without_sha, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    require(recorded_sha == hashlib.sha256(canonical).hexdigest(), "链接记录自身SHA不一致。")
    for field in ("template_sha256", "contract_sha256"):
        require(link.get(field) == prepared.get(field), field + "在prepared与link之间不一致。")
        entries = prepared.get(field)
        require(isinstance(entries, dict) and entries, "缺少绑定文件校验清单：" + field)
        for name, expected in entries.items():
            require(isinstance(name, str) and name not in ("", ".", "..") and "/" not in name and "\\" not in name,
                    "绑定文件名不是目录内的直接文件。")
            require(sha(binding / name) == digest(expected, field + "." + name), "绑定文件已变化：" + name)
    templates = prepared["template_sha256"]
    cpp = digest(templates.get(CHECKER_NAME + ".cpp"), "检查源码校验值")
    observer = digest(templates.get("ad_wire_observer.h"), "AD观察头校验值")
    snapshot = read_json(binding / "source_snapshot_09.json")
    require(isinstance(snapshot, dict) and len(snapshot) == source_count
            and sha(binding / "source_snapshot_09.json") == prepared.get("frozen_snapshot_sha256")
            and link.get("frozen_snapshot_sha256") == prepared.get("frozen_snapshot_sha256"),
            "冻结源码快照身份或数量不一致。")
    contract_sha = prepared["contract_sha256"].get("shutdown_contract.json")
    require(sha(case / "shutdown_contract.json") == contract_sha, "场景和绑定目录的退出合同不属于同一冻结输入。")
    require(contract.get("schema") == "sn100-shutdown-contract-09-v1"
            and contract.get("release_id") == release and contract.get("main_sha256") == main_source,
            "退出合同身份不符。")
    markers = contract.get("required_markers")
    guard = contract.get("n_shutdown_guard")
    require(isinstance(markers, list) and markers and all(isinstance(marker, str) and marker for marker in markers)
            and len(set(markers)) == len(markers)
            and N_READY_MARKER in markers and isinstance(guard, dict)
            and guard.get("required_ready_marker") == N_READY_MARKER
            and guard.get("n_function_verified") is False
            and guard.get("in_flight_n_close_verified") is False,
            "本批09关闭合同标记缺失、重复或扩大了N验收边界。")
    require("LIS_CONNECTION_STOPPED" in markers
            and any(re.search(pattern, "LIS_CONNECTION_SHUTDOWN_UNCONFIRMED")
                    for pattern in contract.get("failure_patterns", [])),
            "最终09合同未包含真实LIS停止及关闭未确认条件。")
    n_source_count = check_lifecycle_sources(guard, snapshot, binding, "N关闭保护")
    current_paths = ("shell/alarm_current_shell.cpp", "shell/alarm_shell.cpp", "alarm_history/alarm_history_host.cpp",
                     "alarm_history/alarm_history_page.cpp", "alarm_history/alarm_history_page_current.cpp",
                     "alarm_history/original_current_alarm_state.cpp", "protocol/read_only_messages.cpp",
                     "original_files/original_system_profile.cpp")
    current_sources = {name: snapshot.get(name) for name in current_paths}
    check_lifecycle_sources({"source_sha256": current_sources}, snapshot, binding, "当前告警与CSV解释")
    task_guard = contract.get("task_shutdown_guard")
    require(isinstance(task_guard, dict) and task_guard.get("required_marker") == TASK_READY_MARKER
            and TASK_READY_MARKER in markers
            and {"SHELL_READY", "SAMPLE_STORE_CLOSED", "REAGENT_STORE_CLOSED", "THREADS_STOPPED true"} <= set(markers)
            and task_guard.get("task_business_verified") is False
            and task_guard.get("in_flight_task_single_outer_close_verified") is False,
            "09实际Task关门条件缺失或扩大为未经验证的业务结论。")
    task_source_count = check_lifecycle_sources(task_guard, snapshot, binding, "Task关闭保护")
    approved = read_json(binding / "approved_final_merge_review.json")
    require(isinstance(approved, dict) and approved.get("final_source_files") == snapshot
            and task_guard.get("approved_review_sha256") == sha(binding / "approved_final_merge_review.json"),
            "Task关闭保护没有绑定本批完整已批准源码审查。")
    observation_scope = read_json(binding / "observation_scope_09.json")
    require(isinstance(observation_scope, dict) and prepared.get("observation_scope") == observation_scope
            and observation_scope.get("schema") == "sn100-review-09-observation-scope-v1"
            and "release_id" in observation_scope and observation_scope["release_id"] in (None, release)
            and observation_scope.get("checker_source_sha256") == cpp
            and observation_scope.get("new_construction_proven") is False
            and observation_scope.get("same_main_memory_bool_read") is False
            and observation_scope.get("raw_output_preserved") is True
            and all(observation_scope.get(name) is False for name in
                    ("task_business_verified", "n_function_verified", "in_flight_task_single_outer_close_verified",
                     "in_flight_n_close_verified", "product_executed"))
            and observation_scope.get("device_tx_scope") == {"base": "Qj", "after_base": "explicit_at_most_one_Qk"},
            "急诊派生范围没有绑定本批检查源码，或扩大了创建/内存bool证明范围。")
    require(execution.get("release_id") == release and execution.get("source_count") == source_count
            and execution.get("object_count") == object_count and execution.get("main_object") == objects[0]
            and execution.get("link_provenance_sha256") == sha(binding / "link_provenance.json")
            and execution.get("link_record_sha256") == recorded_sha,
            "本批实际构建结果与准备、链接记录不一致。")
    step_codes = execution.get("actual_step_exit_codes")
    require(execution.get("checker_built") is True
            and execution.get("complete_source_archive_dependencies_objects_unchanged") is True
            and isinstance(step_codes, dict)
            and set(step_codes) == {"freeze", "build_review", "checker_dependency_check"}
            and all(type(value) is int and value == 0 for value in step_codes.values()),
            "本批冻结、构建、依赖检查没有实际成功且保持输入。")
    require(execution.get("checker_source_sha256") == cpp and execution.get("observer_sha256") == observer,
            "本批实际构建没有使用当前绑定检查源码与AD观察头。")
    current_header = digest(templates.get("current_alarm_observer_09.h"), "当前告警观察头校验值")
    lis_header = digest(templates.get("lis_connection_observer_09.h"), "LIS状态观察头校验值")
    checker = digest(execution.get("checker_sha256"), "execution.checker_sha256")
    require(sha(binding / CHECKER_NAME) == checker, "当前检查程序不等于本批实际构建结果。")
    require(read_text(binding / "review_release_id", 1024).strip() == release,
            "本批检查程序发布身份文件不一致。")
    require(sha(binding / "execution_09_inputs_before.json") == sha(binding / "execution_09_inputs_after.json")
            and before == after, "实际构建前后输入不相同。")
    require(before.get("release_id") == release and before.get("application_sha256") == application
            and before.get("source_count") == source_count and before.get("object_count") == object_count
            and before.get("source_archive_sha256") == source_archive
            and before.get("formal_objects_sha256") == {"obj_%d.o" % item["index"]: item["sha256"] for item in objects},
            "实际构建前后输入与本批正式应用、源码包或对象身份不一致。")
    for name, value in (("prepared", prepared), ("execution", execution), ("inputs", before)):
        require(value.get("board_accessed") is False and value.get("product_executed") is False,
                name + "不是本批仅准备/构建记录。")
    require(execution.get("n_function_verified") is False and execution.get("in_flight_n_close_verified") is False,
            "准备结果越界声称了N业务或在途关闭已验证。")
    require(execution.get("task_business_verified") is False
            and execution.get("in_flight_task_single_outer_close_verified") is False
            and execution.get("new_construction_proven") is False
            and execution.get("same_main_memory_bool_read") is False,
            "准备结果越界声称了Task业务、在途关闭、急诊创建来源或main内存bool已验证。")
    identity = {}
    for line in read_text(case / "identity.txt", 1024 * 1024).splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            require(key not in identity, "identity.txt有重复键：" + key)
            identity[key] = value
    require(identity.get("MODE") == "auto", "本工具只审本次auto专项结果，不把人工窗口当专项。")
    require(identity.get("RELEASE_ID") == release and identity.get("APP_SHA256") == application
            and identity.get("PROBE_SHA256") == checker, "场景正式版本/检查器SHA不属于本批最终09。")
    return {"release_id": release, "application_sha256": application, "checker_sha256": checker,
            "checker_source_sha256": cpp, "observer_sha256": observer, "main_source_sha256": main_source,
            "main_object_sha256": objects[0]["sha256"], "source_archive_sha256": source_archive,
            "prepared_sha256": sha(binding / "prepared_inputs.json"),
            "link_sha256": sha(binding / "link_provenance.json"), "link_record_sha256": recorded_sha,
            "execution_sha256": sha(binding / "execution_09_result.json"),
            "inputs_before_and_after_sha256": sha(binding / "execution_09_inputs_before.json"),
            "contract_sha256": contract_sha, "required_shutdown_marker_count": len(markers),
            "n_lifecycle_source_count": n_source_count, "task_lifecycle_source_count": task_source_count,
            "observation_scope_sha256": sha(binding / "observation_scope_09.json"),
            "source_count": source_count, "formal_object_count": object_count,
            "current_alarm_observer_sha256": current_header, "lis_connection_observer_sha256": lis_header,
            "current_alarm_source_sha256": current_sources}


def find_pass(lines, name):
    pattern = re.compile(r"(?:^|\s)PASS\s+" + re.escape(name) + r"\s*$")
    found = [number for number, line in enumerate(lines, 1) if pattern.search(line)]
    require(len(found) == 1, "无法唯一定位本次检查边界：" + name)
    return found[0]


def review_check_array(probe):
    """保留候选原记录，只派生当前视图范围；不据空孔倒推创建来源。"""
    checks = probe.get("checks")
    require(isinstance(checks, list) and checks
            and all(isinstance(item, dict) and isinstance(item.get("name"), str)
                    and type(item.get("passed")) is bool for item in checks),
            "检查器逐项结果缺失或格式不符。")
    raw = probe.get("emergency_default_container_observation")
    require(isinstance(raw, dict) and type(raw.get("evaluated")) is bool and type(raw.get("passed")) is bool,
            "缺少09急诊当前视图的实际观察记录。")
    require(raw.get("same_main_memory_bool_read") is False
            and raw.get("sample_identity_or_tasks_recorded") is False
            and all(raw.get(name) is False for name in ("normal_new_rack_verified", "other_bool_branch_verified",
                                                       "save_reopen_manual_cal_clear_verified", "physical_screen_verified")),
            "急诊原记录扩大了本次只读观察范围。")
    if "new_construction_proven" in raw:
        require(raw["new_construction_proven"] is False, "急诊快照不能证明创建来源。")
    optional = [item for item in checks if item["name"] == EMERGENCY_CHECK]
    lis = probe.get("lis_connection_observation")
    alarm = probe.get("current_alarm_observation")
    require(isinstance(lis, dict) and type(lis.get("software_state_evaluated")) is bool
            and type(lis.get("software_state_passed")) is bool,
            "缺少本次LIS软件状态项的实际评估记录；不读取LIS正文。")
    require(isinstance(alarm, dict) and type(alarm.get("evaluated")) is bool
            and type(alarm.get("passed")) is bool and alarm.get("status") in ("pass", "pending", "fail"),
            "缺少本次当前告警项的实际评估记录。")
    lis_checks = [item for item in checks if item["name"] == LIS_CHECK]
    alarm_checks = [item for item in checks if item["name"] == ALARM_CHECK]
    alarm_evaluated = alarm["evaluated"] or alarm["status"] == "fail"
    base = [item for item in checks if item["name"] not in (EMERGENCY_CHECK, LIS_CHECK, ALARM_CHECK)]
    require(len(base) == 24 and len(optional) == int(raw["evaluated"])
            and len(lis_checks) == int(lis["software_state_evaluated"])
            and len(alarm_checks) == int(alarm_evaluated),
            "实际数组不等于24基础项加三个真正评估的可选项；未预填总数。")
    if lis_checks:
        require(lis_checks[0]["passed"] is lis["software_state_passed"], "LIS软件状态项与原记录不一致。")
    if alarm_checks:
        require(alarm_checks[0]["passed"] is alarm["passed"], "当前告警项与原记录不一致。")
    else:
        require(alarm["status"] == "pending" and alarm["passed"] is False,
                "没有评估当前告警项时必须保留pending，不能冒充通过。")
    require(type(probe.get("check_count")) is int and probe["check_count"] == len(checks),
            "检查器check_count不等于实际checks数组长度。")
    if raw["evaluated"]:
        require(raw.get("status") in ("pass", "fail") and raw["passed"] is (raw["status"] == "pass")
                and optional[0]["passed"] is raw["passed"], "可选急诊项和实际观察结果不一致。")
        if raw["passed"]:
            is_tube = raw.get("container_is_tube")
            expected = raw.get("expected_container")
            count = integer(raw.get("expected_emergency_holes"), "expected_emergency_holes", 1)
            require(type(is_tube) is bool and type(expected) is int and expected == (0 if is_tube else 1)
                    and type(raw.get("emergency_rack_count")) is int and raw["emergency_rack_count"] == 1
                    and raw.get("startup_and_observation_ini_hashes_match") is True,
                    "急诊已通过记录的bool映射、架数或INI指纹条件不符。")
            digest(raw.get("config_sha256"), "急诊观察config_sha256")
            digest(raw.get("theme_sha256"), "急诊观察theme_sha256")
            holes = raw.get("holes")
            require(isinstance(holes, list) and len(holes) == count, "急诊已通过记录的实际孔数不足。")
            seen = set()
            for hole in holes:
                require(isinstance(hole, dict) and set(hole) == {"hole_number", "actual_container"}
                        and type(hole.get("hole_number")) is int and 1 <= hole["hole_number"] <= count
                        and hole["hole_number"] not in seen and type(hole.get("actual_container")) is int
                        and hole["actual_container"] == expected, "急诊已通过记录的孔号/实际container不符。")
                seen.add(hole["hole_number"])
    else:
        require(raw.get("status") == "pending" and raw["passed"] is False
                and isinstance(raw.get("reason"), str) and raw["reason"],
                "未evaluated的急诊观察必须保留pending及原因，不能计为通过。")
    require(all(item["passed"] is True for item in checks), "实际检查数组包含失败，不能只信顶层failed=0。")
    emergency = {
        "status": raw["status"], "evaluated": raw["evaluated"], "current_view_comparison_passed": raw["passed"],
        "base_check_count": len(base), "optional_check_count": len(optional), "actual_check_count": len(checks),
        "new_construction_proven": False, "same_main_memory_bool_read": False,
        "scope": "只审核冻结候选保存的当前干净急诊快照及未变化INI映射；未独立证明创建来源、时点或main同一内存bool，也未读取可见下拉框、业务库或原INI内容。",
    }
    return checks, raw, emergency, {
        "base_check_count": len(base), "emergency_check_count": len(optional),
        "lis_software_state_check_count": len(lis_checks), "current_alarm_check_count": len(alarm_checks),
        "actual_check_count": len(checks), "lis_status": lis.get("status"),
        "lis_software_state_evaluated": lis["software_state_evaluated"],
        "lis_software_state_passed": lis["software_state_passed"],
        "current_alarm_status": alarm["status"], "current_alarm_evaluated": alarm_evaluated,
        "lis_message_body_read_or_recorded_here": False,
    }


def parse_current_alarm(raw):
    """按正式只读解析器核真实Qk/QK，保留字段原文；反斜杠不是括号转义。"""
    require(2 <= len(raw) <= 128000 and raw[:2] in (b"Qk", b"QK"), "当前告警消息种类/本地长度预算不符。")
    text = raw.decode("utf-8", errors="strict")
    fields, cursor = [], 2
    while cursor < len(text):
        require(text[cursor] == "[", "当前告警字段没有连续完整的方括号。")
        end = text.find("]", cursor + 1)
        require(end >= 0, "当前告警字段没有结束括号。")
        value = text[cursor + 1:end]
        require("[" not in value and not any(unicodedata.category(char) == "Cc" for char in value),
                "当前告警字段含嵌套括号或控制字符。")
        require(len(fields) < 4096 and len(value.encode("utf-8")) <= 4096, "当前告警字段超过正式本地预算。")
        fields.append(value)
        cursor = end + 1
    require(fields and re.fullmatch(r"[0-9]{1,10}", fields[0]), "当前告警数量不是原ASCII非负整数格式。")
    count = int(fields[0])
    columns = 3 if raw[:2] == b"QK" else 2
    require(count <= 1024 and len(fields) == 1 + count * columns, "当前告警数量或精确字段数不符。")
    rows = []
    for index in range(count):
        values = fields[1 + index * columns:1 + (index + 1) * columns]
        row = {"error_code": values[0]}
        row.update({"description": values[1], "measure": values[2]} if columns == 3
                   else {"raw_parameters": values[1]})
        rows.append(row)
    return {"command": raw[:2].decode("ascii"), "declared_count": count, "rows": rows}


def actual_hex(value, name):
    require(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{2}(?: [0-9a-f]{2})*", value),
            name + "不是实际记录的规范十六进制。")
    return bytes.fromhex(value)


def query_tx_payload(raw):
    require(len(raw) >= 12 and raw[:2] == b"\x02\x03", "查询窗口TX没有完整0203帧头。")
    length = int.from_bytes(raw[2:4], "big")
    require(length > 4 and len(raw) == length + 7 and raw[-2:] == b"\x04\x05",
            "查询窗口TX长度/帧尾不符。")
    packet = raw[4:-3]
    checksum = 0
    for value in packet:
        checksum ^= value
    require(checksum == raw[-3] and packet[:4] == b"\x00\x00\x00\x01",
            "查询窗口TX校验或单片查询格式不符。")
    return packet[4:]


def qt_trim(text):
    def space(char):
        return ord(char) in (9, 10, 11, 12, 13, 0x85) or unicodedata.category(char) in ("Zs", "Zl", "Zp")
    first, last = 0, len(text)
    while first < last and space(text[first]): first += 1
    while last > first and space(text[last - 1]): last -= 1
    return text[first:last]


def original_csv_fields(line):
    fields, current, quoted, cursor = [], [], False, 0
    while cursor < len(line):
        char = line[cursor]
        if char == '"':
            if quoted and cursor + 1 < len(line) and line[cursor + 1] == '"':
                current.append('"'); cursor += 1
            else: quoted = not quoted
        elif char == "," and not quoted:
            fields.append(qt_trim("".join(current))); current = []
        else: current.append(char)
        cursor += 1
    require(not quoted, "真实告警规则CSV引号未闭合。")
    fields.append(qt_trim("".join(current)))
    return fields


def alarm_rules(case):
    metadata = read_json(case / "collection.json").get("alarm_rule_source")
    require(isinstance(metadata, dict) and metadata.get("path") == "rules/abnormal_Config.csv",
            "缺少另行白名单采集的真实规则CSV来源。")
    path = case / metadata["path"]
    actual = sha(path)
    require(path.stat().st_size <= 4 * 1024 * 1024 and metadata.get("local_verified") is True
            and metadata.get("stored_exactly_as_remote") is True and metadata.get("redactions") == []
            and metadata.get("metadata_before_after_equal") is True and metadata.get("startup_sha256_matches") is True
            and all(metadata.get(name) == actual for name in
                    ("raw_sha", "local_sha", "startup_sha256", "read_before_sha256", "read_after_sha256")),
            "规则CSV缺失、变化或脱敏；不能冒充启动时原规则。")
    remote = metadata.get("remote_path")
    require(isinstance(remote, str) and remote.startswith("/") and remote.endswith("/db/systemInfo/abnormal_Config.csv"),
            "规则CSV远端来源不是本次原资料路径。")
    startup = read_text(case / "alarm_rules_before.sha256", 1024 * 1024).splitlines()
    require(startup == [actual + "  " + remote], "启动规则指纹清单与实际规则来源不一致。")
    check = read_text(case / "alarm_rules_check.txt", 1024 * 1024)
    require(remote + ": OK" in check and ": FAILED" not in check, "结束时规则校验没有明确通过。")
    exits = re.findall(r"(?m)^ALARM_RULES_CHECK_EXIT=(-?[0-9]+)\s*$", check)
    require(not exits or exits == ["0"], "规则校验真实退出结果不符。")
    raw = path.read_bytes()
    try:
        text, encoding = raw.decode("utf-8", errors="strict"), "UTF-8"
    except UnicodeDecodeError:
        text, encoding = raw.decode("gb18030", errors="strict"), "GB18030"
        require(text.encode("gb18030", errors="strict") == raw, "规则GB18030不能无损往返。")
    require(metadata.get("encoding") == encoding, "独立规则编码与采集记录不一致。")
    if text.startswith("\ufeff"): text = text[1:]
    lines = text.split("\n")
    require(lines and qt_trim(lines[0]).startswith("id,error_code,describe_cn,measure,"), "原规则CSV表头不匹配。")
    rules, count = {}, 0
    for line in lines[1:]:
        line = qt_trim(line)
        if not line: continue
        fields = original_csv_fields(line)
        require(len(fields) >= 9 and fields[1] and fields[7].lower() in ("true", "false"),
                "原规则CSV字段、编号或启用值不合法。")
        count += 1
        # 显示转换按第一匹配编号，不根据open/动作配置过滤；原规则顺序不可被dict覆盖。
        rules.setdefault(fields[1], {"description": fields[2], "measure": fields[3]})
    return rules, {"status": "verified", "path": metadata["path"], "sha256": actual,
                   "encoding": encoding, "parsed_rule_count": count, "first_code_count": len(rules),
                   "startup_and_collection_bytes_equal": True, "raw_csv_output_here": False}


def qk_row_from_rules(source, rules):
    code, raw = source["error_code"], source["raw_parameters"]
    rule = rules.get(code)
    description, measure = (rule["description"], rule["measure"]) if rule else ("", "")
    parameters = [value for value in raw.split("|") if value != ""]
    if 1 <= len(parameters) <= 5:
        # QString多参数arg按原格式中最小编号依次替换，不把参数中新出现的%再次展开。
        pattern = re.compile(r"%L?([0-9]{1,2})")
        numbers = sorted({int(match[1]) for match in pattern.finditer(description)})
        require(0 not in numbers, "规则含未独立确认的%0占位符，本次说明展开待验。")
        replacements = dict(zip(numbers, parameters))
        description = pattern.sub(lambda match: replacements.get(int(match[1]), match[0]), description)
    if not description: description, measure = code + raw, ""
    return description, measure


def _current_alarm_evidence(case, lines, record):
    require(isinstance(record, dict) and type(record.get("triggered")) is bool,
            "缺少当前告警实际触发记录。")
    require(record.get("exact_request_reply_correlation_proven") is False
            and record.get("all_instrument_healthy") is False
            and record.get("history_database_unchanged_claimed") is False,
            "当前告警记录扩大为精确回执、整机健康或整进程无写库证明。")
    begin_pattern = re.compile(r"\bREVIEW_CURRENT_ALARM_QUERY_BEGIN\s+generation=\s*([0-9]+)\s*$")
    end_pattern = re.compile(r"\bREVIEW_CURRENT_ALARM_QUERY_END\s+generation=\s*([0-9]+)\s*$")
    begins = [(n, match[1]) for n, line in enumerate(lines, 1) if (match := begin_pattern.search(line))]
    ends = [(n, match[1]) for n, line in enumerate(lines, 1) if (match := end_pattern.search(line))]
    if not record["triggered"]:
        require(not begins and not ends and record.get("status") == "pending"
                and record.get("evaluated") is False and record.get("passed") is False,
                "未触发当前告警时却有查询边界或通过声明。")
        return {"status": "pending", "triggered": False, "reason": "真实前提未满足，未执行Qk；不补造回包。",
                "is_acknowledgement": False, "qk_csv_description_expansion_independently_verified": False}
    require(len(begins) == len(ends) == 1 and begins[0][0] < ends[0][0], "当前告警BEGIN/END不唯一或顺序不成立。")
    begin, end = begins[0][0], ends[0][0]
    gen = generation(record.get("generation"), "current_alarm.generation")
    require(gen > 0 and begins[0][1] == ends[0][1] == str(gen), "查询边界和记录不是同一实际连接代次。")
    stream, tx, start_partial, end_partial = RxStream(), [], None, None
    partial_message_indices = []
    rx_pattern = re.compile(r"\bDEVICE_WIRE RX\s+([0-9a-fA-F ]+)\s*$")
    tx_pattern = re.compile(r"\bDEVICE_WIRE (TX\S*(?: [A-Za-z_]+)?)\s+([0-9a-fA-F ]+)\s*$")
    for number, line in enumerate(lines, 1):
        if number in (begin, end):
            partial = bool(stream.buffer or stream.total or stream.assembly)
            if partial: partial_message_indices.append(stream.messages)
            if number == begin: start_partial = partial
            else: end_partial = partial
        if "DEVICE_WIRE RX" in line:
            match = rx_pattern.search(line)
            require(match is not None and re.fullmatch(r"[0-9a-fA-F]{2}(?: +[0-9a-fA-F]{2})*", match[1].strip()),
                    "全日志RX存在无法审查的十六进制行。")
            stream.feed(bytes.fromhex(match[1]), number)
        if begin < number < end and "DEVICE_WIRE TX" in line:
            match = tx_pattern.search(line)
            require(match is not None, "当前告警窗口TX日志格式不符。")
            raw = bytes.fromhex(match[2])
            payload = query_tx_payload(raw)
            require(payload in (b"Qj", b"Qk"), "当前告警窗口含批准查询之外的TX。")
            if payload == b"Qk" and (stream.buffer or stream.total or stream.assembly):
                # 固定RxStream的first_line是首片完成行；另记TX时的半帧，防止把此前首字节改称查询后到达。
                partial_message_indices.append(stream.messages)
            tx.append({"line": number, "direction": match[1], "hex": raw.hex(" "), "command": payload.decode("ascii")})
    trailing_partial = bool(stream.buffer or stream.total or stream.assembly)
    captured_tx = record.get("actual_tx_blocks")
    require(isinstance(captured_tx, list) and len(captured_tx) == len(tx), "头内TX数量不等于完整BEGIN/END窗口。")
    for expected, item in zip(tx, captured_tx):
        require(isinstance(item, dict) and item.get("hex") == expected["hex"]
                and item.get("direction") == expected["direction"], "头内TX字节/方向与实际日志不一致。")
        generation(item.get("generation"), "actual_tx_blocks.generation")
        if expected["command"] == "Qk":
            require(item["generation"] == str(gen), "明确Qk不属于本次连接代次。")
    qk_tx = [item for item in tx if item["command"] == "Qk"]
    require(len(qk_tx) <= 1 and record.get("tx_qk_count") == len(qk_tx)
            and record.get("tx_qj_count") == sum(item["command"] == "Qj" for item in tx)
            and record.get("tx_other_count") == record.get("tx_unparsed_block_count") == 0,
            "窗口内不是最多一次明确Qk或计数与真实TX不符。")
    current = [message for message in stream.completed
               if begin < message["first_line"] <= message["last_line"] < end
               and message["raw"][:2] in (b"Qk", b"QK")]
    crossing = [message for message in stream.completed if message["raw"][:2] in (b"Qk", b"QK")
                and ((message["first_line"] < begin < message["last_line"])
                     or (message["first_line"] < end < message["last_line"]))]
    partial_current = any(index < len(stream.completed) and stream.completed[index]["raw"][:2] in (b"Qk", b"QK")
                          for index in partial_message_indices)
    if trailing_partial or crossing or partial_current:
        return {"status": "pending", "triggered": True, "generation": str(gen),
                "actual_qk_tx_count": len(qk_tx), "whole_log_rx_frames": stream.frames,
                "whole_log_rx_messages": stream.messages, "begin_had_partial_rx": start_partial,
                "end_had_partial_rx": end_partial, "whole_log_trailing_partial_rx": trailing_partial,
                "cross_boundary_current_message_count": len(crossing), "partial_current_message": partial_current,
                "is_acknowledgement": False, "exact_request_reply_correlation_proven": False, "QK_confirms_Qk": False,
                "reason": "全日志连续解码后仍有半帧/缺片或跨边界当前消息；保留边界不足，不把窗口中间当完整流起点，也不补造对应回包。"}
    recorded = record.get("actual_payloads")
    observed = record.get("current_observations")
    require(isinstance(recorded, list) and isinstance(observed, list) and len(recorded) <= len(current),
            "头内当前消息没有对应完整真实RX来源。")
    parsed, rejected, decoded = [], [], []
    timing_inconclusive = False
    try:
        rules, rule_evidence = alarm_rules(case)
    except (AuditError, OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        rules, rule_evidence = None, {"status": "pending", "reason": str(error), "raw_csv_output_here": False}
    mapping_complete = rules is not None
    for index, message in enumerate(current):
        item = {"first_line": message["first_line"], "last_line": message["last_line"],
                "fragments": message["fragments"], "payload_hex": message["raw"].hex(" ")}
        try:
            item["decoded"] = parse_current_alarm(message["raw"])
        except (AuditError, UnicodeDecodeError, ValueError) as error:
            item["parse_error"] = str(error)
        decoded.append(item)
        if index >= len(recorded): continue  # 头内窗口在正常关闭前冻结，END前可有真实迟到包。
        entry = recorded[index]
        require(isinstance(entry, dict) and actual_hex(entry.get("payload_hex"), "actual_payloads.payload_hex") == message["raw"]
                and entry.get("command") == message["raw"][:2].decode("ascii")
                and entry.get("connection") == str(gen) and type(entry.get("after_explicit_qk_tx")) is bool,
                "头内payload不是全窗口真实当前消息的有序前缀或连接号不符。")
        require(type(entry.get("elapsed_ms")) in (int, float)
                and entry["elapsed_ms"] <= record.get("payload_and_page_window_end_ms", -1),
                "头内payload超过已声明的关闭前观察边界。")
        # RX原字节先到达、产品随后才分发的情况，不能只靠回调内bool改称查询后回包。
        wire_after_tx = bool(qk_tx) and message["first_line"] > qk_tx[0]["line"]
        crosses_tx = bool(qk_tx) and message["first_line"] < qk_tx[0]["line"] < message["last_line"]
        if crosses_tx or entry["after_explicit_qk_tx"] is not wire_after_tx:
            timing_inconclusive = True
        if "decoded" in item: parsed.append((item["decoded"], entry, wire_after_tx))
        else: rejected.append(index)
    require(record.get("actual_payload_qk_count") == sum(item.get("command") == "Qk" for item in recorded)
            and record.get("actual_payload_qK_count") == sum(item.get("command") == "QK" for item in recorded),
            "头内原始消息数量与数组不一致。")
    if timing_inconclusive:
        return {"status": "pending", "triggered": True, "generation": str(gen),
                "actual_qk_tx_count": len(qk_tx), "decoded_window_current_messages": decoded,
                "wire_and_callback_after_query_boundary_differ": True,
                "is_acknowledgement": False, "exact_request_reply_correlation_proven": False, "QK_confirms_Qk": False,
                "reason": "实际TX/RX行界限与回调分发先后不能等同，或消息跨Qk发送界限；不靠头内bool证明查询后的接收。"}
    # 原消费者可因本地展开容量等原因拒绝已解析输入；缺观察应待验，不能伪造成空批成功。
    matched, cursor = [], 0
    for event in observed:
        require(isinstance(event, dict) and event.get("connection") == str(gen)
                and type(event.get("after_explicit_qk_tx")) is bool and isinstance(event.get("rows"), list)
                and type(event.get("row_count")) is int and event["row_count"] == len(event["rows"]),
                "完整观察来源、数量或行数组不符。")
        match = None
        while cursor < len(parsed):
            value, source, wire_after_tx = parsed[cursor]; cursor += 1
            if (value["command"] != event.get("command") or wire_after_tx is not event["after_explicit_qk_tx"]
                    or value["declared_count"] != event["row_count"]):
                continue
            candidate = True
            for actual, raw_row in zip(event["rows"], value["rows"]):
                if not isinstance(actual, dict) or actual.get("error_code") != raw_row["error_code"] + ("*" if value["command"] == "QK" else ""):
                    candidate = False; break
                if value["command"] == "QK":
                    candidate = candidate and actual.get("description") == raw_row["description"] and actual.get("measure") == raw_row["measure"]
                elif rules is not None:
                    try:
                        description, measure = qk_row_from_rules(raw_row, rules)
                    except AuditError:
                        mapping_complete = False
                    else:
                        candidate = candidate and actual.get("description") == description and actual.get("measure") == measure
            if candidate:
                match = value; break
        require(match is not None and match["declared_count"] == event["row_count"], "完整观察找不到同序真实解析消息。")
        wall_time = datetime.fromisoformat(event["received_at"]).strftime("%Y/%m/%d %H:%M:%S")
        for actual, source in zip(event["rows"], match["rows"]):
            require(isinstance(actual, dict) and set(actual) == {"error_code", "date_time", "description", "measure"}
                    and all(isinstance(text, str) for text in actual.values())
                    and actual["date_time"] == wall_time
                    and actual["error_code"] == source["error_code"] + ("*" if match["command"] == "QK" else ""),
                    "显示观察的原编号、顺序或接收时间不符。")
            if match["command"] == "QK":
                require(actual["description"] == source["description"] and actual["measure"] == source["measure"],
                        "QK显示观察改写了主控原描述/措施。")
            elif rules is not None:
                try:
                    description, measure = qk_row_from_rules(source, rules)
                except AuditError:
                    mapping_complete = False
                else:
                    require(actual["description"] == description and actual["measure"] == measure,
                            "产品Qk观察不等于从启动同字节CSV与真实原参数独立展开的说明/措施。")
        matched.append(event)
    qk_count = sum(item.get("command") == "Qk" and item["after_explicit_qk_tx"] for item in matched)
    qK_count = sum(item.get("command") == "QK" and item["after_explicit_qk_tx"] for item in matched)
    require(record.get("qk_count") == qk_count and record.get("qK_count") == qK_count
            and record.get("qk_observed") is (qk_count > 0), "当前完整观察计数与Qk/QK类别不一致。")
    table = record.get("page_snapshot_before_close", {})
    rows = table.get("rows", [])
    page_ok = bool(matched) and table.get("table_exists") is True and table.get("capture_complete") is True
    page_ok = page_ok and type(table.get("row_count")) is int and table["row_count"] == len(rows)
    descriptions = set()
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get("description"), str): page_ok = False; continue
        if row["description"] in descriptions: page_ok = False
        descriptions.add(row["description"])
        page_ok = page_ok and any(row in event["rows"] for event in matched)
    if matched:
        page_ok = page_ok and all(row["description"] in descriptions for row in matched[-1]["rows"])
    waiting_qK = [item for item in matched if item.get("command") == "QK" and item["after_explicit_qk_tx"]
                  and item.get("query_state_after_observation", {}).get("refresh_pending") is True]
    if record.get("qK_non_confirmation_evaluated") is True:
        require(waiting_qK and record.get("qK_non_confirmation_passed") is True, "QK被误称为Qk确认或缺少真实等待观察。")
    states = record.get("query_states", [])
    timeout = any(isinstance(item, dict) and item.get("connection") == str(gen)
                  and item.get("refresh_pending") is False
                  and "当前告警查询等待结束" in item.get("message", "") for item in states)
    require(record.get("local_timeout_state_event_observed") is timeout, "本地等待提示声明没有对应真实状态事件。")
    # 其它Qj/Qq的跨边界半帧不影响一个完整Qk；只把跨边界的当前告警消息单列待验。
    partial_current = any(index < len(stream.completed) and stream.completed[index]["raw"][:2] in (b"Qk", b"QK")
                          for index in partial_message_indices)
    cross_scope = bool(crossing or partial_current)
    complete = (record.get("capture_complete") is True and not cross_scope
                and record.get("new_event_payload_count") == 0 and page_ok and len(qk_tx) == 1 and qk_count > 0
                and record.get("query_queued") is True and record.get("same_connection_at_observation_end") is True
                and record.get("page_closed") is True and record.get("view_closed_signal_count", 0) > 0
                and record.get("query_state_end_of_observation", {}).get("valid_observation") is True
                and record.get("query_state_end_of_observation", {}).get("refresh_pending") is False)
    if record.get("passed") is True and not cross_scope:
        require(complete and record.get("status") == "pass" and record.get("evaluated") is True
                and record.get("page_correspondence_evaluated") is True
                and record.get("page_correspondence_passed") is True, "头内当前告警通过声明没有完整独立证据。")
    return {"status": "pass" if complete and mapping_complete and record.get("passed") is True else "pending",
            "triggered": True, "generation": str(gen), "begin_line": begin, "end_line": end,
            "actual_qk_tx_count": len(qk_tx), "whole_log_rx_frames": stream.frames,
            "whole_log_rx_messages": stream.messages, "decoded_window_current_messages": decoded,
            "recorded_current_payloads_matched": len(recorded), "late_current_messages_after_header_capture": len(current) - len(recorded),
            "rejected_actual_current_payload_indices": rejected,
            "valid_payloads_without_observation": len(parsed) - len(matched),
            "decoded_qk_messages": sum(item.get("decoded", {}).get("command") == "Qk" for item in decoded),
            "decoded_QK_messages": sum(item.get("decoded", {}).get("command") == "QK" for item in decoded),
            "cross_boundary_or_partial_scope": cross_scope, "qk_observed_after_query": qk_count, "QK_observed_after_query": qK_count,
            "begin_had_partial_rx": start_partial, "end_had_partial_rx": end_partial,
            "actual_table_correspondence": bool(page_ok), "local_timeout_state_event_observed": timeout,
            "is_acknowledgement": False, "exact_request_reply_correlation_proven": False,
            "QK_confirms_Qk": False, "qk_csv_description_expansion_independently_verified": mapping_complete and qk_count > 0,
            "alarm_rule_source": rule_evidence,
            "page_visibility_independently_verified": False,
            "generation_scope": "连接号来自同版检查器真实信号及日志元数据；包字节自身无连接号，不能由原包推算代次。",
            "reason": "分别核真实RX、启动同字节CSV、产品观察与表格；无Qk、规则来源不足或跨边界当前消息保持待验，Qk不是ACK。"}


def review_current_alarm(case, lines, record):
    try:
        return _current_alarm_evidence(case, lines, record)
    except (AuditError, OSError, ValueError, TypeError, KeyError, AttributeError, IndexError, re.error) as error:
        claimed = isinstance(record, dict) and (record.get("passed") is True or record.get("status") == "fail")
        return {"status": "fail" if claimed else "pending", "evidence_consistent": False, "error": str(error),
                "is_acknowledgement": False, "exact_request_reply_correlation_proven": False, "QK_confirms_Qk": False}


def audit(case, binding):
    bound = check_binding(case, binding)
    release = bound["release_id"]
    probe = read_json(case / "results/result.json")
    require(probe.get("release_id") == release and probe.get("board") == "board_b", "结果JSON不是本次09板B。")
    require(probe.get("completed") is True and type(probe.get("failed")) is int and probe["failed"] == 0,
            "检查器没有完整结束或已有失败。")
    checks, raw_emergency, emergency, optional_checks = review_check_array(probe)
    ad = probe.get("independent_ad")
    require(isinstance(ad, dict), "缺少independent_ad结果。")
    old = generation(ad.get("old_generation"), "old_generation")
    new = generation(ad.get("new_generation"), "new_generation")
    observed = ad.get("raw_observation")
    require(isinstance(observed, dict), "缺少本代原始观察结果。")
    require(new > old and generation(observed.get("generation"), "raw_observation.generation") == new,
            "AD观察没有明确属于严格增加的新连接代次。")
    require(ad.get("same_new_connection") is True and observed.get("current_qq_received") is True,
            "没有声明观察到同一新连接的有效Qq。")
    require(observed.get("error") == ""
            and integer(observed.get("error_count"), "error_count") == 0
            and integer(observed.get("rejected_generation_feeds"), "rejected_generation_feeds") == 0,
            "观察器已有帧/代次错误。")
    require(all(integer(observed.get(name), name) == 0 for name in
                ("buffered_bytes", "assembly_bytes", "total_fragments", "next_fragment")),
            "比较时仍有半帧或缺片，不能用之前读数放行。")
    raw_log = read_text(case / "app.log")
    lines = ANSI.sub("", raw_log).splitlines()
    release_lines = [line for line in lines if re.search(r"\bREVIEW_RELEASE_ID\s+" + re.escape(release) + r"\s*$", line)]
    require(len(release_lines) == 1, "app.log没有唯一的本次检查器发布身份。")
    clear_line, reopen_line, ad_line = (find_pass(lines, name) for name in (CLEAR_CHECK, REOPEN_CHECK, AD_CHECK))
    require(clear_line < reopen_line < ad_line, "实际日志中的清除、重连、AD比较顺序不成立。")
    stream, rx_lines = RxStream(), []
    wire = re.compile(r"\bDEVICE_WIRE RX\s+([0-9a-fA-F ]+)\s*$")
    for number in range(clear_line + 1, ad_line):
        line = lines[number - 1]
        if "DEVICE_WIRE RX" not in line:
            continue
        match = wire.search(line)
        require(match is not None, "本次新连接窗口RX日志格式无法完整解析，第%d行。" % number)
        value = match[1].strip()
        require(re.fullmatch(r"[0-9a-fA-F]{2}(?: +[0-9a-fA-F]{2})*", value), "RX十六进制不完整。")
        raw = bytes.fromhex(value)
        rx_lines.append(number)
        stream.feed(raw, number)
    require(rx_lines and not stream.buffer and stream.total == 0 and not stream.assembly,
            "新连接窗口没有完整RX证据，或比较时仍有未完成帧/分片。")
    qq = []
    for message in stream.completed:
        if message["raw"].startswith(b"Qq"):
            parsed = parse_qq(message["raw"])
            parsed.update({key: message[key] for key in ("first_line", "last_line", "fragments")})
            qq.append(parsed)
    require(qq, "新连接清除边界之后没有从原始RX独立还原出的Qq；不回退使用其他批次或旧连接值。")
    latest = qq[-1]
    require(observed.get("raw_qq") == latest["raw_qq"]
            and observed.get("raw_qq_hex") == latest["raw_qq_hex"], "JSON中的原Qq不等于新连接窗口实际最后一条Qq。")
    require(observed.get("raw_qq_history") == [entry["raw_qq"] for entry in qq[-32:]]
            and observed.get("history_truncated") is (len(qq) > 32), "原Qq历史与实际RX窗口不一致。")
    require(observed.get("channels_first_declared_pairs") == latest["channels"]
            and observed.get("declared_count") == latest["declared_count"]
            and observed.get("duplicate_channel_keys") == latest["duplicate_keys"]
            and observed.get("ignored_tail_fields") == latest["ignored_tail_fields"], "首N对/首键/尾字段的独立解析与观察JSON不一致。")
    for name, expected in (("valid_frames", stream.frames), ("complete_messages", stream.messages),
                           ("fragmented_messages", stream.fragmented), ("seen_qq_count", len(qq)), ("valid_qq_count", len(qq))):
        require(type(observed.get(name)) is int and observed[name] == expected, "实际RX与观察器计数不一致：" + name)
    channels = latest["channels"]
    missing = [str(channel) for channel in range(1, 25)
               if str(channel) not in channels or len(channels[str(channel)].split(" ")) < 4]
    supplied = sum(4 for channel in range(1, 25)
                   if str(channel) in channels and len(channels[str(channel)].split(" ")) >= 4)
    coverage = ad.get("current_qq_coverage")
    require(isinstance(coverage, dict) and coverage.get("missing_or_short_keys") == missing
            and type(coverage.get("supplied_cells")) is int and coverage["supplied_cells"] == supplied,
            "current_qq_coverage不等于原RX首N对实际提供的覆盖范围。")
    require(not missing and supplied == 96 and coverage.get("ok") is True,
            "本次实际Qq没有完整提供1至24通道的96项，不能沿用旧列或其他批次数值。")
    rows = ad.get("table_rows_at_comparison")
    require(isinstance(rows, list) and rows, "缺少实际table_rows_at_comparison，不能只信passed=true。")
    keyed = {}
    for row in rows:
        require(isinstance(row, dict) and isinstance(row.get("key"), str), "表格行缺少完整字符串键。")
        key = row["key"]
        require(key not in keyed, "实际表格有重复完整通道键：" + key)
        keyed[key] = row
    mismatches = []
    for channel in range(1, 25):
        key = str(channel)
        row = keyed.get(key)
        if row is None:
            mismatches.append({"channel": key, "reason": "missing_exact_table_key"})
            continue
        values, reported = row.get("values"), row.get("reported")
        if not (isinstance(values, list) and len(values) == 4 and all(isinstance(value, str) for value in values)
                and isinstance(reported, list) and len(reported) == 4 and all(type(value) is bool for value in reported)):
            mismatches.append({"channel": key, "reason": "actual_cell_arrays_not_four_strings_and_booleans"})
            continue
        expected = channels[key].split(" ")[:4]
        for column in range(4):
            if values[column] != expected[column] or reported[column] is not True:
                mismatches.append({"channel": key, "value_column": column + 2, "expected_text": expected[column],
                                   "actual_text": values[column], "expected_reported": True, "actual_reported": reported[column]})
    comparison = ad.get("display_comparison")
    require(isinstance(comparison, dict) and comparison.get("generation") == str(new)
            and comparison.get("checked_rows") == 24 and comparison.get("checked_cells") == 96
            and comparison.get("mismatches") == [] and comparison.get("status") == "pass"
            and comparison.get("ok") is True, "观察器的显示比较记录不完整或与96格范围不一致。")
    cleared = ad.get("cleared_before_new_connection")
    require(isinstance(cleared, dict) and cleared.get("status") == "pass" and cleared.get("ok") is True
            and cleared.get("mismatches") == [] and integer(cleared.get("checked_cells"), "clear.checked_cells") >= 96,
            "前一连接清除检查未明确覆盖实际数值格。")
    require(ad.get("status") == "pass" and ad.get("passed") is True, "专项自身未给出AD通过；不能用独立解析掩盖专项失败。")
    require(ad.get("physical_ad_validity_verified") is False, "专项不能把原文字显示一致解释为物理AD有效。")
    result = {
        "schema": SCHEMA,
        "status": "pass" if not mismatches else "fail", "ad_evidence_consistent": not mismatches,
        "n_function_verified": False, "in_flight_n_close_verified": False,
        "task_business_verified": False, "in_flight_task_single_outer_close_verified": False,
        "new_construction_proven": False, "same_main_memory_bool_read": False,
        "emergency_current_view_review": emergency,
        "raw_emergency_default_container_observation": raw_emergency,
        "binding": bound, "old_generation": str(old), "new_generation": str(new),
        "raw_rx_window": {"clear_pass_line": clear_line, "reopen_pass_line": reopen_line,
                          "ad_comparison_pass_line": ad_line, "rx_log_lines": rx_lines,
                          "valid_frames": stream.frames, "complete_messages": stream.messages,
                          "fragmented_messages": stream.fragmented, "qq_messages": len(qq)},
        "latest_qq_independently_decoded": latest,
        "actual_table_rows_1_to_24": [keyed[str(channel)] for channel in range(1, 25) if str(channel) in keyed],
        "extra_table_keys_not_required": sorted(set(keyed) - {str(channel) for channel in range(1, 25)}),
        "checked_value_cells": 96, "software_checks_recorded": len(checks), "mismatches": mismatches,
        "evidence_sha256": {name: sha(case / name) for name in ("app.log", "identity.txt", "results/result.json", "shutdown_contract.json")},
        "numeric_generation_scope": "数值代次来自本批已冻结09检查器JSON并交叉核对；RX包本身没有socket代次字段，不能独立从包字节推导数值代次。",
        "raw_connection_boundary_scope": "仅使用实际日志中清除检查之后、AD比较检查之前的RX，绝不回退到此前连接或其他批次读数。",
        "clear_state_limit": "清除前96格没有另存逐格快照；本项只交叉核对冻结检查器的清除记录与实际日志边界。",
        "scope": {"physical_ad_validity_verified": False, "administrator_credentials_verified": False,
                  "physical_screen_or_input_verified": False, "mechanical_or_detection_functions_verified": False,
                  "full_tx_scope_and_shutdown_contract_checked_here": False,
                  "full_tx_scope_and_shutdown_owner": "existing_summarize_run.py",
                  "n_function_verified": False, "in_flight_n_close_verified": False,
                  "task_business_verified": False, "in_flight_task_single_outer_close_verified": False,
                  "new_construction_proven": False, "same_main_memory_bool_read": False,
                  "product_executed_by_this_script": False, "network_accessed_by_this_script": False},
    }
    result["optional_software_checks"] = optional_checks
    result["current_alarm_independent_review"] = review_current_alarm(case, lines, probe.get("current_alarm_observation"))
    if result["current_alarm_independent_review"]["status"] == "fail":
        result["status"] = "fail"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case_directory", type=Path)
    parser.add_argument("binding_directory", type=Path)
    parser.add_argument("--output", type=Path, help="可选，新报告绝对路径；不覆盖已有文件")
    args = parser.parse_args()
    try:
        require(args.case_directory.is_absolute() and args.binding_directory.is_absolute(), "两个输入目录都须为明确绝对路径。")
        case, binding = args.case_directory.resolve(strict=True), args.binding_directory.resolve(strict=True)
        require(case.is_dir() and binding.is_dir(), "输入必须是已取回的本次case和绑定目录。")
        if args.output:
            require(args.output.is_absolute() and not args.output.exists() and not args.output.is_symlink(), "报告须为明确、尚不存在的绝对路径。")
            require(args.output.parent.is_dir(), "报告父目录尚不存在，本工具不猜测或自动创建。")
        report = audit(case, binding)
    except (AuditError, OSError, ValueError, TypeError, KeyError, AttributeError, IndexError, re.error) as exc:
        report = {"schema": SCHEMA, "status": "fail",
                  "ad_evidence_consistent": False, "error": str(exc),
                  "product_executed_by_this_script": False, "network_accessed_by_this_script": False,
                  "full_tx_scope_and_shutdown_checked_here": False,
                  "n_function_verified": False, "in_flight_n_close_verified": False,
                  "task_business_verified": False, "in_flight_task_single_outer_close_verified": False,
                  "new_construction_proven": False, "same_main_memory_bool_read": False}
    report["reviewed_at_utc"] = datetime.now(timezone.utc).isoformat()
    rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        try:
            require(args.output.is_absolute() and not args.output.exists() and not args.output.is_symlink(), "拒绝覆盖已有报告或非绝对报告路径。")
            with args.output.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(rendered)
        except (AuditError, OSError) as exc:
            print(json.dumps({"status": "fail", "error": str(exc), "report_not_written": True}, ensure_ascii=False))
            return 2
    print(rendered, end="")
    return 0 if report.get("ad_evidence_consistent") is True and report.get("status") != "fail" else 1


if __name__ == "__main__":
    sys.exit(main())
