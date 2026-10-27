#!/usr/bin/env python3
"""WSL-only CLI checks for optional soak timing evidence; target remains C++/Qt."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

sys.dont_write_bytecode = True


def verify_log_scopes(trace):
    """Check complete retained operation spans, allowing only clipped ring heads."""
    names = ('log', 'log.size', 'log.write', 'log.stdout', 'log.segment', 'log.close')
    pending = {}
    seen = set()
    spans = {name: [] for name in names}
    for row in trace['events']:
        if row['source'] != 'soak':
            continue
        stage = row['stage']
        for name in names:
            if stage == name + '.begin':
                assert name not in pending, ('overlapping scope', name)
                pending[name] = row
                seen.add(name)
            elif stage == name + '.end':
                start = pending.pop(name, None)
                if start is None:
                    assert trace['overwritten'] > 0 and name not in seen, ('unpaired scope end', name)
                    continue
                assert start['collector_ns'] <= row['collector_ns']
                if name == 'log':
                    assert start['value'] == row['value'], 'wire log sequence changed inside a scope'
                spans[name].append((row['collector_ns'] - start['collector_ns']) / 1e6)
    assert not pending, ('unfinished retained scopes', pending)
    assert all(spans[name] for name in ('log', 'log.size', 'log.write', 'log.close'))
    if not trace['overwritten']:
        assert spans['log.stdout'] and spans['log.segment']
    return {name: {'complete_retained_count': len(values), 'max_ms': max(values) if values else None}
            for name, values in spans.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--only', nargs='+', choices=['disabled', 'enabled', 'ring_overwrite',
                        'log_budget', 'timing_save_failure'], help='Run selected independent cases')
    args = parser.parse_args()
    if not args.binary.is_absolute() or not args.binary.is_file():
        parser.error('binary must be an existing absolute path')
    if not args.output_dir.is_absolute() or args.output_dir.exists():
        parser.error('output-dir must be a new absolute directory')
    args.output_dir.mkdir(parents=True)
    spec = importlib.util.spec_from_file_location('soak_supervisor', Path(__file__).with_name('run_protocol_soak.py'))
    supervisor = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(supervisor)
    results = []
    scenarios = [('disabled', [], 0), ('enabled', ['--timing-trace'], 0),
                 ('ring_overwrite', ['--timing-trace', '--rate', '50'], 0),
                 ('log_budget', ['--timing-trace', '--segment-bytes', '1024', '--log-budget-bytes', '8192'], 3),
                 ('timing_save_failure', ['--timing-trace'], 3)]
    for name, extra, expected in scenarios:
        if args.only and name not in args.only:
            continue
        destination = args.output_dir / name
        duration = '24' if name == 'ring_overwrite' else '12'
        command = [str(args.binary), '--output-dir', str(destination), '--duration-seconds', duration,
                   '--reconnect-seconds', '2', '--retry-every', '2', *extra]
        started = time.monotonic()
        with (args.output_dir / (name + '.stdout.log')).open('xb') as stream:
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT)
            try:
                if name == 'timing_save_failure':
                    # Only the explicit new test directory is changed. Preserve all outputs.
                    deadline = time.monotonic() + 3
                    while not (destination / 'progress.json').exists() and time.monotonic() < deadline:
                        if child.poll() is not None:
                            break
                        time.sleep(.01)
                    assert (destination / 'progress.json').is_file(), 'target did not initialize'
                    (destination / 'timing.json').mkdir()
                code = child.wait(timeout=35)
            finally:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=5)
        summary = json.loads((destination / 'summary.json').read_text())
        result = {'name': name, 'exit_code': code, 'expected_exit_code': expected,
                  'elapsed_seconds': time.monotonic() - started, 'summary': summary}
        results.append(result)
        # Save partial results before assertions so a failure remains inspectable.
        (args.output_dir / 'result.json').write_text(json.dumps(results, indent=2) + '\n')
        assert code == expected, (name, code, summary)
        assert summary['passed'] == (expected == 0), (name, summary)
        assert not summary['pending_after_close'] and summary['link_state_after_close'] == 'disconnected'
        if expected == 0:
            result['wire_verification'] = supervisor.verify_wire(destination, summary)
        if name == 'disabled':
            assert not summary['timing_enabled'] and summary['timing_events'] == 0
            assert not (destination / 'timing.json').exists()
        elif name == 'timing_save_failure':
            assert summary['error'] == 'timing trace save failed' and not summary['outputs_saved']
        else:
            trace = json.loads((destination / 'timing.json').read_text())
            events = trace['events']
            assert summary['timing_saved'] and len(events) <= trace['capacity'] == 8192
            assert len(events) + trace['overwritten'] == trace['events_seen'] == summary['timing_events']
            assert [row['sequence'] for row in events] == list(range(events[0]['sequence'], trace['events_seen'] + 1))
            assert all(a['collector_ns'] <= b['collector_ns'] for a, b in zip(events, events[1:]))
            result['log_scopes'] = verify_log_scopes(trace)
            stages = {row['stage'] for row in events}
            if expected == 0:
                assert {'write.returned', 'socket.bytesWritten', 'peer.read.actual',
                        'deadline.retry', 'progress.commit.begin', 'progress.commit.end'} <= stages, stages
                assert any(row['stage'] == 'write.returned' and row['value'] == 13 for row in events)
            if name == 'ring_overwrite':
                assert trace['overwritten'] > 0 and len(events) == 8192
            result['timing'] = {'events_seen': trace['events_seen'], 'retained': len(events),
                                'overwritten': trace['overwritten']}
        print(name, 'exit', code, 'verified', flush=True)
    (args.output_dir / 'result.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
