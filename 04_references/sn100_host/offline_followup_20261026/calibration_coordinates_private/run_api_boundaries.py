#!/usr/bin/env python3
"""Prepare/run six supplemental direct API checks only in an explicitly granted slot."""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys

HERE = Path(__file__).resolve().parent
BASE_MANIFEST_SHA = 'fbd573f1d3628f4d6b6a285bc1d37452fabe9c27bbee2489bec7aaad85fcd4c6'
FILES = {'calibration_coordinates.h', 'calibration_coordinates.cpp',
         'calibration_coordinates_test.cpp', 'run_core.py', 'oracle.json', 'cases.tsv',
         'inputs_manifest.json', 'calibration_coordinates_api_test.cpp',
         'api_boundary_cases.json', 'run_api_boundaries.py'}
CASE_IDS = ['direction_minus_one', 'direction_two', 'reciprocal_negative_zero',
            'reciprocal_negative_four', 'reciprocal_denorm_min_overflow',
            'identity_negative_zero_sign']


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def verify_inputs(manifest_sha):
    require(sha(HERE / 'api_inputs_manifest.json') == manifest_sha,
            'Supplemental manifest differs from the reviewed handoff')
    manifest = json.loads((HERE / 'api_inputs_manifest.json').read_text())
    require(set(manifest['files']) == FILES, 'Supplemental input inventory changed')
    actual = {name: sha(HERE / name) for name in FILES}
    require(actual == manifest['files'], 'Supplemental or original source/input changed')
    require(actual['inputs_manifest.json'] == BASE_MANIFEST_SHA,
            'The original 23-case manifest must remain byte-identical')
    document = json.loads((HERE / 'api_boundary_cases.json').read_text())
    expected = document['cases']
    require(document['case_count'] == 6 and len(expected) == 6 and
            [case['id'] for case in expected] == CASE_IDS, 'Keep six separate API cases')
    require(sum(case['expected_valid'] for case in expected) == 2 and
            document['valid_count'] == 2 and document['rejected_count'] == 4,
            'Supplemental valid/rejected counts differ')
    return actual, expected


def input_value(case):
    special = case.get('input_special')
    if special == 'negative_zero':
        return -0.0
    if special == 'denorm_min':
        return float.fromhex(case['expected_input_hex'])
    require(special is None, 'Unexpected supplemental special input')
    return case['input']


def check_actual_output(path, expected):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    require(len(rows) == 6, 'Missing/extra supplemental API results')
    checks = []
    for row, case in zip(rows, expected):
        good = case['expected_valid']
        signbit = case['expected_output_signbit'] if good else -1
        require(row['id'] == case['id'] and int(row['mode']) == case['mode'] and
                int(row['direction_integer']) == case['direction_integer'] and
                row['input_token'] == case['input_token'], 'Actual API case identity differs')
        require(row['expected_valid'] == ('1' if good else '0') and
                row['expected_error'] == case['expected_error'] and
                int(row['expected_output_signbit']) == signbit,
                'Harness printed a different fixed expectation')
        if good:
            require(float(row['expected_value']) == case['expected_value'] and
                    (math.copysign(1.0, float(row['expected_value'])) < 0) == bool(signbit),
                    'Harness printed a different fixed value/sign')
        else:
            require(row['expected_value'] == '-', 'Rejected expected value must be unavailable')
        observed_input = float(row['actual_input'])
        require(observed_input == input_value(case) and
                (math.copysign(1.0, observed_input) < 0) == bool(case['expected_input_signbit']) and
                int(row['input_signbit']) == case['expected_input_signbit'],
                'Actual API input value/sign differs')
        passed = row['actual_valid'] == ('1' if good else '0') and \
            row['readable'] == ('1' if good else '0') and row['actual_error'] == case['expected_error'] and \
            row['input_unchanged'] == '1' and row['invalid_read_guard'] == '1' and row['passed'] == '1' and \
            int(row['output_signbit']) == signbit
        if good:
            value = float(row['actual_value'])
            passed = passed and math.isfinite(value) and value == case['expected_value'] and \
                (math.copysign(1.0, value) < 0) == bool(signbit)
        else:
            passed = passed and row['actual_value'] == '<unavailable>'
        checks.append({'id': case['id'], 'passed': passed, 'actual': row})
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
    require(not out.exists(), 'Preserve previous evidence; use a fresh supplemental attempt')
    out.mkdir()
    save(out / 'invocation.json', {'argv': sys.argv, 'parent_pid': os.getpid(),
         'slot_id': args.slot_id, 'scope': 'six_direct_api_boundaries_only',
         'original_23_executed_here': False, 'production_objects_used': False})
    before = None
    successful = False
    summary = {'schema': 'sn100-private-coordinate-api-run-v1', 'passed': False,
               'scope': 'six_direct_api_boundaries_only', 'original_23_executed_here': False,
               'test_result': None}
    try:
        before, expected = verify_inputs(args.manifest_sha256)
        save(out / 'source_before.json', before)
        # Import only after the unchanged helper's SHA has passed the manifest check.
        spec = importlib.util.spec_from_file_location('coordinate_core_driver', HERE / 'run_core.py')
        core = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(core)
        core.verify_inputs(BASE_MANIFEST_SHA)  # Data/SHA check only; does not run the original 23.
        compiler = shutil.which('g++')
        require(compiler is not None, 'Existing g++ required; do not install a toolchain')
        compiler = str(Path(compiler).resolve(strict=True))
        version = core.run('compiler_version', [compiler, '--version'], out, 5)
        require(version['exit_code'] == 0, 'Compiler metadata command failed')
        binary = out / 'calibration_coordinates_api_test'
        compiled = core.run('compile', [compiler, '-std=c++14', '-O2', '-fno-fast-math',
            '-Wall', '-Wextra', '-Wpedantic', '-Werror', str(HERE / 'calibration_coordinates.cpp'),
            str(HERE / 'calibration_coordinates_api_test.cpp'), '-o', str(binary)], out, 20)
        require(compiled['exit_code'] == 0, 'Compilation failed; no old binary may be run')
        save(out / 'binary.json', {'path': str(binary), 'sha256': sha(binary)})
        result = core.run('test', [str(binary)], out, 5)
        summary['test_result'] = result
        checks = check_actual_output(out / 'test.stdout.log', expected)
        summary.update(cases=checks, case_count=len(checks),
                       passed_cases=sum(case['passed'] for case in checks),
                       failed_cases=sum(not case['passed'] for case in checks))
        successful = result['exit_code'] == 0 and all(case['passed'] for case in checks)
    except BaseException as error:
        summary.update(driver_error=type(error).__name__, message=str(error))
    finally:
        try:
            after, _ = verify_inputs(args.manifest_sha256)
            save(out / 'source_after.json', after)
            summary['inputs_unchanged'] = after == before
        except BaseException as error:
            summary.update(inputs_unchanged=False, post_verification_error=str(error))
        summary['passed'] = successful and summary['inputs_unchanged']
        save(out / 'verification.json', summary)
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
