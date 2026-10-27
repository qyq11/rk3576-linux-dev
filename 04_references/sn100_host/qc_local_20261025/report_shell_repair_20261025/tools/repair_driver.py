#!/usr/bin/env python3
"""Private, serial report-shell harness repair; never rebuild product objects."""
import argparse
import os
from pathlib import Path
import re
import shutil
import sys

WORK = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(WORK / 'integration_drivers'))
from common import ROOT, PROGRAM, NATIVE, FREEZE, load, save, sha, require, verify_native, run_logged


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=('build', 'offscreen', 'xcb'))
    parser.add_argument('attempt')
    args = parser.parse_args()
    require(os.geteuid() != 0 and re.fullmatch(r'attempt_[0-9]{2}', args.attempt), 'Ordinary user and new simple attempt required')
    attempt = Path(__file__).resolve().parent / args.attempt
    native = verify_native(load(FREEZE))
    if args.phase == 'build':
        require(not attempt.exists(), 'Old attempt must remain intact')
        attempt.mkdir()
        source = attempt / 'source'
        source.mkdir()
        hashes = {}
        for suffix in ('cpp', 'pro'):
            name = 'report_remaining_shell_test.' + suffix
            shutil.copy2(PROGRAM / 'tests' / name, source / name)
            hashes[name] = sha(source / name)
            require(hashes[name] == sha(PROGRAM / 'tests' / name), 'Source copy mismatch')
        save(attempt / 'source.json', {'files': hashes, 'freeze_sha256': sha(FREEZE), 'native_artifacts': native['artifacts']})
        command = ['python3', '-B', str(PROGRAM / 'tests/build_shell_drivers.py'),
                   '--build-dir', str(NATIVE / 'wsl_release'), '--snapshot', str(FREEZE),
                   '--output-dir', str(attempt / 'driver_build_01'), '--jobs', '1', 'report_remaining_shell_test']
        run_logged('build', command, attempt, timeout=120)
    else:
        require(attempt.is_dir(), 'Build attempt must exist')
        source = load(attempt / 'source.json')
        for name, expected in source['files'].items():
            require(sha(PROGRAM / 'tests' / name) == expected, 'Test source changed after build')
        build = attempt / 'driver_build_01'
        proof = load(build / 'binding_proof.json')
        record = proof['drivers']['report_remaining_shell_test']
        require(proof['snapshot_sha256'] == sha(FREEZE) and proof['native_objects_excluding_main'] == 167, 'Wrong frozen object binding')
        require(proof['native_makefile_sha256'] == native['makefile_sha256'], 'Wrong production Makefile')
        require(record['source_sha256'] == source['files']['report_remaining_shell_test.cpp'], 'Driver source mismatch')
        require(sha(build / 'report_remaining_shell_test') == record['binary_sha256'], 'Driver binary mismatch')
        for path, row in proof['native_objects'].items():
            require(sha(path) == row['object_sha256'], 'Product object changed')
        stage = attempt / ('run_01_' + args.phase)
        require(not stage.exists(), 'Old run must remain intact')
        stage.mkdir()
        env = dict(os.environ, QT_QPA_PLATFORM=args.phase, QT_SCALE_FACTOR='1', QT_AUTO_SCREEN_SCALE_FACTOR='0',
                   SN100_WSL_IME='off', SN100_REPORT_REMAINING_APP=str(NATIVE / 'wsl_release/sn100_shell'),
                   SN100_REPORT_REMAINING_TEST_ROOT=str(stage / 'runtime'),
                   SN100_REPORT_REMAINING_EVIDENCE=str(stage / 'evidence'))
        command = [str(build / 'report_remaining_shell_test'), '-o', str(stage / 'qt.txt') + ',txt',
                   '-o', str(stage / 'qt.xml') + ',junitxml']
        receipt = run_logged('test', command, stage, env=env, timeout=120)
        text = (stage / 'qt.txt').read_text(encoding='utf-8', errors='replace')
        totals = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', text)
        require(totals and totals[-1] == ('5', '0', '0'), 'Expected actual five pass / zero failure / zero skip')
        verify_native(load(FREEZE))
        save(stage / 'verification.json', {'passed': True, 'qpa': args.phase, 'totals': list(map(int, totals[-1])),
             'source_sha256': record['source_sha256'], 'driver_sha256': record['binary_sha256'],
             'binding_proof_sha256': sha(build / 'binding_proof.json'), 'source_snapshot_sha256': sha(FREEZE),
             'main_artifacts': native['artifacts'], 'actual_exit': receipt['exit_code']})
    print(args.phase + ' complete', flush=True)


if __name__ == '__main__':
    main()
