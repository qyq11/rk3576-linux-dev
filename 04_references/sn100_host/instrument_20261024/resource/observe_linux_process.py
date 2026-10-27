#!/usr/bin/env python3
"""Host-only /proc observer for a fresh offline GUI/test process (never on RK)."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def sample_process(pid):
    root = Path('/proc') / str(pid)
    stat = (root / 'stat').read_text().rsplit(')', 1)[1].split()
    if stat[0] == 'Z':
        return None
    values = {}
    for line in (root / 'status').read_text().splitlines():
        key, _, value = line.partition(':')
        if key in ('VmRSS', 'VmHWM', 'Threads'):
            values[key] = int(value.split()[0])
    try:
        pss = next(line for line in (root / 'smaps_rollup').read_text().splitlines()
                   if line.startswith('Pss:'))
        values['Pss'] = int(pss.split()[1])
    except (PermissionError, StopIteration, FileNotFoundError):
        values['Pss'] = None
    values.update(cpu_ticks=int(stat[11]) + int(stat[12]),
                  fd_count=len(list((root / 'fd').iterdir())))
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', required=True)
    parser.add_argument('--stop-after', type=float, default=0,
                        help='Request ordinary SIGTERM after this many real seconds; 0 waits for natural exit')
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    output = Path(args.output_dir)
    if not command or not output.is_absolute() or output.exists() or args.timeout <= 0:
        parser.error('command, positive timeout and a new absolute output directory are required')
    output.mkdir(parents=True)
    started = time.monotonic()
    samples = []
    termination_requested = False
    timed_out = False
    with (output / 'process.log').open('wb') as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            while process.poll() is None:
                elapsed = time.monotonic() - started
                if elapsed >= args.timeout:
                    timed_out = True
                    process.kill()
                    break
                if args.stop_after > 0 and elapsed >= args.stop_after and not termination_requested:
                    process.send_signal(signal.SIGTERM)
                    termination_requested = True
                try:
                    value = sample_process(process.pid)
                    if value is not None:
                        samples.append(dict(elapsed_seconds=elapsed, **value))
                except (FileNotFoundError, ProcessLookupError):
                    pass
                time.sleep(0.1)
            code = process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
    duration = time.monotonic() - started
    (output / 'samples.json').write_text(json.dumps(samples, indent=2), encoding='utf-8')
    summary = dict(command=command, pid=process.pid, elapsed_seconds=duration,
                   exit_code=code, timed_out=timed_out,
                   ordinary_sigterm_requested=termination_requested,
                   sample_period_seconds=0.1, sample_count=len(samples),
                   scope='Observed direct child only; WSL host baseline, not RK budget/real-time qualification',
                   pss_unavailable_samples=sum(row['Pss'] is None for row in samples))
    if len(samples) >= 2:
        span = samples[-1]['elapsed_seconds'] - samples[0]['elapsed_seconds']
        summary['cpu_percent_one_logical_core'] = (
            100 * (samples[-1]['cpu_ticks'] - samples[0]['cpu_ticks']) / os.sysconf('SC_CLK_TCK') / span)
        summary['observed_span_seconds'] = span
        for field in ('VmRSS', 'VmHWM', 'Threads', 'fd_count', 'Pss'):
            observed = [row[field] for row in samples if row.get(field) is not None]
            summary['maximum_' + field] = max(observed) if observed else None
    (output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, ensure_ascii=False))
    return 1 if timed_out else (code if code >= 0 else 1)


if __name__ == '__main__':
    raise SystemExit(main())
