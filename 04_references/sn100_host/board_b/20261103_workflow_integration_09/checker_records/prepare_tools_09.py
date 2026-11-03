"""仅生成09独立检查工具；不冻结源码、不编译、不运行、不联网。"""
from pathlib import Path
import ast
import hashlib
import json
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
OLD = ROOT / '04_references/sn100_host/board_b/20261103_status_login_08/checker'
PERMISSION = ROOT / '04_references/sn100_host/board_b/20261103_status_login_08_permission'
PROMOTED = ROOT / 'temp/sn100_host/workflow_night_20261102/integration_09_revision_03_review'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def change(text, old, new):
    require(text.count(old) == 1, '适配位置不唯一：' + old[:100])
    return text.replace(old, new, 1)


def new(name, value):
    if name.endswith('.py'):
        ast.parse(value)
    with (HERE / name).open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(value)


def generation(text):
    for a, b in [('_08', '_09'), ('-08-', '-09-'), ('"08"', '"09"'), ("'08'", "'09'"),
                 ('最终08', '最终09'), ('本轮08', '本轮09'), ('本批08', '本批09'),
                 ('08正式', '09正式'), ('08发布', '09发布'), ('08检查', '09检查'),
                 ('HERE.parents[4]', 'HERE.parents[3]'), ('../../../../..', '../../../..')]:
        text = text.replace(a, b)
    return text


root_inputs = json.loads((PROMOTED / 'root_actual_build_inputs.json').read_text(encoding='utf-8'))
for key, target in [('final_review', 'approved_final_merge_review.json'),
                    ('final_source_manifest', 'staged_source_manifest_09.json')]:
    source = ROOT / root_inputs[key]['path']
    require(sha(source) == root_inputs[key]['sha256'], '总控晋级来源改变：' + key)
    with (HERE / target).open('xb') as stream:
        stream.write(source.read_bytes())
with (HERE / 'root_promoted_inputs_09.json').open('xb') as stream:
    stream.write((PROMOTED / 'root_actual_build_inputs.json').read_bytes())
for name in ['ad_wire_observer.h', 'capture_application_exit.sh']:
    with (HERE / name).open('xb') as stream:
        stream.write((OLD / name).read_bytes())

names = ['bind_build_08.py', 'freeze_release_08.py', 'shutdown_contract_08.py', 'build_review_08.sh']
for name in names:
    text = generation((OLD / name).read_text(encoding='utf-8'))
    if name == 'bind_build_08.py':
        text = text.replace('integration_09_revision_02', 'integration_09_revision_05')
        start = text.index('TEMPLATE_NAMES = (')
        end = text.index('\nCONTRACT_NAMES = (', start)
        text = text[:start] + '''TEMPLATE_NAMES = (
    "freeze_release_09.py", "bind_build_09.py", "build_review_09.sh", "shutdown_contract_09.py",
    "status_login_review_09.cpp", "ad_wire_observer.h", "lis_connection_observer_09.h",
    "current_alarm_observer_09.h", "run_case_on_board.sh", "capture_application_exit.sh", "summarize_run.py",
    "task_shutdown_contract_09.py", "approved_final_merge_review.json", "staged_source_manifest_09.json",
    "root_promoted_inputs_09.json", "observation_scope_09.json", "execute_final_09_build.py",
)
''' + text[end:]
        start = text.index("    owner = read_json(HERE / 'official_object_binding.json')")
        end = text.index('    return objects, generated_hashes, mains[0]', start)
        text = text[:start] + text[end:]
        text = change(text, '    snapshot = validate_snapshot(read_json(snapshot_path))',
            '    snapshot = validate_snapshot(read_json(snapshot_path))\n'
            '    require(snapshot == read_json(HERE / "staged_source_manifest_09.json"), "正式09源码不等于总控晋级后像")')
        text = text.replace('task_shutdown_contract_adapter_candidate.py', 'task_shutdown_contract_09.py')
    elif name == 'freeze_release_08.py':
        text = text.replace('task_shutdown_contract_adapter_candidate', 'task_shutdown_contract_09')
        text = text.replace('62683e9babba255ae1f2bf58a4a69b5961a7a7bdcd5c59d1a36ef847a6527eac',
                            root_inputs['final_review']['sha256'])
    elif name == 'shutdown_contract_08.py':
        text = change(text, '    "PRINTER_SETTINGS_CLOSED", "LIS_SETTINGS_CLOSED",',
                      '    "PRINTER_SETTINGS_CLOSED", "LIS_SETTINGS_CLOSED", "LIS_CONNECTION_STOPPED",')
        text = change(text, '        "runtime_verified": False,',
            '        "conditional_markers": [{"marker": "ALARM_HISTORY_VIEW_CLOSED",\n'
            '            "when_probe_field_true": "current_alarm_observation.page_opened",\n'
            '            "source": "shell/alarm_shell.cpp", "source_sha256": sha(frozen / "shell/alarm_shell.cpp")}],\n'
            '        "lis_connection_shutdown": {"source_sha256": {name: sha(frozen / name) for name in (\n'
            '            "shell/main.cpp", "lis_transport/lis_connection_controller.cpp",\n'
            '            "lis_transport/lis_connection_controller.h", "lis_transport/lis_tcp_transport.cpp",\n'
            '            "lis_transport/lis_tcp_transport.h")},\n'
            '            "required_marker": "LIS_CONNECTION_STOPPED",\n'
            '            "unconfirmed_marker": "LIS_CONNECTION_SHUTDOWN_UNCONFIRMED",\n'
            '            "connection_or_business_verified": False},\n'
            '        "runtime_verified": False,')
    new(name.replace('_08', '_09'), text)

task = generation((OLD / 'task_shutdown_contract_adapter_candidate.py').read_text(encoding='utf-8'))
new('task_shutdown_contract_09.py', task)
runner = generation((PERMISSION / 'run_case_on_board.sh').read_text(encoding='utf-8'))
runner = change(runner, 'stat -c "%U:%G %a %n" "$run/ini_before.sha256" >"$run/manifest_access.txt"',
    'stat -c "%U:%G %a %n" "$run/ini_before.sha256" >"$run/manifest_access.txt"\n'
    '# 只读固定的仪器告警规则，供事后独立核原回包映射；不复制或改写配置正文。\n'
    'alarm_rules=$data/db/systemInfo/abnormal_Config.csv\n'
    '[[ -f $alarm_rules && ! -L $alarm_rules && $(readlink -f -- "$alarm_rules") == "$alarm_rules" ]] || die \'原告警规则来源缺失或重定向。\'\n'
    '[[ $(stat -c %s "$alarm_rules") -le 4194304 ]] || die \'原告警规则超过4MiB，只记录失败，不截断。\'\n'
    'sha256sum "$alarm_rules" >"$run/alarm_rules_before.sha256"')
runner = change(runner, 'sha256sum -c "$run/controller_ini_before.sha256" >"$run/controller_ini_check.txt" 2>&1 || printf \'CONTROLLER_INI_CHANGED=1\\n\' >>"$run/identity.txt"',
    'sha256sum -c "$run/controller_ini_before.sha256" >"$run/controller_ini_check.txt" 2>&1 || printf \'CONTROLLER_INI_CHANGED=1\\n\' >>"$run/identity.txt"\n'
    'sha256sum -c "$run/alarm_rules_before.sha256" >"$run/alarm_rules_check.txt" 2>&1 || printf \'ALARM_RULES_CHANGED=1\\n\' >>"$run/identity.txt"')
new('run_case_on_board.sh', runner)

summary = (OLD / 'summarize_run.py').read_text(encoding='utf-8')
summary = change(summary, "        allowed = {'Qj', 'Ur', 'Up', 'UP'} if mode == 'manual' and manual_sync_authorized else {'Qj'}",
    "        allowed = ({'Qj', 'Qk'} if mode == 'auto' else\n"
    "                   {'Qj', 'Ur', 'Up', 'UP'} if manual_sync_authorized else {'Qj'})")
summary = change(summary, 'tx = []', "expected_alarm_query = bytes.fromhex('0203000600000001516b3b0405')\ntx = []")
summary = change(summary, "if mode == 'auto' and frame != expected_query:",
                 "if mode == 'auto' and frame not in (expected_query, expected_alarm_query):")
summary = summary.replace('自动专项出现非Qj字节', '自动专项出现非Qj/Qk字节')
summary = change(summary, 'scope_ok = not errors and ini_ok', '''qk_count = commands.count('Qk')
if mode == 'auto' and qk_count > 1:
    errors.append({'error': '当前告警查询超过明确允许的一次Qk'})
begin = [i for i, line in enumerate(text.splitlines(), 1) if line.startswith('REVIEW_CURRENT_ALARM_QUERY_BEGIN')]
end = [i for i, line in enumerate(text.splitlines(), 1) if line.startswith('REVIEW_CURRENT_ALARM_QUERY_END')]
if mode == 'auto' and qk_count:
    if len(begin) != 1 or len(end) != 1 or begin[0] >= end[0]:
        errors.append({'error': '缺少一次Qk观察的真实区间'})
    else:
        for entry in tx:
            if bytes.fromhex(entry['hex']) == expected_alarm_query and not begin[0] < entry['line'] < end[0]:
                errors.append({'error': 'Qk不在独立明确查询区间内'})
alarm_observation = probe.get('current_alarm_observation', {}) if isinstance(probe, dict) else {}
for item in contract.get('conditional_markers', []):
    if item.get('when_probe_field_true') == 'current_alarm_observation.page_opened' and alarm_observation.get('page_opened') is True:
        marker = item['marker']
        markers[marker] = bool(re.search(r'^' + re.escape(marker) + r'\\s*$', text, re.M))
scope_ok = not errors and ini_ok''')
summary = change(summary, "(run / 'parsed_summary.json').write_text", '''result['current_alarm_observation'] = alarm_observation
result['lis_connection_observation'] = probe.get('lis_connection_observation', {}) if isinstance(probe, dict) else {}
result['allowed_device_query_scope'] = {'Qj': '原基础观察段及主控周期查询', 'Qk': '仅后续显式一次当前告警查询', 'qk_complete_messages': qk_count}
result['new_construction_proven'] = False
result['same_main_memory_bool_read'] = False
(run / 'parsed_summary.json').write_text''')
new('summarize_run.py', summary)
print('09_TOOLS_DRAFTED=1; SOURCE_FROZEN=0; COMPILED=0; PRODUCT_EXECUTED=0; NETWORK_ACCESSED=0')
