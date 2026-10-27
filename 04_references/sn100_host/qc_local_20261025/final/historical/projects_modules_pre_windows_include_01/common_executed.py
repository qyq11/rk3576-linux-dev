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
    require(isinstance(frozen, dict) and frozen, 'Nonempty formal source freeze required')
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
    verify_frozen(frozen)
    report = load(NATIVE / 'verification.json')
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
    """Only the new session created by run_logged, including its own children."""
    cleanup = {'owned_process_group': process.pid, 'term_requested': False, 'kill_requested': False}
    try:
        os.killpg(process.pid, signal.SIGTERM)
        cleanup['term_requested'] = True
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
        cleanup['kill_requested'] = True
    except ProcessLookupError:
        pass
    process.wait()
    return cleanup


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
            process = subprocess.Popen(record['command'], cwd=ROOT, env=env, start_new_session=True,
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
