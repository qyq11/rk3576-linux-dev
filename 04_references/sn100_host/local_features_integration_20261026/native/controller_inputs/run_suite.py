#!/usr/bin/env python3
"""Serial legacy 16 calls plus QC page/shell at both QPAs; optional extra suites."""
import argparse
import json
import os
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from common import WORK, PROGRAM, NATIVE, FREEZE, DRIVERS, sha, save, load, now, require, verify_frozen, verify_native, verify_controller_inputs, private_environment, run_logged


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('attempt', help='New evidence suffix, e.g. final_01')
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--component-suite-json', type=Path)
    parser.add_argument('--components-approved', action='store_true')
    parser.add_argument('--start-at', help='Run a preliminary suffix; omitted calls are never counted as executed')
    args = parser.parse_args()
    require(args.root_go and os.geteuid() != 0 and re.fullmatch(r'[a-zA-Z0-9_-]+', args.attempt),
            'Explicit root go, ordinary WSL user and simple new suffix required')
    require(not args.component_suite_json or args.components_approved, 'Root component approval flag required')
    control_before = verify_controller_inputs()
    output = WORK / ('suite_' + args.attempt)
    require(not output.exists(), 'New suite evidence directory required; old failures are retained')
    output.mkdir()
    results = []
    omitted = []
    planned = []
    started = args.start_at is None
    try:
        save(output / 'controller_inputs_before.json', control_before)
        frozen = load(FREEZE)
        native = verify_native(frozen)
        binding = load(DRIVERS / 'binding_proof.json')
        binding_sha256 = sha(DRIVERS / 'binding_proof.json')
        require(binding['snapshot_sha256'] == sha(FREEZE), 'Binding proof freeze mismatch')
        require(binding['native_makefile_sha256'] == native['makefile_sha256'], 'Binding proof Makefile mismatch')
        for name, row in binding['native_objects'].items():
            require(sha(name) == row['object_sha256'], 'Bound product object changed: ' + name)
        build = NATIVE / 'wsl_release'
        env = dict(QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR='1', QT_AUTO_SCREEN_SCALE_FACTOR='0',
            SN100_WSL_IME='off', SN100_SHELL_APP=str(build / 'sn100_shell'),
            SN100_REPORT_RENDERER=str(build / 'sn100_report_renderer'),
            SN100_REPORT_PDF_PYTHON=os.environ.get('SN100_REPORT_PDF_PYTHON',
                '/mnt/c/Users/52400/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'),
            SN100_SHELL_SHA256=sha(build / 'sn100_shell'))

        def driver_source(driver, record):
            relative = record.get('source_relative')
            require(isinstance(relative, str) and relative, 'Driver source_relative missing: ' + driver)
            path = Path(relative)
            require(not path.is_absolute() and '..' not in path.parts, 'Unsafe driver source_relative: ' + driver)
            source = (PROGRAM / path).resolve(strict=True)
            require(source.is_relative_to(PROGRAM.resolve()), 'Driver source escapes PROGRAM: ' + driver)
            require(sha(source) == record['source_sha256'], 'Test source changed: ' + driver)
            return relative

        def run(name, driver, methods=(), extra=None, qpa='xcb', prepare_dirs=()):
            nonlocal started
            require(name not in planned, 'Duplicate planned suite name: ' + name)
            planned.append(name)
            if not started:
                if name != args.start_at:
                    omitted.append(name)
                    return
                started = True
            require(driver in binding['drivers'], 'Driver missing from fresh binding proof: ' + driver)
            record = binding['drivers'][driver]
            require(sha(DRIVERS / driver) == record['binary_sha256'], 'Bound driver changed: ' + driver)
            source_relative = driver_source(driver, record)
            stage = output / name
            stage.mkdir()
            current, private = private_environment(stage, qpa)
            current.update(env)
            current['QT_QPA_PLATFORM'] = qpa
            current.update(extra or {})
            private['explicit_test_environment'] = dict(env, QT_QPA_PLATFORM=qpa, **(extra or {}))
            save(stage / 'environment.json', private)
            for relative in prepare_dirs:
                require(isinstance(relative, str) and relative and not Path(relative).is_absolute() and '..' not in Path(relative).parts, 'Unsafe test preparation directory')
                directory = stage / relative
                require(directory.resolve().is_relative_to(stage.resolve()) and not directory.exists(), 'Directory must be a new child of its stage')
                directory.mkdir(parents=True)

            command = [str(DRIVERS / driver), *methods, '-o', str(stage / 'qt.txt') + ',txt',
                       '-o', str(stage / 'qt.xml') + ',junitxml']
            receipt = run_logged('test', command, stage, env=current, timeout=120)
            receipt.update(name=name, qpa=qpa, driver=driver, driver_sha256=record['binary_sha256'],
                           source_relative=source_relative, source_sha256=record['source_sha256'])
            text = (stage / 'qt.txt').read_text(encoding='utf-8', errors='strict')
            totals = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', text)
            receipt['totals'] = list(map(int, totals[-1])) if totals else None
            results.append(receipt)
            save(output / 'result.json', results)
            require(totals and totals[-1][1:] == ('0', '0'), 'QtTest failure/skipped/no totals: ' + name)
            require(re.search(r'Totals: \d+ passed, 0 failed, 0 skipped, 0 blacklisted,', text), 'Blacklisted/no complete Qt totals: ' + name)
            xml = ET.parse(stage / 'qt.xml').getroot()
            cases = xml.findall('testcase')
            require(xml.tag == 'testsuite' and cases and all(row.get('result') == 'pass' for row in cases)
                    and not any(xml.findall('.//' + tag) for tag in ('failure', 'error', 'skipped')), 'Qt XML testcase outcomes differ: ' + name)
            # Qt5 errors attribute can count qInfo/warnings, not failed testcases. Preserve it verbatim.
            receipt['xml_root_attributes'] = dict(xml.attrib)
            receipt['xml_sha256'] = sha(stage / 'qt.xml')
            receipt['txt_sha256'] = sha(stage / 'qt.txt')
            save(output / 'result.json', results)
            print(json.dumps(receipt), flush=True)

        for qpa in ('offscreen', 'xcb'):
            base = output / ('lis_' + qpa)
            run('lis_' + qpa, 'lis_settings_shell_test', extra={
                'SN100_LIS_APP': str(build / 'sn100_shell'), 'SN100_LIS_TEST_ROOT': str(base / 'runtime'),
                'SN100_LIS_EVIDENCE': str(base / 'evidence')}, qpa=qpa)
        for qpa in ('offscreen', 'xcb'):
            base = output / ('printer_' + qpa)
            run('printer_' + qpa, 'printer_settings_shell_test', extra={
                'SN100_PRINTER_APP': str(build / 'sn100_shell'), 'SN100_PRINTER_TEST_ROOT': str(base / 'runtime'),
                'SN100_PRINTER_EVIDENCE': str(base / 'evidence')}, qpa=qpa)
            base = output / ('login_' + qpa)
            run('login_' + qpa, 'password_login_shell_test', extra={
                'SN100_LOGIN_EVIDENCE_DIR': str(base / 'evidence'),
                'SN100_LOGIN_RUNTIME_DIR': str(base / 'runtime')}, qpa=qpa)
        base = output / 'core_shared'
        for method in ('writeExperiment', 'readExperiment', 'viewportExperiment',
                       'initializationFailureExperiment', 'applicationExitExperiment'):
            run('core_' + method, 'integrated_shell_test', [method], {'SN100_INTEGRATION_EVIDENCE_DIR': str(base)})
        run('catalog', 'catalog_shell_test', ('referencesAndGuards', 'samplePresetWorkflow',
            'fullProductStartupAndShutdown', 'productStartupFailure', 'unknownInitialActorFails'),
            {'SN100_CATALOG_EVIDENCE_DIR': str(output / 'catalog/evidence')})
        run('replay', 'status_replay_shell_test', extra={
            'SN100_STATUS_REPLAY_EVIDENCE_DIR': str(output / 'replay/evidence'),
            'SN100_STATUS_REPLAY_FIXTURE': str(PROGRAM / 'tests/fixtures/status_replay.json'),
            'SN100_STATUS_REPLAY_APP': str(build / 'sn100_shell')})
        run('loopback', 'status_loopback_shell_test', extra={
            'SN100_LOOPBACK_EVIDENCE_DIR': str(output / 'loopback/evidence'),
            'SN100_LOOPBACK_TEST_ROOT': str(output / 'loopback/runtime'),
            'SN100_LOOPBACK_APP': str(build / 'sn100_shell'),
            'SN100_LOOPBACK_APP_SHA256': sha(build / 'sn100_shell'),
            'SN100_LOOPBACK_REPLAY_FIXTURE': str(PROGRAM / 'tests/fixtures/status_replay.json')})
        run('instrument', 'instrument_page_test', extra={'SN100_INSTRUMENT_EVIDENCE_DIR': str(output / 'instrument/evidence')})
        run('report', 'integrated_report_test', ('committedHospitalAndReportLifecycle', 'persistedHospitalAndLegacyEntry'),
            {'SN100_INTEGRATED_REPORT_EVIDENCE_DIR': str(output / 'report/evidence')})
        for qpa in ('offscreen', 'xcb'):
            for role, driver in (('page', 'qc_local_page_test'), ('shell', 'qc_local_shell_test')):
                name = 'qc_' + role + '_' + qpa
                base = output / name
                run(name, driver, extra={'SN100_QC_TEST_ROOT': str(base / 'runtime'),
                    'SN100_QC_EVIDENCE': str(base / 'evidence'), 'SN100_QC_APP': str(build / 'sn100_shell')}, qpa=qpa)
        # Mandatory original15 descriptor calls plus the original20 calls above remain intact.
        legacy_planned_names = set(planned)
        suites = load(Path(__file__).with_name('extra_suite.json'))
        legacy_planned_names.update(suite['name'] + '_' + qpa for suite in suites for qpa in suite['qpas'])
        component_planned_names = set()
        save(output / 'extra_suite_descriptor.json', suites)
        component_digest = None
        if args.component_suite_json:
            require(args.component_suite_json.is_absolute(), 'Component descriptor must be absolute')
            components = load(args.component_suite_json)
            require(isinstance(components, list) and components, 'Nonempty component plan required')
            component_digest = sha(args.component_suite_json)
            save(output / 'component_suite_descriptor.json', components)
            component_planned_names = {suite['name'] + '_' + qpa for suite in components for qpa in suite['qpas']}
            suites = suites + components
        for suite in suites:
            require(isinstance(suite, dict) and isinstance(suite['qpas'], list), 'Actual descriptor types required')
            for qpa in suite['qpas']:
                require(qpa in ('offscreen', 'xcb'), 'Unknown QPA in approved descriptor')
                name = suite['name'] + '_' + qpa
                substitutions = {'stage': str(output / name), 'app': str(build / 'sn100_shell'),
                                 'renderer': str(build / 'sn100_report_renderer'), 'program': str(PROGRAM)}
                extra = {key: value.format(**substitutions) for key, value in suite.get('env', {}).items()}
                run(name, suite['driver'], suite.get('methods', ()), extra, qpa, suite.get('prepare_dirs', ()))
        if component_digest is not None:
            require(sha(args.component_suite_json) == component_digest, 'Component descriptor changed while running')
        require(started, 'Requested start-at name was not found')
        verify_native(frozen)
        require(sha(DRIVERS / 'binding_proof.json') == binding_sha256, 'Binding proof changed during suite')
        for receipt in results:
            driver = receipt['driver']
            record = binding['drivers'][driver]
            require(sha(DRIVERS / driver) == record['binary_sha256'], 'Executed driver changed: ' + driver)
            driver_source(driver, record)
        control_after = verify_controller_inputs()
        save(output / 'controller_inputs_after.json', control_after)
        require(control_after == control_before, 'Controller inputs changed during suite')
        save(output / 'verification.json', {'schema': 'sn100-local-features-cumulative-v1', 'passed': True,
             'finished_utc': now(), 'invocations': len(results),
             'full_suite': not omitted and len(results) == len(planned), 'omitted_invocations': omitted,
             'planned_invocations': planned, 'legacy_baseline_plan_invocations': len(legacy_planned_names),
             'components_included': bool(args.component_suite_json),
             'new_unified_shell_contracts_verified': False,
             'pending_required_contracts': ['D1 single exit with real warning ACK', 'other-page draft Cancel must not retain exit retry', 'actual main HospitalStore ready/closed independent dictionary dispatch', 'explicit alarm flag/default missing/incompatible source/reopen/device-action restrictions'],
             'legacy_invocations': sum(row['name'] in legacy_planned_names for row in results),
             'component_invocations': sum(row['name'] in component_planned_names for row in results),
             'component_planned_invocations': len(component_planned_names),
             'qc_invocations': sum(row['name'].startswith(('qc_page_','qc_shell_')) for row in results),
             'source_inputs': len(frozen), 'source_unchanged': True,
             'main_sha256': sha(build / 'sn100_shell'), 'renderer_sha256': sha(build / 'sn100_report_renderer'),
             'scope': ('Selected suffix preliminary gate; omitted calls have not run. ' if omitted else 'Full cumulative plan. ')
                 + 'Actual NEW native objects and product exits. Component tests do not establish new main wiring/D1 guards. Windows/RK/instrument/clinical/budgets/long stability pending'})
    except BaseException as error:
        save(output / 'failure.json', {'error_type': type(error).__name__, 'error': str(error),
             'failed_utc': now(), 'completed_results': results, 'old_failures_preserved': True})
        raise


if __name__ == '__main__':
    main()
