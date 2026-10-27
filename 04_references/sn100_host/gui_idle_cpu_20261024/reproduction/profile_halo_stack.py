"""Host diagnostic: GDB owns its child; no target attach, board, or source edits."""
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import time

root = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
out = root / 'temp/sn100_host/o09_loopback/cpu_gdb'
out.mkdir()
app = root / 'temp/sn100_host/status_replay_20261024/native/wsl_release/sn100_shell'
source = root / 'temp/sn100_host/status_replay_20261024/resource_agent/runtime_01'
for name in ('hospital', 'account', 'history', 'catalog'):
    shutil.copytree(source / name, out / 'runtime' / name)
digest = hashlib.sha256(app.read_bytes()).hexdigest()
assert digest == '150c9f0335e564cc0cef367c82a876908afa1177657e3944864e01d12fd54bbc'
arguments = [str(app), '--offline', '--accounts', '--history', '--catalog', '--samples',
             '--data-dir', str(out / 'runtime'), '--viewport', '1600x900',
             '--status-replay', str(root / '06_programs/sn100_host/tests/fixtures/status_replay.json')]
environment = dict(os.environ, QT_QPA_PLATFORM='xcb')
process = subprocess.Popen(['gdb', '--quiet', '--interpreter=mi2', '--args', *arguments],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    bufsize=1, env=environment)
lines = queue.Queue()
log = (out / 'gdb.log').open('w')
def reader():
    for line in process.stdout:
        log.write(line)
        log.flush()
        lines.put(line)
threading.Thread(target=reader, daemon=True).start()
token = 0
def until(predicate, timeout=20):
    end = time.monotonic() + timeout
    received = []
    while time.monotonic() < end:
        line = lines.get(timeout=max(0.01, end-time.monotonic()))
        received.append(line)
        if predicate(line): return ''.join(received)
    raise RuntimeError('GDB response timed out')
def command(value):
    global token
    token += 1
    process.stdin.write(str(token) + value + '\n')
    process.stdin.flush()
    result = until(lambda line: line.startswith(str(token) + '^'))
    assert not result.splitlines()[-1].startswith(str(token) + '^error'), result
    return result
try:
    command('-gdb-set pagination off')
    command('-gdb-set mi-async on')
    command('-gdb-set print thread-events off')
    command('-interpreter-exec console "handle SIGTERM nostop noprint pass"')
    command('-exec-run')
    time.sleep(6)
    command('-exec-interrupt --all')
    until(lambda line: line.startswith('*stopped'))
    command('-break-insert "QtMaterialFlatButtonStateMachine::setHaloScaleFactor(double)"')
    command('-exec-continue')
    until(lambda line: line.startswith('*stopped') and 'breakpoint-hit' in line)
    stack = command('-stack-list-frames 0 18')
    (out / 'halo_stack.mi').write_text(stack)
    command('-break-disable 1')
    command('-interpreter-exec console "signal SIGTERM"')
    until(lambda line: line.startswith('*stopped') and 'exited-normally' in line)
    command('-gdb-exit')
    process.wait(timeout=10)
    assert hashlib.sha256(app.read_bytes()).hexdigest() == digest
    (out / 'summary.json').write_text(json.dumps({'binary_sha256': digest,
        'gdb_exit_code': process.returncode, 'inferior_normal_exit': True,
        'scope': 'Diagnostic breakpoint call stack only; debugger overhead excluded from CPU baseline'}, indent=2))
    print(stack)
finally:
    if process.poll() is None:
        process.stdin.write('-gdb-exit\n')
        process.stdin.flush()
        process.wait(timeout=10)
    log.close()
