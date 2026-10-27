#!/usr/bin/env python3
"""Explicit future root-go: fresh Release build and QtCore anonymous tests only."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


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
    # start_new_session gives only this newly-created process/session its PGID.
    receipt = {'owned_pgid': process.pid, 'term_requested': False, 'kill_requested': False}

    def wait_gone(seconds):
        deadline = time.monotonic() + seconds
        while True:
            process.poll()  # reap the direct child without an unbounded wait
            alive = group_alive(process.pid)
            if alive is not True or time.monotonic() >= deadline:
                return alive
            time.sleep(0.05)

    alive = group_alive(process.pid)
    if alive is True:
        try:
            os.killpg(process.pid, signal.SIGTERM)
            receipt['term_requested'] = True
        except ProcessLookupError:
            pass
        except OSError as error:
            receipt['term_error'] = repr(error)
        alive = wait_gone(3)
    if alive is True:
        try:
            os.killpg(process.pid, signal.SIGKILL)
            receipt['kill_requested'] = True
        except ProcessLookupError:
            pass
        except OSError as error:
            receipt['kill_error'] = repr(error)
        alive = wait_gone(2)
    receipt.update(group_alive_after_cleanup=alive,
                   group_stopped_confirmed=alive is False,
                   direct_exit_after_cleanup=process.poll())
    return receipt


def execute(name, argv, output, cwd, timeout):
    receipt = {'argv': list(map(str, argv)), 'cwd': str(cwd), 'started_utc': now()}
    save(output / (name + '.command.json'), receipt)
    process = None
    timed_out = False
    cleanup = None
    began = time.monotonic()
    failure = None
    direct_exit_before_cleanup = None
    group_stopped = None
    try:
        with (output / (name + '.log')).open('xb') as log:
            process = subprocess.Popen(receipt['argv'], cwd=cwd, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            process.wait(timeout=timeout)
    except BaseException as error:
        failure = error
        timed_out = isinstance(error, subprocess.TimeoutExpired)
    finally:
        if process is not None:
            direct_exit_before_cleanup = process.poll()
            alive = group_alive(process.pid)
            if alive is not False:
                try:
                    cleanup = stop_owned_group(process)
                    group_stopped = cleanup['group_stopped_confirmed']
                except BaseException as error:
                    cleanup = {'owned_pgid': process.pid, 'group_stopped_confirmed': False,
                               'cleanup_exception_type': type(error).__name__, 'cleanup_exception': str(error)}
                    group_stopped = False
                    if failure is None:
                        failure = error
                if failure is None:
                    failure = RuntimeError('Owned group survived its direct process or group state is unknown')
            else:
                group_stopped = True
        # 原始退出首先落盘；缺结果不会伪造exit0，失败不会执行后续阶段。
        code = process.returncode if process is not None else None
        receipt.update(actual_exit_code=code, direct_exit_before_cleanup=direct_exit_before_cleanup,
                       timed_out=timed_out, exception_type=type(failure).__name__ if failure else None,
                       exception=str(failure) if failure else None, owned_group_stopped=group_stopped,
                       seconds=time.monotonic() - began, finished_utc=now(), cleanup=cleanup)
        save(output / (name + '.exit.json'), receipt)
        (output / (name + '.exit')).write_text(str(code) + '\n', encoding='ascii')
    if failure is not None:
        raise failure
    if code != 0 or group_stopped is not True:
        raise RuntimeError(name + ' failed; actual receipt/log retained')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', required=True, action='store_true')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--jobs', type=int, choices=(1, 2), default=2)
    parser.add_argument('--qmake', default='/usr/lib/qt5/bin/qmake')
    parser.add_argument('--make', default='make')
    args = parser.parse_args()
    if not args.root_go or os.name != 'posix' or os.geteuid() == 0:
        parser.error('Explicit go from ordinary Linux user required; no GUI or device runner')
    source = Path(__file__).resolve().parent
    output = args.output_dir
    if not output.is_absolute() or output.exists() or output.is_symlink() or not output.parent.is_dir():
        parser.error('Use a new absolute output directory with an existing parent')
    if not output.resolve().is_relative_to(source):
        parser.error('Keep private run artifacts inside this authorized qc_history_slice directory')
    preparation_bytes = (source / 'preparation.json').read_bytes()
    preparation_sha = hashlib.sha256(preparation_bytes).hexdigest()
    preparation = json.loads(preparation_bytes.decode('utf-8'))
    required = {'qc_history_core.h', 'qc_history_core.cpp', 'qc_history_slice.pro', 'tests.cpp',
                'run_slice.py', 'statistics/statistics.h', 'statistics/statistics.cpp', 'origins.json'}
    if (preparation.get('schema') != 'sn100-private-qc-history-slice-preparation-v1' or
            not isinstance(preparation.get('source_inputs'), dict) or
            set(preparation['source_inputs']) != required):
        raise RuntimeError('Preparation must bind the exact eight required private inputs')
    expected_cases = preparation.get('test_cases')
    if (not isinstance(expected_cases, list) or len(expected_cases) != 20 or
            not all(isinstance(name, str) and name for name in expected_cases) or
            len(set(expected_cases)) != 20 or preparation.get('prepared_test_case_count') != 20):
        raise RuntimeError('Preparation must bind exactly twenty named anonymous cases')
    before = {}
    for relative, expected in preparation['source_inputs'].items():
        path = Path(relative)
        if path.is_absolute() or '..' in path.parts:
            raise RuntimeError('Unsafe preparation source path')
        if (not (source / path).is_file() or (source / path).is_symlink() or
                not (source / path).resolve(strict=True).is_relative_to(source)):
            raise RuntimeError('Private source must be an ordinary file inside this private directory')
        before[relative] = sha(source / path)
        if before[relative] != expected:
            raise RuntimeError('Prepared source changed: ' + relative)
    output.mkdir()
    build = output / 'build'
    build.mkdir()
    save(output / 'inputs_before.json', before)
    # 留下本次精确编译输入，不搬运行数据库、正式应用对象或认证文件。
    for relative in before:
        destination = output / 'executed_sources' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((source / relative).read_bytes())
        if sha(destination) != before[relative]:
            raise RuntimeError('Executed source copy changed before build: ' + relative)
    (output / 'preparation_executed.json').write_bytes(preparation_bytes)
    passed = False
    try:
        execute('qmake', [args.qmake, str(source / 'qc_history_slice.pro'), 'CONFIG+=release',
                         'CONFIG-=debug'], output, build, 60)
        execute('make', [args.make, '-j' + str(args.jobs)], output, build, 180)
        binary = build / 'qc_history_core_tests'
        if not binary.is_file():
            raise RuntimeError('No new test executable; never substitute an old binary')
        execute('test', [str(binary)], output, build, 60)
        result = json.loads((output / 'test.log').read_text(encoding='utf-8'))
        actual_cases = result.get('cases', [])
        if (result.get('schema') != 'sn100-private-qc-history-tests-v1' or
                result.get('anonymous') is not True or result.get('failed_checks') != 0 or
                result.get('case_count') != len(preparation['test_cases']) or
                [row.get('name') for row in actual_cases] != preparation['test_cases'] or
                not all(row.get('passed') is True and row.get('checks', 0) > 0 for row in actual_cases) or
                sum(row.get('checks', 0) for row in actual_cases) != result.get('checks') or
                result.get('checks', 0) < 100 or result.get('cv_unit') != 'ratio-not-percent'):
            raise RuntimeError('Actual test JSON incomplete or reports failure')
        save(output / 'test_results.json', result)
        save(output / 'artifact.json', {'path': str(binary), 'sha256': sha(binary)})
        passed = True
    finally:
        after = {relative: sha(source / relative) for relative in before}
        preparation_after_sha = sha(source / 'preparation.json')
        unchanged = before == after and preparation_sha == preparation_after_sha
        save(output / 'inputs_after.json', after)
        save(output / 'verification.json', {'passed': passed and unchanged,
            'source_before_after_equal': before == after,
            'preparation_before_after_equal': preparation_sha == preparation_after_sha,
            'preparation_sha256': preparation_sha, 'preparation_after_sha256': preparation_after_sha,
            'private_core_only': True, 'qc_history_page_completed': False,
            'clinical_rule_evaluation': False, 'gui_database_or_device': False,
            'note': 'Only actual future receipts can establish build/test success; source preparation alone does not.'})
    if not unchanged:
        raise RuntimeError('Source or preparation changed during private build/test')


if __name__ == '__main__':
    main()
