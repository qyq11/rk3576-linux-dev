#!/usr/bin/env python3
"""Queued sample page/store regression: new source freeze and --root-go required.

No work runs on import. The integration chat must grant a separate GUI/build
slot after receiving the final module source manifest. Never runs an old main.
"""
import argparse
import json
import os
from pathlib import Path
import re
import runpy
import shlex
import xml.etree.ElementTree as ET

from run_reset_slice import HERE, PROGRAM, require, run, save, sha

FUNCTIONAL_BASELINE = ('directoryAndSchema', 'guestAndRoundtrip', 'localPresetWhileInstrumentBusy',
    'validationAndRollback', 'combinationsAndForeignKeys', 'rackReferencesAndEmergency',
    'lockFailureAndRecovery', 'pageDraftAndDependencyRefresh', 'pageDetailAndBatchBoundary',
    'pageCombinationSaveAndFailure', 'pagePermissionAndBusyClose')
TARGETS = (('workflow', 'sample_reset_workflow_test', 'sample_reset_workflow_test.pro', (), 14),
           ('baseline', 'samples_test', 'samples_test.pro', FUNCTIONAL_BASELINE, 13))


def sources(frozen):
    inventory = runpy.run_path(str(PROGRAM / 'shell/package_arm64.py'), run_name='sample_regression_inventory')
    actual = inventory['snapshot'](PROGRAM)
    require(actual == frozen, 'Full source set/SHA differs from supplied new freeze; old426/5123 is insufficient')
    return actual


def inventory(build, binary):
    return {'binary_sha256': sha(binary), 'makefile_sha256': sha(build / 'Makefile'),
            'objects_sha256': {p.relative_to(build).as_posix(): sha(p) for p in sorted(build.rglob('*.o'))},
            'generated_sha256': {p.relative_to(build).as_posix(): sha(p) for p in sorted(build.rglob('*'))
                                if p.is_file() and p.parent.name in ('.moc', '.ui', '.rcc')}}


def make_words(makefile, variable):
    text = makefile.read_text(encoding='utf-8').replace('\\\n', ' ')
    match = re.findall(r'^' + variable + r'\s*=\s*(.*)$', text, re.MULTILINE)
    require(len(match) == 1, 'Makefile variable missing/ambiguous: ' + variable)
    return shlex.split(match[0])


def prepare_environment(runtime):
    for key in tuple(os.environ):
        if key.startswith(('SN100_', 'QT_', 'QML', 'QMAKE', 'CCACHE')):
            del os.environ[key]
    values = {'QT_SCALE_FACTOR': '1', 'QT_AUTO_SCREEN_SCALE_FACTOR': '0'}
    for key, name in (('XDG_RUNTIME_DIR', 'xdg-runtime'), ('XDG_CONFIG_HOME', 'config'),
                      ('XDG_DATA_HOME', 'data'), ('XDG_CACHE_HOME', 'cache'), ('TMPDIR', 'tmp')):
        directory = runtime / name
        directory.mkdir(mode=0o700)
        values[key] = str(directory)
    values['TMP'] = values['TEMP'] = values['TMPDIR']
    values['SN100_SAMPLE_RESET_ROOT'] = str(runtime / 'reset')
    values['SN100_SAMPLE_TEST_ROOT'] = str(runtime / 'baseline')
    Path(values['SN100_SAMPLE_RESET_ROOT']).mkdir()
    Path(values['SN100_SAMPLE_TEST_ROOT']).mkdir()
    os.environ.update(values)
    return values


def qt_result(stage, expected):
    txt, xml = stage / 'qt.txt', stage / 'qt.xml'
    totals = re.findall(r'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped',
                        txt.read_text(encoding='utf-8', errors='replace'))
    cases = list(ET.parse(xml).getroot().iter('testcase'))
    result = {'totals': list(map(int, totals[0])) if len(totals) == 1 else None,
              'cases': [dict(case.attrib) for case in cases], 'qt_txt_sha256': sha(txt),
              'qt_junitxml_sha256': sha(xml), 'expected_including_init_cleanup': expected,
              'failure_error_skipped': [dict(case.attrib) for case in cases
                                      if any(child.tag in ('failure', 'error', 'skipped') for child in case)]}
    save(stage / 'qt_verification.json', result)
    require(result['totals'] == [expected, 0, 0] and len(cases) == expected
            and not result['failure_error_skipped'], 'Qt/JUnit failure or incomplete suite: ' + str(stage))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--snapshot', type=Path, required=True)
    args = parser.parse_args()
    require(args.root_go and os.name == 'posix' and os.geteuid() != 0, 'Ordinary WSL and explicit --root-go required')
    import pwd
    require(pwd.getpwuid(os.geteuid()).pw_name == 'qyq'
            and os.environ.get('WSL_DISTRO_NAME') == 'Ubuntu-24.04', 'Ubuntu-24.04/qyq required')
    require(args.output.is_absolute() and args.output.parent.resolve() == HERE
            and not args.output.exists() and not args.output.is_symlink(), 'Fresh direct-child output required')
    require(args.snapshot.is_absolute() and args.snapshot.is_file(), 'Absolute newly frozen source manifest required')
    prepared = json.loads((HERE / 'workflow_preparation.json').read_text(encoding='utf-8-sig'))
    frozen = json.loads(args.snapshot.read_text(encoding='utf-8-sig'))
    require(all(frozen.get(name) == digest and sha(PROGRAM / name) == digest
                for name, digest in prepared['module_source_sha256'].items()), 'Prepared module is absent/changed in new freeze')
    driver_inputs = {str(Path(__file__).resolve()): sha(__file__),
                    str(HERE / 'run_reset_slice.py'): sha(HERE / 'run_reset_slice.py'),
                    str(PROGRAM / 'shell/package_arm64.py'): sha(PROGRAM / 'shell/package_arm64.py')}
    require(driver_inputs == prepared['driver_inputs_sha256'], 'Prepared driver/helper/inventory changed')
    sources(frozen)
    args.output.mkdir(mode=0o700)
    report = {'schema': 'sn100-sample-reset-workflow-v1', 'passed': False,
              'source_inputs': len(frozen), 'source_snapshot_sha256': sha(args.snapshot),
              'module_source_sha256': prepared['module_source_sha256'], 'driver_inputs_sha256': driver_inputs,
              'main_executed': False, 'stages': [], 'tests': [], 'scope': 'fresh standalone module page/store tests',
              'baseline_omitted_functions': ['mixedLoad'],
              'omission_reason': 'Original response/resource sampling is separately queued; eleven baseline business methods remain unchanged',
              'board_instrument_original_windows_or_resource_acceptance': False}
    save(args.output / 'source_before.json', frozen)
    save(args.output / 'verification.json', report)
    try:
        for label, target, pro, methods, expected in TARGETS:
            stage = args.output / label
            build, logs, runtime = stage / 'build', stage / 'logs', stage / 'runtime'
            for directory in (stage, build, logs, runtime):
                directory.mkdir()
            record = {'name': label, 'target': target, 'phases': [], 'passed': False}
            report['stages'].append(record)
            prepare_environment(runtime)
            try:
                qmake = ['/usr/lib/qt5/bin/qmake', str(PROGRAM / 'samples/tests' / pro),
                         '-o', str(build / 'Makefile'), '-spec', 'linux-g++', 'CONFIG+=release', 'CONFIG-=debug']
                print('SAMPLE_WORKFLOW_START ' + label, flush=True)
                record['phases'].append(run('qmake', qmake, build, logs, 120))
                record['phases'].append(run('make', ['/usr/bin/make', '-C', str(build), '-j2'], build, logs, 600))
                binary = build / target
                before = inventory(build, binary)
                require(before['objects_sha256'], 'Fresh objects missing')
                require(make_words(build / 'Makefile', 'TARGET') == [target], 'Unexpected target')
                require(set(before['objects_sha256']) == set(make_words(build / 'Makefile', 'OBJECTS')),
                        'Fresh object set differs from actual Makefile')
                source_proof = {}
                for word in make_words(build / 'Makefile', 'SOURCES'):
                    path = (build / word).resolve(strict=True)
                    if path.is_relative_to(PROGRAM.resolve()):
                        name = path.relative_to(PROGRAM.resolve()).as_posix()
                        require(frozen.get(name) == sha(path), 'Unfrozen compiled source: ' + name)
                        source_proof[name] = sha(path)
                    else:
                        require(path.is_relative_to(build.resolve()), 'Compiled source escapes private build')
                require('samples/sample_task_reset.cpp' in source_proof and 'samples/sample_store.cpp' in source_proof,
                        'New production reset/store were not compiled')
                save(stage / 'build_after_make.json', before)
                save(stage / 'compiled_sources.json', source_proof)
                for qpa in ('offscreen', 'xcb'):
                    case = stage / qpa
                    case.mkdir()
                    private = case / 'runtime'
                    private.mkdir()
                    values = prepare_environment(private)
                    os.environ['QT_QPA_PLATFORM'] = qpa
                    save(case / 'environment.json', dict(values, QT_QPA_PLATFORM=qpa))
                    command = [str(binary), *methods, '-v1', '-o', str(case / 'qt.txt') + ',txt',
                               '-o', str(case / 'qt.xml') + ',junitxml']
                    receipt = run('test', command, case, case, 120)
                    parsed = qt_result(case, expected)
                    report['tests'].append({'name': label, 'qpa': qpa, 'actual_exit': receipt['actual_exit'],
                                            'binary_sha256': before['binary_sha256'], **parsed})
                    save(args.output / 'verification.json', report)
                    require(inventory(build, binary) == before, 'Compiled artifacts changed during tests')
                    sources(frozen)
                after = inventory(build, binary)
                save(stage / 'build_after_tests.json', after)
                require(after == before, 'Fresh build changed')
                record['passed'] = True
            finally:
                save(stage / 'verification.json', record)
                save(args.output / 'verification.json', report)
        require(len(report['tests']) == 4 and all(row['passed'] for row in report['stages']), 'Coverage incomplete')
        sources(frozen)
        require(sha(args.snapshot) == report['source_snapshot_sha256'], 'Freeze file changed')
        require(all(sha(path) == digest for path, digest in driver_inputs.items()), 'Driver inputs changed')
        save(args.output / 'source_after.json', sources(frozen))
        report['passed'] = True
    except BaseException as error:
        report.update(passed=False, error_type=type(error).__name__, error=str(error))
        raise
    finally:
        save(args.output / 'verification.json', report)
    print('SAMPLE_WORKFLOW_VERIFIED ' + str(args.output / 'verification.json'), flush=True)


if __name__ == '__main__':
    main()
