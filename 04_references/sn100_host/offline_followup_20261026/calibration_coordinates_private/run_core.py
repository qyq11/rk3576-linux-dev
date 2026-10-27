#!/usr/bin/env python3
"""Bounded private C++14 coordinate test; requires an explicit serial slot."""
import argparse
import csv
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
ORACLE_SOURCE = ROOT / 'temp/sn100_host/offline_followup_20261026/calibration_next_scope/anonymous_coordinate_cases.json'
FILES = {'calibration_coordinates.h', 'calibration_coordinates.cpp',
         'calibration_coordinates_test.cpp', 'run_core.py', 'oracle.json', 'cases.tsv'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def expected_rows(document):
    """Copy approved fields; never calculate a transform or a new expected value."""
    require(len(document['valid']) == 12 and len(document['rejected']) == 11,
            'Keep the exact approved 12+11 cases')
    rows = []
    for good, cases in ((True, document['valid']), (False, document['rejected'])):
        for case in cases:
            rows.append({'id': case['id'], 'mode': int(case['mode']), 'direction': case['direction'],
                         'input': case.get('input_special', case.get('input')),
                         'valid': good, 'expected': case.get('expected'),
                         'error': 'None' if good else case['expected_error']})
    require(len({row['id'] for row in rows}) == 23, 'Repeated case IDs')
    return rows


def verify_inputs(manifest_sha):
    manifest_path = HERE / 'inputs_manifest.json'
    require(sha(manifest_path) == manifest_sha, 'Manifest differs from the reviewed handoff')
    manifest = json.loads(manifest_path.read_text())
    require(set(manifest['files']) == FILES, 'Private source/input inventory changed')
    actual = {name: sha(HERE / name) for name in FILES}
    require(actual == manifest['files'], 'Private source or fixed case data changed')
    require(sha(ORACLE_SOURCE) == manifest['approved_oracle_sha256']
            and sha(HERE / 'oracle.json') == manifest['approved_oracle_sha256'],
            'Approved independent oracle changed')
    document = json.loads((HERE / 'oracle.json').read_text())
    expected = expected_rows(document)
    with (HERE / 'cases.tsv').open(newline='') as stream:
        copied = list(csv.DictReader(stream, delimiter='\t'))
    require(len(copied) == 23, 'TSV case count differs')
    for row, original in zip(copied, expected):
        require(row['id'] == original['id'] and int(row['mode']) == original['mode']
                and row['direction'] == original['direction']
                and row['expected_valid'] == ('1' if original['valid'] else '0')
                and row['expected_error'] == original['error'], 'Copied case identity/status differs')
        if isinstance(original['input'], str):
            require(row['input'] == original['input'], 'Special input differs')
        else:
            require(float(row['input']) == original['input'], 'Fixed numeric input differs')
        require((float(row['expected_value']) == original['expected']) if original['valid']
                else row['expected_value'] == '-', 'Approved expected value differs')
    tolerance = document['candidate_numerical_tolerance']
    return actual, expected, tolerance


def group_exists(pid):
    try:
        os.killpg(pid, 0)
        return True
    except ProcessLookupError:
        return False


def run(name, command, out, timeout):
    receipt = {'command': command, 'cwd': str(HERE), 'timeout_seconds': timeout,
               'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'pid': None, 'exit_code': None, 'timed_out': False, 'cleanup_signals': []}
    save(out / (name + '.command.json'), receipt)
    started = time.monotonic()
    process = None
    failure = None
    try:
        with (out / (name + '.stdout.log')).open('wb') as stdout, \
                (out / (name + '.stderr.log')).open('wb') as stderr:
            process = subprocess.Popen(command, cwd=HERE, stdout=stdout, stderr=stderr,
                                       start_new_session=True)
            receipt['pid'] = process.pid
            save(out / (name + '.process.json'), {'pid': process.pid, 'state': 'running'})
            process.wait(timeout=timeout)
    except BaseException as error:
        failure = error
        receipt.update(exception=type(error).__name__, message=str(error),
                       timed_out=isinstance(error, subprocess.TimeoutExpired))
    finally:
        if process is not None:
            for kind in (signal.SIGTERM, signal.SIGKILL):
                if not group_exists(process.pid):
                    break
                try:
                    os.killpg(process.pid, kind)  # Only this newly created session.
                    receipt['cleanup_signals'].append(int(kind))
                except ProcessLookupError:
                    break
                try:
                    process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    pass
            receipt['exit_code'] = process.poll()
            receipt['owned_group_gone'] = not group_exists(process.pid)
        receipt['elapsed_seconds'] = time.monotonic() - started
        receipt['state'] = 'not_started' if process is None else 'finished'
        save(out / (name + '.exit.json'), receipt)  # Actual exit before parsing/asserting.
        save(out / (name + '.process.json'), {'pid': receipt['pid'], 'state': 'finished',
                                             'exit_code': receipt['exit_code']})
    if failure:
        raise failure
    require(receipt['exit_code'] is not None and receipt['owned_group_gone'],
            'Owned process/group not confirmed stopped; no next command')
    return receipt


def check_actual_output(path, expected, tolerance):
    with path.open(newline='') as stream:
        actual = list(csv.DictReader(stream, delimiter='\t'))
    require(len(actual) == 23, 'Missing/extra actual case results')
    checks = []
    for row, original in zip(actual, expected):
        require(row['id'] == original['id'] and int(row['mode']) == original['mode']
                and row['direction'] == original['direction'], 'Actual case order/identity differs')
        require(row['expected_valid'] == ('1' if original['valid'] else '0')
                and row['expected_error'] == original['error'], 'Test printed a different expected status')
        require(row['input'] == original['input'] if isinstance(original['input'], str)
                else float(row['input']) == original['input'], 'Test printed a different actual input')
        success = row['actual_valid'] == ('1' if original['valid'] else '0')
        success = success and row['actual_error'] == original['error']
        success = success and all(row[field] == '1' for field in
                                  ('input_unchanged', 'invalid_read_guard', 'passed'))
        if original['valid']:
            require(float(row['expected_value']) == original['expected'], 'Test changed numeric oracle')
            value = float(row['actual_value'])
            success = success and row['readable'] == '1' and math.isfinite(value) and (
                abs(value - original['expected']) <= max(tolerance['absolute'],
                                                        tolerance['relative'] * abs(original['expected'])))
        else:
            success = success and row['readable'] == '0' and row['actual_value'] == '<unavailable>'
        checks.append({'id': original['id'], 'passed': success, 'actual': row})
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true', required=True)
    parser.add_argument('--slot-id', required=True)
    parser.add_argument('--attempt', required=True)
    parser.add_argument('--manifest-sha256', required=True)
    args = parser.parse_args()
    require(os.geteuid() != 0, 'Ordinary WSL user required')
    require(re.fullmatch(r'[A-Za-z0-9_-]+', args.slot_id) and
            re.fullmatch(r'[A-Za-z0-9_-]+', args.attempt), 'Simple slot/attempt ID required')
    require(re.fullmatch(r'[0-9a-f]{64}', args.manifest_sha256), 'Exact reviewed manifest SHA required')
    out = HERE / args.attempt
    require(not out.exists(), 'Preserve previous evidence; use a fresh attempt')
    out.mkdir()
    save(out / 'invocation.json', {'argv': sys.argv, 'parent_pid': os.getpid(),
         'slot_id': args.slot_id, 'standalone_private_core': True, 'production_objects_used': False})
    successful = False
    before = None
    summary = {'schema': 'sn100-private-coordinate-run-v1', 'test_result': None, 'passed': False}
    try:
        before, expected, tolerance = verify_inputs(args.manifest_sha256)
        save(out / 'source_before.json', before)
        compiler = shutil.which('g++')
        require(compiler is not None, 'Existing g++ required; do not install a toolchain')
        compiler = str(Path(compiler).resolve(strict=True))
        version = run('compiler_version', [compiler, '--version'], out, 5)
        require(version['exit_code'] == 0, 'Compiler metadata command failed')
        binary = out / 'calibration_coordinates_test'
        compile_result = run('compile', [compiler, '-std=c++14', '-O2', '-fno-fast-math',
             '-Wall', '-Wextra', '-Wpedantic', '-Werror', str(HERE / 'calibration_coordinates.cpp'),
             str(HERE / 'calibration_coordinates_test.cpp'), '-o', str(binary)], out, 20)
        require(compile_result['exit_code'] == 0, 'Compilation failed; no old binary may be run')
        save(out / 'binary.json', {'path': str(binary), 'sha256': sha(binary)})
        test = run('test', [str(binary), str(HERE / 'cases.tsv'),
                           str(tolerance['absolute']), str(tolerance['relative'])], out, 5)
        summary['test_result'] = test
        checks = check_actual_output(out / 'test.stdout.log', expected, tolerance)
        summary.update(cases=checks, case_count=len(checks),
                       passed_cases=sum(case['passed'] for case in checks),
                       failed_cases=sum(not case['passed'] for case in checks))
        successful = test['exit_code'] == 0 and all(case['passed'] for case in checks)
    except BaseException as error:
        summary.update(driver_error=type(error).__name__, message=str(error))
    finally:
        try:
            after, _, _ = verify_inputs(args.manifest_sha256)
            save(out / 'source_after.json', after)
            summary['inputs_unchanged'] = after == before
        except BaseException as error:
            summary.update(inputs_unchanged=False, post_verification_error=str(error))
        summary['passed'] = successful and summary['inputs_unchanged']
        save(out / 'verification.json', summary)
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

