import hashlib
import json
import os
from pathlib import Path
import secrets
import sqlite3
import subprocess
import tempfile

work=Path(__file__).resolve().parent
root=work.parents[2]
program=root/'06_programs/sn100_host'
build=work/'native/wsl_release'
out=work/'resource_01'
runtime=work/'resource_runtime_01'
assert not out.exists() and not runtime.exists()
runtime.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
frozen=json.loads((work/'native/final_before.json').read_text())
assert all(sha(program/p)==h for p,h in frozen.items())
before={name:sha(build/name) for name in ('sn100_shell','sn100_report_renderer')}
fd,name=tempfile.mkstemp(prefix='.resource-bootstrap-',dir=work)
secret=Path(name)
os.write(fd,secrets.token_hex(24).encode());os.close(fd)
command=['python3',str(program/'tests/observe_linux_process.py'),'--output-dir',str(out),
    '--stop-after','30','--timeout','50','--',str(build/'sn100_shell'),'--offline','--accounts',
    '--history','--catalog','--samples','--instrument','--password-login','--printer-settings',
    '--status-loopback','--seed-catalog-demo','--seed-anonymous','--test-secret-file',str(secret),
    '--data-dir',str(runtime),'--viewport','1600x900']
try:
    p=subprocess.run(command,env=dict(os.environ,QT_QPA_PLATFORM='xcb',QT_SCALE_FACTOR='1',QT_AUTO_SCREEN_SCALE_FACTOR='0'),capture_output=True,text=True,timeout=65)
finally:
    secret.unlink(missing_ok=True)
assert out.exists()
(out/'observer_driver.log').write_text(p.stdout+p.stderr)
(out/'observer_exit.json').write_text(json.dumps({'actual_exit_code':p.returncode})+'\n')
log=(out/'process.log').read_text()
closed=['HOSPITAL_STORE_CLOSED','ACCOUNT_STORE_CLOSED','HISTORY_STORE_CLOSED','PROJECT_STORE_CLOSED',
    'REAGENT_STORE_CLOSED','SAMPLE_STORE_CLOSED','MAINTENANCE_STORE_CLOSED','PRINTER_SETTINGS_CLOSED']
checks={'observer_exit0':p.returncode==0,'ordinary_child_exit0':'EXIT code= 0' in log,
    'all_closures_once':all(log.count(marker)==1 for marker in closed),
    'threads_stopped':'THREADS_STOPPED true' in log,'loopback_stopped':'STATUS_LOOPBACK_STOPPED true' in log,
    'printer_ready':'PRINTER_SETTINGS_READY' in log,'bootstrap_unlinked':not secret.exists(),
    'source_unchanged':all(sha(program/path)==h for path,h in frozen.items()),
    'binaries_unchanged':before=={name:sha(build/name) for name in before}}
samples=json.loads((out/'samples.json').read_text())
tail=[row for row in samples if 10<=row['elapsed_seconds']<30]
assert len(tail)>100
span=tail[-1]['elapsed_seconds']-tail[0]['elapsed_seconds']
steady={'window':'10<=elapsed_seconds<30','sample_count':len(tail),'observed_span_seconds':span,
    'cpu_percent_one_logical_core':100*(tail[-1]['cpu_ticks']-tail[0]['cpu_ticks'])/os.sysconf('SC_CLK_TCK')/span}
for key in ('VmRSS','VmHWM','Pss','Threads','fd_count'):
    values=[r[key] for r in tail if r.get(key) is not None]
    steady['min_'+key]=min(values) if values else None
    steady['max_'+key]=max(values) if values else None
# Count anonymous schemas only after every worker and child has stopped.
# Do not read authentication values or hash/archive runtime DB/INI/secrets.
rows={}
for path in runtime.rglob('*'):
    if not path.is_file() or path.suffix not in ('.db','.sqlite'):continue
    with path.open('rb') as f: signature=f.read(16)
    if signature!=b'SQLite format 3\x00':continue
    db=sqlite3.connect('file:'+str(path)+'?mode=ro',uri=True)
    try:
        tables=[r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")]
        rows[path.relative_to(runtime).as_posix()]={table:db.execute('SELECT count(*) FROM "'+table.replace('"','""')+'"').fetchone()[0] for table in tables}
    finally:db.close()
info={'checks':checks,'binary_sha256':before,'source_inputs':len(frozen),'steady':steady,
    'load':'WSL Release/XCB1600x900 Guest, seven anonymous stores, local settings worker, 1Hz self-owned Qj, original instrument page7, idle password entry; fresh explicit anonymous catalog fixture',
    'limits':'Single30s whole-product baseline after all build/GUI/hash work ended; not pure printer increment or a resource/RT/long-stability budget. Actual printer names may be empty; no print jobs.',
    'database_row_counts':rows,'runtime_file_count':sum(x.is_file() for x in runtime.rglob('*')),
    'runtime_bytes':sum(x.stat().st_size for x in runtime.rglob('*') if x.is_file()),
    'runtime_or_credentials_archived':False}
(out/'verification.json').write_text(json.dumps(info,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'checks':checks,'steady':steady},ensure_ascii=False))
assert all(checks.values())
