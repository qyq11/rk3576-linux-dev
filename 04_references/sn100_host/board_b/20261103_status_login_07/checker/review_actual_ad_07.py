#!/usr/bin/env python3
"""独立只读核对最终07实际联机结果中的AD生效证据。

用法：review_actual_ad_07.py ABSOLUTE_CASE_DIRECTORY ABSOLUTE_BINDING_DIRECTORY
                         [--output ABSOLUTE_NEW_REPORT_JSON]
只读本次取回的app.log、results/result.json及绑定记录，不运行产品、不联网。
默认只输出JSON；只有明确给--output时才新建报告，绝不覆盖输入或已有报告。
发布身份和程序校验值从本批07准备、链接及实际构建记录交叉读取，不写入06或既往实际值。
完整TX范围和18项退出合同由既有summarize_run.py验收，本工具不代替它。
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys


SCHEMA = "sn100-actual-ad-independent-review-07-v1"
CHECKER_NAME = "status_login_review_07"
N_READY_MARKER = "REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false"
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


def check_binding(case, binding):
    prepared = read_json(binding / "prepared_inputs.json")
    link = read_json(binding / "link_provenance.json")
    contract = read_json(binding / "shutdown_contract.json")
    execution = read_json(binding / "execution_07_result.json")
    before = read_json(binding / "execution_07_inputs_before.json")
    after = read_json(binding / "execution_07_inputs_after.json")
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
        require(value.get("schema") == "sn100-review-07-v1" and value.get("review_generation") == "07",
                name + "不是07绑定记录，不能沿用其他批次结果。")
        require(value.get("release_id") == release and value.get("source_count") == source_count
                and value.get("source_archive_sha256") == source_archive,
                name + "与本批发布、源码数量或源码包身份不一致。")
    require(prepared.get("status") == "bound_to_final_07_archive", "准备输入尚未绑定最终07归档。")
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
    snapshot = read_json(binding / "source_snapshot_07.json")
    require(isinstance(snapshot, dict) and len(snapshot) == source_count
            and sha(binding / "source_snapshot_07.json") == prepared.get("frozen_snapshot_sha256")
            and link.get("frozen_snapshot_sha256") == prepared.get("frozen_snapshot_sha256"),
            "冻结源码快照身份或数量不一致。")
    contract_sha = prepared["contract_sha256"].get("shutdown_contract.json")
    require(sha(case / "shutdown_contract.json") == contract_sha, "场景和绑定目录的退出合同不属于同一冻结输入。")
    require(contract.get("schema") == "sn100-shutdown-contract-07-v1"
            and contract.get("release_id") == release and contract.get("main_sha256") == main_source,
            "退出合同身份不符。")
    markers = contract.get("required_markers")
    guard = contract.get("n_shutdown_guard")
    require(isinstance(markers, list) and len(markers) == 18 and len(set(markers)) == 18
            and N_READY_MARKER in markers and isinstance(guard, dict)
            and guard.get("required_ready_marker") == N_READY_MARKER
            and guard.get("n_function_verified") is False
            and guard.get("in_flight_n_close_verified") is False,
            "不是本批含18条真实标记且保留N验收边界的07关闭合同。")
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
    checker = digest(execution.get("checker_sha256"), "execution.checker_sha256")
    require(sha(binding / CHECKER_NAME) == checker, "当前检查程序不等于本批实际构建结果。")
    require(read_text(binding / "review_release_id", 1024).strip() == release,
            "本批检查程序发布身份文件不一致。")
    require(sha(binding / "execution_07_inputs_before.json") == sha(binding / "execution_07_inputs_after.json")
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
    identity = {}
    for line in read_text(case / "identity.txt", 1024 * 1024).splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            require(key not in identity, "identity.txt有重复键：" + key)
            identity[key] = value
    require(identity.get("MODE") == "auto", "本工具只审本次auto专项结果，不把人工窗口当专项。")
    require(identity.get("RELEASE_ID") == release and identity.get("APP_SHA256") == application
            and identity.get("PROBE_SHA256") == checker, "场景正式版本/检查器SHA不属于本批最终07。")
    return {"release_id": release, "application_sha256": application, "checker_sha256": checker,
            "checker_source_sha256": cpp, "observer_sha256": observer, "main_source_sha256": main_source,
            "main_object_sha256": objects[0]["sha256"], "source_archive_sha256": source_archive,
            "prepared_sha256": sha(binding / "prepared_inputs.json"),
            "link_sha256": sha(binding / "link_provenance.json"), "link_record_sha256": recorded_sha,
            "execution_sha256": sha(binding / "execution_07_result.json"),
            "inputs_before_and_after_sha256": sha(binding / "execution_07_inputs_before.json"),
            "contract_sha256": contract_sha, "required_shutdown_marker_count": len(markers),
            "source_count": source_count, "formal_object_count": object_count}


def find_pass(lines, name):
    pattern = re.compile(r"(?:^|\s)PASS\s+" + re.escape(name) + r"\s*$")
    found = [number for number, line in enumerate(lines, 1) if pattern.search(line)]
    require(len(found) == 1, "无法唯一定位本次检查边界：" + name)
    return found[0]


def audit(case, binding):
    bound = check_binding(case, binding)
    release = bound["release_id"]
    probe = read_json(case / "results/result.json")
    require(probe.get("release_id") == release and probe.get("board") == "board_b", "结果JSON不是本次07板B。")
    require(probe.get("completed") is True and type(probe.get("failed")) is int and probe["failed"] == 0,
            "检查器没有完整结束或已有失败。")
    checks = probe.get("checks")
    require(isinstance(checks, list) and len(checks) == 24
            and all(isinstance(item, dict) and item.get("passed") is True for item in checks),
            "检查器逐项结果缺失或含失败，不能只信顶层failed=0。")
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
        "numeric_generation_scope": "数值代次来自本批已冻结07检查器JSON并交叉核对；RX包本身没有socket代次字段，不能独立从包字节推导数值代次。",
        "raw_connection_boundary_scope": "仅使用实际日志中清除检查之后、AD比较检查之前的RX，绝不回退到此前连接或其他批次读数。",
        "clear_state_limit": "清除前96格没有另存逐格快照；本项只交叉核对冻结检查器的清除记录与实际日志边界。",
        "scope": {"physical_ad_validity_verified": False, "administrator_credentials_verified": False,
                  "physical_screen_or_input_verified": False, "mechanical_or_detection_functions_verified": False,
                  "full_tx_scope_and_18_shutdown_contract_checked_here": False,
                  "full_tx_scope_and_shutdown_owner": "existing_summarize_run.py",
                  "n_function_verified": False, "in_flight_n_close_verified": False,
                  "product_executed_by_this_script": False, "network_accessed_by_this_script": False},
    }
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
    except (AuditError, OSError, ValueError, TypeError, KeyError, AttributeError, IndexError) as exc:
        report = {"schema": SCHEMA, "status": "fail",
                  "ad_evidence_consistent": False, "error": str(exc),
                  "product_executed_by_this_script": False, "network_accessed_by_this_script": False,
                  "full_tx_scope_and_shutdown_checked_here": False,
                  "n_function_verified": False, "in_flight_n_close_verified": False}
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
    return 0 if report.get("ad_evidence_consistent") is True else 1


if __name__ == "__main__":
    sys.exit(main())
