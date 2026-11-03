"""在电脑只解析板端日志；不运行产品，不连接仪器。"""
import argparse
import csv
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
args = parser.parse_args()
run = args.directory.resolve()
text = (run / 'app.log').read_text(encoding='utf-8', errors='replace')
text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text)
identity = dict(line.split('=', 1) for line in (run / 'identity.txt').read_text().splitlines() if '=' in line)
mode = identity['MODE']
code = int((run / 'wrapper_exit.txt').read_text().strip())
actual_exit_path = run / 'application_exit.txt'
actual_exit = int(actual_exit_path.read_text().strip()) if actual_exit_path.exists() else None
# 与05 frame_codec的HostToDevice编码逐字段对应：0203、长度、分片号、片数、消息、异或、0405。
packet = bytes.fromhex('00000001') + b'Qj'
checksum = 0
for value in packet:
    checksum ^= value
expected_query = bytes.fromhex('0203') + len(packet).to_bytes(2, 'big') + packet + bytes([checksum]) + bytes.fromhex('0405')
tx = []
errors = []
commands = []
assembly = bytearray()
expected_total = 0
next_index = 0
manual_sync_authorized = identity.get('ORIGINAL_LOGIN_SYNC_AUTHORIZED') == '1' and (run / 'authority_note.txt').exists()
if mode not in {'auto', 'manual'}:
    errors.append({'error': '未知场景类型'})
if mode == 'manual' and not manual_sync_authorized:
    errors.append({'error': '未登记原登录自动同步授权，人工项仍待验'})
for number, line in enumerate(text.splitlines(), 1):
    if 'DEVICE_WIRE TX' not in line:
        continue
    match = re.search(r'DEVICE_WIRE TX queued\s+([0-9a-fA-F ]+)\s*$', line)
    if not match:
        errors.append({'line': number, 'error': 'TX日志格式无法完整解析'})
        continue
    try:
        frame = bytes.fromhex(match.group(1))
    except ValueError:
        errors.append({'line': number, 'error': 'TX十六进制不完整'})
        continue
    tx.append({'line': number, 'hex': frame.hex(' ')})
    if mode == 'auto' and frame != expected_query:
        errors.append({'line': number, 'error': '自动专项出现非Qj字节'})
    # 每个wireQueued应是一帧。未知/不完整格式不得当作已排除危险命令。
    if len(frame) < 12 or frame[:2] != b'\x02\x03' or frame[-2:] != b'\x04\x05':
        errors.append({'line': number, 'error': '帧头尾不符'})
        continue
    length = int.from_bytes(frame[2:4], 'big')
    if len(frame) != length + 7:
        errors.append({'line': number, 'error': '帧长度不符'})
        continue
    packet = frame[4:4+length]
    checksum = 0
    for value in packet:
        checksum ^= value
    if checksum != frame[-3]:
        errors.append({'line': number, 'error': '帧校验不符'})
        continue
    index = int.from_bytes(packet[:2], 'big')
    total = int.from_bytes(packet[2:4], 'big')
    if not (1 <= total <= 64 and 0 <= index < total and 1 <= len(packet[4:]) <= 2000):
        errors.append({'line': number, 'error': '分片元数据或长度不符'})
        continue
    if index == 0:
        if expected_total:
            errors.append({'line': number, 'error': '前一消息缺片，不能当作通信范围已核完'})
        expected_total = total
        next_index = 0
        assembly = bytearray()
    if total != expected_total or index != next_index:
        errors.append({'line': number, 'error': '孤立、重复或乱序发送分片'})
        expected_total = 0
        assembly = bytearray()
        continue
    assembly.extend(packet[4:])
    next_index += 1
    if next_index == expected_total:
        command = bytes(assembly[:2]).decode('ascii', errors='replace')
        commands.append(command)
        allowed = {'Qj', 'Ur', 'Up', 'UP'} if mode == 'manual' and manual_sync_authorized else {'Qj'}
        if command not in allowed:
            errors.append({'line': number, 'error': '人工窗口出现预期外命令：' + command})
        expected_total = 0
        assembly = bytearray()
if expected_total:
    errors.append({'error': '进程日志结束时发送消息仍有缺片'})
if not tx:
    errors.append({'error': '没有完整TX证据，不能判定通信范围通过'})
probe_path = run / 'results/result.json'
probe = json.loads(probe_path.read_text()) if probe_path.exists() else None
check_exit_path = run / 'results/check_exit.txt'
check_exit = int(check_exit_path.read_text().strip()) if check_exit_path.exists() else None
contract = json.loads((run / 'shutdown_contract.json').read_text(encoding='utf-8'))
if contract.get('release_id') != identity['RELEASE_ID']:
    errors.append({'error': '关闭检查条件与本轮发布不一致'})
required = contract['required_markers']
core_markers = {'SHELL_READY', 'THREADS_STOPPED true', 'ORIGINAL_ALARM_WORKER_STOPPED true', 'ORIGINAL_CHANNEL_WORKER_STOPPED true',
                'REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false'}
if not isinstance(required, list) or not required or not core_markers.issubset(set(required)):
    errors.append({'error': '关闭条件缺少本模式必需的就绪/总线程/告警/通道SQL后台标记'})
markers = {name: bool(re.search(r'^' + re.escape(name) + r'\s*$', text, re.M)) for name in required}
failures = [pattern for pattern in contract['failure_patterns'] if re.search(pattern, text, re.M)]
alarm_shutdown_unconfirmed = 'ORIGINAL_ALARM_SHUTDOWN_UNCONFIRMED' in text
window_close_unconfirmed = 'REVIEW_WINDOW_CLOSE_NOT_CONFIRMED' in text
if failures: errors.append({'error': '出现当前06关闭或接收失败标记', 'patterns': failures})
ini_ok = all((run / name).exists() and ': FAILED' not in (run / name).read_text()
             and ': OK' in (run / name).read_text() for name in ('ini_check.txt', 'controller_ini_check.txt'))
cpu = []
rss = []
pss = []
rows = list(csv.DictReader((run / 'resources.tsv').open(), delimiter='\t'))
hz = int((run / 'clock_ticks_per_second.txt').read_text())
for row in rows:
    if row['rss_kib'].isdigit(): rss.append(int(row['rss_kib']))
    if row['pss_kib'].isdigit(): pss.append(int(row['pss_kib']))
for left, right in zip(rows, rows[1:]):
    if left['pid'] != right['pid']: continue
    elapsed = float(right['uptime_s']) - float(left['uptime_s'])
    ticks = int(right['utime_ticks']) + int(right['stime_ticks']) - int(left['utime_ticks']) - int(left['stime_ticks'])
    if elapsed > 0 and ticks >= 0: cpu.append(ticks * 100 / hz / elapsed)
scope_ok = not errors and ini_ok and identity.get('SAME_CONTROLLER_ALIVE') == '1'
software_ok = scope_ok and code == 0 and actual_exit == 0 and all(markers.values()) and not alarm_shutdown_unconfirmed and not window_close_unconfirmed and bool(re.search(r'EXIT code=\s*0(?:\s|$)', text))
if mode == 'auto':
    software_ok = software_ok and check_exit == 0 and bool(probe) and probe.get('failed') == 0 and probe.get('completed') is True and probe.get('independent_ad', {}).get('passed') is True and probe.get('independent_ad', {}).get('same_new_connection') is True and probe.get('independent_ad', {}).get('current_qq_coverage', {}).get('ok') is True
n_accepted = len(re.findall(r'^REAGENT_LEVEL_ACCEPTED\s+', text, re.M))
n_processed = len(re.findall(r'^REAGENT_LEVEL_PROCESSED\s+', text, re.M))
n_display = len(re.findall(r'^REAGENT_LEVEL_DISPLAY\s+', text, re.M))
n_guard = contract.get('n_shutdown_guard', {})
if n_guard.get('required_ready_marker') != 'REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false':
    errors.append({'error': 'N关门就绪条件尚未绑定最终源码'})
    software_ok = False
result = {'mode': mode, 'release_id': identity['RELEASE_ID'], 'wrapper_exit': code, 'application_wait_exit': actual_exit,
          'automatic_check_exit': check_exit,
          'application_exit_lines': [line for line in text.splitlines() if 'EXIT code=' in line],
          'software_checks_passed': software_ok, 'complete_startup_to_exit_tx': tx,
          'observed_complete_message_commands': sorted(set(commands)), 'scope_errors': errors,
          'original_login_sync_authorized': manual_sync_authorized,
          'shutdown_markers': markers, 'shutdown_contract_release': contract.get('release_id'), 'current_shutdown_failures': failures, 'ini_hashes_unchanged': ini_ok,
          'original_alarm_shutdown_unconfirmed': alarm_shutdown_unconfirmed,
          'review_window_close_unconfirmed': window_close_unconfirmed,
          'same_controller_alive': identity.get('SAME_CONTROLLER_ALIVE') == '1',
          'resource_scope': 'limited_window_no_pass_budget_or_control_deadline_claim',
          'resources': {'samples': len(rows), 'cpu_percent_one_core_mean': sum(cpu)/len(cpu) if cpu else None,
                        'cpu_percent_one_core_max': max(cpu) if cpu else None,
                        'rss_kib_max': max(rss) if rss else None, 'pss_kib_max': max(pss) if pss else None},
          'physical_input_screen_verified': False, 'administrator_credentials_verified': False,
          'normal_ui_close_reopen_verified': False, 'real_testing_green_verified': False,
          'mechanical_or_detection_functions_verified': False}
result['n_lifecycle_observation'] = {
    'accepted_log_count': n_accepted, 'processed_log_count': n_processed, 'display_log_count': n_display,
    'ready_at_close': markers.get('REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false', False),
    'idle_without_accepted_n': n_accepted == 0,
    'n_function_verified': False, 'in_flight_n_single_outer_close_verified': False,
    'scope': '只核真实关闭保护到达空闲就绪和既有存储线程收尾；不制造N、不由24项基础检查或定时关闭重试推断N业务及一次在途退出通过。',
    'final_source_evidence': n_guard,
}
if probe: result['probe_result'] = probe
(run / 'parsed_summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print(json.dumps({k: result[k] for k in ('mode', 'release_id', 'wrapper_exit', 'software_checks_passed', 'scope_errors')}, ensure_ascii=False))
raise SystemExit(0 if software_ok else 1)
