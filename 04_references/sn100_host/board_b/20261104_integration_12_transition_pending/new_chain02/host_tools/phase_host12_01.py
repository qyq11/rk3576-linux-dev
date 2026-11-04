"""总管理阶段工具：只整理真实回执、生成下一步脚本，不连接板卡。"""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
CODE = ROOT / 'temp/sn100_host/workflow_next_20261103/runtime_transition_root_review_12_02'
CALLS = ROOT / 'temp/sn100_host/root_manager_20261103'
CAP = CALLS / 'runtime_transition_captures/integration_12_01_runtime_02'
CORE = HERE / 'core_inputs.frozen_01.json'
PHASES = ['metadata', 'prepare', 'preflight', 'stop', 'activate', 'launch', 'observe']
STOP_KEYS = {'result': 'OLD_RESULT_SHA256', 'exit_code': 'OLD_EXIT_CODE_SHA256',
             'stdout': 'STOP_STDOUT_SHA256', 'stderr': 'STOP_STDERR_SHA256',
             'actual_exit': 'STOP_TOOL_EXIT_FILE_SHA256', 'app_pid': 'OLD_APP_PID_FILE_SHA256',
             'app_start_ticks': 'OLD_APP_START_FILE_SHA256', 'app_exe': 'OLD_APP_EXE_FILE_SHA256',
             'boot_id': 'OLD_BOOT_FILE_SHA256'}

def sha(raw):
    return hashlib.sha256(raw).hexdigest()

def record(path):
    raw = path.read_bytes()
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(raw), 'bytes': len(raw)}

def write(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)

def write_json(path, value):
    write(path, (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))

def load(path):
    return json.loads(path.read_bytes())

def fields(raw):
    assert raw.endswith(b'\n') and b'\r' not in raw
    result = {}
    for row in raw.decode('ascii').splitlines():
        key, sep, value = row.partition('=')
        assert sep and key not in result and value
        result[key] = value
    return result

def capture(phase):
    stem = 'runtime12r2_' + phase + '_01'
    dispatched = (CALLS / (stem + '.sh')).read_bytes().replace(b'\r\n', b'\n')
    assert (CALLS / (stem + '.started')).read_text(encoding='ascii').strip() == sha(dispatched)
    assert dispatched == (CODE / 'generated' / (phase + '_01') / 'control.sh').read_bytes()
    raw = (CALLS / (stem + '.log')).read_bytes()
    actual_exit = (CALLS / (stem + '.exit')).read_bytes()
    assert actual_exit in (b'0\n', b'0\r\n'), (phase, actual_exit)
    matches = list(re.finditer(rb'(?m)^RUNTIME_RECEIPT_SHA256=([0-9a-f]{64})\n', raw))
    assert len(matches) == 1
    # board08按stdout+stderr顺序保存。收据是stdout的最后一个输出，
    # 以其真实SHA和EXIT_CODE边界无损拆出尾随stderr，原合并日志继续保留。
    end_marker = b'EXIT_CODE=0\n'
    receipt_end = raw.index(end_marker, matches[0].end()) + len(end_marker)
    receipt = raw[matches[0].end():receipt_end]
    stdout = raw[:receipt_end]
    stderr = raw[receipt_end:]
    assert sha(receipt) == matches[0].group(1).decode('ascii')
    values = fields(receipt)
    assert values['PHASE'] == phase and values['EXIT_CODE'] == '0'
    assert values['CORE_INPUTS_SHA256'] == record(CORE)['sha256']
    assert values['AUTHORIZATION_SHA256'] == record(HERE / ('authorization_' + phase + '_01.json'))['sha256']
    assert values['TRANSITION_ID'] == 'integration_12_01_runtime_02'
    assert values['SERIAL'] == '8314b1e8ffd0321f'
    write(CAP / (phase + '.log'), stdout)
    write(CAP / (phase + '.stderr.log'), stderr)
    # 保留原始exit；此处只将已核对的真实0换成生成器要求的LF文本。
    write(CAP / (phase + '.exit'), b'0\n')
    write(CAP / (phase + '.receipt'), receipt)
    write_json(CAP / (phase + '.provenance.json'), {
        'original_log': record(CALLS / (stem + '.log')),
        'original_exit': record(CALLS / (stem + '.exit')),
        'original_started': record(CALLS / (stem + '.started')),
        'receipt': record(CAP / (phase + '.receipt')),
        'stdout_export': record(CAP / (phase + '.log')),
        'stderr_export': record(CAP / (phase + '.stderr.log')),
        'stream_export': 'board08原件为stdout+stderr；按已校验SHA的stdout末尾收据边界拆分，无损保留尾随stderr。',
        'exit_export': '真实外层退出为0，仅将CRLF规范为LF；原件保留。',
        'board_action_executed_by_this_tool': False})
    print(json.dumps({'phase': phase, 'receipt': record(CAP / (phase + '.receipt')), 'facts': values}, ensure_ascii=False))

def generate(phase):
    index = PHASES.index(phase)
    assert index > 0
    previous = PHASES[index - 1]
    authorization = load(CODE / 'phase_authorization.template.json')
    authorization.update({'frozen': True, 'phase': phase, 'allowed': True,
                          'core_inputs': record(CORE), 'root_approval': record(HERE / 'root_runtime_approval_01.md'),
                          'previous_authorization': record(HERE / ('authorization_' + previous + '_01.json')),
                          'previous_receipt': record(CAP / (previous + '.receipt')),
                          'previous_phase_exit': record(CAP / (previous + '.exit')),
                          'previous_phase_log': record(CAP / (previous + '.log'))})
    if index >= PHASES.index('activate'):
        authorization.update({'stop_intent_record': load(CORE)['stable_target_intent'],
                              'old_stop_receipt': record(CAP / 'stop.receipt'),
                              'old_stop_phase_exit': record(CAP / 'stop.exit'),
                              'old_stop_phase_log': record(CAP / 'stop.log'),
                              'old_stop_files': {key: record(CAP / 'old_stop_files' / key) for key in STOP_KEYS}})
    path = HERE / ('authorization_' + phase + '_01.json')
    write_json(path, authorization)
    output = CODE / 'generated' / (phase + '_01')
    command = [__import__('sys').executable, '-X', 'utf8', str(CODE / 'generate_runtime_controls.py'),
               '--phase', phase, '--inputs', str(CORE), '--authorization', str(path), '--output', str(output)]
    result = subprocess.run(command, capture_output=True)
    write(HERE / ('generate_' + phase + '_01.stdout.log'), result.stdout)
    write(HERE / ('generate_' + phase + '_01.stderr.log'), result.stderr)
    write(HERE / ('generate_' + phase + '_01.exit'), (str(result.returncode) + '\n').encode('ascii'))
    assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
    control = output / 'control.sh'
    wsl_path = '/mnt/c/' + str(control.resolve())[3:].replace('\\', '/')
    result = subprocess.run(['wsl', '-d', 'Ubuntu-24.04', '-u', 'qyq', '--', 'bash', '-n', wsl_path], capture_output=True)
    write(HERE / ('syntax_' + phase + '_01.stdout.log'), result.stdout)
    write(HERE / ('syntax_' + phase + '_01.stderr.log'), result.stderr)
    write(HERE / ('syntax_' + phase + '_01.exit'), (str(result.returncode) + '\n').encode('ascii'))
    assert result.returncode == 0, result.stderr
    raw = control.read_bytes()
    assert b'\r' not in raw
    write(CALLS / ('runtime12r2_' + phase + '_01.sh'), raw)
    print(json.dumps({'phase': phase, 'control': record(control), 'authorization': record(path), 'syntax_exit': 0,
                      'board_executed': False}, ensure_ascii=False))

def prepare_stop_collector():
    core = load(CORE)
    meta = core['metadata_directory']
    old = core['fresh_baseline']['old_application']['run']
    paths = {'result': old + '/result.txt', 'exit_code': old + '/exit_code',
             'stdout': meta + '/phase_logs/stop/stdout.log', 'stderr': meta + '/phase_logs/stop/stderr.log',
             'actual_exit': meta + '/phase_logs/stop/actual_exit', 'app_pid': old + '/app.pid',
             'app_start_ticks': old + '/app.start_ticks', 'app_exe': old + '/app.exe', 'boot_id': old + '/boot_id'}
    rows = ['set -Eeuo pipefail',
            '[[ $(cat /proc/sys/kernel/random/boot_id) == ' + shlex.quote(core['fresh_baseline']['boot_id']) + ' ]]',
            '[[ $(readlink ' + shlex.quote(core['base'] + '/current') + ') == ' + shlex.quote(core['fresh_baseline']['old_current']) + ' ]]']
    for key, path in paths.items():
        quoted = shlex.quote(path)
        rows += [f'[[ -f {quoted} && ! -L {quoted} ]]', f'[[ $(stat -c %s {quoted}) -le 16777216 ]]',
                 f"printf 'BEGIN_STOP_FILE={key}\\n'", f'sha256sum -- {quoted}', f'base64 -w0 -- {quoted}',
                 "printf '\\nEND_STOP_FILE\\n'"]
    write(CALLS / 'runtime12r2_collect_stop_01.sh', ('\n'.join(rows) + '\n').encode('ascii'))
    print('仅生成旧停原件读取脚本，未执行板端操作。')

def capture_stop_files():
    dispatched = (CALLS / 'runtime12r2_collect_stop_01.sh').read_bytes().replace(b'\r\n', b'\n')
    assert (CALLS / 'runtime12r2_collect_stop_01.started').read_text(encoding='ascii').strip() == sha(dispatched)
    raw = (CALLS / 'runtime12r2_collect_stop_01.log').read_bytes()
    assert (CALLS / 'runtime12r2_collect_stop_01.exit').read_bytes() in (b'0\n', b'0\r\n')
    receipt = fields((CAP / 'stop.receipt').read_bytes())
    core = load(CORE)
    old = core['fresh_baseline']['old_application']['run']
    meta = core['metadata_directory']
    expected_paths = {'result': old + '/result.txt', 'exit_code': old + '/exit_code',
                      'stdout': meta + '/phase_logs/stop/stdout.log',
                      'stderr': meta + '/phase_logs/stop/stderr.log',
                      'actual_exit': meta + '/phase_logs/stop/actual_exit',
                      'app_pid': old + '/app.pid', 'app_start_ticks': old + '/app.start_ticks',
                      'app_exe': old + '/app.exe', 'boot_id': old + '/boot_id'}
    pattern = rb'BEGIN_STOP_FILE=([a-z_]+)\n([0-9a-f]{64})  ([^\n]+)\n([A-Za-z0-9+/=]*)\nEND_STOP_FILE\n'
    matches = list(re.finditer(pattern, raw))
    assert b''.join(m.group(0) for m in matches) == raw and len(matches) == 9
    values = {}
    for match in matches:
        key = match.group(1).decode('ascii')
        assert key in STOP_KEYS and key not in values
        assert match.group(3).decode('ascii') == expected_paths[key]
        data = base64.b64decode(match.group(4), validate=True)
        assert sha(data) == match.group(2).decode('ascii') == receipt[STOP_KEYS[key]]
        values[key] = data
    assert set(values) == set(STOP_KEYS)
    for key, data in values.items():
        write(CAP / 'old_stop_files' / key, data)
    write_json(CAP / 'old_stop_files/provenance.json', {
        'collector_log': record(CALLS / 'runtime12r2_collect_stop_01.log'),
        'collector_exit': record(CALLS / 'runtime12r2_collect_stop_01.exit'),
        'collector_script': record(CALLS / 'runtime12r2_collect_stop_01.sh'),
        'collector_started': record(CALLS / 'runtime12r2_collect_stop_01.started'),
        'stop_receipt': record(CAP / 'stop.receipt'),
        'files': {key: record(CAP / 'old_stop_files' / key) for key in STOP_KEYS},
        'remote_read_only': True})
    print(json.dumps({'stop_originals': len(values), 'all_receipt_hashes_match': True}))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=['capture', 'generate', 'prepare-stop-collector', 'capture-stop-files'])
    parser.add_argument('phase', nargs='?', choices=PHASES)
    args = parser.parse_args()
    if args.operation == 'capture':
        capture(args.phase)
    elif args.operation == 'generate':
        generate(args.phase)
    elif args.operation == 'prepare-stop-collector':
        prepare_stop_collector()
    else:
        capture_stop_files()
