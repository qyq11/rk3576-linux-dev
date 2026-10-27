"""Archive a small allowlist of cumulative evidence, never runtime account data."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parent
WORK = ROOT.parent
PROJECT = WORK.parents[2]
SOURCE = WORK / 'source/06_programs/sn100_host'
ARCHIVE = PROJECT / '04_references/sn100_host/instrument_20261024/cumulative'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def file_audit(expected, paths_are_absolute=False):
    failures = []
    for name, expected_sha in expected.items():
        path = Path(name) if paths_are_absolute else SOURCE / name
        actual = sha(path) if path.is_file() else None
        if actual != expected_sha:
            failures.append({'path': name, 'expected': expected_sha, 'actual': actual})
    return {'files': len(expected), 'sha256_mismatches': failures}


def qt_result(qpa, name):
    path = ROOT / 'logs' / qpa / (name + '.xml')
    xml = ET.parse(path).getroot()
    incidents = xml.findall('./TestFunction/Incident')
    counts = {kind: sum(item.attrib.get('type') == kind for item in incidents)
              for kind in ('pass', 'fail', 'skip')}
    txt = (path.with_suffix('.txt')).read_text(encoding='utf-8')
    match = re.search(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped,.*?, (\d+)ms', txt)
    if not match:
        raise RuntimeError(f'No QtTest text totals: {path}')
    if tuple(map(int, match.groups()[:3])) != (counts['pass'], counts['fail'], counts['skip']):
        raise RuntimeError(f'Text/XML result mismatch: {path}')
    root_duration = xml.find('./Duration')
    return {'qpa': qpa, 'case': name, 'passed': counts['pass'], 'failed': counts['fail'],
            'skipped': counts['skip'], 'text_elapsed_ms': int(match.group(4)),
            'xml_elapsed_ms': float(root_duration.attrib['msecs']),
            'exit_code': 1 if counts['fail'] else 0,
            'exit_code_basis': 'Coordinator completed the set-e runner sequence; QtTest text/XML outcomes independently agree. Separate per-invocation shell exit receipts were not persisted.',
            'txt': f'logs/{qpa}/{name}.txt', 'xml': f'logs/{qpa}/{name}.xml'}


def main():
    build = read_json(ROOT / 'build_result.json')
    checkpoint = read_json(ROOT / 'checkpoint_inputs.json')
    objects = read_json(ROOT / 'production_objects.json')
    source_audit = file_audit(checkpoint)
    object_audit = file_audit(objects, True)
    product_audit = file_audit(build['product_pair'], True)
    driver_audit = file_audit({row['binary']: row['sha256'] for row in build['drivers']}, True)
    actual_source_count = sum(path.is_file() for path in SOURCE.rglob('*'))
    if actual_source_count != 468 or len(checkpoint) != 468 or len(objects) != 136:
        raise RuntimeError('Unexpected checkpoint source/object count')
    if any(audit['sha256_mismatches'] for audit in (source_audit, object_audit, product_audit, driver_audit)):
        raise RuntimeError('Cumulative source/object/product/driver SHA audit failed')

    results = []
    for qpa in ('offscreen', 'xcb'):
        for case in ('writeExperiment', 'readExperiment', 'initializationFailureExperiment'):
            results.append(qt_result(qpa, 'integrated_' + case))
        results.append(qt_result(qpa, 'status_replay'))
    for case in ('viewportExperiment', 'applicationExitExperiment'):
        results.append(qt_result('xcb', 'integrated_' + case))
    failed_viewport = qt_result('offscreen', 'integrated_viewportExperiment')
    for result in results:
        expected = 5 if result['case'] == 'status_replay' else 3
        if (result['passed'], result['failed'], result['skipped']) != (expected, 0, 0):
            raise RuntimeError(f'Unexpected functional result: {result}')
    if (failed_viewport['passed'], failed_viewport['failed']) != (2, 1):
        raise RuntimeError('The original offscreen viewport failure was not preserved')
    boundary = read_json(ROOT / 'viewport_environment_boundary.json')
    if any(sha(ROOT / row['path']) != row['sha256'] for row in boundary['failed_invocation']['evidence']):
        raise RuntimeError('The recorded offscreen viewport failure bytes changed')

    copies = []
    def add(source, destination):
        copies.append((source, destination))

    for result in results + [failed_viewport]:
        for kind in ('txt', 'xml'):
            add(ROOT / result[kind], result[kind])
    for name in ('build_result.json', 'checkpoint_inputs.json', 'production_objects.json',
                 'run_contract.json', 'viewport_environment_boundary.json'):
        add(ROOT / name, name)
    for name in ('build_drivers.py', 'archive_cumulative.py', 'run_case.sh', 'target_wrapper.sh',
                 'run_all.sh', 'resume_after_offscreen_viewport.sh'):
        add(ROOT / name, 'scripts/' + name)
    add(WORK / 'native_final_result.json', 'native_final_result.json')
    for driver in ('integrated_shell_test', 'status_replay_shell_test'):
        add(ROOT / driver / 'driver.pro', 'drivers/' + driver + '.pro')
        add(ROOT / 'logs' / (driver + '_build.log'), 'build_logs/' + driver + '.txt')
    add(SOURCE / 'tests/fixtures/status_replay.json', 'fixtures/status_replay.json')

    # Strictly whitelisted metadata only: write_result includes account lists,
    # so neither that raw JSON nor account-page captures are archived.
    public_runtime = {}
    product_replays = []
    for qpa in ('offscreen', 'xcb'):
        runtime = ROOT / 'runtime' / qpa
        written = read_json(runtime / 'integrated/write_result.json')
        readback = read_json(runtime / 'integrated/read_result.json')
        public_runtime[qpa] = {
            'write': {key: written[key] for key in (
                'writer_process_id', 'navigation_rounds', 'hospital_locked_save_guards_and_retry',
                'roles_checked', 'csv_all_records', 'csv_selected_records',
                'partial_commit_detected', 'credentials_exported')},
            'read': {key: readback[key] for key in (
                'independent_process_readback_equal', 'guest_after_restart', 'credentials_exported')},
            'reader_numeric_pid': None,
            'independent_pid_evidence': 'The frozen readExperiment asserts current PID differs from writer_process_id; this assertion passed. Each case is a separate run_case.sh OS process. A numeric reader PID is not saved by this test.',
            'excluded': 'Raw write_result account_public_sets, all account rows/fields, raw account/other logged-in viewport screenshots, runtime databases/tags and transient bootstrap files.'}
        if not public_runtime[qpa]['read']['independent_process_readback_equal']:
            raise RuntimeError('Independent readback failed')
        if public_runtime[qpa]['write']['credentials_exported'] or public_runtime[qpa]['read']['credentials_exported']:
            raise RuntimeError('Runtime evidence reports credentials exported')
        initialization = runtime / 'integrated/initialization_failure_result.json'
        init_result = read_json(initialization)
        if init_result['exit_code'] != 3 or not init_result['all_three_workers_closed_and_stopped']:
            raise RuntimeError('Expected initialization-failure child outcome missing')
        if init_result['credentials_exported']:
            raise RuntimeError('Initialization evidence reports credentials exported')
        if not init_result['ready_was_not_reported'] or not init_result['sentinel_unchanged']:
            raise RuntimeError('Initialization failure reported readiness or changed its sentinel')
        add(initialization, f'public/{qpa}/initialization_failure_result.json')
        replay = runtime / 'status_replay'
        log = (replay / 'product.log').read_text(encoding='utf-8')
        checks = {'ready': 'SHELL_READY' in log,
                  'replay_finished_invalidates_old_status': bool(re.search(r'STATUS_REPLAY_STEP 14 .*fresh= false finished= true', log)),
                  'planned_tx_file_rx_present': 'STATUS_REPLAY_PLANNED_TX' in log and 'STATUS_REPLAY_FILE_RX' in log,
                  'replay_stopped': 'STATUS_REPLAY_STOPPED true' in log,
                  'hospital_store_closed': bool(re.search(r'^STORE_CLOSED$', log, re.M)),
                  'single_worker_stopped': 'THREAD_STOPPED true' in log,
                  'exit_0': bool(re.search(r'^EXIT code=\s*0\s*$', log, re.M)),
                  'invalid_input_runtime_absent': not (replay / 'must_not_initialize').exists()}
        if not all(checks.values()):
            raise RuntimeError(f'Replay product evidence failed: {qpa}')
        product_replays.append({'qpa': qpa, 'mode': '--offline --status-replay; hospital store only',
                                'exit_code': 0, 'checks': checks,
                                'relative_input_child_exit_code': 2,
                                'relative_input_exit_evidence': 'Frozen productLifecycleAndInputFailure QCOMPARE(invalid.exitCode(),2) passed; the requested data root is independently absent.',
                                'events': len(re.findall(r'^STATUS_REPLAY_STEP ', log, re.M))})
        add(replay / 'product.log', f'public/{qpa}/status_product.txt')
        add(replay / 'apply_metrics.json', f'public/{qpa}/apply_metrics.json')
        for name in ('replay_1600.png', 'replay_1920.png', 'replay_finished.png'):
            add(replay / name, f'screenshots/{qpa}/{name}')

    xcb_runtime = ROOT / 'runtime/xcb/integrated'
    application = read_json(xcb_runtime / 'application_exit_result.json')
    ready_pid = read_json(xcb_runtime / 'ready_pid.json')
    if application['exit_code'] != 0 or not application['all_three_workers_closed_and_stopped'] or application['credentials_exported']:
        raise RuntimeError('Expected native application exit evidence missing')
    if ready_pid['application_sha256'] != build['product_pair'][str(WORK / 'native/wsl_release/sn100_shell')]:
        raise RuntimeError('Application exit used an unexpected product')
    if ready_pid['application_process_id'] != application['application_process_id'] or ready_pid['test_process_id'] == application['application_process_id']:
        raise RuntimeError('Native application/test PID handoff does not correlate')
    output = application['actual_output']
    expected_stores = ('HOSPITAL', 'ACCOUNT', 'HISTORY')
    if not all(name + '_STORE_CLOSED' in output for name in expected_stores):
        raise RuntimeError('Application exit did not close its configured stores')
    for name in ('application_exit_result.json', 'ready_pid.json', 'viewport_result.json'):
        add(xcb_runtime / name, 'public/xcb/' + name)

    ARCHIVE.mkdir(parents=True, exist_ok=True)
    manifest = []
    for source, relative in copies:
        destination = ARCHIVE / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        expected_sha = sha(source)
        if sha(destination) != expected_sha:
            raise RuntimeError(f'Copy SHA mismatch: {relative}')
        manifest.append({'source': str(source.relative_to(PROJECT)), 'path': relative,
                         'sha256': expected_sha, 'bytes': source.stat().st_size})
    write_json(ARCHIVE / 'archive_manifest.json', manifest)
    write_json(ARCHIVE / 'public_runtime_summary.json', public_runtime)
    write_json(ARCHIVE / 'post_execution_sha_audit.json', {
        'checkpoint_source': source_audit, 'production_objects': object_audit,
        'main_helper': product_audit, 'test_drivers': driver_audit,
        'actual_source_file_count': actual_source_count,
        'compared_against': 'This private 468-file checkpoint only; no new live formal login source was read.',
        'before_execution': 'build_result.json and corresponding input/object manifests',
        'after_execution': 'Independent SHA recomputation during cumulative archival'})
    verification = {
        'schema_version': 1, 'date': '2026-10-24', 'module': 'instrument_cumulative_regression',
        'conclusion': 'Both QPA write/read/initFailure passed, status replay passed; native XCB viewport and application close passed. The failed offscreen physical-hit-test invocation is retained separately as environment not applicable.',
        'environment': {'os': 'Ubuntu-24.04 WSL, ordinary user qyq', 'qt': '5.15.13',
                        'compiler': 'GCC 13.2.0', 'architecture': 'x86_64', 'build': 'Release'},
        'results': results, 'preserved_environment_failure': failed_viewport,
        'environment_failure_explanation': 'viewport_environment_boundary.json',
        'test_process_rules': 'writeExperiment and readExperiment are separate OS processes with the same new per-QPA data root. Reader numeric PID is not saved; distinct PID is asserted in the frozen test and passed. See public_runtime_summary.json.',
        'product_exit': {'product_sha256': ready_pid['application_sha256'],
                         'test_process_id': ready_pid['test_process_id'],
                         'application_process_id': application['application_process_id'],
                         'native_window_id': ready_pid['native_window_id'],
                         'exit_code': 0, 'configured_and_closed_stores': list(expected_stores),
                         'ready_idle_observed_ms': application['idle_observed_ms'],
                         'test_total_text_elapsed_ms': 12381,
                         'scope': 'Functionality only: ready idle, PID-verified WM_DELETE_WINDOW and normal three-store close. The 12.381-second figure is total QtTest text duration, not a precise product runtime or performance acceptance result.'},
        'replay_child_products': product_replays,
        'expected_failure_products': {'exit_code': 3, 'scope': 'Unmarked nonempty history sentinel; no SHELL_READY, sentinel unchanged, all three configured stores closed. Both QPA raw public JSON preserved.'},
        'input_contract': 'run_contract.json',
        'fixture_contracts': {
            'integrated_shell_test': 'Anonymous history fixture is embedded in the linked production QRC; account secret is generated privately at runtime and removed. No account fixture/data is archived.',
            'embedded_history_fixture_sha256': sha(SOURCE / 'tests/fixtures/history_reports.json'),
            'status_replay_shell_test': 'New absolute evidence root; explicit checkpoint fixtures/status_replay.json; exact final product path; same QPA inherited by child. Relative replay path rejects before data initialization.',
            'status_replay_fixture_sha256': sha(SOURCE / 'tests/fixtures/status_replay.json')},
        'build_and_sha_evidence': {'build_result': 'build_result.json', 'source_inputs': 'checkpoint_inputs.json',
                                   'source_count': 468, 'production_objects': 'production_objects.json',
                                   'production_object_count': 136, 'excluded': 'main.o',
                                   'post_execution_audit': 'post_execution_sha_audit.json',
                                   'test_driver_products': build['drivers'], 'product_pair': build['product_pair']},
        'scope_limits': ['measureExperiment 60-second workload intentionally skipped this round; no result claimed.',
                         'State-to-widget apply metrics exclude painting, event queue, network and instrument response.',
                         'Offscreen viewport failure remains 2/1 and is not counted as a product pass; no assertion or production source was weakened.',
                         'The three-store/one-store child modes here are not the separate seven-store instrument baseline.',
                         'Original Windows visual parity, RK screen/input and real instrument actions/interlocks remain pending.'],
        'archive': {'manifest': 'archive_manifest.json', 'copied_files': len(manifest),
                    'raw_png_files': sum(row['path'].endswith('.png') for row in manifest),
                    'copy_sha256_mismatches': 0, 'raw_pixels_edited': False,
                    'runtime_databases_tags_account_fields_or_bootstrap_copied': False,
                    'raw_write_result_and_logged_in_account_screenshots_excluded': True},
        'formal_source_modified': False, 'gui_executed_during_archival': False}
    write_json(ARCHIVE / 'verification.json', verification)
    print(json.dumps({'archive': str(ARCHIVE), 'verification': str(ARCHIVE / 'verification.json'),
                      'manifest': str(ARCHIVE / 'archive_manifest.json'), 'copied_files': len(manifest),
                      'functional_invocations_passed': len(results), 'preserved_failed_invocations': 1,
                      'source_object_product_driver_sha_mismatches': 0}, ensure_ascii=False))


if __name__ == '__main__':
    main()
