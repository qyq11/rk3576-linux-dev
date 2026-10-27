#!/usr/bin/env python3
"""Future exclusive root-go only: fresh QtCore/QtNetwork three-arm peer gate experiment."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
import xml.etree.ElementTree as ET


CASES = ('baseline', 'peer_read_gate', 'peer_reply_gate')
QUERY = bytes.fromhex('0203000600000001516b3b0405')
REPLY = bytes.fromhex('0607000900000001516b5b305d0d0809')
PRIVATE = 'temp/sn100_host/offline_followup_20261026/protocol_peer_gate/'
OFFICIAL = tuple('06_programs/sn100_host/protocol/' + stem + suffix
                 for stem in ('read_only_client', 'tcp_transport', 'read_only_messages', 'frame_codec', 'status_session')
                 for suffix in ('.cpp', '.h'))
REQUIRED = set(OFFICIAL) | {PRIVATE + name for name in (
    'protocol_peer_gate_test.cpp', 'protocol_peer_gate_test.pro', 'run_peer_gate.py', 'experiment_plan.json')}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    # Every stage uses a new directory. Never overwrite an earlier receipt or failure.
    with Path(path).open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def group_alive(pgid):
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return None


def stop_owned_group(process):
    receipt = {'owned_pgid': process.pid, 'term_requested': False, 'kill_requested': False}

    def wait_gone(seconds):
        deadline = time.monotonic() + seconds
        while True:
            process.poll()
            alive = group_alive(process.pid)
            if alive is not True or time.monotonic() >= deadline:
                return alive
            time.sleep(0.05)

    alive = group_alive(process.pid)
    for sig, seconds, key in ((signal.SIGTERM, 3, 'term'), (signal.SIGKILL, 2, 'kill')):
        if alive is not True:
            break
        try:
            os.killpg(process.pid, sig)
            receipt[key + '_requested'] = True
        except ProcessLookupError:
            pass
        except OSError as error:
            receipt[key + '_error'] = repr(error)
        alive = wait_gone(seconds)
    receipt.update(group_alive_after_cleanup=alive, group_stopped_confirmed=alive is False,
                   direct_exit_after_cleanup=process.poll())
    return receipt


def execute(name, argv, output, cwd, timeout, explicit_env=None):
    receipt = {'argv': list(map(str, argv)), 'cwd': str(cwd), 'started_utc': now(),
               'timeout_seconds': timeout, 'explicit_environment': explicit_env or {}}
    save(output / (name + '.command.json'), receipt)
    process = None
    failure = None
    cleanup = None
    direct_exit = None
    group_stopped = None
    began = time.monotonic()
    environment = dict(os.environ, **(explicit_env or {}))
    try:
        with (output / (name + '.log')).open('xb') as log:
            process = subprocess.Popen(receipt['argv'], cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                                       env=environment, start_new_session=True)
            save(output / (name + '.process.json'), {'pid': process.pid, 'owned_pgid': process.pid,
                                                   'start_new_session': True})
            process.wait(timeout=timeout)
    except BaseException as error:
        failure = error
    finally:
        if process is not None:
            direct_exit = process.poll()
            alive = group_alive(process.pid)
            group_stopped = alive is False
            if alive is not False:
                try:
                    cleanup = stop_owned_group(process)
                    group_stopped = cleanup['group_stopped_confirmed']
                except BaseException as error:
                    cleanup = {'owned_pgid': process.pid, 'group_stopped_confirmed': False,
                               'cleanup_exception': repr(error)}
                    group_stopped = False
                if failure is None:
                    failure = RuntimeError('Owned process group survived direct exit or its state was unknown')
        code = process.poll() if process is not None else None
        receipt.update(actual_exit_code=code, direct_exit_before_cleanup=direct_exit,
                       timed_out=isinstance(failure, subprocess.TimeoutExpired),
                       exception_type=type(failure).__name__ if failure else None,
                       exception=str(failure) if failure else None,
                       cleanup=cleanup, owned_group_stopped=group_stopped,
                       seconds=time.monotonic() - began, finished_utc=now())
        save(output / (name + '.exit.json'), receipt)
        with (output / (name + '.exit')).open('x', encoding='ascii') as stream:
            stream.write(str(code) + '\n')
    if failure is not None:
        raise failure
    require(code == 0 and group_stopped is True, name + ' failed; original command/exit/log retained')
    return receipt


def validate_case(path, name):
    value = json.loads(path.read_text(encoding='utf-8'))
    negative = name != 'baseline'
    require(value.get('schema') == 'sn100-protocol-peer-gate-case-v1' and value.get('case') == name,
            'Missing/wrong case raw evidence: ' + name)
    expected = {'timeout_ms': 120, 'max_retries': 1, 'case_guard_ms': 3000, 'postmortem_ms': 250,
                'capture_capacity': 2048, 'attempts': 2, 'finished': 1, 'observations': 1,
                'peer_frames': 2, 'reply_count': 1, 'transport_accepted_bytes': 26,
                'transport_bytesWritten_progress': 26, 'peer_reply_bytesWritten_progress': 16,
                'expected_failure': negative, 'request_success': not negative,
                'capture_overflow': False, 'guard_expired': False, 'error': ''}
    require(all(value.get(key) == expected_value for key, expected_value in expected.items()),
            'Unexpected observed terminal/count/contract: ' + name)
    if negative:
        require(value.get('terminal_detail', '').startswith('client.timeout:'),
                'Negative arm must preserve the real timeout failure, not cancellation or late success')
    checks = value.get('checks')
    require(isinstance(checks, dict) and len(checks) >= 20 and all(item is True for item in checks.values()),
            'One or more actual C++ invariants failed: ' + name)
    snapshot = value.get('terminal_snapshot', {})
    require(snapshot == {'peer_qt_buffered_bytes': 13 if name == 'peer_read_gate' else 0,
                         'peer_decoded_frames': 1 if name == 'peer_read_gate' else 2,
                         'observations': 0 if negative else 1, 'replies_written': 0 if negative else 1,
                         'client_pending': False}, 'Terminal peer buffer/consumption snapshot mismatch: ' + name)
    cleanup = value.get('cleanup_snapshot', {})
    require(cleanup.get('transport_connected') is False and cleanup.get('transport_pending_bytes') == 0
            and cleanup.get('client_pending') is False and cleanup.get('client_socket_state') == 0
            and cleanup.get('peer_socket_state') == 0, 'Actual close/abort state not clean: ' + name)
    events = value.get('events')
    require(isinstance(events, list) and 0 < len(events) <= 2048, 'Missing/bounded timing raw events')
    require([row.get('seq') for row in events] == list(range(1, len(events) + 1)), 'Raw sequence gap')
    times = [row.get('collector_ns') for row in events]
    require(all(isinstance(item, (int, float)) and item >= 0 for item in times) and times == sorted(times),
            'Collector times missing/non-monotone')

    def rows(stage):
        return [row for row in events if row.get('stage') == stage]

    def only(stage):
        actual = rows(stage)
        require(len(actual) == 1, 'Expected exactly one ' + stage + ': ' + name)
        return actual[0]

    def before(left, right):
        require(left['seq'] < right['seq'] and left['collector_ns'] <= right['collector_ns'],
                'Stage order differs: ' + left['stage'] + ' / ' + right['stage'] + ': ' + name)

    def wire(stage):
        return b''.join(bytes.fromhex(row.get('hex', '')) for row in rows(stage))

    require(wire('client.wire.queued') == QUERY + QUERY and wire('peer.read.actual') == QUERY + QUERY,
            'Actual queued/consumed query bytes differ from independent literals')
    require(wire('peer.reply.write.begin') == REPLY and wire('client.wire.read') == REPLY,
            'Actual peer reply/client read bytes differ from independent literal')
    frames = rows('peer.frame.decoded')
    require(len(frames) == 2 and all(bytes.fromhex(row.get('hex', '')) == b'Qk' for row in frames),
            'Decoded frame count is not two; chunk count is not substituted')
    require([row.get('value') for row in rows('client.query.queued')] == [1, 2], 'Unexpected retries')
    writes = rows('transport.write.returned')
    require(writes and all(0 <= row['value'] <= row['extra'] for row in writes)
            and sum(row['value'] for row in writes) == 26, 'Actual socket accepted bytes mismatch')
    require(sum(row['value'] for row in rows('client.socket.progress.not_peer_ack')) == 26
            and sum(row['value'] for row in rows('peer.bytesWritten.not_client_ack')) == 16,
            'Local bytesWritten progress mismatch; it is not peer/TCP ACK')
    initial = only('client.deadline.initial')
    retry = only('client.deadline.retry')
    terminal = only('client.request.failure' if negative else 'client.request.success')
    require(not rows('client.request.success' if negative else 'client.request.failure'),
            'Failure/success terminal was changed or duplicated')
    require(initial['value'] == 1 and retry['value'] == 2 and terminal['value'] == 2
            and initial['generation'] == retry['generation'] == terminal['generation']
            and initial['revision'] == retry['revision'] == terminal['revision'], 'Request revision/attempt association changed')
    before(initial, retry)
    before(retry, terminal)
    if negative:
        require(terminal['source_elapsed_ns'] // 1000000 >= retry['extra'],
                'Original failure occurred before its client-clock retry deadline')
    latch = only('driver.verdict.latched')
    observed = only('client.observed')
    before(terminal, latch)
    before(only('peer.first_reply.withheld'), retry)
    before(retry, frames[1])
    if negative:
        release = only('peer.gate.release')
        before(latch, release)
        before(release, only('peer.reply.write.begin'))
        before(release, observed)
        if name == 'peer_read_gate':
            held = rows('peer.read.gate.held')
            require(held and all(row['value'] > 0 for row in held), 'No actual Qt buffered bytes at read gate')
            before(held[0], latch)
            before(release, frames[1])
        else:
            before(frames[1], only('peer.reply.gate.held'))
            before(only('peer.reply.gate.held'), latch)
    else:
        before(frames[1], only('peer.reply.write.begin'))
        before(observed, terminal)
        require(not rows('peer.gate.release') and not rows('peer.read.gate.held')
                and not rows('peer.reply.gate.held'), 'Baseline unexpectedly used a gate')
    post_begin = only('driver.postmortem.begin')
    post_end = only('driver.postmortem.end')
    before(latch, post_begin)
    before(post_begin, post_end)
    duration = post_end['collector_ns'] - post_begin['collector_ns']
    require(duration >= 250000000 and value.get('postmortem_observed_ns') == duration,
            'Incomplete 250ms observation after immutable terminal')
    before(observed, post_end)
    before(post_end, only('driver.cleanup.begin'))
    before(only('driver.client.close.begin'), only('driver.client.close.end'))
    before(only('driver.client.close.end'), only('driver.peer.abort.begin'))
    before(only('driver.peer.abort.begin'), only('driver.peer.abort.end'))
    before(only('driver.peer.abort.end'), only('driver.cleanup.end'))
    require(only('driver.request.begin')['seq'] < latch['seq']
            and all(row['seq'] < latch['seq'] for row in rows('client.query.queued')),
            'New logical request/retry after terminal latch')
    return {'case': name, 'expected_failure': negative, 'actual_request_success': value['request_success'],
            'actual_request_finished': value['finished'], 'events': len(events), 'raw_sha256': sha(path),
            'independent_raw_order_and_byte_checks': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--jobs', type=int, choices=(1,), default=1)
    parser.add_argument('--qmake', default='/usr/lib/qt5/bin/qmake')
    parser.add_argument('--make', default='make')
    args = parser.parse_args()
    if not args.root_go or os.name != 'posix' or os.geteuid() == 0:
        parser.error('Use explicit go from an ordinary Linux user; no device/GUI runner')
    source = Path(__file__).resolve().parent
    repo = source.parents[3]
    require(source == repo / PRIVATE.rstrip('/'), 'Unexpected private source directory')
    output = args.output_dir
    if (not output.is_absolute() or output.exists() or output.is_symlink() or not output.parent.is_dir()
            or not output.resolve().is_relative_to(source)):
        parser.error('Use a new absolute run directory inside protocol_peer_gate with an existing parent')
    preparation_path = source / 'preparation.json'
    preparation_bytes = preparation_path.read_bytes()
    preparation_sha = hashlib.sha256(preparation_bytes).hexdigest()
    preparation = json.loads(preparation_bytes.decode('utf-8'))
    require(preparation.get('schema') == 'sn100-protocol-peer-gate-preparation-v1'
            and set(preparation.get('source_inputs', {})) == REQUIRED
            and preparation.get('cases') == list(CASES), 'Preparation input roster/cases differ')
    before = {}
    for relative in sorted(REQUIRED):
        path = repo / relative
        require(path.is_file() and not path.is_symlink() and path.resolve(strict=True).is_relative_to(repo),
                'Input must be an ordinary in-workspace file: ' + relative)
        before[relative] = sha(path)
        require(before[relative] == preparation['source_inputs'][relative], 'Prepared source changed: ' + relative)
    output.mkdir()
    build = output / 'build'
    evidence = output / 'evidence'
    build.mkdir()
    evidence.mkdir()
    save(output / 'inputs_before.json', before)
    (output / 'preparation_executed.json').write_bytes(preparation_bytes)
    for relative, expected in before.items():
        destination = output / 'executed_sources' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((repo / relative).read_bytes())
        require(sha(destination) == expected, 'Executed source snapshot changed: ' + relative)
    passed = False
    stages = {}
    cases = []
    failure = None
    try:
        stages['qmake'] = execute('qmake', [args.qmake, str(source / 'protocol_peer_gate_test.pro'),
                                           'CONFIG+=release', 'CONFIG-=debug'], output, build, 60)
        stages['make'] = execute('make', [args.make, '-j1'], output, build, 180)
        binary = build / 'sn100_protocol_peer_gate_test'
        require(binary.is_file() and not binary.is_symlink(), 'Fresh test executable missing; no old-binary fallback')
        save(output / 'artifact.json', {'path': str(binary), 'sha256': sha(binary), 'bytes': binary.stat().st_size})
        stages['test'] = execute('test', [str(binary), '-o', str(output / 'qt.txt') + ',txt',
                                         '-o', str(output / 'qt.xml') + ',junitxml'], output, build, 12,
                                 {'SN100_PROTOCOL_PEER_GATE_OUTPUT': str(evidence)})
        text = (output / 'qt.txt').read_text(encoding='utf-8')
        totals = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted', text)
        require(totals == [('5', '0', '0', '0')], 'QtTest must contain exactly three rows plus init/cleanup, no failure/skip/blacklist')
        tags = re.findall(r'PASS\s*:\s*ProtocolPeerGateTest::peerGates\(([^)]+)\)', text)
        require(tags == list(CASES), 'Each of the three arms must execute exactly once in order')
        xml = ET.parse(output / 'qt.xml').getroot()
        # Qt5 JUnit groups data rows into their method. Text above proves all three rows separately.
        grouped = list(xml.findall('testcase'))
        require(xml.tag == 'testsuite' and xml.get('name') == 'ProtocolPeerGateTest'
                and xml.get('tests') == '3' and xml.get('errors') == '0' and xml.get('failures') == '0'
                and xml.get('skipped', '0') == '0'
                and [row.get('name') for row in grouped] == ['initTestCase', 'peerGates', 'cleanupTestCase']
                and all(row.get('result') == 'pass' for row in grouped)
                and len(list(xml.iter('testcase'))) == 3
                and not any(list(xml.iter(tag)) for tag in ('failure', 'error', 'skipped')),
                'Raw Qt5 grouped JUnit has missing methods/non-pass/failure/error/skip')
        require({path.name for path in evidence.iterdir()} == {name + '.json' for name in CASES},
                'Case evidence roster differs')
        cases = [validate_case(evidence / (name + '.json'), name) for name in CASES]
        save(output / 'case_validation.json', cases)
        passed = True
    except BaseException as error:
        failure = error
        raise
    finally:
        after = {relative: sha(repo / relative) if (repo / relative).is_file() else None for relative in before}
        preparation_after_sha = sha(preparation_path) if preparation_path.is_file() else None
        unchanged = before == after and preparation_sha == preparation_after_sha
        save(output / 'inputs_after.json', after)
        save(output / 'verification.json', {
            'schema': 'sn100-protocol-peer-gate-run-v1', 'passed': passed and unchanged,
            'source_before_after_equal': before == after,
            'preparation_before_after_equal': preparation_sha == preparation_after_sha,
            'preparation_sha256': preparation_sha, 'preparation_after_sha256': preparation_after_sha,
            'stages_completed': list(stages), 'case_results': cases,
            'case_count': len(cases), 'qt_test_totals_expected': [5, 0, 0],
            'exception_type': type(failure).__name__ if failure else None,
            'exception': str(failure) if failure else None,
            'production_hook_added': False, 'production_source_modified_by_driver': False,
            'old8h_direct_cause_confirmed': False, 'old8h_fixed_claimed': False,
            'scope': 'One synthetic loopback request per arm, three arms once; expected negative terminals remain false',
            'limit': 'No TCP ACK/online transaction ID, real device, GUI, long stability or resource qualification'})
    require(unchanged, 'Inputs/preparation changed during the private experiment')


if __name__ == '__main__':
    main()
