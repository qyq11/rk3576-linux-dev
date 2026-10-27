#!/usr/bin/env python3
"""Prepare-only until the integration chat grants a build slot; --root-go required."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PROGRAM = ROOT / '06_programs/sn100_host'
SOURCES = ('samples/sample_task_reset.h', 'samples/sample_task_reset.cpp',
           'samples/tests/sample_task_reset_test.cpp', 'samples/tests/sample_task_reset_test.pro')


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def inputs():
    return {name: sha(PROGRAM / name) for name in SOURCES}


def run(name, command, cwd, output, timeout):
    record = {'command': list(map(str, command)), 'cwd': str(cwd), 'timeout_seconds': timeout,
              'started_unix_ns': time.time_ns(), 'state': 'not_started'}
    save(output / (name + '.command.json'), record)
    process, failure, cleanup = None, None, {'term': False, 'kill': False}
    try:
        with (output / (name + '.log')).open('wb') as stream:
            env = dict(os.environ)
            for key in tuple(env):
                if key.startswith(('QMAKE', 'CCACHE')) or key in ('MAKEFLAGS', 'MFLAGS', 'GNUMAKEFLAGS', 'MAKELEVEL'):
                    del env[key]
            process = subprocess.Popen(record['command'], cwd=cwd, start_new_session=True,
                                       stdout=stream, stderr=subprocess.STDOUT, env=env)
            save(output / (name + '.process.json'), {'pid': process.pid, 'pgid': process.pid, 'state': 'running'})
            process.wait(timeout=timeout)
    except BaseException as error:
        failure = error
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
                cleanup['term'] = True
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                pass
            try:
                os.killpg(process.pid, signal.SIGKILL)
                cleanup['kill'] = True
            except ProcessLookupError:
                pass
            process.wait()
    record.update(actual_exit=process.returncode if process is not None else None,
                  pid=process.pid if process is not None else None, state='finished' if process else 'not_started',
                  timeout=isinstance(failure, subprocess.TimeoutExpired), cleanup=cleanup,
                  finished_unix_ns=time.time_ns())
    if failure:
        record.update(error_type=type(failure).__name__, error=str(failure))
    # Actual child exit is written before any output parser/assertion.
    save(output / (name + '.exit.json'), record)
    (output / (name + '.exit')).write_text(str(record['actual_exit']) + '\n', encoding='ascii')
    save(output / (name + '.process.json'), {'pid': record['pid'], 'pgid': record['pid'],
                                          'state': record['state'], 'actual_exit': record['actual_exit']})
    if failure:
        raise failure
    require(record['actual_exit'] == 0, name + ' failed; raw receipts retained')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(args.root_go and os.name == 'posix' and os.geteuid() != 0, 'Explicit --root-go and ordinary WSL required')
    import pwd
    require(pwd.getpwuid(os.geteuid()).pw_name == 'qyq'
            and os.environ.get('WSL_DISTRO_NAME') == 'Ubuntu-24.04', 'Ubuntu-24.04/qyq required')
    output = args.output
    require(output.is_absolute() and output.parent.resolve() == HERE and not output.exists()
            and not output.is_symlink(), 'Output must be a fresh direct child of this diagnostic directory')
    before = inputs()
    prepared = json.loads((HERE / 'preparation.json').read_text(encoding='utf-8-sig'))
    require(before == prepared['slice_source_sha256'] and sha(__file__) == prepared['driver_sha256'],
            'Prepared source/driver changed; review and refresh preparation before execution')
    output.mkdir(mode=0o700)
    build, logs = output / 'build', output / 'logs'
    build.mkdir()
    logs.mkdir()
    result = {'schema': 'sn100-sample-task-reset-slice-v1', 'passed': False,
              'source_before': before, 'driver_sha256': sha(__file__), 'phases': [],
              'scope': 'QtCore pure local draft contract only; no GUI/SQL/device/whole-program acceptance',
              'existing_sample_ui_store_modified': False}
    save(output / 'verification.json', result)
    try:
        qmake = '/usr/lib/qt5/bin/qmake'
        command = [qmake, str(PROGRAM / SOURCES[-1]), '-o', str(build / 'Makefile'),
                   '-spec', 'linux-g++', 'CONFIG+=release', 'CONFIG-=debug']
        result['phases'].append(run('qmake', command, build, logs, 30))
        result['phases'].append(run('make', ['/usr/bin/make', '-C', str(build), '-j2'], build, logs, 120))
        binary = build / 'sample_task_reset_test'
        result['binary_sha256_before'] = sha(binary)
        result['objects_before'] = {p.relative_to(build).as_posix(): sha(p) for p in sorted(build.rglob('*.o'))}
        result['makefile_sha256'] = sha(build / 'Makefile')
        command = [str(binary), '-v1', '-o', str(output / 'qt.txt') + ',txt',
                   '-o', str(output / 'qt.xml') + ',junitxml']
        result['phases'].append(run('test', command, output, logs, 20))
        import re
        totals = re.findall(r'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped',
                            (output / 'qt.txt').read_text(encoding='utf-8'))
        cases = list(ET.parse(output / 'qt.xml').getroot().iter('testcase'))
        result['qt_totals'] = list(map(int, totals[0])) if len(totals) == 1 else None
        result['testcase_names'] = [case.get('name') for case in cases]
        result['qt_txt_sha256'] = sha(output / 'qt.txt')
        result['qt_junitxml_sha256'] = sha(output / 'qt.xml')
        result['objects_after'] = {p.relative_to(build).as_posix(): sha(p) for p in sorted(build.rglob('*.o'))}
        result['binary_sha256_after'] = sha(binary)
        result['makefile_sha256_after'] = sha(build / 'Makefile')
        require(result['qt_totals'] == [9, 0, 0] and len(cases) == 9
                and not any(child.tag in ('failure', 'error', 'skipped') for case in cases for child in case),
                'Qt/JUnit contract failure; actual child exit already retained')
        require(result['objects_before'] == result['objects_after']
                and result['binary_sha256_before'] == result['binary_sha256_after']
                and result['makefile_sha256'] == result['makefile_sha256_after'], 'Objects/Makefile/binary changed during test')
        require(inputs() == before, 'Slice source changed during build/run')
        result['passed'] = True
    except BaseException as error:
        result.update(error_type=type(error).__name__, error=str(error))
        raise
    finally:
        result['source_after'] = inputs()
        result['driver_sha256_after'] = sha(__file__)
        if result['source_after'] != before or result['driver_sha256_after'] != result['driver_sha256']:
            result['passed'] = False
        save(output / 'verification.json', result)
    require(result['passed'], 'Final source/driver comparison failed')
    print(json.dumps({'passed': result['passed'], 'verification': str(output / 'verification.json')}), flush=True)


if __name__ == '__main__':
    main()
