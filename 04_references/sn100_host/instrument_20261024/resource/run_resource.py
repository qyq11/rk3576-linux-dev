"""Single host-only fixed-load observation, run after compilation/GUI slot handoff."""
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import uuid

work = Path(__file__).resolve().parent
root = work.parents[2]
program = work / 'source/06_programs/sn100_host'
binary = work / 'native/wsl_release/sn100_shell'
observer = program / 'tests/observe_linux_process.py'
runtime = work / 'resource_runtime'
output = work / 'resource'
if runtime.exists() or output.exists():
    raise SystemExit('Preserve prior runtime/observation; use a new directory for a changed experiment')
runtime.mkdir()
secret = work / 'resource_bootstrap.tmp'
secret.write_text(str(uuid.uuid4()), encoding='utf-8')
secret.chmod(0o600)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
before = {str(p):sha(p) for p in (binary, binary.with_name('sn100_report_renderer'))}
try:
    result = subprocess.run(['python3', '-B', str(observer), '--output-dir',str(output),
        '--stop-after','30','--timeout','50','--','env','QT_QPA_PLATFORM=xcb',str(binary),
        '--offline','--accounts','--history','--catalog','--samples','--instrument',
        '--status-replay',str(program/'tests/fixtures/status_replay.json'),
        '--data-dir',str(runtime),'--seed-anonymous','--test-secret-file',str(secret),
        '--seed-catalog-demo','--viewport','1600x900'], check=False)
finally:
    secret.unlink(missing_ok=True)
after = {str(p):sha(p) for p in (binary, binary.with_name('sn100_report_renderer'))}
log = (output/'process.log').read_text(errors='replace')
closed = ('HOSPITAL_STORE_CLOSED','ACCOUNT_STORE_CLOSED','HISTORY_STORE_CLOSED',
          'PROJECT_STORE_CLOSED','REAGENT_STORE_CLOSED','SAMPLE_STORE_CLOSED','MAINTENANCE_STORE_CLOSED')
checks = {'exit_0': result.returncode==0, 'binary_hash_unchanged':before==after,
          'shell_ready': 'SHELL_READY' in log,
          'initial_instrument_load': log.count('INSTRUMENT_INITIAL_MONTH_READY')>=1,
          'seven_stores_closed': all(item in log for item in closed),
          'threads_stopped': 'THREADS_STOPPED true' in log,
          'replay_stopped': 'STATUS_REPLAY_STOPPED true' in log}
files = [p for p in runtime.rglob('*') if p.is_file()]
metadata = {'load':'new anonymous Guest, seven stores, 14-event file replay then idle on original instrument page',
    'scope':'30 real seconds WSL/XCB; includes cold start. No RK, instrument, accepted resource budget or long-run claim.',
    'checks':checks,'binary_sha256':after,
    'runtime_file_count':len(files),'runtime_bytes':sum(p.stat().st_size for p in files),
    'database_rows':{}}
for path in runtime.rglob('*.db'):
    with sqlite3.connect('file:'+str(path)+'?mode=ro', uri=True) as db:
        names = [r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT GLOB 'sqlite_*'")]
        metadata['database_rows'][str(path.relative_to(runtime))] = {
            name:db.execute('SELECT count(*) FROM "'+name.replace('"','""')+'"').fetchone()[0] for name in names}
(output/'verification.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
print(json.dumps(checks))
raise SystemExit(0 if all(checks.values()) else 1)
