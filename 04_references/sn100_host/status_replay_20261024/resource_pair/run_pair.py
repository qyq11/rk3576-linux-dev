"""Two fixed 12-second host-only probes; existing anonymous data copied, never reseeded."""
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
out = root / 'temp/sn100_host/status_replay_20261024/resource_pair'
public = root / '04_references/sn100_host/status_replay_20261024/resource_pair'
source = root / 'temp/sn100_host/status_replay_20261024/resource_agent/runtime_01'
app = root / 'temp/sn100_host/status_replay_20261024/native/wsl_release/sn100_shell'
helper = app.parent / 'sn100_report_renderer'
fixture = root / '06_programs/sn100_host/tests/fixtures/status_replay.json'
tick_hz = os.sysconf('SC_CLK_TCK')
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

def inventory(directory):
    return {str(p.relative_to(directory)): dict(bytes=p.stat().st_size, sha256=digest(p))
            for p in sorted(directory.rglob('*')) if p.is_file()}

def stat_ticks(path):
    fields = path.read_text().rsplit(')', 1)[1].split()
    return dict(state=fields[0], cpu_ticks=int(fields[11]) + int(fields[12]),
                start_time_ticks=int(fields[19]))

def sample(pid, elapsed):
    proc = Path('/proc') / str(pid)
    process_stat = stat_ticks(proc / 'stat')
    if process_stat['state'] == 'Z':
        return None
    result = dict(elapsed_seconds=elapsed, **process_stat)
    for line in (proc / 'status').read_text().splitlines():
        key, _, value = line.partition(':')
        if key in ('VmRSS', 'VmHWM', 'Threads'):
            result[key] = int(value.split()[0])
    result['Pss'] = next(int(line.split()[1]) for line in (proc / 'smaps_rollup').read_text().splitlines()
                         if line.startswith('Pss:'))
    result['fd_count'] = len(list((proc / 'fd').iterdir()))
    result['thread_stats'] = []
    for thread in sorted((proc / 'task').iterdir()):
        try:
            result['thread_stats'].append(dict(tid=int(thread.name), comm=(thread / 'comm').read_text().strip(),
                                               **stat_ticks(thread / 'stat')))
        except (FileNotFoundError, ProcessLookupError):
            pass
    return result

def summarize_tail(samples):
    tail = [row for row in samples if 6 <= row['elapsed_seconds'] <= 12]
    assert len(tail) >= 40
    span = tail[-1]['elapsed_seconds'] - tail[0]['elapsed_seconds']
    threads = {}
    for row in tail:
        for stat in row['thread_stats']:
            key = (stat['tid'], stat['start_time_ticks'])
            threads.setdefault(key, []).append(dict(elapsed_seconds=row['elapsed_seconds'], **stat))
    hottest = []
    for (tid, birth), seen in threads.items():
        first, last = seen[0], seen[-1]
        delta = last['cpu_ticks'] - first['cpu_ticks']
        hottest.append(dict(tid=tid, start_time_ticks=birth, comm=last['comm'],
                            observed_first_seconds=first['elapsed_seconds'], observed_last_seconds=last['elapsed_seconds'],
                            observed_samples=len(seen), cpu_tick_delta=delta,
                            cpu_percent_of_tail_one_core=100 * delta / tick_hz / span))
    hottest.sort(key=lambda value: (-value['cpu_tick_delta'], value['tid']))
    return dict(selection='existing process samples with 6 <= elapsed <= 12 seconds; no interpolation',
                start_elapsed_seconds=tail[0]['elapsed_seconds'], end_elapsed_seconds=tail[-1]['elapsed_seconds'],
                observed_seconds=span, sample_count=len(tail),
                cpu_tick_delta=tail[-1]['cpu_ticks'] - tail[0]['cpu_ticks'],
                cpu_percent_one_logical_core=100 * (tail[-1]['cpu_ticks'] - tail[0]['cpu_ticks']) / tick_hz / span,
                ranges={field: dict(minimum=min(row[field] for row in tail), maximum=max(row[field] for row in tail))
                        for field in ('VmRSS', 'VmHWM', 'Pss', 'Threads', 'fd_count')},
                threads_by_observed_cpu_delta=hottest,
                thread_accounting_limit='10Hz /proc sampling. A transient thread can disappear between reads; first/last observed counters omit CPU before first observation and after last. comm is a kernel thread name, not a causal explanation.')

assert not public.exists() and source.is_dir()
assert digest(app) == '150c9f0335e564cc0cef367c82a876908afa1177657e3944864e01d12fd54bbc'
source_before = inventory(source)
assert source_before and not any(p.is_symlink() for p in source.rglob('*'))
initial = {}
for name in ('without_replay', 'with_replay'):
    destination = out / name / 'runtime'
    assert not destination.exists()
    shutil.copytree(source, destination, copy_function=shutil.copy2)
    initial[name] = inventory(destination)
    assert initial[name] == source_before
assert initial['without_replay'] == initial['with_replay']
save(out / 'initial_data_manifest.json', dict(source=str(source), original=source_before, clones=initial,
                                             equal=True, scope='Closed anonymous runtime only; file hashes, no field values or database contents archived'))
frozen = {str(p.relative_to(root)): digest(p) for p in (app, helper, fixture)}
results = []
environment = os.environ.copy()
environment['QT_QPA_PLATFORM'] = 'xcb'
environment['SN100_REPORT_RENDERER'] = str(helper)
started_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
for name in ('without_replay', 'with_replay'):
    group = out / name
    command = [str(app), '--offline', '--accounts', '--history', '--catalog', '--samples',
               '--data-dir', str(group / 'runtime'), '--viewport', '1600x900']
    if name == 'with_replay':
        command += ['--status-replay', str(fixture)]
    samples = []
    requested = False
    timed_out = False
    start = time.monotonic()
    with (group / 'process.log').open('wb') as log:
        process = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            while process.poll() is None:
                elapsed = time.monotonic() - start
                if elapsed >= 20:
                    timed_out = True
                    process.kill()
                    break
                if elapsed >= 12 and not requested:
                    process.send_signal(signal.SIGTERM)
                    requested = True
                try:
                    row = sample(process.pid, elapsed)
                    if row is not None:
                        samples.append(row)
                except (FileNotFoundError, ProcessLookupError):
                    pass
                time.sleep(0.1)
            code = process.wait(timeout=5)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
    duration = time.monotonic() - start
    text = (group / 'process.log').read_text(errors='replace')
    checks = dict(exit_zero=code == 0, ordinary_sigterm=requested and not timed_out,
                  ready='SHELL_READY' in text, threads_stopped='THREADS_STOPPED true' in text,
                  final_inputs_unchanged=frozen == {str(p.relative_to(root)): digest(p) for p in (app, helper, fixture)})
    for store in ('HOSPITAL', 'ACCOUNT', 'HISTORY', 'PROJECT', 'REAGENT', 'SAMPLE'):
        checks[store.lower() + '_closed'] = store + '_STORE_CLOSED' in text
    if name == 'with_replay':
        checks['replay_eof_invalid'] = any('STATUS_REPLAY_STEP 14 ' in line and 'fresh= false finished= true' in line
                                         for line in text.splitlines())
        checks['replay_stopped'] = 'STATUS_REPLAY_STOPPED true' in text
    else:
        checks['no_replay_output'] = 'STATUS_REPLAY' not in text
    save(group / 'samples.json', samples)
    result = dict(name=name, pid=process.pid, command=command, elapsed_seconds=duration, exit_code=code,
                  sample_count=len(samples), nominal_sample_period_seconds=0.1, checks=checks,
                  passed=all(checks.values()), tail=summarize_tail(samples))
    save(group / 'summary.json', result)
    results.append(result)
    if not result['passed']:
        break
checks = dict(both_groups_run=len(results) == 2, groups_pass=all(row['passed'] for row in results),
              initial_data_identical=initial['without_replay'] == initial['with_replay'] == source_before,
              original_runtime_unchanged=inventory(source) == source_before,
              final_inputs_unchanged=frozen == {str(p.relative_to(root)): digest(p) for p in (app, helper, fixture)})
comparison = dict(started_utc=started_utc, finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  checks=checks, passed=all(checks.values()), frozen_inputs=frozen,
                  cpu_clock_ticks_per_second=tick_hz, groups=results,
                  scope='One sequential pair, WSL x86_64 Release, same Qt xcb/1600x900 and six-store parameters, cloned identical closed anonymous data; only replay flag differs. No new seeding/credential, no network/device.',
                  limits='Short exploratory comparison only, one run per condition, fixed order without replication. Does not establish statistical significance, complete causality, RK resource budget, UI latency or long stability. Thread names identify measured counters only.')
save(out / 'comparison.json', comparison)
public.mkdir(parents=True)
for name in ('without_replay', 'with_replay'):
    group = out / name
    if (group / 'summary.json').exists():
        (public / name).mkdir()
        for filename in ('process.log', 'samples.json', 'summary.json'):
            shutil.copy2(group / filename, public / name / filename)
for path in (out / 'comparison.json', out / 'initial_data_manifest.json', Path(__file__)):
    shutil.copy2(path, public / path.name)
manifest = ''.join(digest(p) + '  ' + str(p.relative_to(public)) + '\n'
                   for p in sorted(public.rglob('*')) if p.is_file())
(public / 'SHA256SUMS').write_text(manifest, encoding='utf-8')
print(json.dumps(dict(passed=comparison['passed'], checks=checks,
                     groups=[dict(name=row['name'], exit_code=row['exit_code'], elapsed_seconds=row['elapsed_seconds'],
                                  tail_cpu_percent=row['tail']['cpu_percent_one_logical_core'],
                                  hottest_threads=row['tail']['threads_by_observed_cpu_delta'][:5]) for row in results],
                     comparison_sha256=digest(public / 'comparison.json')), ensure_ascii=False))
raise SystemExit(0 if comparison['passed'] else 1)
