#!/usr/bin/env python3
"""One existing retest method only. A new integration freeze and slot are required."""
import argparse
import json
import os
from pathlib import Path

from run_reset_slice import HERE, PROGRAM, require, run, save, sha
from run_workflow import inventory, make_words, prepare_environment, qt_result, sources

METHOD = 'retestCancelDiscardSaveAndReopen'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--snapshot', type=Path, required=True)
    args = parser.parse_args()
    import pwd
    require(args.root_go and os.name == 'posix' and os.geteuid() != 0
            and pwd.getpwuid(os.geteuid()).pw_name == 'qyq'
            and os.environ.get('WSL_DISTRO_NAME') == 'Ubuntu-24.04', 'Ubuntu-24.04/qyq and explicit slot required')
    require(args.output.is_absolute() and args.output.parent.resolve() == HERE
            and not args.output.exists() and not args.output.is_symlink(), 'Fresh direct-child output required')
    require(args.snapshot.is_absolute() and args.snapshot.is_file(), 'New full source freeze required')
    prepared = json.loads((HERE / 'discard_feedback_preparation.json').read_text(encoding='utf-8'))
    frozen = json.loads(args.snapshot.read_text(encoding='utf-8-sig'))
    for name, digest in prepared['module_source_sha256'].items():
        require(frozen.get(name) == digest and sha(PROGRAM / name) == digest,
                'Post-fix sources absent from new freeze: ' + name)
    driver_inputs = {str(path): sha(path) for path in
                     (Path(__file__).resolve(), HERE / 'run_reset_slice.py', HERE / 'run_workflow.py',
                      PROGRAM / 'shell/package_arm64.py')}
    require(driver_inputs == prepared['driver_inputs_sha256'], 'Prepared driver/helper changed')
    sources(frozen)
    args.output.mkdir(mode=0o700)
    build, logs = args.output / 'build', args.output / 'logs'
    build.mkdir()
    logs.mkdir()
    report = {'schema': 'sn100-discard-feedback-targeted-v1', 'passed': False,
              'source_inputs': len(frozen), 'source_snapshot_sha256': sha(args.snapshot),
              'module_source_sha256': prepared['module_source_sha256'], 'driver_inputs_sha256': driver_inputs,
              'method': METHOD, 'main_executed': False, 'phases': [], 'tests': [],
              'scope': 'Only one existing retest/cancel/discard/save/reopen method; init+cleanup count=3 per QPA',
              'full_workflow_baseline_performance_board_or_instrument_acceptance': False}
    save(args.output / 'source_before.json', frozen)
    save(args.output / 'verification.json', report)
    try:
        initial_runtime = args.output / 'build-runtime'
        initial_runtime.mkdir()
        prepare_environment(initial_runtime)
        pro = PROGRAM / 'samples/tests/sample_reset_workflow_test.pro'
        report['phases'].append(run('qmake', ['/usr/lib/qt5/bin/qmake', str(pro), '-o', str(build / 'Makefile'),
                                                   '-spec', 'linux-g++', 'CONFIG+=release', 'CONFIG-=debug'],
                                    build, logs, 120))
        report['phases'].append(run('make', ['/usr/bin/make', '-C', str(build), '-j2'], build, logs, 600))
        binary = build / 'sample_reset_workflow_test'
        before = inventory(build, binary)
        require(set(before['objects_sha256']) == set(make_words(build / 'Makefile', 'OBJECTS')),
                'Fresh object set differs from Makefile')
        source_proof = {}
        for word in make_words(build / 'Makefile', 'SOURCES'):
            path = (build / word).resolve(strict=True)
            if path.is_relative_to(PROGRAM.resolve()):
                name = path.relative_to(PROGRAM.resolve()).as_posix()
                require(frozen.get(name) == sha(path), 'Unfrozen compiled source: ' + name)
                source_proof[name] = sha(path)
            else:
                require(path.is_relative_to(build.resolve()), 'Source escapes private build')
        require(all(name in source_proof for name in prepared['modified_source_sha256']),
                'Post-fix production page and original test were not both compiled')
        save(args.output / 'compiled_sources.json', source_proof)
        save(args.output / 'build_before_tests.json', before)
        for qpa in ('offscreen', 'xcb'):
            case = args.output / qpa
            case.mkdir()
            runtime = case / 'runtime'
            runtime.mkdir()
            environment = prepare_environment(runtime)
            os.environ['QT_QPA_PLATFORM'] = qpa
            save(case / 'environment.json', dict(environment, QT_QPA_PLATFORM=qpa))
            command = [str(binary), METHOD, '-v1', '-o', str(case / 'qt.txt') + ',txt',
                       '-o', str(case / 'qt.xml') + ',junitxml']
            receipt = run('test', command, case, case, 60)
            report['phases'].append(receipt)
            report['tests'].append({'qpa': qpa, 'actual_exit': receipt['actual_exit'], **qt_result(case, 3)})
            require(inventory(build, binary) == before, 'Test changed its compiled artifacts')
            sources(frozen)
            save(args.output / 'verification.json', report)
        save(args.output / 'build_after_tests.json', inventory(build, binary))
        require(all(sha(path) == digest for path, digest in driver_inputs.items()), 'Driver/helper changed')
        require(sha(args.snapshot) == report['source_snapshot_sha256'], 'Freeze file changed')
        save(args.output / 'source_after.json', sources(frozen))
        groups = []
        for receipt in report['phases']:
            try:
                os.killpg(receipt['pid'], 0)
                exists = True
            except ProcessLookupError:
                exists = False
            groups.append({'pid': receipt['pid'], 'pgid': receipt['pid'], 'group_exists': exists,
                           'actual_exit': receipt['actual_exit']})
        report['owned_groups'] = groups
        require(not any(row['group_exists'] for row in groups), 'Owned process group remains')
        report['all_owned_groups_gone'] = True
        report['passed'] = True
    except BaseException as error:
        report.update(error_type=type(error).__name__, error=str(error))
        raise
    finally:
        save(args.output / 'verification.json', report)
    print(json.dumps({'passed': report['passed'], 'verification': str(args.output / 'verification.json')}), flush=True)


if __name__ == '__main__':
    main()
