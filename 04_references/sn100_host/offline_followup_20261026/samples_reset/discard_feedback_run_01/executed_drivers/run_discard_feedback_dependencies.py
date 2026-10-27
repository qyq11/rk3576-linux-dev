#!/usr/bin/env python3
"""Targeted original retest method: reuse 57 verified objects, rebuild two; slot required."""
import argparse
import json
import os
from pathlib import Path

from run_reset_slice import HERE, PROGRAM, require, run, save, sha
from run_workflow import prepare_environment, qt_result
from sample_discard_dependencies import parse_makefile

METHOD = 'retestCancelDiscardSaveAndReopen'
OLD_BUILD = HERE / 'workflow_01/workflow/build'
REBUILT = {'.obj/sample_page.o': 'samples/sample_page.cpp',
           '.obj/sample_reset_workflow_test.o': 'samples/tests/sample_reset_workflow_test.cpp'}


def binding(prepared):
    for name, digest in prepared['dependency_source_sha256'].items():
        require(sha(PROGRAM / name) == digest, 'Actual standalone dependency changed: ' + name)
    for name, digest in prepared['original_objects_sha256'].items():
        require(sha(OLD_BUILD / name) == digest, 'Actually tested original object changed: ' + name)
    for name, digest in prepared['generated_sha256'].items():
        require(sha(OLD_BUILD / name) == digest, 'Original generated artifact changed: ' + name)
    require(sha(OLD_BUILD / 'Makefile') == prepared['makefile_sha256'], 'Original Makefile changed')
    require(sha(OLD_BUILD / 'sample_reset_workflow_test') == prepared['old_tested_binary_sha256'], 'Old binary changed')
    require(parse_makefile((OLD_BUILD / 'Makefile').read_text(encoding='utf-8')) == prepared['makefile_mapping'],
            'Original dependency rules, flags or ordered objects differ')
    for path, digest in prepared['driver_inputs_sha256'].items():
        require(sha(path) == digest, 'Targeted driver/helper/provenance changed: ' + path)
    return {'dependency_source_sha256': prepared['dependency_source_sha256'],
            'makefile_sha256': prepared['makefile_sha256'],
            'original_objects_sha256': prepared['original_objects_sha256'],
            'generated_sha256': prepared['generated_sha256'],
            'driver_inputs_sha256': prepared['driver_inputs_sha256']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    import pwd
    require(args.root_go and os.name == 'posix' and os.geteuid() != 0
            and pwd.getpwuid(os.geteuid()).pw_name == 'qyq'
            and os.environ.get('WSL_DISTRO_NAME') == 'Ubuntu-24.04', 'Ubuntu-24.04/qyq and explicit slot required')
    require(args.output.is_absolute() and args.output.parent.resolve() == HERE
            and not args.output.exists() and not args.output.is_symlink(), 'Fresh direct-child output required')
    preparation_path = HERE / 'discard_feedback_dependency_preparation.json'
    preparation_sha = sha(preparation_path)
    prepared = json.loads(preparation_path.read_text(encoding='utf-8'))
    require(prepared['schema'] == 'sn100-discard-feedback-dependency-preparation-v2', 'Dependency-only plan required')
    before = binding(prepared)
    args.output.mkdir(mode=0o700)
    build, logs, runtime = (args.output / name for name in ('build', 'logs', 'runtime'))
    for directory in (build, logs, runtime, build / '.moc'):
        directory.mkdir()
    environment = prepare_environment(runtime)
    save(args.output / 'environment.json', environment)
    save(args.output / 'binding_before.json', before)
    report = {'schema': 'sn100-discard-feedback-targeted-v2', 'passed': False,
              'method': METHOD, 'phases': [], 'tests': [], 'reused_objects': prepared['reused_objects_sha256'],
              'dependency_source_count': len(prepared['dependency_source_sha256']),
              'binding_scope': 'Only actual standalone Makefile source/header/UI/resource/build inputs; QC independent',
              'old_whole_source_match_required': False, 'native_build_or_main_executed': False,
              'full_workflow_baseline_performance_board_or_instrument_acceptance': False}
    save(args.output / 'verification.json', report)
    try:
        flags = prepared['makefile_mapping']['flags']
        includes = ['-I' + str(build / '.moc')]
        for word in flags['INCPATH']:
            if word.startswith('-I') and not Path(word[2:]).is_absolute():
                word = '-I' + str((OLD_BUILD / word[2:]).resolve())
            includes.append(word)
        test_source = PROGRAM / REBUILT['.obj/sample_reset_workflow_test.o']
        moc = build / '.moc/sample_reset_workflow_test.moc'
        command = ['/usr/lib/qt5/bin/moc', *flags['DEFINES'], '--include', str(OLD_BUILD / '.moc/moc_predefs.h'),
                   *includes, str(test_source), '-o', str(moc)]
        report['phases'].append(run('moc', command, build, logs, 30))
        replacements = {}
        for original_object, source_name in REBUILT.items():
            new_object = build / Path(original_object).name
            command = ['/usr/bin/g++', '-c', *flags['CXXFLAGS'], *includes, '-o', str(new_object),
                       str(PROGRAM / source_name)]
            report['phases'].append(run('compile_' + new_object.stem, command, build, logs, 120))
            replacements[original_object] = new_object
        binary = build / 'sample_reset_workflow_test'
        objects = [str(replacements[name]) if name in replacements else str(OLD_BUILD / name)
                   for name in flags['OBJECTS']]
        command = ['/usr/bin/g++', *flags['LFLAGS'], '-o', str(binary), *objects, *flags['LIBS']]
        report['phases'].append(run('link', command, build, logs, 60))
        private = {str(path): sha(path) for path in (binary, moc, *replacements.values())}
        save(args.output / 'private_artifacts_before.json', private)
        save(args.output / 'link_object_mapping.json', {'ordered_original': flags['OBJECTS'],
                                                       'ordered_linked_paths': objects,
                                                       'recompiled_sources': REBUILT})
        for qpa in ('offscreen', 'xcb'):
            case = args.output / qpa
            case.mkdir()
            case_runtime = case / 'runtime'
            case_runtime.mkdir()
            environment = prepare_environment(case_runtime)
            os.environ['QT_QPA_PLATFORM'] = qpa
            save(case / 'environment.json', dict(environment, QT_QPA_PLATFORM=qpa))
            command = [str(binary), METHOD, '-v1', '-o', str(case / 'qt.txt') + ',txt',
                       '-o', str(case / 'qt.xml') + ',junitxml']
            receipt = run('test', command, case, case, 60)
            report['phases'].append(receipt)
            report['tests'].append({'qpa': qpa, 'actual_exit': receipt['actual_exit'], **qt_result(case, 3)})
            require(binding(prepared) == before, 'Standalone dependencies/artifacts changed during test')
            save(args.output / 'verification.json', report)
        require({path: sha(path) for path in private} == private, 'Fresh two objects/moc/binary changed')
        require(sha(preparation_path) == preparation_sha, 'Preparation changed')
        save(args.output / 'private_artifacts_after.json', private)
        save(args.output / 'binding_after.json', binding(prepared))
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
        report.update(all_owned_groups_gone=True, passed=True)
    except BaseException as error:
        report.update(error_type=type(error).__name__, error=str(error))
        raise
    finally:
        save(args.output / 'verification.json', report)
    print(json.dumps({'passed': report['passed'], 'verification': str(args.output / 'verification.json')}), flush=True)


if __name__ == '__main__':
    main()
