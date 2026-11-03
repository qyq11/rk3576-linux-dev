#!/usr/bin/env python3
"""从最终07入口和实际runner参数生成退出时必须核对的日志。

这不是运行验收。只认已审过的完整业务启用条件；新增或看不懂的关闭
分支会停止生成，不能悄悄少查一个工作线程。输出绑定最终main的SHA。
"""

import argparse
import hashlib
import json
import re
import shlex
from pathlib import Path

from bind_build_07 import new_json, new_text, regular, require, sha, validate_release_id


REQUIRED_OPTIONS = {
    "--offline", "--accounts", "--history", "--catalog", "--samples", "--instrument",
    "--password-login", "--printer-settings", "--lis-settings", "--quality-control",
    "--calibration", "--alarm-history", "--data-dir", "--viewport", "--original-db-root",
    "--start-project-page", "--instrument-control", "--instrument-config", "--theme-config",
}
VALUE_OPTIONS = {"--data-dir", "--viewport", "--original-db-root", "--instrument-config", "--theme-config"}
DISABLED_OPTIONS = {"--status-replay", "--status-loopback", "--lis-raw-inbox"}
PLAIN_MARKERS = {
    "ALARM_HISTORY_SOURCE_CLOSED", "ACCOUNT_STORE_CLOSED", "HISTORY_STORE_CLOSED",
    "QUALITY_CONTROL_STORE_CLOSED", "SAMPLE_STORE_CLOSED", "CALIBRATION_STORE_CLOSED",
    "PROJECT_STORE_CLOSED", "REAGENT_STORE_CLOSED", "MAINTENANCE_STORE_CLOSED",
    "PRINTER_SETTINGS_CLOSED", "LIS_SETTINGS_CLOSED",
}
BOOL_EXPRESSIONS = {
    "ORIGINAL_CHANNEL_WORKER_STOPPED": "!originalChannelThread.isRunning()",
    "ALARM_HISTORY_WORKER_STOPPED": "!alarmHistoryThread.isRunning()",
    "ORIGINAL_ALARM_WORKER_STOPPED": "!originalAlarmThread.isRunning()",
    "THREAD_STOPPED": "!databaseThread.isRunning()",
    "LIS_RAW_INBOX_STOPPED": "closed",
    "STATUS_REPLAY_STOPPED": "!statusReplayTimer.isActive()",
    "STATUS_LOOPBACK_STOPPED": "stopped",
}
EXCLUDED = {
    "STORE_CLOSED": "实际传入--accounts，三元表达式使用HOSPITAL_STORE_CLOSED。",
    "THREAD_STOPPED": "实际启用accounts/history，使用THREADS_STOPPED分支。",
    "LIS_RAW_INBOX_STOPPED": "实际未传--lis-raw-inbox。",
    "STATUS_REPLAY_STOPPED": "实际未传--status-replay。",
    "STATUS_LOOPBACK_STOPPED": "实际未传--status-loopback。",
}
OUTPUT_NAMES = (
    "shutdown_contract.json", "required_shutdown_markers.txt",
    "forbidden_shutdown_markers.txt", "shutdown_contract_release_id",
)
KNOWN_THREADS = {
    "databaseThread", "accountThread", "historyThread", "projectThread", "reagentThread",
    "sampleThread", "maintenanceThread", "originalChannelThread", "printerThread", "lisThread",
    "alarmHistoryThread", "qualityThread", "calibrationThread", "originalAlarmThread",
}


def without_comments(text):
    """保留字串、位置和行号，只把注释内容换成空格。"""
    pattern = r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*|/\*[\s\S]*?\*/'
    return re.sub(pattern, lambda m: m[0] if m[1] else re.sub(r"[^\n]", " ", m[0]), text)


def log_statements(text):
    clean = without_comments(text)
    for match in re.finditer(r"\bq(?:Info|Warning|Critical|Fatal|Debug)\s*\(\s*\)", clean):
        quoted = None
        escaped = False
        end = match.end()
        while end < len(clean):
            char = clean[end]
            if quoted:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == quoted:
                    quoted = None
            elif char in {'"', "'"}:
                quoted = char
            elif char == ";":
                break
            end += 1
        require(end < len(clean), "日志语句未结束，需审查最终main")
        statement = clean[match.start():end + 1]
        strings = []
        for literal in re.finditer(r'"(?:\\.|[^"\\])*"', statement):
            try:
                strings.append(json.loads(literal[0]))
            except json.JSONDecodeError as exc:
                raise RuntimeError("日志使用了尚未支持的C++字串，需审查") from exc
        yield {
            "line": clean.count("\n", 0, match.start()) + 1,
            "level": re.search(r"q(\w+)", match[0])[1],
            "statement": statement, "literals": strings,
        }


def runner_options(runner):
    # 原文里--offline独占续行。先定位，再只折叠参数片段，不能把它并到timeout行后再找行首。
    text = regular(runner).read_text(encoding="utf-8")
    matches = list(re.finditer(r'(?m)^[ \t]*--offline\b(.*?)>[ \t]*"\$run/app\.log"', text, re.S))
    require(len(matches) == 1, "无法唯一确定runner实际传给应用的参数，需审查")
    arguments = ("--offline" + matches[0][1]).replace("\\\n", " ")
    tokens = shlex.split(arguments)
    flags = {}
    i = 0
    while i < len(tokens):
        flag = tokens[i]
        require(flag.startswith("--") and flag not in flags, "runner参数结构变化或重复，需审查")
        if flag in VALUE_OPTIONS:
            require(i + 1 < len(tokens) and not tokens[i + 1].startswith("--"), "runner值参数缺失：" + flag)
            flags[flag] = tokens[i + 1]
            i += 2
        else:
            flags[flag] = True
            i += 1
    require(set(flags) == REQUIRED_OPTIONS, "runner启用范围变化，不能沿用本轮关闭规则")
    require(not (set(flags) & DISABLED_OPTIONS), "本轮不允许启用回放、回环或原报文页")
    require(flags["--data-dir"] == flags["--original-db-root"] == "$data", "原资料根用法变化，需审查")
    require(flags["--instrument-config"] == "$configuration" and flags["--theme-config"] == "$theme",
            "实际配置来源变化，需审查")
    return flags


def bool_expression(statement, marker):
    # 除已单独处理的三元分支，其余日志必须只有一个生命周期字串。
    match = re.fullmatch(
        r'\s*qInfo\s*\(\s*\)(?:\s*\.\s*noquote\s*\(\s*\))?\s*<<\s*"'
        + re.escape(marker) + r'"\s*<<\s*([\s\S]+);\s*', statement)
    require(match is not None, "布尔停止日志格式变化，需审查：" + marker)
    return re.sub(r"\s+", "", match[1])


def thread_inventory(clean):
    # 多个变量可同一行声明；指针、new QThread、自动创建等新形式不能默默跳过。
    body = re.sub(r"(?m)^\s*#.*$", "", clean)
    declarations = list(re.finditer(r"\bQThread\s+([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*;", body))
    require(len(re.findall(r"\bQThread\b(?!\s*::)", body)) == len(declarations),
            "发现尚未支持的线程创建/声明方式，必须审查退出路径")
    names = [name.strip() for declaration in declarations for name in declaration[1].split(",")]
    require(len(names) == len(set(names)) and set(names) == KNOWN_THREADS,
            "main中的工作线程新增、删除或改名，必须重核所有停止条件")
    return sorted(names)


def n_shutdown_evidence(frozen, main_clean):
    """从最终源码核N关门日志和原保护；不把它叫作线程停止或N业务通过。"""
    paths = ('shell/reagent_level_shell.cpp', 'shell/frmmain.cpp',
             'reagents/original_reagent_level_worker.cpp', 'reagents/original_reagent_level_worker.h')
    source_hashes = {name: sha(regular(frozen / name)) for name in paths}
    level_text = regular(frozen / paths[0]).read_text(encoding='utf-8')
    level_clean = without_comments(level_text)
    close_text = regular(frozen / paths[1]).read_text(encoding='utf-8')
    close_clean = without_comments(close_text)
    functions = list(re.finditer(
        r'\bbool\s+frmMain::requestReagentLevelShutdown\s*\(\s*\)\s*\{(.*?)^\}',
        level_clean, re.S | re.M))
    require(len(functions) == 1, '最终N关闭保护函数缺失或形式变化，必须重审')
    function = functions[0][0]
    logs = [entry for entry in log_statements(function)
            if any('REAGENT_LEVEL_SHUTDOWN_READY' in value for value in entry['literals'])]
    require(len(logs) == 1 and logs[0]['level'] == 'Info',
            '最终N源码没有唯一真实关门就绪日志；不能凭空生成停止条件')
    log = logs[0]
    compact_log = re.sub(r'\s+', '', log['statement'])
    prefix = r'qInfo\(\)(?:\.noquote\(\))?<<'
    marker = r'(?:"REAGENT_LEVEL_SHUTDOWN_READYactive"|"REAGENT_LEVEL_SHUTDOWN_READY"<<"active")'
    active = r'(?:bool\(reagentLevelActive_\)|static_cast<bool>\(reagentLevelActive_\))'
    busy = r'(?:\(reagentLevelWorker_&&reagentLevelWorker_->isBusy\(\)\)|bool\(reagentLevelWorker_&&reagentLevelWorker_->isBusy\(\)\))'
    require(re.fullmatch(prefix + marker + r'<<' + active + r'<<"worker_busy"<<' + busy + ';', compact_log),
            'N就绪日志没有直接打印真实active/worker busy字段，必须核最终代码')
    # 根本轮只允许在成功返回前补日志。其他保护和失败分支变化须重新审查。
    original_guard = '''bool frmMain::requestReagentLevelShutdown()
{
    reagentLevelClosing_=true;
    invalidateReagentLevels();
    if (reagentLevelActive_ || (reagentLevelWorker_ && reagentLevelWorker_->isBusy()))
    {
        reportReagentLevel(tr("正在撤销N观测并等待在途保存结束；不终止数据库线程或假定已回滚。"));
        return false;
    }
    return true;
}'''
    require(re.sub(r'\s+', '', function.replace(log['statement'], '', 1))
            == re.sub(r'\s+', '', original_guard), 'N关门保护不只是补可见日志，必须重审实际关闭规则')
    compact_close = re.sub(r'\s+', '', close_clean)
    require('if(!requestReagentLevelShutdown()){event->ignore();return;}' in compact_close,
            '真实窗口关闭未经过N保护，必须重审')
    require('receiver->moveToThread(reagents_->taskStore()->thread());' in re.sub(r'\s+', '', level_clean)
            and 'connect(receiver->thread(),&QThread::finished,receiver,&QObject::deleteLater);'
            in re.sub(r'\s+', '', level_clean), 'N后台不再复用原ReagentStore线程，不能沿用当前生命周期方案')
    require('if(reagentLevelClosing_&&!reagentLevelWorker_->isBusy())'
            'QTimer::singleShot(0,this,[this](){window()->close();});' in re.sub(r'\s+', '', level_clean),
            '最终N收尾仍未回到真实外层窗口关闭；不能使用第四窄片之前的代码')
    compact_main = re.sub(r'\s+', '', main_clean)
    require('reagentThread.wait();' in compact_main and '!reagentThread.isRunning()' in compact_main,
            '原ReagentStore线程没有最终wait/汇总停止证明')
    return {
        'source_sha256': source_hashes,
        'required_ready_marker': 'REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false',
        'log_statement_sha256': hashlib.sha256(log['statement'].encode('utf-8')).hexdigest(),
        'source': paths[0], 'shutdown_function': 'frmMain::requestReagentLevelShutdown',
        'worker_thread': 'existing ReagentStore thread; no new QThread',
        'required_existing_markers': ['REAGENT_STORE_CLOSED', 'THREADS_STOPPED true'],
        'proof_scope': '只证明真实关闭保护到达成功分支时active和worker pending均空；不是N线程停止或提交中关闭验收。',
        'n_function_verified': False, 'in_flight_n_close_verified': False,
    }


def make_contract(frozen, release_id, runner):
    validate_release_id(release_id)
    frozen = Path(frozen)
    require(frozen.is_absolute() and frozen.name == "sn100_host" and frozen.parent.name == "frozen_07",
            "只接受最终归档生成的frozen_07/sn100_host")
    main = regular(frozen / "shell/main.cpp")
    text = main.read_text(encoding="utf-8")
    flags = runner_options(runner)
    # 本轮按实际参数和入口变量一起核对；变量改名或变更选项语义须重新审查。
    option_bindings = {
        "withAccounts": "accounts", "withHistory": "history", "withCatalog": "catalog",
        "withOriginalProjects": "original-db-root", "withInstrument": "instrument",
        "withDeviceControl": "instrument-control", "withLisRawInbox": "lis-raw-inbox",
        "withStatusReplay": "status-replay", "withStatusLoopback": "status-loopback",
        "withSamples": "samples", "withPrinterSettings": "printer-settings",
        "withLisSettings": "lis-settings", "withQualityControl": "quality-control",
        "withCalibration": "calibration", "withAlarmHistory": "alarm-history",
    }
    clean = without_comments(text)
    for variable, option in option_bindings.items():
        expression = (r"\b(?:const\s+)?bool\s+" + variable
                      + r'\s*=\s*parser\.isSet\s*\(\s*QStringLiteral\s*\(\s*"'
                      + re.escape(option) + r'"\s*\)\s*\)\s*;')
        require(re.search(expression, clean), "入口选项条件变化，需审查：" + variable)
    threads = thread_inventory(clean)
    n_evidence = n_shutdown_evidence(frozen, clean)
    logs = list(log_statements(text))
    ready = [entry for entry in logs if entry["level"] == "Info" and "SHELL_READY" in entry["literals"]]
    require(len(ready) == 1 and re.sub(r"\s+", "", ready[0]["statement"]) == 'qInfo()<<"SHELL_READY";',
            "启动就绪日志新增、缺失或变化，需审查")
    found = {}
    for entry in logs:
        if entry["level"] != "Info":
            continue
        markers = []
        for literal in entry["literals"]:
            markers.extend(re.findall(r"\b[A-Z][A-Z0-9_]*(?:_CLOSED|_STOPPED)\b", literal))
        for marker in markers:
            require(marker not in found, "生命周期标记出现多个来源，需审查：" + marker)
            found[marker] = entry
    all_literals = [json.loads(match[0]) for match in re.finditer(r'"(?:\\.|[^"\\])*"', clean)]
    all_lifecycle = {marker for literal in all_literals
                     for marker in re.findall(r"\b[A-Z][A-Z0-9_]*(?:_CLOSED|_STOPPED)\b", literal)}
    require(all_lifecycle == set(found), "发现qInfo以外或动态组合的关闭标记，必须审查日志用途")
    require("CHANNEL_STORE_CLOSED" not in found and "ORIGINAL_CHANNEL_STORE_CLOSED" not in found,
            "通道库新增了关闭日志，先审查实际发出条件，不能凭名称加入")
    expected_known = PLAIN_MARKERS | set(BOOL_EXPRESSIONS) | {"HOSPITAL_STORE_CLOSED", "STORE_CLOSED", "THREADS_STOPPED"}
    unknown = set(found) - expected_known
    require(not unknown, "发现未审过的关闭/停止分支，必须先审查：" + ",".join(sorted(unknown)))
    require(set(found) == expected_known, "原有生命周期日志缺失，需审查最终退出链")
    required = ["SHELL_READY"]
    evidence = [{"marker": "SHELL_READY", "line": ready[0]["line"], "requires_true": False,
                 "required": True, "purpose": "证明已到正式启动就绪点；单独不证明正常退出。"}]
    aggregate_threads = []
    for marker, entry in sorted(found.items(), key=lambda pair: (pair[1]["line"], pair[0])):
        statement = entry["statement"]
        compact = re.sub(r"\s+", "", statement)
        is_bool = marker in BOOL_EXPRESSIONS or marker == "THREADS_STOPPED"
        if marker in {"HOSPITAL_STORE_CLOSED", "STORE_CLOSED"}:
            require(compact == 'qInfo()<<(withAccounts?"HOSPITAL_STORE_CLOSED":"STORE_CLOSED");',
                    "医院关闭替代分支变化，需审查")
        elif marker in PLAIN_MARKERS:
            require(compact in {'qInfo()<<"' + marker + '";', 'qInfo().noquote()<<"' + marker + '";'},
                    "关闭日志增加动态字段，需审查：" + marker)
        else:
            expression = bool_expression(statement, marker)
            if marker == "THREADS_STOPPED":
                remaining = re.sub(r"![A-Za-z_]\w*\.isRunning\(\)", "", expression)
                require(re.fullmatch(r"[()&]*", remaining) is not None and "!databaseThread.isRunning()" in expression,
                        "汇总停止表达式变化，不能确认true的含义")
                aggregate_threads = re.findall(r"!([A-Za-z_]\w*)\.isRunning\(\)", expression)
                actual = set(aggregate_threads)
                all_threads = KNOWN_THREADS | {"statusLoopback"}
                require(len(actual) == len(aggregate_threads)
                        and actual in (all_threads, all_threads - {"originalChannelThread"}),
                        "汇总停止表达式遗漏/新增线程，必须审查；通道专属结果始终另查")
            else:
                require(expression == BOOL_EXPRESSIONS[marker], "停止布尔表达式变化，需审查：" + marker)
        evidence.append({
            "marker": marker, "line": entry["line"], "requires_true": is_bool,
            "required": marker not in EXCLUDED, "excluded_reason": EXCLUDED.get(marker),
            "statement_sha256": hashlib.sha256(statement.encode("utf-8")).hexdigest(),
        })
        if marker not in EXCLUDED:
            required.append(marker + (" true" if is_bool else ""))
    require("ORIGINAL_CHANNEL_WORKER_STOPPED true" in required, "缺少通道写库线程的独立停止证明")
    # 必须拒绝本次日志出现过false，不能用稍后true覆盖前面的失败。
    patterns = [r"\b" + re.escape(m[:-5]) + r"\s+false\b" for m in required if m.endswith(" true")]
    unconfirmed = sorted({token for literal in all_literals
                          for token in re.findall(r"\b[A-Z][A-Z0-9_]*SHUTDOWN_UNCONFIRMED\b", literal)})
    require({"ORIGINAL_CHANNEL_SHUTDOWN_UNCONFIRMED", "ORIGINAL_ALARM_SHUTDOWN_UNCONFIRMED"}.issubset(unconfirmed),
            "入口停止未确认日志变化，需审查")
    patterns.extend(r"\b" + re.escape(marker) + r"\b" for marker in unconfirmed)
    require(any("ORIGINAL_CHANNEL_ERROR" in entry["literals"] for entry in logs), "通道错误日志变化，需审查")
    patterns.append(r"\bORIGINAL_CHANNEL_ERROR\b")
    # 只读直接置失败标志的那条if；后面的成功/取消文案也比较outcome，不能混入。
    failure_conditions = list(re.finditer(
        r"\bif\s*\(([^;{}]*?)\)\s*originalChannelFailure\s*=\s*true\s*;", clean))
    require(len(failure_conditions) == 1, "不能唯一确定直接设置通道失败的if条件，需审查")
    failure_condition = failure_conditions[0][1]
    comparison = r'\boutcome\s*==\s*QStringLiteral\s*\(\s*"([A-Za-z0-9_]+)"\s*\)'
    outcomes = sorted(set(re.findall(comparison, failure_condition)))
    condition_shape = re.sub(r"\s+", "", re.sub(comparison, "OUTCOME", failure_condition))
    require(re.fullmatch(r"OUTCOME(?:\|\|OUTCOME)*", condition_shape), "通道失败条件不再是结果枚举的或判断，需审查")
    require(set(outcomes) == {"Failed", "CommittedReloadFailed", "Unconfirmed"}, "通道失败结果分支变化，需审查")
    require(any(any(s.startswith("ORIGINAL_CHANNEL_RESULT ") for s in entry["literals"]) for entry in logs),
            "通道结果日志格式变化，需审查")
    patterns.append(r"\bORIGINAL_CHANNEL_RESULT\b[^\r\n]*\boutcome=\s*(?:" + "|".join(outcomes) + r")\b")
    patterns.append(r"\bREVIEW_WINDOW_CLOSE_NOT_CONFIRMED\b")
    required.append(n_evidence['required_ready_marker'])
    patterns.extend((r'^REAGENT_LEVEL_SHUTDOWN_READY\b[^\r\n]*\bactive\s+true\b',
                     r'^REAGENT_LEVEL_SHUTDOWN_READY\b[^\r\n]*\bworker_busy\s+true\b'))
    for pattern in patterns:
        re.compile(pattern)
    return {
        "schema": "sn100-shutdown-contract-07-v1", "release_id": release_id,
        "main_sha256": sha(main), "runner_sha256": sha(runner), "runner_options": flags,
        "source": "frozen_07/sn100_host/shell/main.cpp", "required_markers": required,
        "failure_patterns": patterns, "failure_patterns_are_regex": True,
        "marker_evidence": evidence, "unconfirmed_markers": unconfirmed,
        "main_qthread_inventory": threads, "aggregate_is_running_inventory": aggregate_threads,
        "aggregate_includes_channel_thread": "originalChannelThread" in aggregate_threads,
        "channel_failed_outcomes": outcomes,
        "n_shutdown_guard": n_evidence,
        "channel_failure_condition_line": clean.count("\n", 0, failure_conditions[0].start()) + 1,
        "notes": [
            "必须同时核对检查器结果、应用真实退出码、timeout封装码及正常窗口关闭保护。",
            "ALARM_HISTORY_SOURCE_CLOSED也可能在运行中出现，单独出现不能证明最终退出。",
            "THREADS_STOPPED不能替代ORIGINAL_CHANNEL_WORKER_STOPPED true。",
            "未声明通道库关闭日志；通道线程停止和未确认结果分别核对。",
        ],
        "runtime_verified": False,
    }


def write_contract(contract, output):
    output = Path(output)
    require(output.is_dir(), "关闭合同输出目录不存在")
    for name in OUTPUT_NAMES:
        require(not (output / name).exists() and not (output / name).is_symlink(), "关闭证据已存在，不能覆盖：" + name)
    new_json(output / OUTPUT_NAMES[0], contract)
    new_text(output / OUTPUT_NAMES[1], "\n".join(contract["required_markers"]) + "\n")
    new_text(output / OUTPUT_NAMES[2], "\n".join(contract["failure_patterns"]) + "\n")
    new_text(output / OUTPUT_NAMES[3], contract["release_id"] + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frozen_root", type=Path)
    parser.add_argument("release_id")
    parser.add_argument("output_directory", type=Path)
    parser.add_argument("--runner", type=Path, default=Path(__file__).resolve().parent / "run_case_on_board.sh")
    args = parser.parse_args()
    contract = make_contract(args.frozen_root, args.release_id, args.runner)
    write_contract(contract, args.output_directory)
    print("SHUTDOWN_CONTRACT_BOUND_TO_07=" + contract["release_id"])


if __name__ == "__main__":
    main()
