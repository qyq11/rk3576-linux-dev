#!/usr/bin/env python3
"""Local WSL supervisor only. The tested target is C++/Qt; never deploy this to RK."""
import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone


def utc():
    return datetime.now(timezone.utc).isoformat()


def digest(path):
    hasher = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            hasher.update(block)
    return hasher.hexdigest()


def atomic_json(path, value):
    temporary = path.with_suffix(path.suffix + '.new')
    with temporary.open('w', encoding='utf-8') as output:
        json.dump(value, output, ensure_ascii=False, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    temporary.replace(path)


def sample(pid):
    proc = Path('/proc') / str(pid)
    values = {}
    for line in (proc / 'status').read_text().splitlines():
        key, _, value = line.partition(':')
        if key in ['VmRSS', 'VmHWM', 'Threads']:
            values[key] = int(value.split()[0])
    for line in (proc / 'smaps_rollup').read_text().splitlines():
        if line.startswith('Pss:'):
            values['Pss'] = int(line.split()[1])
    fields = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    values['cpu_ticks'] = int(fields[11]) + int(fields[12])
    values['fd'] = len(list((proc / 'fd').iterdir()))
    return values


def resource_summary(path, elapsed):
    first, last, maximum = [], deque(maxlen=600), {}
    ticks = []
    count = 0
    with path.open() as stream:
        for line in stream:
            value = json.loads(line)
            if 'sample_error' in value:
                continue
            count += 1
            if len(first) < 600:
                first.append(value)
            last.append(value)
            ticks.append(value['cpu_ticks'])
            if len(ticks) > 2:
                ticks.pop(1)
            for key in ['VmRSS', 'VmHWM', 'Pss', 'Threads', 'fd']:
                if key in value:
                    maximum[key] = max(maximum.get(key, 0), value[key])
    def average(rows, key):
        found = [row[key] for row in rows if key in row]
        return sum(found) / len(found) if found else None
    return {'nominal_interval_seconds': 1, 'samples': count, 'units': 'KiB except Threads/fd',
            'maximum': maximum, 'first_600_samples_mean_RSS': average(first, 'VmRSS'),
            'last_600_samples_mean_RSS': average(last, 'VmRSS'),
            'first_600_samples_mean_PSS': average(first, 'Pss'), 'last_600_samples_mean_PSS': average(last, 'Pss'),
            'cpu_one_core_percent': ((ticks[-1] - ticks[0]) / os.sysconf('SC_CLK_TCK') / elapsed * 100) if ticks and elapsed else None,
            'limit': 'Sampled baseline and trend, not proof against every leak or an RK resource budget'}


def verify_wire(directory, summary):
    observed = events = actual = unconfirmed = 0
    expected = bytearray()
    golden = {bytes.fromhex(value) for value in ['0203000600000001516a3a0405',
              '0203000600000001516b3b0405', '02030006000000015148180405']}
    for segment in sorted(directory.glob('wire_*.jsonl')):
        with segment.open(encoding='utf-8') as stream:
            for line in stream:
                event = json.loads(line)
                events += 1
                assert event['sequence'] == events, 'event sequence gap'
                kind = event['kind']
                wire = bytes.fromhex(event.get('hex', ''))
                if kind == 'client.TX.queued.not_peer_ack':
                    assert wire in golden, 'query not independent golden readonly frame'
                    expected.extend(wire)
                elif kind == 'peer.RX.actual':
                    assert bytes(expected).startswith(wire), 'actual peer RX mismatch'
                    del expected[:len(wire)]
                    actual += len(wire)
                elif kind == 'client.TX.unconfirmed_on_cancel':
                    assert wire == expected, 'unconfirmed cancel bytes mismatch'
                    unconfirmed += len(wire)
                    expected.clear()
                elif kind == 'client.RX.parsed' and wire == b'Qq':
                    observed += 1
                    assert event['data']['channels']['Base'] == str(observed), 'diagnostic sequence gap'
    assert not expected
    assert observed == summary['diagnostics_sent'] == summary['diagnostics_received']
    assert actual == summary['peer_query_rx_bytes']
    assert unconfirmed == summary['cancelled_unconfirmed_query_bytes']
    assert actual + unconfirmed == summary['query_attempts'] * 13
    return {'events': events, 'diagnostics_sequential': observed, 'actual_peer_rx_bytes': actual,
            'cancelled_unconfirmed_bytes': unconfirmed, 'independent_golden_and_sequence_check': 'passed'}


def verify_timing(directory, summary):
    value = json.loads((directory / 'timing.json').read_text())
    events = value['events']
    assert summary['timing_enabled'] and summary['timing_saved'], 'timing not saved'
    assert len(events) <= value['capacity'] == 8192, 'timing capacity mismatch'
    assert len(events) + value['overwritten'] == value['events_seen'] == summary['timing_events']
    assert events and [row['sequence'] for row in events] == list(range(events[0]['sequence'], value['events_seen'] + 1))
    assert all(a['collector_ns'] <= b['collector_ns'] for a, b in zip(events, events[1:])), 'collector clock order'
    return {'retained_events': len(events), 'overwritten_events': value['overwritten'],
            'events_seen': value['events_seen'], 'bounded_ring_and_clock_order': 'passed'}


def supervise(job):
    specification = json.loads((job / 'job.json').read_text())
    binary = job / 'binary/sn100_protocol_soak'
    status = {'schema': 'sn100-o05-3-job-v1', 'state': 'running', 'started_utc': utc(),
              'supervisor_pid': os.getpid(), 'completed_stages': [], 'active_stage': None,
              'hardware_verified': False}
    child = None
    def interrupted(_signum, _frame):
        raise InterruptedError('supervisor interrupted; no pass inferred')
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        for stage in specification['stages']:
            assert digest(binary) == specification['binary_sha256'], 'frozen binary changed'
            destination = job / stage['name']
            started = time.monotonic()
            command = [str(binary), '--output-dir', str(destination), '--duration-seconds', str(stage['seconds']),
                       '--rate', '10', '--reconnect-seconds', str(stage['reconnect_seconds']), '--retry-every', '10']
            if specification.get('timing_trace', False):
                command.append('--timing-trace')
            status['active_stage'] = stage
            status['updated_utc'] = utc()
            atomic_json(job / 'status.json', status)
            atomic_json(Path(specification['archive_dir']) / 'job_status.json', status)
            with (job / (stage['name'] + '.stdout.log')).open('xb') as output, \
                 (job / (stage['name'] + '.resources.jsonl')).open('x') as resources:
                child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT)
                status['target_pid'] = child.pid
                atomic_json(job / 'status.json', status)
                last_update = -10
                previous_monotonic, previous_wall = started, time.time()
                while child.poll() is None:
                    now, wall = time.monotonic(), time.time()
                    monotonic_gap, wall_gap = now - previous_monotonic, wall - previous_wall
                    if monotonic_gap > 120 or abs(wall_gap - monotonic_gap) > 120:
                        raise TimeoutError('host pause/clock discontinuity exceeded120s; continuous load not proven')
                    previous_monotonic, previous_wall = now, wall
                    elapsed = now - started
                    if elapsed > stage['seconds'] + 30:
                        raise TimeoutError('target exceeded real duration plus30s drain/watchdog budget')
                    try:
                        value = sample(child.pid)
                    except (OSError, ValueError) as error:
                        value = {'sample_error': str(error)}
                    value['elapsed_seconds'] = elapsed
                    resources.write(json.dumps(value) + '\n')
                    resources.flush()
                    if elapsed - last_update >= 10:
                        status.update(updated_utc=utc(), stage_elapsed_seconds=elapsed)
                        progress = destination / 'progress.json'
                        if progress.exists():
                            status['target_progress'] = json.loads(progress.read_text())
                            if time.time() - progress.stat().st_mtime > 120:
                                raise TimeoutError('target progress stale for120s')
                        atomic_json(job / 'status.json', status)
                        last_update = elapsed
                    time.sleep(1)
                code = child.wait()
            child = None
            elapsed = time.monotonic() - started
            summary = json.loads((destination / 'summary.json').read_text())
            assert code == 0 and summary['passed'] and summary['outputs_saved'], (code, summary)
            assert summary['elapsed_ms'] >= stage['seconds'] * 1000
            assert summary['diagnostics_received'] >= max(1, int(stage['seconds'] * 10 * .90))
            assert summary['heartbeat']['samples'] >= int(stage['seconds'] * 100 * .90)
            assert summary['heartbeat']['maximum_ms'] <= 120000
            assert not summary['pending_after_close'] and not summary['fresh_status_after_close']
            assert summary['logical_queries'] == summary['completed_queries'] + summary['cancelled_queries']
            checked = verify_wire(destination, summary)
            if specification.get('timing_trace', False):
                checked['timing'] = verify_timing(destination, summary)
            measured = resource_summary(job / (stage['name'] + '.resources.jsonl'), elapsed)
            stage_result = {'stage': stage, 'exit_code': code, 'observed_seconds': elapsed,
                            'finished_utc': utc(), 'wire_check': checked, 'resources': measured, 'summary': summary}
            atomic_json(job / (stage['name'] + '.verified.json'), stage_result)
            # 只写本任务专属证据目录；不由后台进程覆盖多人共用README/计划/主记录。
            archive = Path(specification['archive_dir']) / stage['name']
            archive.mkdir(exist_ok=False)
            for path in destination.iterdir():
                if path.is_file(): shutil.copyfile(path, archive / path.name)
            for suffix in ['.stdout.log', '.resources.jsonl', '.verified.json']:
                shutil.copyfile(job / (stage['name'] + suffix), archive / (stage['name'] + suffix))
            manifest = [f'{digest(path)}  {path.name}' for path in sorted(archive.iterdir()) if path.is_file()]
            (archive / 'SHA256SUMS').write_text('\n'.join(manifest) + '\n')
            status['completed_stages'].append(stage_result)
            atomic_json(job / 'status.json', status)
            atomic_json(Path(specification['archive_dir']) / 'job_status.json', status)
        status.update(state='passed', active_stage=None, finished_utc=utc(), updated_utc=utc())
    except BaseException as error:
        if child is not None and child.poll() is None:
            try: child.terminate()
            except ProcessLookupError: pass
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try: child.kill()
                except ProcessLookupError: pass
                child.wait(timeout=5)
        status.update(state='interrupted' if isinstance(error, (InterruptedError, KeyboardInterrupt)) else 'failed',
                      error=str(error), finished_utc=utc(), updated_utc=utc())
    atomic_json(job / 'status.json', status)
    atomic_json(Path(specification['archive_dir']) / 'job_status.json', status)
    print(json.dumps(status, ensure_ascii=False), flush=True)
    return 0 if status['state'] == 'passed' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--archive-dir', type=Path)
    parser.add_argument('--continuous-seconds', type=int, default=7200)
    parser.add_argument('--recovery-seconds', type=int, default=28800)
    parser.add_argument('--background', action='store_true')
    parser.add_argument('--timing-trace', action='store_true', help='Freeze optional bounded target timing capture')
    parser.add_argument('--worker', type=Path, help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    if arguments.worker:
        return supervise(arguments.worker.resolve(strict=True))
    for value in [arguments.output_dir, arguments.archive_dir]:
        if value is None or not value.is_absolute() or value.exists() or not value.parent.is_dir():
            parser.error('output/archive must be new absolute directories with existing parents')
    if arguments.binary is None or not arguments.binary.is_absolute() or not arguments.binary.is_file():
        parser.error('binary must be an existing absolute executable path')
    if not (1 <= arguments.continuous_seconds <= 86400 and 1 <= arguments.recovery_seconds <= 86400):
        parser.error('stage duration must be1..86400 real seconds')
    if arguments.output_dir.resolve() == arguments.archive_dir.resolve():
        parser.error('output/archive must be separate directories')
    job = arguments.output_dir
    job.mkdir()
    arguments.archive_dir.mkdir()
    (job / 'binary').mkdir()
    frozen = job / 'binary/sn100_protocol_soak'
    shutil.copyfile(arguments.binary, frozen)
    frozen.chmod(0o755)
    root = Path(__file__).resolve().parents[3]
    source_dir = root / '06_programs/sn100_host/protocol'
    source_hashes = {}
    snapshot = arguments.archive_dir / 'source_snapshot'
    snapshot.mkdir()
    for path in sorted(source_dir.iterdir()):
        if path.suffix in ['.cpp', '.h', '.pro', '.sh']:
            source_hashes[path.relative_to(root).as_posix()] = digest(path)
            shutil.copyfile(path, snapshot / path.name)
    source_hashes[Path(__file__).relative_to(root).as_posix()] = digest(Path(__file__))
    shutil.copyfile(__file__, snapshot / 'run_protocol_soak.py')
    specification = {'schema': 'sn100-o05-3-job-input-v1', 'created_utc': utc(),
        'binary_sha256': digest(frozen), 'original_binary': str(arguments.binary),
        'archive_dir': str(arguments.archive_dir), 'source_sha256': source_hashes,
        'timing_trace': arguments.timing_trace,
        'stages': [{'name': 'continuous_2h' if arguments.continuous_seconds == 7200 else f'continuous_{arguments.continuous_seconds}s',
                    'seconds': arguments.continuous_seconds, 'reconnect_seconds': 0},
                   {'name': 'recovery_8h' if arguments.recovery_seconds == 28800 else f'recovery_{arguments.recovery_seconds}s',
                    'seconds': arguments.recovery_seconds, 'reconnect_seconds': 60}],
        'hardware_verified': False, 'minimum_actual_load_fraction': .90, 'maximum_host_pause_seconds': 120,
        'sleep_policy': 'Actual QElapsedTimer/process monotonic elapsed and minimum90% diagnostic/heartbeat load required; >120s supervisor gap or wall/monotonic discrepancy fails; no RK performance threshold implied'}
    atomic_json(job / 'job.json', specification)
    atomic_json(arguments.archive_dir / 'job.json', specification)
    if arguments.background:
        with (job / 'supervisor.stdout.log').open('xb') as output:
            process = subprocess.Popen([sys.executable, str(snapshot / 'run_protocol_soak.py'), '--worker', str(job)],
                stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        atomic_json(job / 'launcher.json', {'supervisor_pid': process.pid, 'started_utc': utc(),
                                          'status': str(job / 'status.json')})
        print(json.dumps({'state': 'launched', 'pid': process.pid, 'job': str(job), 'status': str(job / 'status.json')}))
        return 0
    return supervise(job)


if __name__ == '__main__':
    raise SystemExit(main())
