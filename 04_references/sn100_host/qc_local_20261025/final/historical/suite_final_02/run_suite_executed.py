#!/usr/bin/env python3
"""Serial legacy 16 calls plus QC page/shell at both QPAs; optional extra suites."""
import argparse
import json
import os
from pathlib import Path
import re
from common import WORK, PROGRAM, NATIVE, FREEZE, DRIVERS, sha, save, load, now, require, verify_frozen, verify_native, run_logged


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('attempt', help='New evidence suffix, e.g. final_01')
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--extra-suite-json', type=Path)
    args = parser.parse_args()
    require(args.root_go and os.geteuid() != 0 and re.fullmatch(r'[a-zA-Z0-9_-]+', args.attempt),
            'Explicit root go, ordinary WSL user and simple new suffix required')
    output = WORK / ('suite_' + args.attempt)
    require(not output.exists(), 'New suite evidence directory required; old failures are retained')
    output.mkdir()
    results = []
    try:
        frozen = load(FREEZE)
        native = verify_native(frozen)
        binding = load(DRIVERS / 'binding_proof.json')
        binding_sha256 = sha(DRIVERS / 'binding_proof.json')
        require(binding['snapshot_sha256'] == sha(FREEZE), 'Binding proof freeze mismatch')
        require(binding['native_makefile_sha256'] == native['makefile_sha256'], 'Binding proof Makefile mismatch')
        for name, row in binding['native_objects'].items():
            require(sha(name) == row['object_sha256'], 'Bound product object changed: ' + name)
        build = NATIVE / 'wsl_release'
        env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_SCALE_FACTOR='1', QT_AUTO_SCREEN_SCALE_FACTOR='0',
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

        def run(name, driver, methods=(), extra=None, qpa='xcb'):
            require(driver in binding['drivers'], 'Driver missing from fresh binding proof: ' + driver)
            record = binding['drivers'][driver]
            require(sha(DRIVERS / driver) == record['binary_sha256'], 'Bound driver changed: ' + driver)
            source_relative = driver_source(driver, record)
            stage = output / name
            stage.mkdir()
            current = dict(env, QT_QPA_PLATFORM=qpa, **(extra or {}))
            command = [str(DRIVERS / driver), *methods, '-o', str(stage / 'qt.txt') + ',txt',
                       '-o', str(stage / 'qt.xml') + ',junitxml']
            receipt = run_logged('test', command, stage, env=current, timeout=120)
            receipt.update(name=name, qpa=qpa, driver=driver, driver_sha256=record['binary_sha256'],
                           source_relative=source_relative, source_sha256=record['source_sha256'])
            text = (stage / 'qt.txt').read_text(encoding='utf-8', errors='replace')
            totals = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', text)
            receipt['totals'] = list(map(int, totals[-1])) if totals else None
            results.append(receipt)
            save(output / 'result.json', results)
            require(totals and totals[-1][1:] == ('0', '0'), 'QtTest failure/skipped/no totals: ' + name)
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
        if args.extra_suite_json:
            require(args.extra_suite_json.is_absolute(), 'Extra suite descriptor must be absolute')
            suites = load(args.extra_suite_json)
            save(output / 'extra_suite_descriptor.json', suites)
            for suite in suites:
                for qpa in suite.get('qpas', ['offscreen', 'xcb']):
                    name = suite['name'] + '_' + qpa
                    substitutions = {'stage': str(output / name), 'app': str(build / 'sn100_shell'),
                                     'renderer': str(build / 'sn100_report_renderer'), 'program': str(PROGRAM)}
                    extra = {key: value.format(**substitutions) for key, value in suite.get('env', {}).items()}
                    run(name, suite['driver'], suite.get('methods', ()), extra, qpa)
        verify_native(frozen)
        require(sha(DRIVERS / 'binding_proof.json') == binding_sha256, 'Binding proof changed during suite')
        for receipt in results:
            driver = receipt['driver']
            record = binding['drivers'][driver]
            require(sha(DRIVERS / driver) == record['binary_sha256'], 'Executed driver changed: ' + driver)
            driver_source(driver, record)
        save(output / 'verification.json', {'schema': 'sn100-four-windows-cumulative-v1', 'passed': True,
             'finished_utc': now(), 'invocations': len(results), 'legacy_invocations': 16,
             'qc_invocations': 4, 'source_inputs': len(frozen), 'source_unchanged': True,
             'main_sha256': sha(build / 'sn100_shell'), 'renderer_sha256': sha(build / 'sn100_report_renderer'),
             'scope': 'Actual native Release objects and real product subprocess exits; original Windows/RK/instrument/clinical/long stability pending'})
    except BaseException as error:
        save(output / 'failure.json', {'error_type': type(error).__name__, 'error': str(error),
             'failed_utc': now(), 'completed_results': results, 'old_failures_preserved': True})
        raise


if __name__ == '__main__':
    main()
