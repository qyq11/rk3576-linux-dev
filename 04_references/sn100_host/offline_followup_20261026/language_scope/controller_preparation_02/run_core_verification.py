"""Deferred QtCore-only verification; no formal application objects are used."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'temp/sn100_host/offline_followup_20261026/language_scope'
FIXTURE_SHA = '307cef95dd5b36ff215055d427e1885b7fc3650c9224cf76d63056545996ff60'
METHODS = ['emptySamplesAreNotCoverage', 'frameworkMessagesDoNotCoverApplicationContext',
    'partialCatalogIsNotCompleteCoverage', 'sampleCoverageDoesNotSwitchTheApplication',
    'relativePathIsRejectedWithoutImplicitSearch', 'missingCatalogIsAnExplicitFailure',
    'malformedCatalogDoesNotModifyItsInput', 'providedQtEnglishSidecarIsNotApplicationCoverage']


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def save(path, value):
    temporary = path.with_name(path.name + '.writing')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def inputs():
    paths = [WORK / 'run_core_verification.py']
    for folder in ('draft', 'fixtures'):
        paths += [path for path in (WORK / folder).rglob('*') if path.is_file()]
    return {path.relative_to(WORK).as_posix(): sha(path) for path in sorted(paths)}


def group_exists(pid):
    try:
        os.killpg(pid, 0)
        return True
    except ProcessLookupError:
        return False


def reap_owned(process):
    actions = []
    for action, number in (('SIGTERM', signal.SIGTERM), ('SIGKILL', signal.SIGKILL)):
        if group_exists(process.pid):
            os.killpg(process.pid, number)
            actions.append(action)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass
        if not group_exists(process.pid):
            break
        time.sleep(0.05)
    process.wait(timeout=3)
    return actions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute-slot', action='store_true')
    parser.add_argument('--input-manifest', type=Path, required=True)
    parser.add_argument('--input-manifest-sha256', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if not args.execute_slot:
        parser.error('Wait for the root window to grant the exclusive execution slot')
    require(os.name == 'posix', 'This queued controller is for the existing WSL Qt5 environment')
    require(Path(__file__).resolve() == WORK / 'run_core_verification.py', 'Run the private temp controller, not an archive copy')
    require(args.output_dir.is_absolute() and not args.output_dir.exists(), 'New absolute output directory required')
    out = args.output_dir.resolve()
    require(out.is_relative_to(WORK.resolve()) and out != WORK.resolve(), 'Output must remain inside this task temp directory')
    require(args.input_manifest.is_absolute(), 'Explicit absolute manifest required')
    require(re.fullmatch('[0-9a-f]{64}', args.input_manifest_sha256) is not None, 'Explicit lowercase SHA-256 required')
    out.mkdir(parents=True)
    deadline = time.monotonic() + 300
    result = {'schema': 'sn100-language-core-verification-v1', 'started_utc': now(),
        'scope': 'Private QtCore resource inspection and eight tests; not formal language-page or whole-app switching',
        'formal_application_objects_used': False, 'formal_source_modified': False,
        'system_language_modified': False, 'gui_started': False, 'retries': 0,
        'stages': [], 'passed': False, 'slot_released': False}
    owned = []

    def run(name, command, cwd, seconds, selected=None, allowed=(0,)):
        remaining = deadline - time.monotonic()
        require(remaining > 0, 'Five-minute controller bound reached')
        bound = min(seconds, remaining)
        env = os.environ.copy()
        removed = []
        for key in ('QMAKESPEC', 'QMAKEPATH', 'QMAKEFEATURES', 'QT_PLUGIN_PATH',
                    'QML2_IMPORT_PATH', 'PKG_CONFIG_SYSROOT_DIR', 'PKG_CONFIG_LIBDIR'):
            if key in env:
                removed.append(key)
                env.pop(key)
        env.update({'TMPDIR': str(out / 'tmp')})
        env.update(selected or {})
        receipt = {'name': name, 'command': [str(x) for x in command], 'cwd': str(cwd),
            'started_utc': now(), 'timeout_seconds': bound,
            'selected_environment': {'TMPDIR': str(out / 'tmp'), **(selected or {})},
            'removed_build_environment_names': removed,
            'stdout': name + '.stdout.log', 'stderr': name + '.stderr.log'}
        save(out / (name + '.command.json'), receipt)
        process = None
        failure = None
        actions = []
        cleanup_error = None
        orphan = False
        started = time.monotonic()
        try:
            with (out / receipt['stdout']).open('wb') as stdout, (out / receipt['stderr']).open('wb') as stderr:
                process = subprocess.Popen(receipt['command'], cwd=cwd, env=env,
                    stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr, start_new_session=True)
                owned.append(process)
                save(out / (name + '.process.json'), {'pid': process.pid, 'pgid': process.pid, 'state': 'running'})
                process.wait(timeout=bound)
        except BaseException as error:
            failure = error
        finally:
            try:
                if process is not None and (process.poll() is None or group_exists(process.pid)):
                    orphan = process.poll() is not None and group_exists(process.pid)
                    actions = reap_owned(process)
                alive = process is not None and group_exists(process.pid)
            except BaseException as error:
                cleanup_error = str(error)
                alive = True
            receipt.update(pid=None if process is None else process.pid,
                pgid=None if process is None else process.pid,
                state='not_started' if process is None else ('finished' if process.poll() is not None else 'exit_unknown'),
                exit_code=None if process is None else process.returncode,
                timed_out=isinstance(failure, subprocess.TimeoutExpired),
                error=None if failure is None else str(failure),
                error_type=None if failure is None else type(failure).__name__,
                cleanup_actions=actions, cleanup_error=cleanup_error,
                orphaned_children_found=orphan, process_group_exists=alive,
                seconds=time.monotonic() - started, finished_utc=now())
            save(out / (name + '.exit.json'), receipt)
            save(out / (name + '.process.json'), {key: receipt[key] for key in ('pid', 'pgid', 'state', 'exit_code', 'process_group_exists')})
            (out / (name + '.exit')).write_text(str(receipt['exit_code']) + '\n', encoding='ascii')
            result['stages'].append(receipt)
        require(failure is None and cleanup_error is None and not alive and not orphan,
                name + ' failed or left owned processes; raw receipts retained')
        require(receipt['exit_code'] in allowed, name + ' returned unexpected actual exit ' + str(receipt['exit_code']))
        return receipt

    try:
        result['input_manifest'] = {'path': str(args.input_manifest),
            'expected_sha256': args.input_manifest_sha256, 'actual_sha256': sha(args.input_manifest)}
        require(result['input_manifest']['actual_sha256'] == args.input_manifest_sha256, 'Private manifest SHA changed')
        manifest = json.loads(args.input_manifest.read_text(encoding='utf-8-sig'))
        before = inputs()
        save(out / 'input_sha_before.json', before)
        changed = sorted(name for name in set(before) | set(manifest['files']) if before.get(name) != manifest['files'].get(name))
        save(out / 'input_manifest_comparison.json', {'different_paths': changed,
            'differences': {name: {'expected': manifest['files'].get(name), 'actual': before.get(name)} for name in changed}})
        require(not changed, 'Complete private input set or SHA differs')
        fixture = json.loads((WORK / 'fixtures/qt_en_catalog.json').read_text(encoding='utf-8'))
        payload = bytes.fromhex(fixture['hex'])
        require(len(payload) == 23 and hashlib.sha256(payload).hexdigest() == fixture['sha256'] == FIXTURE_SHA,
                'Provided 23-byte catalog fixture changed')
        for folder in ('build_probe', 'build_test', 'fixtures', 'tmp'):
            (out / folder).mkdir()
        catalog = out / 'fixtures/qt_en.qm'
        catalog.write_bytes(payload)
        save(out / 'catalog_input_before.json', {'path': str(catalog), 'bytes': len(payload), 'sha256': sha(catalog)})
        tools = {'qmake': Path('/usr/lib/qt5/bin/qmake'), 'moc': Path('/usr/lib/qt5/bin/moc'),
            'make': Path(shutil.which('make') or '/missing-make'),
            'gcc': Path(shutil.which('gcc') or '/missing-gcc'),
            'g++': Path(shutil.which('g++') or '/missing-g++'),
            'Qt5Core': Path('/usr/lib/x86_64-linux-gnu/libQt5Core.so.5'),
            'Qt5Test': Path('/usr/lib/x86_64-linux-gnu/libQt5Test.so.5')}

        def tool_snapshot():
            return {name: {'path': str(path.resolve(strict=True)), 'sha256': sha(path)} for name, path in tools.items()}

        tools_before = tool_snapshot()
        save(out / 'toolchain_before.json', tools_before)
        run('qmake_version', [tools['qmake'], '-v'], out, 10)
        version_text = (out / 'qmake_version.stdout.log').read_text(errors='replace') + (out / 'qmake_version.stderr.log').read_text(errors='replace')
        require('Qt version 5.' in version_text, 'Expected existing Qt5 qmake')
        compiler_options = ['QMAKE_CC=' + str(tools['gcc'].resolve()),
                            'QMAKE_CXX=' + str(tools['g++'].resolve()), 'QMAKE_LINK=' + str(tools['g++'].resolve())]
        for target, project in (('probe', WORK / 'draft/catalog_probe.pro'), ('test', WORK / 'draft/tests/catalog_probe_test.pro')):
            build = out / ('build_' + target)
            run('qmake_' + target, [tools['qmake'], project, '-spec', 'linux-g++',
                'CONFIG+=release', 'CONFIG-=debug', 'CONFIG-=debug_and_release'] + compiler_options, build, 30)
            run('make_' + target, [tools['make'], '-j2'], build, 120)
            save(out / (target + '_build_artifacts.json'), {'makefile_sha256': sha(build / 'Makefile'),
                'binary_sha256': sha(build / ('language_catalog_probe' if target == 'probe' else 'language_catalog_test'))})
        run('test', [out / 'build_test/language_catalog_test', '-o', str(out / 'qt.txt') + ',txt',
            '-o', str(out / 'qt.xml') + ',xml'], out, 30,
            {'LANGUAGE_SCOPE_TEST_DIR': str(out / 'test_data')})
        xml = ET.parse(out / 'qt.xml')
        actual_methods = {node.attrib['name'] for node in xml.findall('./TestFunction')}
        require(set(METHODS).issubset(actual_methods), 'One or more planned tests were not executed')
        totals = re.search(r'Totals: ([0-9]+) passed, ([0-9]+) failed, ([0-9]+) skipped', (out / 'qt.txt').read_text(encoding='utf-8'))
        require(totals is not None and int(totals[2]) == 0 and int(totals[3]) == 0, 'Qt tests failed or skipped')
        result['test_totals'] = {'passed': int(totals[1]), 'failed': int(totals[2]), 'skipped': int(totals[3]),
                                 'business_methods': METHODS}
        probe = run('catalog_probe', [out / 'build_probe/language_catalog_probe', '--catalog', catalog], out, 10, allowed=(0, 2, 3))
        observed = json.loads((out / 'catalog_probe.stdout.log').read_text(encoding='utf-8'))
        save(out / 'catalog_observed.json', observed)
        require(observed['translated_sample_count'] == 0 and not observed['sample_coverage_complete'],
                'Provided Qt sidecar unexpectedly covers application samples; preserve and review')
        require(len(observed['samples']) == 2 and all(not row['entry_found'] for row in observed['samples']), 'Unexpected sample observations')
        require(not any(observed[name] for name in ('full_application_switching_assessed', 'translator_installed', 'language_setting_written')),
                'Probe claimed or performed a language switch')
        if observed['catalog_loaded']:
            require(probe['exit_code'] == 3 and not observed['error'], 'Loaded-but-uncovered catalog must retain actual exit 3')
        else:
            require(probe['exit_code'] == 2 and bool(observed['error']), 'Load failure must retain actual exit 2')
        result['catalog_actual_exit'] = probe['exit_code']
        result['catalog_loaded'] = observed['catalog_loaded']
        result['catalog_application_samples_found'] = observed['translated_sample_count']
        after = inputs()
        save(out / 'input_sha_after.json', after)
        save(out / 'catalog_input_after.json', {'path': str(catalog), 'bytes': catalog.stat().st_size, 'sha256': sha(catalog)})
        tools_after = tool_snapshot()
        save(out / 'toolchain_after.json', tools_after)
        require(before == after == manifest['files'], 'Private inputs changed during execution')
        require(sha(args.input_manifest) == args.input_manifest_sha256 and sha(catalog) == FIXTURE_SHA, 'Manifest or fixture changed')
        require(tools_before == tools_after, 'Existing compiler/Qt tools changed during execution')
        result['input_and_toolchain_sha_unchanged'] = True
        result['passed'] = True
    except BaseException as error:
        result['error'] = str(error)
        result['exception_type'] = type(error).__name__
    finally:
        cleanup_errors = []
        for process in owned:
            try:
                if process.poll() is None or group_exists(process.pid):
                    reap_owned(process)
            except BaseException as error:
                cleanup_errors.append({'pid': process.pid, 'error': str(error)})
        groups = []
        for process in owned:
            try:
                groups.append({'pgid': process.pid, 'exists': group_exists(process.pid)})
            except BaseException as error:
                groups.append({'pgid': process.pid, 'exists': True, 'check_error': str(error)})
                cleanup_errors.append({'pid': process.pid, 'error': str(error)})
        result['owned_process_groups'] = groups
        result['cleanup_errors'] = cleanup_errors
        result['slot_released'] = not cleanup_errors and not any(row['exists'] for row in groups)
        if not result['slot_released']:
            result['passed'] = False
        if 'before' in locals():
            try:
                final_inputs = inputs()
                save(out / 'input_sha_final.json', final_inputs)
                result['private_inputs_unchanged_at_exit'] = final_inputs == before
                if final_inputs != before:
                    result['passed'] = False
            except BaseException as error:
                result['final_input_check_error'] = str(error)
                result['passed'] = False
        for label in ('catalog', 'tools', 'manifest'):
            try:
                if label == 'catalog' and 'catalog' in locals() and catalog.is_file():
                    final_catalog = {'path': str(catalog), 'bytes': catalog.stat().st_size, 'sha256': sha(catalog)}
                    save(out / 'catalog_input_final.json', final_catalog)
                    require(final_catalog['sha256'] == FIXTURE_SHA, 'Catalog fixture changed before exit')
                elif label == 'tools' and 'tools_before' in locals():
                    final_tools = tool_snapshot()
                    save(out / 'toolchain_final.json', final_tools)
                    require(final_tools == tools_before, 'Existing toolchain changed before exit')
                elif label == 'manifest':
                    final_manifest = {'path': str(args.input_manifest), 'sha256': sha(args.input_manifest)}
                    save(out / 'manifest_sha_final.json', final_manifest)
                    require(final_manifest['sha256'] == args.input_manifest_sha256, 'Input manifest changed before exit')
            except BaseException as error:
                result[label + '_final_check_error'] = str(error)
                result['passed'] = False
        result['finished_utc'] = now()
        result['controller_return_code'] = 0 if result['passed'] else 1
        result['outer_actual_exit_requires_caller_receipt'] = True
        save(out / 'execution.json', result)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return result['controller_return_code']


if __name__ == '__main__':
    sys.exit(main())
