import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time

root = Path(__file__).resolve().parents[4]
out = Path(__file__).resolve().parent
public = root / '04_references/sn100_host/idle_cpu_20261024/perf_profile'
source = root / 'temp/sn100_host/status_replay_20261024/resource_agent/runtime_01'
app = root / 'temp/sn100_host/status_replay_20261024/native/wsl_release/sn100_shell'
helper = app.parent / 'sn100_report_renderer'
fixture = root / '06_programs/sn100_host/tests/fixtures/status_replay.json'
perf = Path('/usr/lib/linux-tools/6.18.54.1-microsoft-standard-WSL2/bin/perf')
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
save = lambda p, value: p.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
def inventory(directory):
    return {str(p.relative_to(directory)): dict(bytes=p.stat().st_size, sha256=digest(p))
            for p in sorted(directory.rglob('*')) if p.is_file()}
def sample(pid):
    proc = Path('/proc') / str(pid)
    fields = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    row = dict(elapsed_monotonic=time.monotonic(), state=fields[0],
               cpu_ticks=int(fields[11]) + int(fields[12]), start_time_ticks=int(fields[19]))
    for line in (proc / 'status').read_text().splitlines():
        key, _, value = line.partition(':')
        if key in ('VmRSS', 'VmHWM', 'Threads'): row[key] = int(value.split()[0])
    row['Pss'] = next(int(line.split()[1]) for line in (proc / 'smaps_rollup').read_text().splitlines() if line.startswith('Pss:'))
    row['fd_count'] = len(list((proc / 'fd').iterdir()))
    return row

assert digest(app) == '150c9f0335e564cc0cef367c82a876908afa1177657e3944864e01d12fd54bbc'
assert not public.exists() and not (out / 'runtime').exists()
assert not any(p.is_symlink() for p in source.rglob('*'))
before = {str(p): digest(p) for p in (app, helper, fixture, perf)}
initial = inventory(source)
shutil.copytree(source, out / 'runtime', copy_function=shutil.copy2)
assert initial == inventory(out / 'runtime')
save(out / 'initial_data_manifest.json', dict(source=str(source), source_files=initial, clone_equal=True))
environment = os.environ.copy()
environment.update(QT_QPA_PLATFORM='xcb', SN100_REPORT_RENDERER=str(helper))
command = [str(app), '--offline', '--accounts', '--history', '--catalog', '--samples',
           '--status-replay', str(fixture), '--data-dir', str(out / 'runtime'), '--viewport', '1600x900']
perf_command = [str(perf), 'record', '-e', 'cpu-clock:u', '-F', '199', '--call-graph', 'dwarf,16384',
                '-D', '6000', '-o', str(out / 'perf.data'), '--', 'python3', str(out / 'child.py'), str(out), *command]
save(out / 'contract.json', dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
    command=perf_command, frozen_inputs=before, duration_seconds=12, perf_delay_ms=6000,
    page='Explicit status replay selects main page7 after six-store ready; EOF before sampled idle tail',
    scope='One controlled host CPU call-stack profile, instrumentation overhead present; not an unperturbed resource or RK budget test',
    raw_evidence_policy='perf.data and anonymous runtime remain temp only; public report/script contain symbols and addresses, never raw stack bytes'))
rows = []; identity = None; requested = False; timed_out = False
started = time.monotonic()
with (out / 'process_and_perf.log').open('wb') as log:
    process = subprocess.Popen(perf_command, env=environment, stdout=log, stderr=subprocess.STDOUT)
    while process.poll() is None:
        now = time.monotonic()
        if identity is None and (out / 'child_identity.json').exists():
            identity = json.loads((out / 'child_identity.json').read_text())
        if identity:
            try:
                row = sample(identity['pid'])
                assert row['start_time_ticks'] == identity['start_time_ticks']
                rows.append(row)
                if now - identity['started_monotonic'] >= 12 and not requested:
                    actual = (Path('/proc') / str(identity['pid']) / 'cmdline').read_bytes().split(b'\0')[0].decode()
                    assert actual == str(app)
                    os.kill(identity['pid'], signal.SIGTERM); requested = True
            except (FileNotFoundError, ProcessLookupError, StopIteration):
                pass
        if now - started >= 25:
            timed_out = True
            if identity:
                try:
                    row = sample(identity['pid'])
                    if row['start_time_ticks'] == identity['start_time_ticks']: os.kill(identity['pid'], signal.SIGKILL)
                except (FileNotFoundError, ProcessLookupError): pass
            process.terminate()
            break
        time.sleep(.1)
    perf_exit = process.wait(timeout=5)
save(out / 'samples.json', rows)
child_result = json.loads((out / 'child_exit.json').read_text()) if (out / 'child_exit.json').exists() else {}
logtext = (out / 'process_and_perf.log').read_text(errors='replace')
checks = dict(perf_exit_zero=perf_exit == 0, child_exit_zero=child_result.get('exit_code') == 0,
    normal_sigterm=requested and not timed_out, shell_ready='SHELL_READY' in logtext,
    replay_eof=any('STATUS_REPLAY_STEP' in line and 'fresh= false' in line and 'finished= true' in line for line in logtext.splitlines()),
    threads_stopped='THREADS_STOPPED true' in logtext,
    source_runtime_unchanged=initial == inventory(source),
    frozen_inputs_unchanged=before == {str(p): digest(p) for p in (app, helper, fixture, perf)})
for store in ('HOSPITAL', 'ACCOUNT', 'HISTORY', 'PROJECT', 'REAGENT', 'SAMPLE'):
    checks[store.lower() + '_closed'] = store + '_STORE_CLOSED' in logtext
reports = [
    ('report_self.txt', [str(perf), 'report', '-i', str(out / 'perf.data'), '--stdio', '--no-children', '--sort', 'comm,dso,symbol', '--percent-limit', '0.3']),
    ('report_children.txt', [str(perf), 'report', '-i', str(out / 'perf.data'), '--stdio', '--children', '--sort', 'comm,dso,symbol', '--percent-limit', '1']),
    ('script.txt', [str(perf), 'script', '-i', str(out / 'perf.data')]),
]
for name, arguments in reports:
    with (out / name).open('wb') as log:
        code = subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT).returncode
    checks[name + '_exit_zero'] = code == 0
result = dict(passed=all(checks.values()), checks=checks, perf_exit=perf_exit, child_exit=child_result,
    samples=len(rows), perf_data_bytes=(out / 'perf.data').stat().st_size if (out / 'perf.data').exists() else None,
    perf_data_sha256=digest(out / 'perf.data') if (out / 'perf.data').exists() else None,
    scope='CPU clock user-mode stack samples delayed6s in a single12s page7 replay run; instrumentation overhead; not causal proof by itself or RK budget')
save(out / 'verification.json', result)
public.mkdir(parents=True)
for name in ('run_profile.py', 'child.py', 'contract.json', 'initial_data_manifest.json', 'child_identity.json',
             'child_exit.json', 'samples.json', 'process_and_perf.log', 'report_self.txt', 'report_children.txt', 'script.txt', 'verification.json'):
    path = out / name
    if path.is_file(): shutil.copy2(path, public / name)
(public / 'SHA256SUMS').write_text(''.join(digest(p) + '  ' + p.name + '\n' for p in sorted(public.iterdir()) if p.is_file()))
print(json.dumps(result, ensure_ascii=False))
raise SystemExit(0 if result['passed'] else 1)
