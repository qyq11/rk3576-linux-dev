"""Serial old-capture contrast and fresh native short CLI checks; no GUI/hardware."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import signal
import subprocess

if not __debug__ or os.geteuid() == 0:
    raise RuntimeError('Ordinary WSL user and assertions required')
work = Path(__file__).resolve().parent
root = work.parents[2]
program = root / '06_programs/sn100_host'
out = work / 'protocol_log_scope'
out.mkdir(exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
now = lambda: datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(name, value):
    (out / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
files = ['protocol/soak_main.cpp', 'protocol/soak.pro', 'protocol/read_only_client.cpp',
    'protocol/read_only_client.h', 'protocol/tcp_transport.cpp', 'protocol/tcp_transport.h',
    'protocol/frame_codec.cpp', 'protocol/frame_codec.h', 'protocol/read_only_messages.cpp',
    'protocol/read_only_messages.h', 'protocol/status_session.cpp', 'protocol/status_session.h',
    'tests/protocol_soak_timing_test.py', 'tests/run_protocol_soak.py']
before = {name: sha(program / name) for name in files}
save('source_before.json', before)
stages = []
def run(name, argv, cwd, expected, timeout):
    row = {'name': name, 'argv': list(map(str, argv)), 'cwd': str(cwd),
        'started_utc': now(), 'expected_exit': expected, 'timeout_seconds': timeout}
    save(name + '.command.json', row)
    with (out / (name + '.log')).open('xb') as stream:
        p = subprocess.Popen(row['argv'], cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL, start_new_session=True)
        timed_out = False
        try:
            code = p.wait(timeout=timeout)
        except BaseException:
            timed_out = True
            os.killpg(p.pid, signal.SIGTERM)
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait()
            raise
        finally:
            row.update(exit_code=p.returncode, pid=p.pid, timed_out=timed_out, finished_utc=now())
            save(name + '.exit.json', row)
            stages.append(row)
            save('steps.json', stages)
    print(name, 'actual_exit', code, flush=True)
    assert code == expected, row
    return row

try:
    old = root / 'temp/sn100_host/protocol_timing_20261024/soak_native/sn100_protocol_soak'
    assert sha(old) == '5837009027c610e46ed6c8312fde24b6a0848ddb63154f2a13b79b230553b5eb'
    save('old_binary.json', {'path': str(old), 'sha256': sha(old),
        'meaning': 'Existing frozen native timing checkpoint; new log-scope validation expected to reject absent markers, not a business regression'})
    checker = program / 'tests/protocol_soak_timing_test.py'
    run('old_scope_contrast', ['python3', '-B', checker, '--binary', old, '--output-dir', out / 'old_scope', '--only', 'enabled'], root, 1, 40)
    old_result = json.loads((out / 'old_scope/result.json').read_text())
    assert len(old_result) == 1 and old_result[0]['exit_code'] == 0 and old_result[0]['summary']['passed']
    old_trace = json.loads((out / 'old_scope/enabled/timing.json').read_text())
    assert not any(row['stage'] in {'log.begin', 'log.end', 'log.size.begin', 'log.size.end'} for row in old_trace['events'])
    assert 'AssertionError' in (out / 'old_scope_contrast.log').read_text()
    build = out / 'native_build'
    build.mkdir()
    run('qmake', ['/usr/lib/qt5/bin/qmake', program / 'protocol/soak.pro', 'CONFIG+=release', 'CONFIG-=debug'], build, 0, 40)
    run('make', ['make', '-j2'], build, 0, 180)
    binary = build / 'sn100_protocol_soak'
    assert binary.is_file()
    run('new_cli', ['python3', '-B', checker, '--binary', binary, '--output-dir', out / 'new_cli'], root, 0, 130)
    result = json.loads((out / 'new_cli/result.json').read_text())
    assert len(result) == 5 and all(row['exit_code'] == row['expected_exit_code'] for row in result)
    after = {name: sha(program / name) for name in files}
    save('source_after.json', after)
    assert before == after and sha(old) == '5837009027c610e46ed6c8312fde24b6a0848ddb63154f2a13b79b230553b5eb'
    save('verification.json', {'passed': True, 'source_inputs': before, 'source_unchanged': True,
        'new_binary_sha256': sha(binary), 'old_scope_checker_actual_exit': 1, 'old_actual_target_exit': 0,
        'qmake_actual_exit': 0, 'make_actual_exit': 0, 'new_checker_actual_exit': 0,
        'cases': [{'name': row['name'], 'actual_target_exit': row['exit_code'], 'expected_exit': row['expected_exit_code'],
            'log_scopes': row.get('log_scopes')} for row in result],
        'old8h_direct_cause_confirmed': False, 'hardware_or_GUI_used': False,
        'scope': 'Complete optional log scope/size/rotation/stdout/close observation and actual failure paths only; 120ms/one retry and8192 ring unchanged; no old8h fix, long stability, or budget claim'})
except BaseException as e:
    save('failure.json', {'passed': False, 'error_type': type(e).__name__, 'error': str(e), 'stages': stages})
    raise
