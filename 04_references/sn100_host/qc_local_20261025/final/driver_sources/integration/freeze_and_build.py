#!/usr/bin/env python3
"""Fresh native Release main/helper; run only after all developers stop writes/load."""
import argparse
import os
import time
from common import PROGRAM, ROOT, NATIVE, FREEZE, sha, save, load, now, require, verify_frozen, run_logged


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    args = parser.parse_args()
    require(args.root_go and os.geteuid() != 0, 'Explicit root go and ordinary WSL user required')
    require(not NATIVE.exists(), 'Fresh native directory required; preserve any prior failed attempt')
    NATIVE.mkdir(parents=True)
    started_ns = time.time_ns()
    try:
        command = ['python3', '-B', str(PROGRAM / 'shell/package_arm64.py'), 'snapshot', '--output']
        run_logged('snapshot_before', command + [str(FREEZE)], NATIVE)
        frozen = load(FREEZE)
        verify_frozen(frozen)
        env = dict(os.environ, SN100_SHELL_WORK_DIR=str(NATIVE), SN100_JOBS='2')
        for name in ('QMAKESPEC', 'QMAKEPATH', 'QMAKEFEATURES', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH'):
            env.pop(name, None)
        receipt = run_logged('build', ['bash', str(PROGRAM / 'shell/wsl.sh'), 'integrated-build'], NATIVE, env=env)
        run_logged('snapshot_after', command + [str(NATIVE / 'final_after.json')], NATIVE)
        require(load(NATIVE / 'final_after.json') == frozen, 'Source inputs changed during native build')
        verify_frozen(frozen)
        build = NATIVE / 'wsl_release'
        artifacts = {name: sha(build / name) for name in ('sn100_shell', 'sn100_report_renderer')}
        objects = {path.relative_to(NATIVE).as_posix(): {'sha256': sha(path),
                   'created_after_start': path.stat().st_mtime_ns > started_ns}
                   for path in sorted(NATIVE.rglob('*.o'))}
        require(objects and all(row['created_after_start'] for row in objects.values()), 'Native build reused a prebuild object')
        run_logged('artifact_types', ['file', str(build / 'sn100_shell'), str(build / 'sn100_report_renderer')], NATIVE)
        result = {'schema': 'sn100-four-windows-native-release-v1', 'passed': True, 'finished_utc': now(),
                  'actual_exit': receipt['exit_code'], 'source_inputs': len(frozen),
                  'snapshot_sha256': sha(FREEZE), 'artifacts': artifacts, 'fresh_objects': objects,
                  'makefile_sha256': sha(build / 'Makefile'),
                  'parallel_jobs_maximum': 2, 'source_before_after_equal': True,
                  'scope': 'Fresh WSL x86_64 Release main/helper build only; no Windows GUI/RK/instrument claim'}
        save(NATIVE / 'verification.json', result)
        print({key: value for key, value in result.items() if key != 'fresh_objects'}, flush=True)
    except BaseException as error:
        save(NATIVE / 'failure.json', {'error_type': type(error).__name__, 'error': str(error),
                                     'failed_utc': now(), 'old_outputs_preserved': True})
        raise


if __name__ == '__main__':
    main()
