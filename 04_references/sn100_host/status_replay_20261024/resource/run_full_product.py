"""One isolated, host-only 60-second full-shell observation; no build or device access."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import sqlite3
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[4]
program = root / '06_programs/sn100_host'
out = root / 'temp/sn100_host/status_replay_20261024/resource_agent'
public = root / '04_references/sn100_host/status_replay_20261024/resource'
runtime = out / 'runtime_01'
observed = out / 'observation_01'
secret = out / 'bootstrap.private'
app = root / 'temp/sn100_host/status_replay_20261024/native/wsl_release/sn100_shell'
fixture = program / 'tests/fixtures/status_replay.json'
helper = app.parent / 'sn100_report_renderer'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

assert not runtime.exists() and not observed.exists() and not secret.exists() and not public.exists()
assert app.is_file() and helper.is_file() and fixture.is_file()
assert digest(app) == '150c9f0335e564cc0cef367c82a876908afa1177657e3944864e01d12fd54bbc', 'Final native main fingerprint differs from parent delivery'
before = {str(p.relative_to(root)): digest(p) for p in (app, helper, fixture)}
started = datetime.datetime.now(datetime.timezone.utc).isoformat()
environment = os.environ.copy()
environment['QT_QPA_PLATFORM'] = 'xcb'
environment['SN100_REPORT_RENDERER'] = str(helper)
command = [str(app), '--offline', '--accounts', '--history', '--catalog', '--samples',
           '--status-replay', str(fixture), '--seed-anonymous', '--seed-catalog-demo',
           '--test-secret-file', str(secret), '--data-dir', str(runtime), '--viewport', '1600x900']
disk_samples = []
secret.write_text(secrets.token_urlsafe(24), encoding='utf-8')
secret.chmod(0o600)
try:
    observer = [sys.executable, str(program / 'tests/observe_linux_process.py'),
                '--output-dir', str(observed), '--stop-after', '60', '--timeout', '90', '--', *command]
    with (out / 'observer.log').open('wb') as log:
        process = subprocess.Popen(observer, env=environment, stdout=log, stderr=subprocess.STDOUT)
        clock = time.monotonic()
        while process.poll() is None:
            files = list(runtime.rglob('*')) if runtime.exists() else []
            files = [p for p in files if p.is_file()]
            disk_samples.append(dict(elapsed_seconds=time.monotonic() - clock,
                                     runtime_file_count=len(files),
                                     runtime_bytes=sum(p.stat().st_size for p in files),
                                     process_log_bytes=(observed / 'process.log').stat().st_size
                                     if (observed / 'process.log').exists() else 0))
            time.sleep(1)
        observer_exit = process.wait()
finally:
    # This task's single ephemeral credential is never copied to public evidence.
    secret.unlink(missing_ok=True)

summary = json.loads((observed / 'summary.json').read_text())
logtext = (observed / 'process.log').read_text(errors='replace')
checks = {
    'observer_exit_zero': observer_exit == 0,
    'product_exit_zero': summary['exit_code'] == 0,
    'normal_stop_requested': summary['ordinary_sigterm_requested'] and not summary['timed_out'],
    'at_least_59_seconds': summary['elapsed_seconds'] >= 59,
    'shell_ready': 'SHELL_READY' in logtext,
    'replay_loaded_14': 'STATUS_REPLAY_LOADED 14' in logtext,
    'replay_finished_invalidates_fresh': any('STATUS_REPLAY_STEP' in line and 'fresh= false' in line
                                            and 'finished= true' in line for line in logtext.splitlines()),
    'planned_tx_and_file_rx': 'STATUS_REPLAY_PLANNED_TX' in logtext and 'STATUS_REPLAY_FILE_RX' in logtext,
    'replay_stopped': 'STATUS_REPLAY_STOPPED true' in logtext,
    'threads_stopped': 'THREADS_STOPPED true' in logtext,
    'no_cross_thread_sql_warning': not any(s in logtext for s in ('different thread', 'QThread: Destroyed',
                                                               'Cannot queue arguments', 'database not open')),
    'private_secret_removed': not secret.exists(),
}
for store in ('HOSPITAL', 'ACCOUNT', 'HISTORY', 'PROJECT', 'REAGENT', 'SAMPLE'):
    checks[store.lower() + '_closed'] = store + '_STORE_CLOSED' in logtext
after = {str(p.relative_to(root)): digest(p) for p in (app, helper, fixture)}
checks['binaries_fixture_unchanged'] = before == after
runtime_inventory = []
for path in sorted(runtime.rglob('*')):
    if not path.is_file():
        continue
    item = dict(relative_path=str(path.relative_to(runtime)), bytes=path.stat().st_size)
    if path.suffix == '.db':
        connection = sqlite3.connect(path.as_uri() + '?mode=ro', uri=True)
        try:
            tables = [row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")]
            # Only row counts, never field values or the anonymous password column.
            item['table_row_counts'] = {name: connection.execute('SELECT COUNT(*) FROM "' + name.replace('"', '""') + '"').fetchone()[0]
                                        for name in tables}
        finally:
            connection.close()
    runtime_inventory.append(item)
save(out / 'disk_samples.json', disk_samples)
result = dict(started_utc=started, finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              passed=all(checks.values()), checks=checks, binaries_fixture_before=before,
              binaries_fixture_after=after, observer_summary=summary,
              disk_first=disk_samples[0], disk_last=disk_samples[-1],
              disk_growth_bytes=disk_samples[-1]['runtime_bytes'] - disk_samples[0]['runtime_bytes'],
              runtime_inventory=runtime_inventory,
              load='14 recorded events, one step per 250ms (~3.5 seconds once six stores ready), then idle until 60-second SIGTERM; not 60 seconds sustained load',
              same_turn_build_or_qttest_background='Parent confirms current-turn builds and QtTest have stopped before FINAL_NATIVE_READY; no system-wide isolation claimed',
              scope='WSL x86_64 Release, Qt xcb, 1600x900 viewport, new anonymous six-store startup and 14-step replay; '
                    '60-second idle-after-replay process baseline; no UI interactions/latency measurement, '
                    'no RK budget, no original Windows/real instrument/long-stability qualification',
              excluded='Anonymous runtime databases and ephemeral bootstrap credential are not public evidence')
save(out / 'verification.json', result)
public.mkdir(parents=True)
for path in (out / 'verification.json', out / 'disk_samples.json', out / 'observer.log',
             observed / 'summary.json', observed / 'samples.json', observed / 'process.log', Path(__file__)):
    shutil.copy2(path, public / path.name)
manifest = ''.join(digest(p) + '  ' + p.name + '\n' for p in sorted(public.iterdir()) if p.is_file())
(public / 'SHA256SUMS').write_text(manifest, encoding='utf-8')
print(json.dumps(dict(passed=result['passed'], checks=checks, summary=summary,
                     verification_sha256=digest(public / 'verification.json')), ensure_ascii=False))
sys.exit(0 if result['passed'] else 1)
