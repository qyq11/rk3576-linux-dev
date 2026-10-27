"""One owned, passive 12-second Qt observation of the frozen offline product."""
from pathlib import Path
import datetime, hashlib, json, os, shutil, signal, subprocess, time

root = Path(__file__).resolve().parents[3]
out = root / 'temp/sn100_host/idle_cpu_20261024/observer'
out.mkdir(exist_ok=False)
app = root / 'temp/sn100_host/status_replay_20261024/native/wsl_release/sn100_shell'
helper = app.parent / 'sn100_report_renderer'
fixture = root / '06_programs/sn100_host/tests/fixtures/status_replay.json'
probe = out.parent / 'probe_tool/libsn100_idle_probe.so'
source = root / 'temp/sn100_host/status_replay_20261024/resource_agent/runtime_01'
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
def save(name, value): (out / name).write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')
def inventory(path): return {str(p.relative_to(path)): digest(p) for p in sorted(path.rglob('*')) if p.is_file()}
assert digest(app) == '150c9f0335e564cc0cef367c82a876908afa1177657e3944864e01d12fd54bbc'
assert digest(probe) == '57fd2c2b6dc603cc1d16373669a97abb1ddcabcbd87e285bf9d8d4cd179cd5af'
before = {str(p): digest(p) for p in (app, helper, fixture, probe)}
initial = inventory(source)
assert len(initial) == 9 and not any(p.is_symlink() for p in source.rglob('*'))
shutil.copytree(source, out / 'runtime', copy_function=shutil.copy2)
assert inventory(out / 'runtime') == initial
save('inputs.json', dict(frozen=before, original_anonymous_runtime=initial, copied_equal=True))
environment = os.environ.copy()
environment.update(QT_QPA_PLATFORM='xcb', LD_PRELOAD=str(probe),
                   SN100_IDLE_PROBE_OUTPUT=str(out / 'observation.json'), SN100_REPORT_RENDERER=str(helper))
command = [str(app), '--offline', '--accounts', '--history', '--catalog', '--samples',
           '--status-replay', str(fixture), '--data-dir', str(out / 'runtime'), '--viewport', '1600x900']
start = time.monotonic()
samples, stopped, timed_out = [], False, False
with (out / 'process.log').open('wb') as log:
    process = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT)
    birth = None
    while process.poll() is None:
        elapsed = time.monotonic() - start
        proc = Path('/proc') / str(process.pid)
        try:
            fields = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
            if birth is None: birth = fields[19]
            assert fields[19] == birth
            samples.append(dict(elapsed=elapsed, ticks=int(fields[11]) + int(fields[12])))
        except FileNotFoundError: pass
        if elapsed >= 12 and not stopped:
            assert Path(os.readlink(proc / 'exe')).resolve() == app.resolve()
            process.send_signal(signal.SIGTERM); stopped = True
        if elapsed >= 25:
            timed_out = True
            assert Path(os.readlink(proc / 'exe')).resolve() == app.resolve()
            process.kill(); break
        time.sleep(.1)
    code = process.wait()
text = (out / 'process.log').read_text(errors='replace')
observation = json.loads((out / 'observation.json').read_text()) if (out / 'observation.json').exists() else {}
checks = dict(exit_zero=code == 0, ordinary_sigterm=stopped, no_hard_timeout=not timed_out,
              shell_ready='SHELL_READY' in text, eof_invalidates=any('STATUS_REPLAY_STEP' in line and
              'finished= true' in line and 'fresh= false' in line for line in text.splitlines()),
              replay_stopped='STATUS_REPLAY_STOPPED true' in text, threads_stopped='THREADS_STOPPED true' in text,
              probe_no_error='SN100_IDLE_PROBE_ERROR' not in text,
              probe_complete=observation.get('observation_complete') is True,
              frozen_unchanged=before == {str(p): digest(p) for p in (app, helper, fixture, probe)},
              source_anonymous_data_unchanged=initial == inventory(source))
for store in ('HOSPITAL', 'ACCOUNT', 'HISTORY', 'PROJECT', 'REAGENT', 'SAMPLE'):
    checks[store.lower() + '_closed'] = store + '_STORE_CLOSED' in text
save('samples.json', samples)
save('verification.json', dict(passed=all(checks.values()), checks=checks, command=command,
     process_pid=process.pid, process_start_ticks=birth, exit_code=code,
     elapsed_seconds=time.monotonic() - start, probe_window='6..10s from QApplication::exec; passive hooks/events perturb timing',
     page='7, selected by explicit status-replay, kept through EOF; no interactions',
     scope='Mechanism observation only; not an uninstrumented CPU baseline, original Windows, RK budget or instrument acceptance'))
print(json.dumps(dict(passed=all(checks.values()), checks=checks, exit_code=code,
                     observation_keys=list(observation)), ensure_ascii=False))
raise SystemExit(0 if all(checks.values()) else 1)
