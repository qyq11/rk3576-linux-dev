#!/usr/bin/env python3
"""Prepare/run one root-authorized 30 s WSL whole-product resource observation.

Adapted from lis_settings_20261024/run_resource.py. Importing performs no work.
No runtime database/INI/credential values are read, hashed or archived. Only
post-exit table row counts and file byte sizes are collected from the runtime.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import secrets
import signal
import sqlite3
import subprocess
import tempfile
import time

from common import load, now, require, save, sha, verify_frozen, verify_native


def owned_group_status(pgid):
    """Probe only this invocation's new process group, never search by name."""
    result = {'owned_process_group': pgid, 'group_exists': None, 'verified_stopped': False}
    try:
        os.killpg(pgid, 0)
        result['group_exists'] = True
    except ProcessLookupError:
        result.update(group_exists=False, verified_stopped=True)
    except OSError as error:
        result['probe_error'] = repr(error)
    return result


def cleanup_owned_observer(process):
    """Bounded failure cleanup; group disappearance never supplies a child exit."""
    result = {'owned_process_group': process.pid, 'signals': [], 'observer_reaped': False,
              'actual_child_exit_code': None, 'child_exit_state': 'unknown',
              'scope': 'Only the new observer session/group and descendants inheriting it; escaped groups are not proven'}
    for value in (signal.SIGTERM, signal.SIGKILL):
        if owned_group_status(process.pid)['verified_stopped']:
            break
        row = {'signal': value.name, 'requested': False}
        try:
            os.killpg(process.pid, value)
            row['requested'] = True
        except ProcessLookupError:
            row['group_already_absent'] = True
        except OSError as error:
            row['error'] = repr(error)
        result['signals'].append(row)
        # Wait for the group as well as the direct observer. Its early death
        # must not shorten the application's TERM grace period to zero.
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            process.poll()
            if owned_group_status(process.pid)['verified_stopped']:
                break
            time.sleep(0.05)
    try:
        process.wait(timeout=0.1)
        result['observer_reaped'] = True
    except subprocess.TimeoutExpired:
        result['observer_wait_unconfirmed'] = True
    result['actual_observer_exit_code'] = process.returncode
    result['group_after_cleanup'] = owned_group_status(process.pid)
    result['group_stop_verified'] = result['group_after_cleanup']['verified_stopped']
    return result


def post_exit_runtime_counts(runtime):
    """Read schema names/count(*) only after observer reports a stopped child."""
    rows = {}
    errors = {}
    files = [path for path in runtime.rglob('*') if path.is_file()]
    for path in files:
        if path.suffix not in ('.db', '.sqlite'):
            continue
        name = path.relative_to(runtime).as_posix()
        try:
            # No header/signature or application/authentication row reads.
            database = sqlite3.connect(path.resolve().as_uri() + '?mode=ro', uri=True)
            try:
                tables = [row[0] for row in database.execute(
                    "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")]
                rows[name] = {
                    table: database.execute('SELECT count(*) FROM "' + table.replace('"', '""') + '"').fetchone()[0]
                    for table in tables
                }
            finally:
                database.close()
        except sqlite3.Error as error:
            errors[name] = str(error)
    return rows, errors, len(files), sum(path.stat().st_size for path in files)


def steady_window(samples):
    tail = [row for row in samples if 10 <= row['elapsed_seconds'] < 30]
    result = {'window': '10<=elapsed_seconds<30', 'sample_count': len(tail),
              'sample_period_seconds': 0.1, 'sample_period_target_seconds': 0.1, 'memory_unit': 'KiB',
              'window_semantics': 'Fixed observer-clock window, not proven to start after readiness; clock starts before child Popen',
              'sample_period_semantics': '0.1s target sleep plus /proc reading overhead; actual intervals vary',
              'VmHWM_semantics': 'Lifetime RSS high-water mark since process start, including possible startup peaks; not a window-only peak',
              'sampling_limit': 'RSS/PSS/thread/fd fields are read separately; transient peaks between samples may be missed'}
    if len(tail) < 2:
        return result
    span = tail[-1]['elapsed_seconds'] - tail[0]['elapsed_seconds']
    result['observed_span_seconds'] = span
    result['cpu_percent_one_logical_core'] = (
        100 * (tail[-1]['cpu_ticks'] - tail[0]['cpu_ticks']) / os.sysconf('SC_CLK_TCK') / span
        if span > 0 else None)
    for key in ('VmRSS', 'VmHWM', 'Pss', 'Threads', 'fd_count'):
        values = [row[key] for row in tail if row.get(key) is not None]
        result['min_' + key] = min(values) if values else None
        result['max_' + key] = max(values) if values else None
    result['pss_unavailable_samples'] = sum(row.get('Pss') is None for row in tail)
    return result


def audit_loopback_wire(log):
    """Check the observed workload after exit, using the actual short Qj frames."""
    pattern = (r'^STATUS_LOOPBACK_WIRE run=\s*(\d+) generation=\s*(\d+) sequence=\s*(\d+) '
               r'elapsed_ms=\s*(\d+) (TX|RX) HEX=\s*([0-9a-fA-F ]+)$')
    rows = [(int(run), int(generation), int(sequence), int(elapsed), direction, bytes.fromhex(data))
            for run, generation, sequence, elapsed, direction, data in re.findall(pattern, log, re.MULTILINE)]
    require(rows and len(rows) == log.count('STATUS_LOOPBACK_WIRE '), 'Unparsed/missing actual wire records')
    require([row[2] for row in rows] == list(range(1, len(rows) + 1)), 'Actual wire sequence has a gap')
    require(len({(row[0], row[1]) for row in rows}) == 1, 'Unexpected reconnect in the short stable workload')
    require([row[3] for row in rows] == sorted(row[3] for row in rows), 'Wire clock moved backwards')
    counts, stream_bytes = {}, {}
    expected = {'TX': b'Qj', 'RX': b'Qj[4][SIMULATED][2][1][0][0]'}
    for direction, header, tail in (('TX', b'\x02\x03', b'\x04\x05'), ('RX', b'\x06\x07', b'\x08\x09')):
        stream = b''.join(row[5] for row in rows if row[4] == direction)
        stream_bytes[direction] = len(stream)
        position, messages = 0, []
        while position < len(stream):
            require(len(stream) - position >= 4 and stream[position:position + 2] == header, 'Invalid Qj wire header')
            size = int.from_bytes(stream[position + 2:position + 4], 'big')
            require(4 < size <= 2004 and len(stream) - position >= size + 7, 'Incomplete Qj wire frame')
            packet = stream[position + 4:position + 4 + size]
            checksum = 0
            for byte in packet:
                checksum ^= byte
            require(packet[:4] == b'\x00\x00\x00\x01' and checksum == stream[position + 4 + size]
                    and stream[position + 5 + size:position + 7 + size] == tail, 'Invalid Qj fragment/checksum/tail')
            messages.append(packet[4:])
            position += size + 7
        require(messages and all(message == expected[direction] for message in messages), 'Observed Qj payload differs')
        counts[direction] = len(messages)
    require(counts['TX'] >= 20 and counts['RX'] >= 20 and 0 <= counts['TX'] - counts['RX'] <= 1,
            'The observed workload did not sustain actual Qj request/reply traffic')
    times = [row[3] for row in rows if row[4] == 'TX']
    return {'actual_worker_wire_records': len(rows), 'message_counts': counts,
            'wire_bytes_by_direction': stream_bytes, 'wire_chunk_direction': 'TX',
            'wire_chunk_counts_by_direction': {direction: sum(row[4] == direction for row in rows) for direction in ('TX', 'RX')},
            'wire_chunk_elapsed_ms': times,
            'wire_chunk_intervals_ms': [right - left for left, right in zip(times, times[1:])],
            'wire_chunk_timing_semantics': 'Worker-clock times of peer TX read blocks; one read can contain a partial frame or several frames. Not request-send or decoded-message timestamps',
            'message_count_semantics': 'message_counts comes only from decoding each complete direction stream, never from the number of read blocks',
            'strict_request_cadence_verified': False,
            'first_run_generation': list(rows[0][:2]), 'last_sequence': rows[-1][2],
            'all_frames_and_expected_payloads_verified': True,
            'shutdown_pending_request_allowance': 1,
            'scope': 'Self-owned local peer only; not an instrument response or timing-budget qualification'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True,
                        help='Root integrator explicitly confirms source/load freeze and serial execution')
    parser.add_argument('--name', default='resource_01', help='Fresh evidence attempt name; never reuse a prior result')
    args = parser.parse_args()
    require(args.root_go and os.geteuid() != 0, 'Explicit --root-go and ordinary WSL user required')
    require(re.fullmatch(r'[a-zA-Z0-9_\-]+', args.name) is not None, 'Simple evidence attempt name required')

    work = Path(__file__).resolve().parent.parent
    root = work.parents[2]
    program = root / '06_programs/sn100_host'
    build = work / 'native/wsl_release'
    freeze = work / 'native/final_before.json'
    out = work / args.name
    runtime = work / (args.name + '_runtime')
    driver = work / (args.name + '_driver')
    require(not any(path.exists() for path in (out, runtime, driver)),
            'Fresh output/runtime/driver directories required; preserve failed attempts')
    frozen = load(freeze)
    verify_native(frozen)
    require((build / 'sn100_shell').is_file() and (build / 'sn100_report_renderer').is_file(),
            'Fresh matching native main/helper required')

    # All hashes finish before launching the observer; no parallel probe/build/hash workload.
    source_before = {name: sha(program / name) for name in frozen}
    binary_before = {name: sha(build / name) for name in ('sn100_shell', 'sn100_report_renderer')}
    observer = program / 'tests/observe_linux_process.py'
    observer_before = sha(observer)
    driver_before = sha(Path(__file__))
    driver.mkdir()
    runtime.mkdir()
    save(driver / 'source_before.json', source_before)
    save(driver / 'binary_before.json', binary_before)
    bootstrap = None
    process = None
    command = []
    receipt = {'schema': 'sn100-four-windows-resource-driver-v1', 'state': 'prepared',
               'started_utc': now(), 'actual_observer_exit_code': None,
               'actual_child_exit_code': None, 'timed_out': None,
               'child_exit_state': 'unknown', 'runtime_inspection_permitted': False,
               'runtime_values_read_hashed_or_archived': False}
    save(driver / 'observer.exit.json', receipt)
    try:
        descriptor, filename = tempfile.mkstemp(prefix='.resource-bootstrap-', dir=work)
        bootstrap = Path(filename)
        try:
            # Ephemeral random initialization input: generated/written only, never read/logged/hashed.
            os.write(descriptor, secrets.token_hex(24).encode('ascii'))
        finally:
            os.close(descriptor)
        command = ['python3', '-B', str(observer), '--output-dir', str(out),
                   '--stop-after', '30', '--timeout', '50', '--', str(build / 'sn100_shell'),
                   '--offline', '--accounts', '--history', '--catalog', '--samples', '--instrument',
                   '--password-login', '--printer-settings', '--lis-settings',
                   '--quality-control', '--calibration', '--status-loopback',
                   '--seed-catalog-demo', '--seed-anonymous', '--test-secret-file', str(bootstrap),
                   '--data-dir', str(runtime), '--viewport', '1600x900']
        selected_env = {'QT_QPA_PLATFORM': 'xcb', 'QT_SCALE_FACTOR': '1',
                        'QT_AUTO_SCREEN_SCALE_FACTOR': '0'}
        save(driver / 'observer.command.json', {'command': command, 'cwd': str(root),
             'environment_overrides': selected_env, 'source_freeze': str(freeze),
             'start_new_session': True, 'outer_timeout_seconds': 65,
             'scope': 'One 30 s serial fresh anonymous workload; SIGTERM is handled by the application'})
        start = time.monotonic()
        with (driver / 'observer_driver.log').open('wb') as stream:
            try:
                process = subprocess.Popen(command, cwd=root, env=dict(os.environ, **selected_env),
                                           start_new_session=True, stdout=stream, stderr=subprocess.STDOUT)
                receipt.update(observer_pid=process.pid, owned_process_group=process.pid,
                    start_new_session=True, state='observer_running',
                    ownership_basis='Observer child Popen has no session/group override; it inherits this new observer group')
                save(driver / 'observer.process.json', {'pid': process.pid, 'owned_process_group': process.pid,
                    'start_new_session': True, 'state': 'running'})
                save(driver / 'observer.exit.json', receipt)
                process.wait(timeout=65)
                # Preserve the original observer exit before cleanup/summary parsing.
                receipt.update(state='observer_finished', actual_observer_exit_code=process.returncode,
                    timed_out=False, finished_utc=now(), elapsed_seconds=time.monotonic() - start)
                save(driver / 'observer.exit.json', receipt)
                (driver / 'observer.exit').write_text(str(process.returncode) + '\n', encoding='ascii')
                require(process.returncode == 0, 'Observer failed; child exit remains unknown until a complete matching summary')
                status = owned_group_status(process.pid)
                receipt['group_after_observer'] = status
                require(status['verified_stopped'], 'Observer exited but its owned group is not confirmed stopped')
            except BaseException as error:
                # Save the failure even if cleanup itself cannot be confirmed.
                receipt.update(state='driver_timeout' if isinstance(error, subprocess.TimeoutExpired) else 'driver_error',
                    driver_failed=True, timed_out=isinstance(error, subprocess.TimeoutExpired), exception_type=type(error).__name__,
                    exception=str(error), finished_utc=now(), elapsed_seconds=time.monotonic() - start,
                    failure_detected_utc=now(), failure_detected_elapsed_seconds=time.monotonic() - start,
                    actual_observer_exit_code=process.returncode if process is not None else None,
                    actual_child_exit_code=None, child_exit_state='unknown', runtime_inspection_permitted=False,
                    child_exit_basis='No complete matching observer summary was accepted; signals/group disappearance do not supply child exit')
                save(driver / 'observer.exit.json', receipt)
                if process is not None:
                    try:
                        receipt['cleanup'] = cleanup_owned_observer(process)
                    except BaseException as cleanup_error:
                        receipt['cleanup_error'] = repr(cleanup_error)
                    receipt['actual_observer_exit_code'] = process.returncode
                    receipt['group_after_failure'] = owned_group_status(process.pid)
                receipt['cleanup_stop_verified'] = bool(receipt.get('group_after_failure', {}).get('verified_stopped'))
                receipt.update(finished_utc=now(), elapsed_seconds=time.monotonic() - start)
                receipt['failure_scope'] = ('Failed invocation; child actual exit unknown, runtime not inspected. '
                    'Unverified cleanup requires root attention before another observation.')
                save(driver / 'observer.exit.json', receipt)
                (driver / 'observer.exit').write_text(
                    'unknown\n' if receipt['actual_observer_exit_code'] is None else str(receipt['actual_observer_exit_code']) + '\n', encoding='ascii')
                save(driver / 'observer.process.json', {'pid': process.pid if process is not None else None,
                    'owned_process_group': process.pid if process is not None else None,
                    'state': 'stopped' if receipt['cleanup_stop_verified'] else 'stop_unconfirmed',
                    'actual_observer_exit_code': receipt['actual_observer_exit_code'], 'actual_child_exit_code': None})
                raise
        save(driver / 'observer.exit.json', receipt)
        save(driver / 'observer.process.json', {'pid': process.pid, 'owned_process_group': process.pid,
            'state': 'observer_reaped_group_stopped', 'actual_observer_exit_code': process.returncode,
            'actual_child_exit_code': None})
    finally:
        if bootstrap is not None:
            bootstrap.unlink(missing_ok=True)

    try:
        require(out.exists(), 'Observer did not create its output; original exit/command already retained')
        observed = load(out / 'summary.json')
        require(observed.get('command') == command[command.index('--') + 1:] and
                type(observed.get('exit_code')) is int and type(observed.get('pid')) is int and observed['pid'] > 0,
                'Missing/mismatched actual observer child receipt; runtime inspection prohibited')
        receipt.update(actual_child_exit_code=observed['exit_code'],
                       timed_out=observed.get('timed_out'), child_pid=observed['pid'],
                       ordinary_sigterm_requested=observed.get('ordinary_sigterm_requested'),
                       child_exit_state='reaped_by_observer', child_exit_basis='Complete matching observer summary written after process.wait')
        save(driver / 'observer.exit.json', receipt)
        require(process.returncode == 0 and observed['exit_code'] == 0 and observed.get('timed_out') is False and
                observed.get('ordinary_sigterm_requested') is True and receipt['group_after_observer']['verified_stopped'],
                'Ordinary observer/child exit0 and stopped group are required before runtime inspection')
    except BaseException as error:
        receipt.update(state='child_receipt_error', driver_failed=True, exception_type=type(error).__name__,
                       exception=str(error), runtime_inspection_permitted=False)
        save(driver / 'observer.exit.json', receipt)
        raise
    save(driver / 'observer.process.json', {'pid': process.pid, 'owned_process_group': process.pid,
        'state': 'observer_and_child_reaped_group_stopped', 'actual_observer_exit_code': process.returncode,
        'child_pid': observed['pid'], 'actual_child_exit_code': observed['exit_code']})
    log = (out / 'process.log').read_text(encoding='utf-8')
    samples = load(out / 'samples.json')
    steady = steady_window(samples)
    sql_closed = ['HOSPITAL_STORE_CLOSED', 'ACCOUNT_STORE_CLOSED', 'HISTORY_STORE_CLOSED',
                  'PROJECT_STORE_CLOSED', 'REAGENT_STORE_CLOSED', 'SAMPLE_STORE_CLOSED',
                  'MAINTENANCE_STORE_CLOSED', 'QUALITY_CONTROL_STORE_CLOSED', 'CALIBRATION_STORE_CLOSED']
    ini_closed = ['PRINTER_SETTINGS_CLOSED', 'LIS_SETTINGS_CLOSED']
    ready = ['SHELL_READY', 'PRINTER_SETTINGS_READY', 'LIS_SETTINGS_READY',
             'QUALITY_CONTROL_READY', 'CALIBRATION_READY', 'HISTORY_INITIAL_QUERY_READY',
             'INSTRUMENT_INITIAL_MONTH_READY']

    # The observer reaped its child before returning summary; never inspect an unknown/live runtime.
    child_stopped = type(observed.get('exit_code')) is int and receipt['group_after_observer']['verified_stopped']
    require(child_stopped, 'No actual stopped-child receipt; runtime row counts are prohibited')
    receipt['runtime_inspection_permitted'] = True
    save(driver / 'observer.exit.json', receipt)
    rows, count_errors, file_count, runtime_bytes = post_exit_runtime_counts(runtime)
    # Full-set/native receipt checks happen only after the observer reaped the
    # child, never inside the measured interval.
    verify_native(frozen)
    source_after = {name: sha(program / name) for name in frozen}
    binary_after = {name: sha(build / name) for name in binary_before}
    save(driver / 'source_after.json', source_after)
    save(driver / 'binary_after.json', binary_after)
    gui_tx_chunks = [int(value) for value in re.findall(r'^STATUS_LOOPBACK_TX\s+(\d+)\s', log, re.MULTILINE)]
    wire = audit_loopback_wire(log)
    checks = {'observer_exit0': process.returncode == 0,
              'ordinary_child_exit0': observed.get('exit_code') == 0 and 'EXIT code= 0' in log,
              'ordinary_sigterm_requested': observed.get('ordinary_sigterm_requested') is True,
              'observer_not_timed_out': observed.get('timed_out') is False,
              'all_nine_sql_closures_once': all(log.count(marker) == 1 for marker in sql_closed),
              'both_ini_closures_once': all(log.count(marker) == 1 for marker in ini_closed),
              'all_ready_once': all(log.count(marker) == 1 for marker in ready),
              'threads_stopped': 'THREADS_STOPPED true' in log,
              'loopback_started_once': log.count('STATUS_LOOPBACK_STARTED self_owned_peer=127.0.0.1 instrument=false') == 1,
              'loopback_stopped': 'STATUS_LOOPBACK_STOPPED true' in log,
              'actual_qj_workload_verified': wire['all_frames_and_expected_payloads_verified'],
              'guest_entry': 'PASSWORD_LOGIN_ENABLED; default Guest;' in log and '--test-session' not in command,
              'bootstrap_unlinked': bootstrap is not None and not bootstrap.exists(),
              'source_matches_freeze': source_before == frozen,
              'source_unchanged': source_before == source_after,
              'binaries_unchanged': binary_before == binary_after,
              'observer_unchanged': observer_before == sha(observer),
              'driver_unchanged': driver_before == sha(Path(__file__)),
              'steady_window_samples': steady['sample_count'] > 100,
              'runtime_counts_readable': not count_errors}
    info = {'schema': 'sn100-four-windows-resource-v1', 'checks': checks,
            'actual_observer_exit_code': process.returncode,
            'actual_child_exit_code': observed.get('exit_code'),
            'source_input_count': len(frozen), 'binary_sha256_before': binary_before,
            'binary_sha256_after': binary_after, 'source_sha256_before': source_before,
            'source_sha256_after': source_after, 'observer_sha256': observer_before,
            'driver_sha256': driver_before, 'steady': steady,
            'compatibility_field_semantics': {'steady': 'Fixed observer-clock10<=t<30 window; no ready-time alignment established',
                'checks.steady_window_samples': 'Only the original fixed-window sample-count gate, not proof of readiness before10s'},
            'environment': {'host': platform.platform(), 'architecture': platform.machine(),
                            'logical_cpu_count': os.cpu_count(), 'clock_ticks_per_second': os.sysconf('SC_CLK_TCK'),
                            'qt_qpa_platform': 'xcb', 'viewport': '1600x900', 'scale_factor': '1',
                            'build': 'Release', 'display': os.environ.get('DISPLAY'),
                            'wayland_display': os.environ.get('WAYLAND_DISPLAY')},
            'load': 'Nine anonymous SQL stores plus two local printer/LIS INI workers, default Guest/password entry, '
                    'nominal1 Hz configured self-owned local Qj (strict request cadence not verified), original instrument page7, '
                    'QC/calibration initialized, fresh explicit anonymous catalog seed',
            'gui_tx_wire_chunk_count': len(gui_tx_chunks),
            'gui_tx_wire_chunk_elapsed_ms': gui_tx_chunks,
            'gui_tx_wire_chunk_intervals_ms': [right - left for left, right in zip(gui_tx_chunks, gui_tx_chunks[1:])],
            'gui_tx_wire_chunk_semantics': 'GUI-forwarded wire events retain worker elapsedMs for peer read blocks; not GUI delivery times or decoded-message/request counts. Use wire_audit.message_counts for message quantities',
            'wire_audit': wire,
            'shutdown_marker_counts': {marker: log.count(marker) for marker in sql_closed + ini_closed},
            'ready_marker_counts': {marker: log.count(marker) for marker in ready},
            'database_row_counts': rows, 'database_count_errors': count_errors,
            'runtime_initial_file_count': 0, 'runtime_initial_bytes': 0,
            'runtime_file_count': file_count, 'runtime_bytes': runtime_bytes,
            'runtime_growth_scope': 'New empty anonymous runtime to post-exit total; includes initialization and the 30 s workload, not a measured 10-30 s storage slope',
            'runtime_or_credentials_archived': False,
            'limits': 'Single 30 s environment-specific whole-product baseline after every build/GUI/hash workload stopped. '
                      '10<=t<30 fixed observer-clock window; readiness before10s was not timed or established. '
                      'VmHWM is lifetime RSS high-water mark, not a window-only peak; 0.1s is a sleep target plus reading overhead. '
                      'No comparison against the different371-input product as a feature increment. '
                      'CPU/memory/response budgets, same-board RT, RK/instrument and long stability remain unknown/unverified. '
                      'No print job, LIS endpoint, serial/device open or physical action is authorized by this observation.'}
    save(out / 'verification.json', info)
    print(json.dumps({'checks': checks, 'steady': steady,
                      'actual_observer_exit_code': process.returncode,
                      'actual_child_exit_code': observed.get('exit_code')}, ensure_ascii=False), flush=True)
    require(all(checks.values()), 'Resource observation failed; raw process samples/commands/exits preserved')


if __name__ == '__main__':
    main()
