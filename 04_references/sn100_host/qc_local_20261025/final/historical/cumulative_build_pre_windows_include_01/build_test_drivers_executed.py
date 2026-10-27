#!/usr/bin/env python3
"""Bind cumulative QtTest drivers to the exact fresh Release objects; no GUI run."""
import argparse
import os
from pathlib import Path
from common import PROGRAM, NATIVE, FREEZE, DRIVERS, sha, save, load, now, require, verify_frozen, verify_native, run_logged

BASE = ('integrated_shell_test', 'catalog_shell_test', 'status_replay_shell_test',
        'status_loopback_shell_test', 'instrument_page_test', 'integrated_report_test',
        'password_login_shell_test', 'printer_settings_shell_test', 'lis_settings_shell_test',
        'qc_local_page_test', 'qc_local_shell_test')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--extra-driver', action='append', default=[])
    parser.add_argument('--reuse-dir', type=Path)
    args = parser.parse_args()
    require(args.root_go and os.geteuid() != 0, 'Explicit root go and ordinary WSL user required')
    require(load(NATIVE / 'verification.json')['passed'], 'Completed fresh native build required')
    require(not DRIVERS.exists(), 'Fresh cumulative driver output required; preserve failures')
    DRIVERS.mkdir(parents=True)
    try:
        frozen = load(FREEZE)
        native = verify_native(frozen)
        drivers = list(BASE) + args.extra_driver
        require(len(set(drivers)) == len(drivers), 'Duplicate test driver requested')
        def source_directory(name):
            return PROGRAM / ('calibration/tests' if name in
                ('calibration_store_test', 'calibration_page_test') else 'tests')
        test_inputs = {str(source_directory(name) / (name + suffix)): sha(source_directory(name) / (name + suffix))
                       for name in drivers for suffix in ('.cpp', '.pro')}
        save(DRIVERS / 'test_inputs_before.json', test_inputs)
        command = ['python3', '-B', str(PROGRAM / 'tests/build_shell_drivers.py'),
                   '--build-dir', str(NATIVE / 'wsl_release'), '--snapshot', str(FREEZE),
                   '--output-dir', str(DRIVERS), '--jobs', '2', *drivers]
        if args.reuse_dir:
            require(args.reuse_dir.is_absolute(), 'Preserved reuse directory must be absolute')
            command += ['--reuse-dir',str(args.reuse_dir)]
        receipt = run_logged('binding_build', command, DRIVERS)
        proof = load(DRIVERS / 'binding_proof.json')
        require(set(proof['drivers']) == set(drivers), 'Driver set differs from binding proof')
        require(proof['snapshot_sha256'] == sha(FREEZE), 'Driver proof refers to a different freeze')
        require(proof['native_makefile_sha256'] == native['makefile_sha256'], 'Driver proof refers to a different Makefile')
        test_after = {name: sha(name) for name in test_inputs}
        save(DRIVERS / 'test_inputs_after.json', test_after)
        require(test_after == test_inputs, 'A test source/project changed during compilation')
        for name, row in proof['drivers'].items():
            source = source_directory(name) / (name + '.cpp')
            require(row['source_relative'] == source.relative_to(PROGRAM).as_posix(),
                    'Driver proof uses an unexpected source path: ' + name)
            require(row['source_sha256'] == test_inputs[str(source)],
                    'Driver proof source differs from its prebuild input: ' + name)
        verify_native(frozen)
        save(DRIVERS / 'verification.json', {'passed': True, 'finished_utc': now(),
             'source_inputs': len(frozen), 'snapshot_sha256': sha(FREEZE), 'drivers': drivers,
             'actual_exit': receipt['exit_code'], 'parallel_jobs_maximum': 2,
             'reused_test_drivers':sum('reused_from' in row for row in proof['drivers'].values()),
             'scope': 'Actual Release objects, test-only driver compilation/linking; GUI has not run'})
    except BaseException as error:
        save(DRIVERS / 'failure.json', {'error_type': type(error).__name__, 'error': str(error), 'failed_utc': now()})
        raise


if __name__ == '__main__':
    main()
