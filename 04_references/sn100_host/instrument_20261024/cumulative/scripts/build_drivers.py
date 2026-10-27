"""Build cumulative QtTest drivers against this checkpoint's final production objects.

No GUI is run. No file from the live formal source directory is read.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


HERE = Path(__file__).resolve().parent
WORK = HERE.parent
SOURCE = WORK / 'source/06_programs/sn100_host'
BUILD = WORK / 'native/wsl_release'
EXPECTED_MAIN = 'c5da853069071eedc12a601cb5cab8be0529e726196b8ce4d79667b7c232aeb3'
DRIVERS = ('integrated_shell_test', 'status_replay_shell_test')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def inputs():
    return {str(path.relative_to(SOURCE)): sha(path)
            for path in sorted(SOURCE.rglob('*')) if path.is_file()}


def production_objects():
    makefile = (BUILD / 'Makefile').read_text().replace('\\\n', ' ')
    match = re.search(r'^OBJECTS\s*=\s*(.+)$', makefile, re.M)
    if not match:
        raise SystemExit('No production object inventory')
    all_objects = [BUILD / name for name in match.group(1).split()]
    if len(all_objects) != len(set(all_objects)):
        raise SystemExit('Duplicate production object')
    if set(all_objects) != set((BUILD / '.obj').glob('*.o')):
        raise SystemExit('Unexpected missing/unlisted production object')
    if len([path for path in all_objects if path.name == 'main.o']) != 1:
        raise SystemExit('Expected exactly one production main.o')
    objects = [path for path in all_objects if path.name != 'main.o']
    if len(objects) != 136 or any(not path.is_file() for path in objects):
        raise SystemExit(f'Expected 136 final non-main production objects; found {len(objects)}')
    return objects


def projects(objects):
    for name in DRIVERS:
        target = HERE / name
        target.mkdir(exist_ok=True)
        project = '''QT += core gui widgets sql testlib printsupport xml qml charts svg network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
MOC_DIR = .moc
OBJECTS_DIR = .driver_objects
DEFINES += NO_BARCODE HAVE_QT5
'''
        project += f'TARGET = {name}\nINCLUDEPATH += {SOURCE}\n'
        project += f'SOURCES += {SOURCE / "tests" / (name + ".cpp")}\n'
        project += 'OBJECTS += ' + ' '.join(map(str, objects)) + '\n'
        (target / 'driver.pro').write_text(project, encoding='utf-8')


def contract():
    return {
        'source': str(SOURCE),
        'input_policy': 'Use this frozen source only; never substitute live formal source.',
        'product': str(BUILD / 'sn100_shell'),
        'product_helper': str(BUILD / 'sn100_report_renderer'),
        'product_helper_requirement': 'Keep the final main/helper pair together in native/wsl_release. These drivers do not export PDF; child product --history uses the ordinary report-capable shell.',
        'expected_product_sha256': EXPECTED_MAIN,
        'runtime_root': str(HERE / 'runtime'),
        'logs_root': str(HERE / 'logs'),
        'common': {'user': 'qyq', 'qmake': '/usr/lib/qt5/bin/qmake',
                   'qpa': ['offscreen', 'xcb'], 'style': 'Fusion', 'font': 'Noto Sans CJK SC 11',
                   'gui_execution': 'One coordinated serial GUI slot; build script never launches GUI.',
                   'runtime_libraries': 'Use target_wrapper.sh for system x86_64 Qt libraries/plugins.'},
        'integrated_shell_test': {
            'binary': str(HERE / 'integrated_shell_test/integrated_shell_test'),
            'environment': {'SN100_INTEGRATION_EVIDENCE_DIR': str(HERE / 'runtime/<qpa>/integrated'),
                            'SN100_SHELL_APP': str(BUILD / 'sn100_shell')},
            'sequence': ['writeExperiment', 'readExperiment'],
            'process_rule': 'Each method must run in a separate OS process. write/read share the same new evidence root. Do not invoke the entire suite without method arguments.',
            'additional_functional_cases': ['initializationFailureExperiment'],
            'xcb_only_cases': ['viewportExperiment', 'applicationExitExperiment'],
            'viewport_scope': 'Requires real screen geometry, mapped/active native windows and QApplication.widgetAt hit testing. Offscreen is not applicable; preserve its failed invocation as an environment-boundary observation.',
            'xcb_dependencies': ['xdotool in PATH', 'libX11.so.6', 'DISPLAY with the coordinated WSLg session'],
            'xcb_exit_scope': '12-second idle then PID-verified WM_DELETE_WINDOW, functional exit only; no performance claim.',
            'skipped_this_round': 'measureExperiment (60 seconds); separate 30-second seven-store instrument baseline is planned.',
            'fixtures': 'Anonymous account secret generated only at runtime; history fixture embedded in the production QRC. No external fixture argument.',
            'sensitive_runtime': 'Keep all runtime databases, tags and transient secret files under temp. Do not archive account database fields.'},
        'status_replay_shell_test': {
            'binary': str(HERE / 'status_replay_shell_test/status_replay_shell_test'),
            'environment': {'SN100_STATUS_REPLAY_EVIDENCE_DIR': str(HERE / 'runtime/<qpa>/status_replay'),
                            'SN100_STATUS_REPLAY_FIXTURE': str(SOURCE / 'tests/fixtures/status_replay.json'),
                            'SN100_STATUS_REPLAY_APP': str(BUILD / 'sn100_shell')},
            'process_rule': 'One full-suite process for each QPA. Each evidence root must be absolute and must not exist before initTestCase.',
            'child_product': '--offline --data-dir <new product_runtime> --status-replay <checkpoint fixture>',
            'input_failure_child': 'Relative replay path must exit 2 without creating its requested data root.'},
        'qt_test_output': 'Add -o <absolute log>.txt,txt -o <absolute log>.xml,xml to every invocation.',
        'command_pattern': 'env QT_QPA_PLATFORM=<qpa> <suite env...> ./target_wrapper.sh <binary> [method] -o <txt>,txt -o <xml>,xml',
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['prepare', 'build'])
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    (HERE / 'logs').mkdir(exist_ok=True)
    guard_path = HERE / 'checkpoint_inputs.json'
    current_inputs = inputs()
    if len(current_inputs) != 468:
        raise SystemExit(f'Expected the 468-file final source checkpoint, got {len(current_inputs)}')
    if args.mode == 'prepare':
        if guard_path.exists() and json.loads(guard_path.read_text()) != current_inputs:
            raise SystemExit('Previously prepared cumulative source guard differs; do not overwrite it')
        write_json(guard_path, current_inputs)
    elif not guard_path.exists() or json.loads(guard_path.read_text()) != current_inputs:
        raise SystemExit('Prepared source fingerprint does not match this checkpoint')

    objects = production_objects()
    projects(objects)
    write_json(HERE / 'run_contract.json', contract())
    (HERE / 'target_wrapper.sh').write_text('''#!/bin/sh
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export LD_LIBRARY_PATH
QT_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt5/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
export QT_PLUGIN_PATH
exec "$@"
''', encoding='utf-8')
    (HERE / 'target_wrapper.sh').chmod(0o755)
    if args.mode == 'prepare':
        print(json.dumps({'prepared': list(DRIVERS), 'checkpoint_inputs': len(current_inputs),
                          'production_objects': len(objects), 'linked': False, 'gui_run': False}))
        return

    final_receipt = json.loads((WORK / 'native_final_result.json').read_text())
    if final_receipt.get('exit_code') != 0 or final_receipt.get('source_count') != 468 or not final_receipt.get('inputs_unchanged'):
        raise SystemExit('Final native receipt does not confirm the fixed source checkpoint')
    main_path = BUILD / 'sn100_shell'
    helper_path = BUILD / 'sn100_report_renderer'
    if sha(main_path) != EXPECTED_MAIN or not helper_path.is_file():
        raise SystemExit('Final production main/helper pair does not match the authorized checkpoint')
    before = {str(path): sha(path) for path in objects}
    write_json(HERE / 'production_objects.json', before)
    products_before = {str(path): sha(path) for path in (main_path, helper_path)}
    drivers = []
    for name in DRIVERS:
        target = HERE / name
        with (HERE / 'logs' / (name + '_build.log')).open('wb') as log:
            subprocess.run(['/usr/lib/qt5/bin/qmake', str(target / 'driver.pro'), '-spec', 'linux-g++',
                            'CONFIG+=release', 'CONFIG-=debug'], cwd=target, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
            subprocess.run(['make', '-j2'], cwd=target, stdout=log, stderr=subprocess.STDOUT, check=True)
        drivers.append({'name': name, 'binary': str(target / name), 'sha256': sha(target / name),
                        'source': str(SOURCE / 'tests' / (name + '.cpp')),
                        'source_sha256': sha(SOURCE / 'tests' / (name + '.cpp'))})
    if before != {str(path): sha(path) for path in objects}:
        raise SystemExit('Production objects changed during driver linking')
    if products_before != {str(path): sha(path) for path in (main_path, helper_path)}:
        raise SystemExit('Production main/helper changed during driver linking')
    if current_inputs != inputs():
        raise SystemExit('Checkpoint sources changed during driver linking')
    result = {'exit_code': 0, 'source_files': len(current_inputs), 'linked_objects': len(objects),
              'excluded_object': 'main.o', 'production_objects_unchanged': True,
              'main_helper_unchanged': True, 'checkpoint_sources_unchanged': True,
              'native_final_receipt_sha256': sha(WORK / 'native_final_result.json'),
              'drivers': drivers, 'product_pair': products_before, 'gui_run': False}
    write_json(HERE / 'build_result.json', result)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
