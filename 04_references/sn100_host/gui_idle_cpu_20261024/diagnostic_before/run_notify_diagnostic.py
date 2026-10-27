from pathlib import Path
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
OUT = ROOT / 'temp/sn100_host/o09_loopback/cpu_before'
APP = ROOT / 'temp/sn100_host/status_replay_20261024/native/wsl_release/sn100_shell'
SOURCE = ROOT / 'temp/sn100_host/status_replay_20261024/resource_agent/runtime_01'
SHIM = OUT / 'notify_shim.so'
EXPECTED = '150c9f0335e564cc0cef367c82a876908afa1177657e3944864e01d12fd54bbc'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def manifest(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise RuntimeError('runtime symlink refused')
        if path.is_file():
            result[str(path.relative_to(root))] = {'bytes': path.stat().st_size, 'sha256': sha(path)}
    return result

assert sha(APP) == EXPECTED
source_before = manifest(SOURCE)
# Refuse a source runtime still open in any visible Linux process.
for pid in Path('/proc').iterdir():
    if not pid.name.isdecimal():
        continue
    try:
        for fd in (pid / 'fd').iterdir():
            try:
                target = fd.readlink()
                if str(target).startswith(str(SOURCE)):
                    raise RuntimeError('source runtime still open by pid ' + pid.name)
            except (FileNotFoundError, PermissionError):
                pass
    except (FileNotFoundError, PermissionError):
        pass

smoke = '--smoke' in sys.argv
runs = []
for mode in (['smoke_event_filter'] if smoke else ['without_replay_filter', 'with_replay_filter']):
    directory = OUT / mode
    directory.mkdir(exist_ok=False)
    runtime = directory / 'runtime'
    runtime.mkdir()
    for name in ['hospital', 'account', 'history', 'catalog']:
        shutil.copytree(SOURCE / name, runtime / name)
    before_copy = manifest(runtime)
    command = [str(APP), '--offline', '--accounts', '--history', '--catalog', '--samples',
               '--data-dir', str(runtime), '--viewport', '1600x900']
    if mode == 'with_replay_filter':
        command += ['--status-replay', str(ROOT / '06_programs/sn100_host/tests/fixtures/status_replay.json')]
    env = dict(os.environ, QT_QPA_PLATFORM='xcb', LD_PRELOAD=str(SHIM), SN100_NOTIFY_DIAG_DIR=str(directory))
    if smoke:
        env['SN100_NOTIFY_SMOKE'] = '1'
    start = time.monotonic()
    timed_out = False
    with (directory / 'process.log').open('wb') as log:
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, cwd=APP.parent)
        while process.poll() is None and time.monotonic() - start < (1.8 if smoke else 21.5):
            time.sleep(.1)
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
        try:
            code = process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            code = process.wait()
    stats_path = directory / ('notify_' + str(process.pid) + '.json')
    stats = json.loads(stats_path.read_text()) if stats_path.exists() else {}
    log_text = (directory / 'process.log').read_text(errors='replace')
    result = {'mode': mode, 'command': command, 'pid': process.pid,
              'elapsed_seconds': time.monotonic() - start, 'exit_code': code, 'timed_out': timed_out,
              'ordinary_sigterm_requested': True, 'cpu_baseline': False,
              'binary_before_sha256': EXPECTED, 'binary_after_sha256': sha(APP),
              'source_runtime': str(SOURCE), 'copied_scope': ['hospital', 'account', 'history', 'catalog'],
              'copied_files_before': before_copy, 'copied_files_after': manifest(runtime),
              'source_unchanged': manifest(SOURCE) == source_before,
              'shell_ready': 'SHELL_READY' in log_text, 'threads_stopped': 'THREADS_STOPPED true' in log_text,
              'stats_path': str(stats_path), 'navigation_clicked': stats.get('original_navigation_clicked'),
              'current_main_page': stats.get('current_main_page'), 'screenshot_saved': stats.get('screenshot_saved')}
    (directory / 'run.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    runs.append(result)
    print(json.dumps({key: result[key] for key in ['mode', 'pid', 'exit_code', 'current_main_page',
                                                 'screenshot_saved', 'source_unchanged', 'threads_stopped']}), flush=True)
    assert code == 0 and not timed_out and result['source_unchanged']
    assert result['binary_after_sha256'] == EXPECTED and result['threads_stopped']
    assert result['shell_ready']
    if not smoke:
        assert result['navigation_clicked'] and result['current_main_page'] == 7
    assert sum(row['events'] for row in stats['rows']) > 0
    assert result['screenshot_saved']

(OUT / ('smoke_runs.json' if smoke else 'diagnostic_runs.json')).write_text(json.dumps({'cpu_baseline': False, 'runs': runs}, ensure_ascii=False, indent=2) + '\n')
