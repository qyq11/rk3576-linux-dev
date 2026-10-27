#!/usr/bin/env python3
"""Validate and archive the completed447 cumulative receipts only; never execute a test/build or read runtime files."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import xml.etree.ElementTree as ET


WORK = Path(__file__).resolve().parents[1]
ROOT = WORK.parents[2]
SUITE = WORK / 'suite_final_01'
BIND = WORK / 'cumulative_build'
NATIVE = WORK / 'native'
OUT = ROOT / '04_references/sn100_host/qc_history_integration_20261026/cumulative'
PREFIX = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/'
FREEZE_SHA = '1486686b182dc8d1da55c52b45c91ea72f1b8c41677676cedf16d72b3fc4f401'
RAW = ('test.command.json', 'test.exit.json', 'test.process.json', 'test.exit', 'test.log', 'qt.txt', 'qt.xml')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    return json.loads(ordinary(Path(path)).decode('utf-8'))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def linux(path):
    return PREFIX + path.relative_to(ROOT).as_posix()


def ordinary(path):
    require(path.is_file() and not path.is_symlink() and path.resolve(strict=True).is_relative_to(ROOT),
            'Not an ordinary workspace metadata file: ' + str(path))
    data = path.read_bytes()
    try:
        if b'\x00' in data:
            raise ValueError('NUL bytes')
        data.decode('utf-8', errors='strict')
    except (UnicodeDecodeError, ValueError) as error:
        raise RuntimeError('Ordinary text requires root review before copying: ' + str(path)
                           + ' bytes=' + str(len(data)) + ' sha256=' + sha(data)
                           + ' reason=' + str(error)) from None
    return data


def add(selected, archive_name, path):
    require(archive_name not in selected, 'Duplicate archive path')
    parts = PurePosixPath(archive_name).parts
    require(not PurePosixPath(archive_name).is_absolute() and '..' not in parts
            and not any(p in ('runtime', 'build', '.obj', '.moc', 'secret', 'secrets', 'sysroot') for p in parts),
            'Forbidden archive namespace')
    selected[archive_name] = path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    args = parser.parse_args()
    require(args.root_go and not OUT.exists() and not OUT.is_symlink(), 'Explicit go and a new unique archive are required')
    require(OUT.resolve().is_relative_to((ROOT / '04_references/sn100_host/qc_history_integration_20261026').resolve()),
            'Archive path escaped the authorized evidence root')
    selected = {}
    references = [NATIVE / name for name in ('verification.json', 'final_before.json', 'final_after.json')]
    references.extend(ROOT / '04_references/sn100_host/qc_history_integration_20261026/native' / name
                      for name in ('handoff.json', 'verification.json', 'final_before.json'))
    reference_before = {path.relative_to(ROOT).as_posix(): sha(ordinary(path)) for path in references}
    frozen = read(NATIVE / 'final_before.json')
    native = read(NATIVE / 'verification.json')
    require(sha(ordinary(NATIVE / 'final_before.json')) == FREEZE_SHA and len(frozen) == 447
            and frozen == read(NATIVE / 'final_after.json') and native['passed'] is True
            and native['actual_exit'] == 0 and native['source_inputs'] == 447
            and native['snapshot_sha256'] == FREEZE_SHA and native['source_before_after_equal'] is True,
            'Completed native metadata/freeze mismatch')
    require(reference_before['temp/sn100_host/qc_history_integration_20261026/native/verification.json']
            == reference_before['04_references/sn100_host/qc_history_integration_20261026/native/verification.json'],
            'Existing native archive and original verification differ')
    verification = read(SUITE / 'verification.json')
    results = read(SUITE / 'result.json')
    descriptor = read(SUITE / 'extra_suite_descriptor.json')
    binding = read(BIND / 'binding_proof.json')
    bind_verification = read(BIND / 'verification.json')
    before_tests = read(BIND / 'test_inputs_before.json')
    after_tests = read(BIND / 'test_inputs_after.json')
    require(isinstance(results, list) and len(results) == 35 and verification['passed'] is True
            and verification['full_suite'] is True and verification['invocations'] == 35
            and verification['omitted_invocations'] == [] and verification['source_inputs'] == 447
            and verification['source_unchanged'] is True and verification['legacy_invocations'] == 16
            and verification['qc_invocations'] == 4, 'A full35-call cumulative result is mandatory')
    require(bind_verification['passed'] is True and bind_verification['actual_exit'] == 0
            and bind_verification['parallel_jobs_maximum'] == 2 and bind_verification['reused_test_drivers'] == 0
            and bind_verification['source_inputs'] == 447 and bind_verification['snapshot_sha256'] == FREEZE_SHA
            and binding['snapshot_sha256'] == FREEZE_SHA
            and binding['native_makefile_sha256'] == native['makefile_sha256'], 'Fresh22 binding metadata mismatch')
    drivers = binding['drivers']
    require(len(drivers) == 22 and set(drivers) == set(bind_verification['drivers'])
            and before_tests == after_tests and len(before_tests) == 44, 'Driver/test input set mismatch')
    require(verification['main_sha256'] == native['artifacts']['sn100_shell']
            and verification['renderer_sha256'] == native['artifacts']['sn100_report_renderer'], 'Suite main/helper SHA mismatch')
    # Metadata comparison only: never open/re-hash any native object or executable.
    fresh = native['fresh_objects']
    require(len(fresh) == 210, 'Expected the recorded210 fresh native objects')
    shared = binding['native_objects']
    require(len(shared) == binding['native_objects_excluding_main'] == 177, 'Shared binding object count mismatch')
    native_prefix = linux(NATIVE) + '/'
    for name, row in shared.items():
        require(name.startswith(native_prefix), 'Bound object path outside the new native tree')
        relative = name[len(native_prefix):]
        require(relative in fresh and fresh[relative]['sha256'] == row['object_sha256']
                and fresh[relative]['created_after_start'] is True, 'Bound/native recorded object SHA differs: ' + relative)
    source_index = []
    for name, row in drivers.items():
        relative = PurePosixPath(row['source_relative'])
        require(not relative.is_absolute() and '..' not in relative.parts and relative.suffix == '.cpp', 'Unsafe test source metadata')
        source_name = PREFIX + '06_programs/sn100_host/' + relative.as_posix()
        project_name = PREFIX + '06_programs/sn100_host/' + relative.with_suffix('.pro').as_posix()
        require(before_tests.get(source_name) == row['source_sha256'] and project_name in before_tests,
                'Test source/pro metadata differs: ' + name)
        require(all(re.fullmatch('[a-f0-9]{64}', row[key]) for key in
                    ('source_sha256', 'binary_sha256', 'moc_sha256', 'object_sha256')), 'Malformed driver SHA')
        receipts = row['receipts']
        require(len(receipts) == 3 and all(r['exit_code'] == 0 and r.get('command') for r in receipts)
                and not row.get('reused_from'), 'Expected fresh moc/compile/link receipts: ' + name)
        require(source_name in receipts[0]['command'] and source_name in receipts[1]['command']
                and '-c' in receipts[1]['command'], 'Recorded test source not in actual compiler commands: ' + name)
        linked = [arg for arg in receipts[2]['command'] if arg.endswith('.o')]
        require(len(linked) == len(set(linked)) and set(linked) == set(shared) | {linux(BIND / '.obj' / (name + '.o'))},
                'Test link object metadata differs: ' + name)
        source_index.append({'driver': name, 'source_relative': relative.as_posix(),
                             'source_sha256': row['source_sha256'], 'project_sha256': before_tests[project_name],
                             'binary_sha256_from_original_proof': row['binary_sha256'],
                             'source_bytes_copied': False, 'actual_build_exit_codes': [r['exit_code'] for r in receipts]})
    for name in ('verification.json', 'binding_proof.json', 'test_inputs_before.json', 'test_inputs_after.json',
                 'binding_build.command.json', 'binding_build.exit.json', 'binding_build.process.json',
                 'binding_build.exit', 'binding_build.log'):
        add(selected, 'binding/' + name, BIND / name)
    bind_command = read(BIND / 'binding_build.command.json')
    bind_exit = read(BIND / 'binding_build.exit.json')
    bind_process = read(BIND / 'binding_build.process.json')
    require(bind_exit['exit_code'] == 0 and bind_exit['timed_out'] is False and bind_exit['state'] == 'finished'
            and all(bind_exit.get(k) == v for k, v in bind_command.items())
            and bind_process['pid'] == bind_exit['pid'] and bind_process['exit_code'] == 0
            and (BIND / 'binding_build.exit').read_text(encoding='ascii') == '0\n', 'Raw binding parent receipt differs')
    require({p.name for p in (BIND / 'logs').iterdir()} == {name + '.log' for name in drivers}, 'Compiler log roster differs')
    for name in drivers:
        add(selected, 'binding/logs/' + name + '.log', BIND / 'logs' / (name + '.log'))
    plan = []
    def call(name, driver, qpa='xcb', methods=()):
        plan.append((name, driver, qpa, list(methods)))
    for qpa in ('offscreen', 'xcb'):
        call('lis_' + qpa, 'lis_settings_shell_test', qpa)
    for qpa in ('offscreen', 'xcb'):
        call('printer_' + qpa, 'printer_settings_shell_test', qpa)
        call('login_' + qpa, 'password_login_shell_test', qpa)
    for method in ('writeExperiment', 'readExperiment', 'viewportExperiment', 'initializationFailureExperiment', 'applicationExitExperiment'):
        call('core_' + method, 'integrated_shell_test', methods=[method])
    call('catalog', 'catalog_shell_test', methods=['referencesAndGuards', 'samplePresetWorkflow', 'fullProductStartupAndShutdown', 'productStartupFailure', 'unknownInitialActorFails'])
    call('replay', 'status_replay_shell_test')
    call('loopback', 'status_loopback_shell_test')
    call('instrument', 'instrument_page_test')
    call('report', 'integrated_report_test', methods=['committedHospitalAndReportLifecycle', 'persistedHospitalAndLegacyEntry'])
    for qpa in ('offscreen', 'xcb'):
        call('qc_page_' + qpa, 'qc_local_page_test', qpa)
        call('qc_shell_' + qpa, 'qc_local_shell_test', qpa)
    require(len(plan) == 20, 'Base plan count differs')
    for item in descriptor:
        require(item['driver'] in drivers, 'Descriptor references an unbound driver')
        for qpa in item.get('qpas', ['offscreen', 'xcb']):
            call(item['name'] + '_' + qpa, item['driver'], qpa, item.get('methods', []))
    require(len(plan) == 35 and len({item[0] for item in plan}) == 35
            and [r['name'] for r in results] == [item[0] for item in plan], 'Base20+descriptor15 plan/result order differs')
    for name in ('result.json', 'verification.json', 'extra_suite_descriptor.json'):
        add(selected, 'suite/' + name, SUITE / name)
    calls = []
    for row, (name, driver, qpa, methods) in zip(results, plan):
        stage = SUITE / name
        require({p.name for p in stage.iterdir() if p.is_file()} == set(RAW), 'Unexpected fixed root file in ' + name)
        command = read(stage / 'test.command.json')
        exit_receipt = read(stage / 'test.exit.json')
        process = read(stage / 'test.process.json')
        expected_command = [linux(BIND / driver), *methods, '-o', linux(stage / 'qt.txt') + ',txt',
                            '-o', linux(stage / 'qt.xml') + ',junitxml']
        require(command['command'] == exit_receipt['command'] == row['command'] == expected_command,
                'Actual argv/output path/selected methods differ: ' + name)
        require(all(exit_receipt.get(k) == v for k, v in command.items())
                and all(row.get(k) == v for k, v in exit_receipt.items() if k != 'name')
                and row['exit_code'] == 0 and row['timed_out'] is False and row['state'] == 'finished'
                and row.get('exception') is None and row.get('cleanup') is None
                and process == {'pid': row['pid'], 'state': 'finished', 'exit_code': 0}
                and (stage / 'test.exit').read_text(encoding='ascii') == '0\n', 'Raw command/exit/process relation differs: ' + name)
        require(row['driver'] == driver and row['qpa'] == qpa and row['driver_sha256'] == drivers[driver]['binary_sha256']
                and row['source_relative'] == drivers[driver]['source_relative']
                and row['source_sha256'] == drivers[driver]['source_sha256'], 'Result/binding/source relation differs: ' + name)
        text = ordinary(stage / 'qt.txt').decode('utf-8', errors='strict')
        totals = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted', text)
        require(len(totals) == 1 and tuple(map(int, totals[0][1:])) == (0, 0, 0)
                and list(map(int, totals[0][:3])) == row['totals'] and int(totals[0][0]) > 0,
                'Actual Qt text failure/skip/blacklist/totals mismatch: ' + name)
        xml = ET.fromstring(ordinary(stage / 'qt.xml'))
        groups = list(xml.findall('testcase'))
        require(xml.tag == 'testsuite' and int(xml.get('failures', '-1')) == 0
                and int(xml.get('tests', '-1')) == len(groups) and groups
                and all(group.get('result') == 'pass' for group in groups)
                and not any(list(xml.iter(tag)) for tag in ('failure', 'error', 'skipped')), 'Qt XML failure/group mismatch: ' + name)
        errors = int(xml.get('errors', '-1'))
        require(errors >= 0, 'Missing original XML diagnostic counter')
        calls.append({'name': name, 'driver': driver, 'qpa_from_original_result': qpa,
                      'actual_driver_exit': row['exit_code'], 'qt_text_totals': list(map(int, totals[0])),
                      'xml_method_groups': len(groups), 'xml_errors_attribute_preserved': errors,
                      'xml_failures_attribute': int(xml.get('failures')), 'all_xml_cases_pass': True,
                      'source_relative': row['source_relative'], 'source_sha256': row['source_sha256'],
                      'binary_sha256_from_original_binding': row['driver_sha256']})
        for filename in RAW:
            add(selected, 'suite/' + name + '/' + filename, stage / filename)
    executed = {row['driver'] for row in results}
    require(set(drivers) - executed == {'qc_local_response_test'}, 'Unexpected compiled-only driver set')
    add(selected, 'collector/archive_cumulative.py', Path(__file__).resolve())
    for suffix in ('.command.json', '.exit.json', '.stdout.log', '.stderr.log'):
        add(selected, 'collector/attempt01' + suffix,
            WORK / 'integration_drivers' / ('cumulative_archive_collection_01' + suffix))
    add(selected, 'collector/attempt01_review.json', WORK / 'integration_drivers/cumulative_archive_text_review.json')
    # Exact explicit metadata only. No recursive visit to evidence/runtime or build trees.
    blobs = {}
    source_records = {}
    problems = []
    for relative, path in sorted(selected.items()):
        data = ordinary(path)
        problem = None
        if b'\x00' in data:
            problem = 'NUL bytes in ordinary text'
        else:
            try:
                decoded = data.decode('utf-8', errors='strict')
            except UnicodeDecodeError:
                problem = 'non-UTF8 ordinary text'
            else:
                key_markers = ['-----BEGIN ' + kind + ' KEY-----' for kind in ('PRIVATE', 'RSA PRIVATE')]
                if any(marker in decoded for marker in key_markers):
                    problem = 'private-key marker'
                elif re.search(r'''\b(?:password|passwd|secret|credential|access_token|refresh_token)\b\s*["']?\s*[:=]\s*["'][^"'\r\n]+["']''', decoded, re.IGNORECASE):
                    problem = 'potential literal credential assignment; root review required'
        if problem:
            problems.append({'relative': relative, 'bytes': len(data), 'sha256': sha(data), 'reason': problem})
        blobs[relative] = data
        source_records[relative] = {'source': path.relative_to(ROOT).as_posix(), 'bytes': len(data), 'sha256': sha(data)}
    if problems:
        path = WORK / 'integration_drivers/cumulative_archive_text_review.json'
        with path.open('x', encoding='utf-8') as stream:
            json.dump({'archive_created': False, 'problems': problems, 'raw_inputs_modified': False}, stream, indent=2)
        raise RuntimeError('Unsafe ordinary text; report exact file/length/SHA before any archival transformation')
    parent_observation = {'schema': 'sn100-root-retrospective-parent-observation-v1',
        'recorded_after_completion': True, 'recorded_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'source': 'Root direct task message reported its actual exec session completion; not an independent process-emitted OS receipt',
        'exec_session_id': 76452, 'completion_chunk_id': '5a0840', 'actual_parent_exit': 0,
        'limit': 'Original parent PID/start/end clocks/full raw parent stderr were not supplied and are not reconstructed'}
    derived = {'call_audit.json': calls, 'test_source_sha_index.json': source_index,
               'root_parent_observation.json': parent_observation}
    for relative, path in selected.items():
        require(sha(ordinary(path)) == source_records[relative]['sha256'], 'Raw evidence changed during archive preparation')
    reference_after = {path.relative_to(ROOT).as_posix(): sha(ordinary(path)) for path in references}
    require(reference_before == reference_after, 'Referenced native metadata changed')
    handoff = {'schema': 'sn100-qc-history-cumulative-handoff-v1',
        'scope': 'Metadata-only archive of the completed447 WSL cumulative test run; no new build/test/GUI/benchmark executed by this collector',
        'verification_passed_from_original': verification['passed'], 'full_suite': verification['full_suite'],
        'invocations': len(calls), 'base_calls': 20, 'extra_descriptor_calls': len(plan) - 20,
        'omitted_invocations': verification['omitted_invocations'], 'all35_raw_driver_exits_zero': all(r['actual_driver_exit'] == 0 for r in calls),
        'all35_no_failure_skip_blacklist': all(r['qt_text_totals'][1:] == [0, 0, 0] for r in calls),
        'compiled_drivers': len(drivers), 'compiled_only_not_run_in_suite': sorted(set(drivers) - executed),
        'test_input_fingerprints': len(before_tests), 'test_input_before_after_equal': before_tests == after_tests,
        'snapshot_sha256': FREEZE_SHA, 'source_inputs': len(frozen),
        'main_sha256': verification['main_sha256'], 'renderer_sha256': verification['renderer_sha256'],
        'native_archive': '../native/handoff.json', 'native_recorded_fresh_objects': len(fresh),
        'shared_test_objects_metadata_compared': len(shared), 'native_objects_or_binaries_rehashed_by_collector': False,
        'native_object_metadata_matches': True, 'native_reference_metadata': reference_before,
        'binding_builder_actual_child_exit': bind_exit['exit_code'],
        'suite_parent_exit': parent_observation['actual_parent_exit'],
        'suite_parent_exit_evidence': 'root_parent_observation.json: retrospective root tool observation, not an independent OS process receipt',
        'test_cpp_or_pro_source_copied': False,
        'test_source_note': 'Original exact source/project SHA metadata retained;22 test source texts are intentionally not copied because they were not reviewed for authentication material.',
        'collector_attempt01': 'Actual metadata-only collector exit1 preserved under collector/attempt01*. Its own detector source literal caused a self-match; no original run evidence failed, no archive was created and no test/build was repeated. Exact failed collector source remains in temp/sn100_host/qc_history_integration_20261026/integration_drivers/cumulative_archive_collector_preimage_01.py with SHAf9027f4bcc915af7b7a5bc62e4537108c79939d12c2763e82ee0fbada1db8913.',
        'environment_note': 'Original extra_suite_descriptor.json and qpa/result metadata retained. Original runner did not separately persist the complete child environment; no retrospective actual-environment JSON is invented.',
        'xml_diagnostics': {row['name']: row['xml_errors_attribute_preserved'] for row in calls},
        'xml_note': 'Raw errors diagnostic counters remain unchanged, including nonzero Qt message counts. All recorded testcase groups pass and failures attributes are0; errors are never rewritten to0.',
        'no_recursive_runtime_or_evidence_visit': True, 'copied_png_html_pdf': False,
        'database_ini_credentials_binary_object_copied': False, 'source_records': source_records,
        'excluded': ['All runtime/evidence/core_shared contents, including databases, INI, exports, screenshots and credential files',
                     'All .obj/.moc trees, native/build executables and22 driver binaries',
                     'All22 test C++/.pro source texts; accurate fingerprints retained'],
        'limits': ['35 QtTest driver exits are0; internal negative cases may intentionally exercise product failures',
                   'Response driver was compiled only; this archive contains no new response/resource measurement',
                   'Windows32/ARM64 build stages, matched package recovery and RK/real instrument/clinical/long stability are outside this archive',
                   'No conclusion about old intermittent root causes is changed'],
        'copied_raw_file_count': len(blobs), 'metadata_inputs_before_after_equal': True}
    OUT.mkdir()
    for relative, data in {**blobs, **{name: json_bytes(value) for name, value in derived.items()},
                           'handoff.json': json_bytes(handoff)}.items():
        path = OUT / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as stream:
            stream.write(data)
        require(path.read_bytes() == data, 'Archive byte readback mismatch: ' + relative)
    hashes = {path.relative_to(OUT).as_posix(): sha(path.read_bytes()) for path in OUT.rglob('*') if path.is_file()}
    with (OUT / 'SHA256SUMS').open('x', encoding='ascii') as stream:
        stream.write(''.join(value + '  ' + name + '\n' for name, value in sorted(hashes.items())))
    for name, expected in hashes.items():
        require(sha((OUT / name).read_bytes()) == expected, 'Archive SHA readback mismatch')
    for name, path in selected.items():
        require(sha(ordinary(path)) == source_records[name]['sha256'], 'Original evidence changed while copying')
    print(json.dumps({'archive': str(OUT), 'raw_files': len(blobs), 'hashed_files': len(hashes),
        'all_raw_sha_readbacks_equal': True, 'invocations': len(calls), 'compiled_drivers': len(drivers),
        'test_inputs': len(before_tests), 'native_object_contents_rehashed': False,
        'handoff_sha256': sha((OUT / 'handoff.json').read_bytes()), 'sha256sums_sha256': sha((OUT / 'SHA256SUMS').read_bytes())}, sort_keys=True))


if __name__ == '__main__':
    main()
