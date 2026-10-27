"""Shared host-only evidence helpers. Importing this file performs no work."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import runpy
import signal
import subprocess
import time

WORK = Path(__file__).resolve().parent.parent
ROOT = WORK.parents[2]
PROGRAM = ROOT / '06_programs/sn100_host'
NATIVE = WORK / 'native'
FREEZE = NATIVE / 'final_before.json'
DRIVERS = WORK / 'cumulative_build'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def save(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + '.writing')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def verify_frozen(frozen):
    require(isinstance(frozen, dict) and len(frozen) == 447 and sha(FREEZE) == '1486686b182dc8d1da55c52b45c91ea72f1b8c41677676cedf16d72b3fc4f401',
            'Exact completed447 freeze required; old426/432 evidence is invalid here')
    for name in ('quality_control/qc_store.cpp', 'quality_control/quality_control_page.cpp',
                 'calibration/calibration_store.cpp', 'calibration/calibration_page.cpp'):
        require(name in frozen, 'Current four-window source absent from freeze: ' + name)
    # Use the formal inventory itself: merely checking old keys would miss a
    # source/header added after freeze. run_name deliberately avoids its main.
    inventory = runpy.run_path(str(PROGRAM / 'shell/package_arm64.py'),
                              run_name='sn100_fingerprint_inventory')
    current = inventory['snapshot'](PROGRAM)
    require(set(current) == set(frozen), 'Formal source input set changed after freeze')
    require(current == frozen, 'Formal source SHA changed after freeze')


def verify_native(frozen):
    verify_preparation()
    verify_frozen(frozen)
    report = load(NATIVE / 'verification.json')
    require(len(report.get('fresh_objects', {})) == 210, 'Exactly 210 fresh native objects required')
    require(report.get('passed') is True and report.get('actual_exit') == 0,
            'Completed successful native Release receipt required')
    require(report.get('source_inputs') == len(frozen) and report.get('snapshot_sha256') == sha(FREEZE),
            'Native receipt is not bound to this exact source freeze')
    build = NATIVE / 'wsl_release'
    require(report.get('makefile_sha256') == sha(build / 'Makefile'), 'Native Makefile changed after build')
    for name in ('sn100_shell', 'sn100_report_renderer'):
        require(report['artifacts'].get(name) == sha(build / name), 'Native artifact changed: ' + name)
    require(report.get('fresh_objects'), 'Native fresh-object inventory missing')
    for name, row in report['fresh_objects'].items():
        require(row.get('created_after_start') is True and sha(NATIVE / name) == row['sha256'],
                'Native object changed: ' + name)
    return report


def stop_owned_group(process):
    result = {'owned_process_group': process.pid, 'signals': []}
    for sig in (signal.SIGTERM, signal.SIGKILL):
        if group_state(process.pid)['gone']:
            break
        try:
            os.killpg(process.pid, sig)
            result['signals'].append(sig.name)
        except ProcessLookupError:
            break
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            process.poll()
            if group_state(process.pid)['gone']:
                break
            time.sleep(.05)
    process.wait(timeout=5)
    result.update(actual_exit=process.returncode, group_after=group_state(process.pid))
    return result


def run_logged(name, command, output, env=None, timeout=None):
    output = Path(output)
    record = {'name': name, 'command': list(map(str, command)), 'cwd': str(ROOT),
              'started_utc': now(), 'stdout_stderr': name + '.log'}
    save(output / (name + '.command.json'), record)
    started = time.monotonic()
    process = None
    failure = None
    timed_out = False
    cleanup = None
    try:
        with (output / (name + '.log')).open('wb') as stream:
            process = spawn_owned(record['command'], cwd=ROOT, env=env, start_new_session=True,
                                       stdout=stream, stderr=subprocess.STDOUT)
            save(output / (name + '.process.json'), {'pid': process.pid, 'state': 'running'})
            process.wait(timeout=timeout)
    except BaseException as error:
        failure = error
        timed_out = isinstance(error, subprocess.TimeoutExpired)
        if process is not None:
            cleanup = stop_owned_group(process)
    code = process.returncode if process is not None else None
    record.update(exit_code=code, timed_out=timed_out, state='not_started' if process is None else 'finished',
                  seconds=time.monotonic() - started, finished_utc=now(),
                  pid=process.pid if process is not None else None, cleanup=cleanup)
    if failure:
        record.update(exception_type=type(failure).__name__, exception=str(failure))
    # Save the original child exit before any parser/hash/verification operation.
    save(output / (name + '.exit.json'), record)
    (output / (name + '.exit')).write_text(str(code) + '\n', encoding='ascii')
    save(output / (name + '.process.json'), {'pid': record['pid'], 'state': record['state'], 'exit_code': code})
    if failure:
        raise failure
    require(code == 0 and not timed_out,
            name + ' failed; original outputs retained in ' + str(output))
    return record

def group_state(pgid):
    result = {'pgid': pgid, 'gone': False}
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        result['gone'] = True
    except OSError as error:
        result['error'] = repr(error)
    return result


def process_identity(pid):
    result = {'pid': pid, 'pgid': pid, 'start_ticks': None}
    try:
        stat = (Path('/proc') / str(pid) / 'stat').read_text().rsplit(')', 1)[1].split()
        result.update(pgid=int(stat[2]), start_ticks=int(stat[19]))
    except (FileNotFoundError, ProcessLookupError):
        result['leader_already_exited'] = True
    return result


def spawn_owned(*args, **kwargs):
    require(kwargs.get('start_new_session') is True, 'Every tracked phase must create its own session')
    ledger = Path(os.environ['SN100_POST_LEDGER'])
    require(ledger.is_absolute() and ledger.is_file() and ledger.parent.parent.resolve() == WORK.resolve(),
            'Controller-created fresh ownership ledger required')
    process = subprocess.Popen(*args, **kwargs)
    records = load(ledger)
    row = process_identity(process.pid)
    row.update(started_utc=now(), start_new_session=True, originating_driver_pid=os.getpid(),
               command=list(map(str, args[0])))
    records.append(row)
    save(ledger, records)
    return process


def cleanup_recorded_groups(ledger, clean_live):
    records = load(ledger) if Path(ledger).is_file() else []
    results, signals_used = [], False
    for original in records:
        state = dict(original)
        state['before'] = group_state(original['pgid'])
        if not state['before']['gone'] and clean_live:
            current = process_identity(original['pid'])
            # Never signal a reused PID or an unidentifiable leader. Surface it to root.
            if original['start_ticks'] is not None and current.get('start_ticks') == original['start_ticks']:
                state['signals'] = []
                for sig in (signal.SIGTERM, signal.SIGKILL):
                    if group_state(original['pgid'])['gone']:
                        break
                    try:
                        os.killpg(original['pgid'], sig)
                        state['signals'].append(sig.name)
                        signals_used = True
                    except ProcessLookupError:
                        break
                    deadline = time.monotonic() + 3
                    while time.monotonic() < deadline and not group_state(original['pgid'])['gone']:
                        time.sleep(.05)
            else:
                state['cleanup_refused'] = 'Leader gone/identity changed; surviving group requires root attention'
        state['after'] = group_state(original['pgid'])
        results.append(state)
    return {'groups': results, 'all_gone': all(row['after']['gone'] for row in results),
            'signals_used': signals_used, 'child_exit_codes_inferred_from_cleanup': False}


def cleanup_ephemeral_inputs(ledger, remove_if_found):
    results = []
    for row in load(ledger):
        command = row.get('command', [])
        if '--test-secret-file' not in command:
            continue
        path = Path(command[command.index('--test-secret-file') + 1])
        allowed = path.is_absolute() and path.resolve().is_relative_to(WORK.resolve()) and (
            path.name.startswith('.catalog-owner-bootstrap-') or path.name.startswith('.resource-bootstrap-'))
        result = {'path': str(path), 'allowed_owned_ephemeral_path': allowed, 'removed': False,
                  'value_read_hashed_or_archived': False}
        if allowed and not path.is_symlink() and path.exists() and remove_if_found:
            path.unlink()
            result['removed'] = True
        result['absent_after'] = not path.exists() and not path.is_symlink()
        results.append(result)
    return {'inputs': results, 'all_absent': all(row['allowed_owned_ephemeral_path'] and row['absent_after'] for row in results)}


def clean_environment(runtime, qpa):
    env = dict(os.environ)
    removed = []
    for key in tuple(env):
        if key.startswith(('SN100_', 'QT_', 'QMAKE', 'QML', 'CCACHE')) or key in (
                'MAKEFLAGS', 'MFLAGS', 'GNUMAKEFLAGS', 'MAKELEVEL', 'CC', 'CXX', 'LD', 'AR',
                'LD_PRELOAD', 'LD_LIBRARY_PATH', 'LIBRARY_PATH', 'CPATH', 'CPLUS_INCLUDE_PATH',
                'C_INCLUDE_PATH', 'COMPILER_PATH', 'GCC_EXEC_PREFIX'):
            removed.append(key)
            del env[key]
    overrides = {'QT_QPA_PLATFORM': qpa, 'QT_SCALE_FACTOR': '1', 'QT_AUTO_SCREEN_SCALE_FACTOR': '0'}
    for key, name in (('XDG_RUNTIME_DIR', 'xdg'), ('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                      ('XDG_CACHE_HOME', 'cache'), ('TMPDIR', 'tmp')):
        path = Path(runtime) / name
        path.mkdir(mode=0o700)
        overrides[key] = str(path)
    overrides['TMP'] = overrides['TEMP'] = overrides['TMPDIR']
    env.update(overrides)
    return env, {'overrides': overrides, 'removed_inherited_variable_names': sorted(removed)}


def verify_preparation():
    plan = load(Path(__file__).with_name('preparation.json'))
    for path, digest in plan['executed_inputs_sha256'].items():
        require(sha(path) == digest, 'Prepared post-platform driver/input changed: ' + path)
    require(sha(NATIVE / 'verification.json') == plan['native_verification_sha256'], 'Native receipt changed')
    return plan
