"""One fixed host observation after all compilation and GUI slots are idle."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess

root = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
work = root / 'temp/sn100_host/o02_login'
program = root / '06_programs/sn100_host'
build = work / 'native/wsl_release'
output = work / 'resource_01'
runtime = work / 'resource_runtime_01'
assert not output.exists() and not runtime.exists()
runtime.mkdir()
# 只复制已正常关闭的匿名七store运行目录，不重播种、不读取认证字段。
source = root / 'temp/sn100_host/o09_loopback/product_smoke_01/runtime'
for name in ('hospital', 'account', 'history', 'catalog', 'instrument'):
    shutil.copytree(source / name, runtime / name)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
before = {name: sha(build / name) for name in ('sn100_shell', 'sn100_report_renderer')}
snapshot = work / 'native/after.json'
frozen = json.loads(snapshot.read_text())
assert all(sha(program / path) == value for path, value in frozen.items())
command = ['python3', '-B', str(program / 'tests/observe_linux_process.py'),
    '--output-dir', str(output), '--stop-after', '30', '--timeout', '50', '--',
    str(build / 'sn100_shell'), '--offline', '--accounts', '--history', '--catalog', '--samples',
    '--instrument', '--password-login', '--status-loopback', '--data-dir', str(runtime), '--viewport', '1600x900']
result = subprocess.run(command, env=dict(os.environ, QT_QPA_PLATFORM='xcb'), capture_output=True, text=True)
(output / 'driver.log').write_text(result.stdout + result.stderr)
log = (output / 'process.log').read_text()
closed = ('HOSPITAL_STORE_CLOSED', 'ACCOUNT_STORE_CLOSED', 'HISTORY_STORE_CLOSED',
          'PROJECT_STORE_CLOSED', 'REAGENT_STORE_CLOSED', 'SAMPLE_STORE_CLOSED', 'MAINTENANCE_STORE_CLOSED')
after = {name: sha(build / name) for name in before}
checks = {'exit_0': result.returncode == 0, 'binaries_unchanged': before == after,
    'source_frozen': all(sha(program / path) == value for path, value in frozen.items()),
    'seven_stores_closed': all(marker in log for marker in closed),
    'threads_stopped': 'THREADS_STOPPED true' in log,
    'loopback_stopped': 'STATUS_LOOPBACK_STOPPED true' in log,
    'password_login_enabled': 'PASSWORD_LOGIN_ENABLED' in log,
    'actual_wire': 'STATUS_LOOPBACK_WIRE' in log and 'STATUS_LOOPBACK_TX' in log and 'STATUS_LOOPBACK_RX' in log}
samples = json.loads((output / 'samples.json').read_text())
tail = [row for row in samples if 10 <= row['elapsed_seconds'] < 30]
assert len(tail) > 100
span = tail[-1]['elapsed_seconds'] - tail[0]['elapsed_seconds']
steady = {'window': '10 <= elapsed_seconds < 30; excludes cold startup', 'sample_count': len(tail),
    'observed_span_seconds': span, 'cpu_percent_one_logical_core':
        100 * (tail[-1]['cpu_ticks'] - tail[0]['cpu_ticks']) / os.sysconf('SC_CLK_TCK') / span}
for field in ('VmRSS', 'VmHWM', 'Pss', 'Threads', 'fd_count'):
    values = [row[field] for row in tail if row.get(field) is not None]
    steady['minimum_' + field] = min(values) if values else None
    steady['maximum_' + field] = max(values) if values else None
files = [path for path in runtime.rglob('*') if path.is_file()]
metadata = {'checks': checks, 'binary_sha256': after, 'source_snapshot_entries': len(frozen),
    'load': 'WSL Release/XCB 1600x900, Guest, original instrument page, seven reopened anonymous stores, 1Hz self-owned loopback Qj, local-password capability idle',
    'scope': 'One 30s ordinary SIGTERM observation with no concurrent task compilation/GUI. Direct child only; no RK, instrument, resource budget, control interference or long stability claim.',
    'runtime_file_count': len(files), 'runtime_bytes': sum(path.stat().st_size for path in files),
    'steady_segment': steady, 'database_rows': {}}
for path in runtime.rglob('*.db'):
    with sqlite3.connect('file:' + str(path) + '?mode=ro', uri=True) as db:
        tables = [row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT GLOB 'sqlite_*'")]
        metadata['database_rows'][str(path.relative_to(runtime))] = {
            name: db.execute('SELECT count(*) FROM "' + name.replace('"', '""') + '"').fetchone()[0] for name in tables}
(output / 'verification.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(json.dumps({'checks': checks, 'steady_segment': steady}, indent=2))
assert all(checks.values())
