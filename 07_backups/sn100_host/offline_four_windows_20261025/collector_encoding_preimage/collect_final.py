#!/usr/bin/env python3
"""Collect only verified final evidence; never build, run a GUI, or inspect runtime values.

The suite, response and resource attempt directories are explicit arguments.
Missing future receipts fail closed before the official final directory exists.
Application/source archives stay in 07_backups; this collector copies no ELF,
PE, object, runtime database/INI, credential, sysroot or full source tree.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import tarfile
import uuid
import xml.etree.ElementTree as ET

from common import WORK, ROOT, PROGRAM, NATIVE, FREEZE, DRIVERS, load, now, require, sha, verify_native

TEXT = {'.json', '.log', '.exit', '.txt', '.xml', '.tsv', '.csv'}
MEDIA = {'.png', '.html', '.pdf'}
SOURCE = {'.py', '.ps1', '.sh', '.bash', '.cpp', '.h', '.pro'}
BLOCKED_DIRS = {'build', 'final_release', 'release', 'debug', 'wsl_release', 'wsl_renderer_release',
    'wsl_report_stage', '.obj', '.moc', '.ui', '.rcc', 'source', 'source_tree', 'thirdparty',
    'sysroot', 'packages', 'node_modules', '__pycache__', 'audit_originals'}
DEFAULT_FINAL = ROOT / '04_references/sn100_host/qc_local_20261025/final'
HISTORICAL_FREEZE_SHA256 = 'e65d77308b35b89dd6394ed6844e9a2482b32be9da2763f2b0fd5e268098de19'
HISTORICAL_SUITES = ('suite_final_01', 'suite_final_02', 'suite_gate_03', 'suite_gate_04')
HISTORICAL_BINDINGS = ('cumulative_build_01', 'cumulative_build_02', 'cumulative_build_03',
    'cumulative_build_04', 'cumulative_build_pre_atomic_01')
POST_ATOMIC_HISTORICAL_FREEZE_SHA256 = '57f3b03290cc3635a5772a7169d22c1713f164186ab3b368028a0f35f8b710ed'
POST_ATOMIC_FAILED_SUITE = 'suite_final_05'
POST_ATOMIC_CHECKPOINT_SUITE = 'suite_final_06'
OPTIONAL_POST_ATOMIC_BINDING = 'cumulative_build_pre_calibration_reference_01'
POST_INCLUDE_HISTORICAL_FREEZE_SHA256 = '5123bf1442472450bbde23a59fc47287dc85ead34e006c523a96a016ac76cf29'
POST_INCLUDE_FAILED_SUITE = 'suite_final_07'
OPTIONAL_POST_INCLUDE_BINDING = 'cumulative_build_pre_login_geometry_01'
OPTIONAL_PRE_INCLUDE_ROOTS = (
    ('native_pre_windows_include_01', 'native'),
    ('cumulative_build_pre_windows_include_01', 'binding'),
    ('standalone_pre_windows_include_01', 'standalone'),
    ('projects_modules_pre_windows_include_01', 'modules'),
    ('platforms/windows_pre_include_01', 'windows_mixed'),
)
PROJECT_MODULE_TARGETS = (
    ('project_advanced', 'projects/tests/project_advanced_test.pro', 'project_advanced_test', 11),
    ('project_basis', 'projects/tests/projects_test.pro', 'projects_test', 9),
    ('reagent_batches', 'reagents/tests/reagent_batch_test.pro', 'reagent_batch_test', 11),
    ('reagent_basis', 'reagents/tests/tests.pro', 'test_reagents', 11),
    ('projects_app', 'projects/projects.pro', 'sn100_projects', None),
    ('reagents_app', 'reagents/reagents.pro', 'sn100_reagents', None),
)
PROJECT_MODULE_TEST_INPUTS = (
    'projects/tests/project_advanced_test.pro', 'projects/tests/project_advanced_test.cpp',
    'projects/tests/projects_test.pro', 'projects/tests/projects_test.cpp',
    'reagents/tests/reagent_batch_test.pro', 'reagents/tests/reagent_batch_test.cpp',
    'reagents/tests/tests.pro', 'reagents/tests/test_reagents.cpp',
)


def within(path, base, existing=True):
    require(path.is_absolute(), 'Absolute path required: ' + str(path))
    resolved = path.resolve(strict=existing)
    require(resolved.is_relative_to(base.resolve()), 'Path escapes its declared evidence root: ' + str(path))
    require(not path.is_symlink(), 'Evidence input must not be a symlink: ' + str(path))
    return resolved


def host_path(value):
    value = str(value).replace('\\', '/')
    if re.match(r'^[A-Za-z]:/', value):
        return Path('/mnt/' + value[0].lower() + '/' + value[3:])
    return Path(value)


def receipt(path, key='exit_code'):
    require(path.is_file(), 'Missing actual receipt: ' + str(path))
    row = load(path)
    require(row.get(key) == 0 and row.get('timed_out', False) is False,
            'Actual child receipt is not successful: ' + str(path))
    require(row.get('state') not in ('not_started', 'exit_unknown', 'driver_timeout'),
            'Unknown child exit cannot pass: ' + str(path))
    return row


def keyed(records):
    require(isinstance(records, list), 'Expected file-record list')
    result = {row['path']: row['sha256'] for row in records}
    require(len(result) == len(records), 'Duplicate file-record path')
    return result


def source_for(driver, record):
    relative = record.get('source_relative')
    require(isinstance(relative, str) and relative, 'Missing source_relative for ' + driver)
    name = PurePosixPath(relative)
    require(not name.is_absolute() and '..' not in name.parts, 'Unsafe test source path')
    path = within(PROGRAM / relative, PROGRAM)
    require(sha(path) == record['source_sha256'], 'Executed test source changed: ' + driver)
    return path


def verify_bound_native_objects(proof, native, frozen):
    """Match the exact product object set; renderer objects are a separate set."""
    build = NATIVE / 'wsl_release'
    makefile = (build / 'Makefile').read_text(encoding='utf-8').replace('\\\n', ' ')
    declarations = re.findall(r'^OBJECTS\s*=\s*(.*)$', makefile, re.M)
    require(len(declarations) == 1 and '$(' not in declarations[0], 'Expected one expanded native OBJECTS declaration')
    objects = [within(build / name, build) for name in shlex.split(declarations[0])]
    require(len(objects) == len(set(objects)), 'Duplicate native Makefile object')
    entries = [path for path in objects if path.name == 'main.o']
    require(len(entries) == 1, 'Exactly one native main.o must be excluded')
    fresh = {within(NATIVE / name, build): row for name, row in native['fresh_objects'].items()
             if PurePosixPath(name).parts[0] == 'wsl_release'}
    require(set(fresh) == set(objects), 'Native fresh-object inventory differs from exact main Makefile set')
    expected = set(objects) - set(entries)
    bound = {within(host_path(name), build): row for name, row in proof['native_objects'].items()}
    require(len(bound) == len(proof['native_objects']) and set(bound) == expected and
            proof['native_objects_excluding_main'] == len(expected) == 167,
            'Binding must contain exactly the current 167 product objects excluding main')
    for path, row in bound.items():
        require(row['object_sha256'] == fresh[path]['sha256'] == sha(path), 'Bound/native actual object SHA differs: ' + str(path))
        source = host_path(row['source']).resolve(strict=True)
        require(source.is_relative_to(PROGRAM.resolve()) or source.is_relative_to(build.resolve()),
                'Bound object source escapes product/generated source roots')
        require(not source.is_symlink() and sha(source) == row['source_sha256'], 'Bound object source changed: ' + str(source))
        if source.is_relative_to(PROGRAM.resolve()):
            name = source.relative_to(PROGRAM.resolve()).as_posix()
            require(frozen.get(name) == row['source_sha256'], 'Bound production source is outside exact freeze: ' + name)
        # moc/rcc generated sources are verified above against their recorded
        # bytes; they are not falsely required to be one of the 426 formal inputs.
    return {'native_makefile_objects': len(objects), 'bound_product_objects_excluding_main': len(bound),
            'exact_object_set_and_sha_match': True, 'source_snapshots_checked': len(bound)}


def verify_projects_modules(frozen, native):
    """Read actual six-target receipts/inventories; never open runtime catalogs."""
    root = within(WORK / 'projects_modules_final', WORK)
    files = []

    def evidence(path):
        path = within(path, root)
        require(path.is_file(), 'Missing project/reagent module evidence: ' + str(path))
        files.append(path)
        return path

    def metadata(path):
        return load(evidence(path))

    def named(rows, names):
        result = {row['name']: row for row in rows}
        require(len(result) == len(rows) and set(result) == set(names), 'Incomplete/duplicate project/reagent record set')
        return result

    report = metadata(root / 'verification.json')
    require(report.get('schema') == 'sn100-project-modules-final-v1' and report.get('passed') is True,
            'Fresh project/reagent modules are mandatory and have not passed')
    require(report['source_inputs'] == len(frozen) == 426 and
            report['source_snapshot_sha256'] == sha(FREEZE) == native['snapshot_sha256'] and
            report['main_artifacts'] == native['artifacts'] and report['board_or_instrument_verified'] is False and
            report['parallel_jobs_maximum'] == 2 and not report.get('binding_changed_at_final', False),
            'Project/reagent module source/native/offline scope differs')
    required_checks = {'all_six_fresh_release_builds', 'four_complete_qt_suites',
        'two_loaded_standalones_closed_normally', 'six_artifact_sha256_present',
        'source_426_test_driver_main_helper_native_objects_unchanged', 'fresh_objects_and_binaries_unchanged_through_final'}
    require(set(report['checks']) == required_checks and all(value is True for value in report['checks'].values()),
            'Project/reagent actual final checks are incomplete')
    baseline = metadata(root / 'binding_before.json')
    require(metadata(root / 'binding_after.json') == metadata(root / 'binding_observed_final.json') == baseline,
            'Project/reagent root binding changed')
    expected_inputs = {name: frozen[name] for name in PROJECT_MODULE_TEST_INPUTS}
    require(metadata(root / 'source_before.json') == metadata(root / 'source_after.json') == frozen and
            metadata(root / 'test_inputs_before.json') == metadata(root / 'test_inputs_after.json') == expected_inputs,
            'Project/reagent source/test input snapshots differ')
    require(baseline['source_inputs'] == len(frozen) and baseline['source_snapshot_sha256'] == sha(FREEZE) and
            baseline['source_sha256'] == frozen and baseline['test_inputs_sha256'] == expected_inputs and
            baseline['main_artifacts'] == native['artifacts'] and
            baseline['native_verification_sha256'] == sha(NATIVE / 'verification.json') and
            baseline['native_makefile_sha256'] == native['makefile_sha256'] and
            baseline['native_objects_sha256'] == {name: row['sha256'] for name, row in native['fresh_objects'].items()},
            'Project/reagent baseline does not match all current native inputs/objects')
    driver_root = Path(__file__).resolve().parent
    drivers = {within(host_path(name), driver_root): digest for name, digest in baseline['driver_inputs_sha256'].items()}
    require(drivers == {path: sha(path) for path in (driver_root / 'run_projects_modules.py', driver_root / 'common.py')},
            'Project/reagent executed driver/helper changed')
    contract = metadata(root / 'source_contract.json')
    contract_sources = ('projects/main.cpp', 'projects/project_page.cpp', 'reagents/main.cpp',
                        'reagents/reagents_page.cpp', 'reagents/reagent_store.cpp')
    ready = '项目/单位已读取；当前仅保存本地配置，未同步仪器。'
    require(contract['source_sha256'] == {name: frozen[name] for name in contract_sources} and
            contract['projects_stdout_marker'] == ready and contract['invented_stdout_close_or_load_markers'] is False and
            contract['normal_close'] and contract['reagents_readiness'], 'Project/reagent source readiness/close contract differs')
    stages = named(report['stages'], [row[0] for row in PROJECT_MODULE_TARGETS])
    tests = named(report['tests'], [row[0] for row in PROJECT_MODULE_TARGETS if row[3] is not None])
    apps = named(report['standalone'], ('projects_app', 'reagents_app'))
    require(set(report['artifacts']) == {row[2] for row in PROJECT_MODULE_TARGETS}, 'Project/reagent artifact set incomplete')

    for name, project, target, expected in PROJECT_MODULE_TARGETS:
        stage = within(root / name, root)
        build, binary = stage / 'build', stage / 'build' / target
        binary = within(binary, build)
        row = stages[name]
        require(row == metadata(stage / 'verification.json') and row['passed'] is True and row['qmake_project'] == project and
                row['target'] == target and row['kind'] == ('test' if expected is not None else 'standalone') and
                row['checks'] and all(value is True for value in row['checks'].values()), 'Project/reagent stage failed or differs: ' + name)
        for checkpoint in ('binding_before', 'binding_before_run', 'binding_after', 'binding_observed_final'):
            require(metadata(stage / (checkpoint + '.json')) == baseline, 'Module stage binding differs: ' + name)
        environment = metadata(stage / 'environment.json')
        require(environment['overrides']['QT_QPA_PLATFORM'] == 'offscreen' and environment['parallel_jobs_maximum'] == 2 and
                environment['runtime_values_hashed'] is False, 'Module private environment scope differs')
        require(metadata(stage / 'build_before.json') == {'binary_sha256': None, 'objects_sha256': {}, 'generated_sha256': {}},
                'Module build was not originally empty: ' + name)
        inventory = metadata(stage / 'build_after_make.json')
        for checkpoint in ('build_after_run', 'build_observed_final', 'build_at_final'):
            require(metadata(stage / (checkpoint + '.json')) == inventory, 'Module build inventory changed: ' + name)
        actual_objects = {path.relative_to(build).as_posix(): sha(within(path, build)) for path in sorted(build.rglob('*.o'))}
        actual_generated = {path.relative_to(build).as_posix(): sha(within(path, build)) for path in sorted(build.rglob('*'))
            if path.is_file() and (path.parent.name in ('.moc', '.ui', '.rcc') or path.name in ('Makefile', '.qmake.stash'))}
        require(inventory == {'binary_sha256': sha(binary), 'objects_sha256': actual_objects, 'generated_sha256': actual_generated},
                'Actual module binary/object/generated inventory differs: ' + name)
        artifact = report['artifacts'][target]
        require(within(host_path(artifact['path']), build) == binary and artifact['sha256'] == row['binary_sha256'] == sha(binary) and
                artifact['qmake_project'] == project and artifact['freshly_built'] is True, 'Module artifact identity differs')
        proof = metadata(stage / 'build_proof.json')
        makefile = (build / 'Makefile').read_text(encoding='utf-8').replace('\\\n', ' ')

        def words(variable):
            declarations = re.findall(r'^' + variable + r'\s*=\s*(.*)$', makefile, re.M)
            require(len(declarations) == 1 and '$(' not in declarations[0], 'Module Makefile declaration differs: ' + variable)
            return shlex.split(declarations[0])

        require('-O2' in makefile and '-g ' not in makefile and words('TARGET') == [target] and
                proof['objects'] == words('OBJECTS') and len(set(proof['objects'])) == len(proof['objects']) and
                set(actual_objects) == {Path(item).as_posix() for item in proof['objects']} and
                proof['fresh_empty_build_directory'] is True and proof['object_timestamp_tolerance_seconds'] == 2 and
                proof['makefile_sha256'] == sha(build / 'Makefile') and proof['binary_sha256'] == sha(binary),
                'Module fresh Release object/Makefile proof differs')
        sources = []
        for item in words('SOURCES'):
            path = (build / item).resolve(strict=True)
            if path.is_relative_to(PROGRAM.resolve()):
                relative = path.relative_to(PROGRAM.resolve()).as_posix()
                require(frozen.get(relative) == sha(path), 'Unfrozen module qmake source')
                sources.append({'source_relative': relative, 'sha256': sha(path), 'generated': False})
            else:
                path = within(path, build)
                sources.append({'source_relative': path.relative_to(build).as_posix(), 'sha256': sha(path), 'generated': True})
        require(sources == proof['sources'] and any(item['source_relative'] == 'projects/projects_store.cpp' for item in sources),
                'Module build did not use the exact repaired ProjectsStore source')
        phases = row['phases']
        last_phase = 'test' if expected is not None else 'app'
        require([phase['name'] for phase in phases] == ['qmake', 'make', last_phase], 'Module three real phases are incomplete')
        for phase in phases:
            phase_name = phase['name']
            logs = stage / 'logs'
            raw_path = within(host_path(phase['exit_receipt']), logs)
            require(raw_path == logs / (phase_name + '.exit.json'), 'Module exit receipt path differs')
            raw = receipt(evidence(raw_path))
            require(raw == phase and raw['state'] == 'finished' and raw['cleanup'] is None and
                    within(host_path(raw['stdout_stderr']), logs) == logs / (phase_name + '.log'),
                    'Module original exit/phase/log differs')
            command = metadata(logs / (phase_name + '.command.json'))
            require(all(raw.get(key) == value for key, value in command.items()), 'Module original command metadata differs')
            process = metadata(logs / (phase_name + '.process.json'))
            require(process['state'] == 'finished' and process['exit_code'] == raw['exit_code'] and process['pid'] == raw['pid'],
                    'Module child process receipt differs')
            require(evidence(logs / (phase_name + '.exit')).read_text().strip() == str(raw['exit_code']), 'Module plain exit differs')
            evidence(logs / (phase_name + '.log'))
            require(host_path(raw['cwd']).resolve(strict=True) == (build if phase_name in ('qmake', 'make') else stage / 'runtime').resolve(strict=True),
                    'Module actual working directory differs')
        require(host_path(phases[0]['command'][1]).resolve(strict=True) == (PROGRAM / project).resolve(strict=True) and
                phases[1]['command'] == ['/usr/bin/make', '-C', str(build), '-j2'] and
                host_path(phases[2]['command'][0]).resolve(strict=True) == binary, 'Module actual qmake/make/run target differs')
        started = int(datetime.datetime.fromisoformat(phases[0]['started_utc']).timestamp() * 1_000_000_000)
        require(all((build / item).stat().st_mtime_ns + 2_000_000_000 >= started for item in actual_objects) and
                binary.stat().st_mtime_ns + 2_000_000_000 >= started, 'Module objects/executable predate their fresh build')
        if expected is not None:
            parsed = metadata(stage / 'qt_verification.json')
            txt, xml = evidence(stage / 'qt.txt'), evidence(stage / 'qt.xml')
            require(within(host_path(parsed['raw_txt']), stage) == txt and within(host_path(parsed['raw_junitxml']), stage) == xml and
                    parsed['txt_sha256'] == sha(txt) and parsed['junitxml_sha256'] == sha(xml) and
                    parsed['totals'] == [expected, 0, 0] and parsed['expected_passes_including_init_cleanup'] == expected,
                    'Module raw Qt identity/totals differ')
            totals = re.findall(r'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped', txt.read_text(encoding='utf-8', errors='replace'))
            xml_root = ET.parse(xml).getroot()
            cases = list(xml_root.iter('testcase'))
            require(len(totals) == 1 and list(map(int, totals[0])) == parsed['totals'] and len(cases) == expected and
                    {'initTestCase', 'cleanupTestCase'}.issubset({case.get('name') for case in cases}) and
                    not any(child.tag in ('failure', 'error', 'skipped') for case in cases for child in case) and
                    parsed['junit_testcases'] == [dict(case.attrib) for case in cases] and
                    parsed['junit_failure_error_skipped_cases'] == [] and parsed['junit_root_attributes'] == dict(xml_root.attrib),
                    'Module raw JUnit/txt cases failed/skipped/incomplete')
            require(tests[name] == {'name': name, 'target': target, 'actual_exit': phases[2]['exit_code'],
                    'binary_sha256': sha(binary), **parsed}, 'Module Qt root/stage verification differs')
        else:
            app = metadata(stage / 'standalone_verification.json')
            require(app == apps[name] and app['target'] == target and app['actual_exit'] == phases[2]['exit_code'] == 0 and
                    app['binary_sha256'] == sha(binary) and app['seconds'] == phases[2]['seconds'] >= 1.5 and
                    app['timer_ms'] == 1800 and app['close_mode'] == 'existing guarded QWidget::close timer' and
                    app['timeout_cleanup_used'] is False and app['rejected_diagnostics_observed'] == [],
                    'Module standalone ordinary1800ms exit differs')
            output = evidence(stage / 'logs/app.log')
            require(app['app_log_sha256'] == sha(output), 'Module standalone log SHA differs')
            flag = '--exit-after-ms' if name == 'projects_app' else '--quit-after-ms'
            argv = phases[2]['command']
            require(flag in argv and argv[argv.index(flag) + 1] == '1800' and '--seed-demo' in argv and '--test-engineer' in argv,
                    'Module standalone did not use the real anonymous seed/timer CLI')
            if name == 'projects_app':
                require(app['readiness'] == {'stdout_marker': ready, 'count': 1} and output.read_text(encoding='utf-8').count(ready) == 1,
                        'Project standalone real loaded marker differs')
            else:
                png = evidence(stage / 'page_loaded.png')
                loaded = app['readiness']
                require(within(host_path(loaded['path']), stage) == png and loaded['sha256'] == sha(png) and
                        loaded['dimensions'] == [1751, 968] and loaded['positive_stdout_marker_exists'] is False and
                        loaded['visual_acceptance_performed'] is False and png.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'),
                        'Reagent standalone real loaded screenshot differs')
            catalog = app['catalog']
            require(catalog['opened_read_only_after_app_exit'] is True and catalog['runtime_database_hashed'] is False and
                    catalog['marker'] == 'sn100-catalog-offline-v1' and catalog['integrity_check'] == ['ok'] and
                    catalog['foreign_key_errors'] == [] and all(value == 0 for value in catalog['unused_table_counts'].values()),
                    'Module standalone anonymous schema/integrity summary failed')
    return {'root': root, 'verification': report, 'evidence_files': sorted(set(files)), 'test_inputs': expected_inputs}


def verify_tar_pair(application, source, frozen, arm):
    """Stream member hashes; never extract archives or retain binary members."""
    members = {}
    special = {}
    with tarfile.open(application, 'r:gz') as bundle:
        for item in bundle:
            name = PurePosixPath(item.name)
            require(not name.is_absolute() and '..' not in name.parts and (item.isfile() or item.isdir()),
                    'Unsafe application archive member')
            if item.isdir():
                continue
            require(item.name not in members, 'Duplicate application archive member')
            stream = bundle.extractfile(item)
            digest = hashlib.sha256()
            kept = bytearray()
            for block in iter(lambda: stream.read(1048576), b''):
                digest.update(block)
                if item.name in ('SHA256SUMS', 'release_id', 'release.json'):
                    require(len(kept) + len(block) <= 4 * 1024 * 1024, 'Oversized release metadata')
                    kept.extend(block)
            members[item.name] = digest.hexdigest()
            if kept:
                special[item.name] = bytes(kept)
    manifest = {}
    for line in special['SHA256SUMS'].decode('ascii').splitlines():
        digest, name = line.split('  ', 1)
        require(name not in manifest, 'Duplicate application manifest member')
        manifest[name] = digest
    require(set(members) == set(manifest) | {'SHA256SUMS', 'release_id'}, 'Application exact member set differs')
    require(all(members[name] == value for name, value in manifest.items()), 'Application member SHA mismatch')
    release_id = hashlib.sha256(special['SHA256SUMS']).hexdigest()
    require(special['release_id'].decode('ascii').strip() == release_id == arm['package']['release_id'],
            'Application release identity differs')
    metadata = json.loads(special['release.json'])
    require(metadata['sources'] == frozen and metadata['board_runtime_verified'] is False,
            'Application source/runtime identity differs')
    require(members['sn100_shell'] == arm['main_sha256'] and
            members['sn100_report_renderer'] == arm['renderer_sha256'], 'Packaged ARM main/helper differ')
    for capability, flag in (('quality_control_local', '--quality-control'), ('calibration_local', '--calibration')):
        require(capability in metadata['capabilities'] and flag in metadata['startup_feature_arguments'],
                'Current local capability missing from checked package')
    seen = set()
    with tarfile.open(source, 'r:gz') as bundle:
        for item in bundle:
            require(item.isfile() and item.name in frozen and item.name not in seen, 'Unexpected source archive member')
            seen.add(item.name)
            stream = bundle.extractfile(item)
            digest = hashlib.sha256()
            for block in iter(lambda: stream.read(1048576), b''):
                digest.update(block)
            require(digest.hexdigest() == frozen[item.name], 'Source archive member SHA differs')
    require(seen == set(frozen), 'Source archive does not exactly match freeze')
    return {'release_id': release_id, 'application_regular_members': len(members), 'source_members': len(seen)}


def validate(args):
    frozen = load(FREEZE)
    native = verify_native(frozen)
    if args.expected_inputs is not None:
        require(len(frozen) == args.expected_inputs, 'Unexpected final source count')
    require(load(NATIVE / 'final_after.json') == frozen, 'Native before/after source freeze differs')
    for name in ('snapshot_before', 'build', 'snapshot_after', 'artifact_types'):
        receipt(NATIVE / (name + '.exit.json'))
    projects_modules = verify_projects_modules(frozen, native)

    bind = load(DRIVERS / 'verification.json')
    proof = load(DRIVERS / 'binding_proof.json')
    require(bind.get('passed') is True and bind.get('actual_exit') == 0, 'Binding driver failed')
    require(bind['snapshot_sha256'] == proof['snapshot_sha256'] == sha(FREEZE), 'Binding source freeze differs')
    require(proof['native_makefile_sha256'] == native['makefile_sha256'], 'Binding native Makefile differs')
    bound_objects = verify_bound_native_objects(proof, native, frozen)
    require(load(DRIVERS / 'test_inputs_before.json') == load(DRIVERS / 'test_inputs_after.json'),
            'Test compile input snapshots differ')
    receipt(DRIVERS / 'binding_build.exit.json')
    for driver, row in proof['drivers'].items():
        require(sha(DRIVERS / driver) == row['binary_sha256'], 'Bound driver binary changed: ' + driver)
        source_for(driver, row)
        require(len(row['receipts']) == 3 and all(item['exit_code'] == 0 for item in row['receipts']),
                'Missing/failed actual driver compile receipt: ' + driver)

    suite = load(args.suite_dir / 'verification.json')
    results = load(args.suite_dir / 'result.json')
    require(suite.get('passed') is True and suite['source_inputs'] == len(frozen) and suite['source_unchanged'] is True,
            'Unified suite does not match native freeze')
    require(suite.get('full_suite') is True and suite.get('omitted_invocations') == [],
            'A suffix/preliminary suite cannot pass the final full cumulative gate')
    require(suite['invocations'] == len(results) == args.expected_invocations,
            'Wrong number of actual suite invocations')
    require(len({row['name'] for row in results}) == len(results), 'Duplicate suite invocation name')
    expected = {name + '_' + qpa: qpa for name in ('lis', 'printer', 'login') for qpa in ('offscreen', 'xcb')}
    expected_drivers = {name + '_' + qpa: driver for name, driver in
        (('lis', 'lis_settings_shell_test'), ('printer', 'printer_settings_shell_test'), ('login', 'password_login_shell_test'))
        for qpa in ('offscreen', 'xcb')}
    expected.update({'core_' + method: 'xcb' for method in ('writeExperiment', 'readExperiment',
        'viewportExperiment', 'initializationFailureExperiment', 'applicationExitExperiment')})
    expected_drivers.update({name: 'integrated_shell_test' for name in expected if name.startswith('core_')})
    expected.update({name: 'xcb' for name in ('catalog', 'replay', 'loopback', 'instrument', 'report')})
    expected_drivers.update(dict(zip(('catalog', 'replay', 'loopback', 'instrument', 'report'),
        ('catalog_shell_test', 'status_replay_shell_test', 'status_loopback_shell_test', 'instrument_page_test', 'integrated_report_test'))))
    expected.update({'qc_' + role + '_' + qpa: qpa for role in ('page', 'shell') for qpa in ('offscreen', 'xcb')})
    expected_drivers.update({'qc_' + role + '_' + qpa: 'qc_local_' + role + '_test'
        for role in ('page', 'shell') for qpa in ('offscreen', 'xcb')})
    require(len(expected) == 20 and suite['legacy_invocations'] == 16 and suite['qc_invocations'] == 4,
            'Exact base20 invocation contract differs')
    extra = load(args.suite_dir / 'extra_suite_descriptor.json')
    require(extra == load(Path(__file__).with_name('extra_suite.json')),
            'Captured extra-suite descriptor differs from the final driver plan')
    require(sum(len(description['qpas']) for description in extra) == 15,
            'Final descriptor must declare the actual additional15 calls')
    for description in extra:
        for qpa in description['qpas']:
            name = description['name'] + '_' + qpa
            require(name not in expected and qpa in ('offscreen', 'xcb'), 'Duplicate/unsupported declared extra suite name/QPA')
            expected[name] = qpa
            expected_drivers[name] = description['driver']
    require(len(expected) == args.expected_invocations == 35 and
            set(proof['drivers']) == set(expected_drivers.values()) | {'qc_local_response_test'},
            'Final base20 + descriptor15 and exact22 bound driver set differ')
    require({row['name']: row['qpa'] for row in results} == expected,
            'Actual suite invocation/QPA set differs from its exact base and captured extra descriptor')
    require(suite['main_sha256'] == native['artifacts']['sn100_shell'] and
            suite['renderer_sha256'] == native['artifacts']['sn100_report_renderer'], 'Suite main/helper differ')
    for row in results:
        require(row['exit_code'] == 0 and not row['timed_out'] and row['totals'][1:] == [0, 0],
                'Actual Qt suite invocation failed/skipped: ' + row['name'])
        stage = within(args.suite_dir / row['name'], args.suite_dir)
        actual = receipt(stage / 'test.exit.json')
        require(actual['command'] == row['command'], 'Suite command receipt differs from result')
        require(row['driver'] == expected_drivers[row['name']], 'Suite invoked the wrong declared driver')
        require(row['driver_sha256'] == proof['drivers'][row['driver']]['binary_sha256'], 'Suite used a different bound driver')
        require((stage / 'qt.txt').is_file() and (stage / 'qt.xml').is_file(), 'Missing raw Qt text/XML')
        totals = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',
                            (stage / 'qt.txt').read_text(encoding='utf-8', errors='replace'))
        require(totals and list(map(int, totals[-1])) == row['totals'], 'Raw Qt totals differ from result: ' + row['name'])
        ET.parse(stage / 'qt.xml')

    standalone_root = WORK / 'standalone_final'
    standalone = load(standalone_root / 'verification.json')
    require(standalone.get('schema') == 'sn100-four-windows-standalone-v1' and standalone.get('passed') is True,
            'Fresh QC/calibration standalone stage is mandatory and has not passed')
    require(standalone['source_snapshot_sha256'] == sha(FREEZE) and standalone['source_inputs'] == len(frozen)
            and standalone['main_artifacts'] == native['artifacts']
            and standalone['native_verification_sha256'] == sha(NATIVE / 'verification.json'),
            'Standalone source/native artifact provenance differs')
    require(standalone['source_before'] == standalone['source_after'] == frozen,
            'Standalone actual source before/after differs')
    require(load(standalone_root / 'source_before.json') == load(standalone_root / 'source_after.json') == frozen,
            'Standalone saved raw source maps differ')
    require(load(standalone_root / 'native_binding_before.json') == load(standalone_root / 'native_binding_after.json'),
            'Standalone saved native binding changed')
    require(standalone['checks'] and all(value is True for value in standalone['checks'].values()),
            'Standalone actual checks did not pass')
    required_phases = {'quality_control_qmake', 'quality_control_make', 'quality_control_run',
        'calibration_qmake', 'calibration_make', 'calibration_catalog_owner', 'calibration_run'}
    require(required_phases == set(standalone['stages']), 'Standalone actual stage set differs')
    for phase, row in standalone['stages'].items():
        require(row['exit_code'] == 0 and row.get('timed_out', False) is False,
                'Standalone actual subprocess failed: ' + phase)
        raw = receipt(within(standalone_root / row['exit_receipt'], standalone_root))
        require(raw['command'] == row['command'] and raw['exit_code'] == row['exit_code'],
                'Standalone raw command/exit differs: ' + phase)
    require(set(standalone['modules']) == {'quality_control', 'calibration'}, 'Standalone module pair incomplete')
    for name, row in standalone['modules'].items():
        binary = within(standalone_root / row['binary'], standalone_root / 'build')
        require(sha(binary) == row['binary_sha256'] and row['normal_shutdown_basis'],
                'Standalone binary or normal shutdown evidence differs: ' + name)
        require(sha(binary.parent / 'Makefile') == row['makefile_sha256'], 'Standalone Makefile changed')
        run = standalone['stages'][row['run_stage']]
        require(host_path(run['command'][0]).resolve(strict=True) == binary and
                row['startup_marker_observed'] is True and
                row['startup_marker'] in (standalone_root / run['log']).read_text(encoding='utf-8'),
                'Standalone actual binary/startup marker differs: ' + name)
        if name == 'calibration':
            require(row['normal_shutdown_marker_observed'] is True and
                    row['normal_shutdown_marker'] in (standalone_root / run['log']).read_text(encoding='utf-8'),
                    'Calibration standalone normal-close marker missing')
    owner = standalone['stages']['calibration_catalog_owner']
    require(owner['readiness_observed'] is True and owner['ordinary_sigterm_requested'] is True and
            all(value is True for value in owner['checks'].values()), 'Standalone real catalog owner did not close normally')
    require(standalone['runtime_data_ini_or_credentials_archived'] is False, 'Standalone privacy scope differs')

    windows_root = WORK / 'platforms/windows'
    windows = load(windows_root / 'verification.json')
    require(windows.get('exit_code') == 0 and windows.get('published') is True and
            windows.get('source_matches_native_freeze') is True and
            windows.get('sources_tools_external_inputs_unchanged') is True,
            'Windows full-build receipt is incomplete')
    require(windows['native_freeze_sha256'] == sha(FREEZE) and windows['source_snapshot_entries'] == len(frozen),
            'Windows source freeze differs')
    require(load(windows_root / 'source_before.json') == load(windows_root / 'source_after.json') == frozen,
            'Windows actual before/after source inventory differs')
    for name in ('toolchain', 'external_inputs', 'shell_external_inputs', 'renderer_external_inputs'):
        require(keyed(load(windows_root / (name + '_before.json'))) == keyed(load(windows_root / (name + '_after.json'))),
                'Windows before/after input SHA differs: ' + name)
    require({row['target'] for row in windows['stages']} == {'shell', 'renderer'}, 'Missing Windows target')
    for target in windows['stages']:
        require(target['qmake_exit'] == target['make_exit'] == target['import_exit'] == 0, 'Windows target child failed')
        for phase in ('qmake', 'make', 'imports'):
            receipt(windows_root / (target['target'] + '_' + phase + '.log.exit.json'))
    windows_artifacts = {}
    for row in windows['outputs']:
        path = within(host_path(row['path']), windows_root / 'final_release')
        require(sha(path) == row['sha256'] and row['machine_hex'] == '0x014c' and row['optional_header_hex'] == '0x010b',
                'Windows final artifact SHA/architecture differs')
        windows_artifacts[path.name] = row['sha256']
    require(set(windows_artifacts) == {'sn100_shell.exe', 'sn100_report_renderer.exe'}, 'Windows main/helper pair incomplete')

    arm_root = WORK / 'platforms/arm64'
    arm = load(arm_root / 'verification.json')
    require(arm['source_entries'] == len(frozen) and arm['source_freeze_sha256'] == sha(FREEZE) and
            arm['source_before_after_and_package_equal_native_freeze'] is True,
            'ARM source freeze differs')
    require(all(arm[name] == 0 for name in ('build_exit_code', 'closure_exit_code', 'package_exit_code')),
            'ARM build/closure/package failed')
    require(arm['board_runtime_verified'] is False and not arm['closure_errors'], 'ARM offline closure state differs')
    for name in ('source_before.json', 'source_after_build.json', 'source_after_package.json'):
        require(load(arm_root / name) == frozen, 'ARM actual source snapshot differs: ' + name)
    for name in ('sysroot', 'toolchain'):
        require(load(arm_root / (name + '_before.json')) == load(arm_root / (name + '_after.json')),
                'ARM external input fingerprint differs: ' + name)
    for name in ('compiler_version', 'tool_versions', 'snapshot_before', 'list_inputs', 'build',
                 'snapshot_after_build', 'closure', 'package', 'snapshot_after_package'):
        receipt(arm_root / (name + '_exit.json'))
    for name, key in (('sn100_shell', 'main_sha256'), ('sn100_report_renderer', 'renderer_sha256')):
        require(sha(arm_root / 'build' / name) == arm[key], 'ARM current executable differs: ' + name)
    compiler = load(arm_root / 'compiler_receipt_summary.json')
    compile_receipts = sorted((arm_root / 'compiler_receipts').glob('*.json'))
    require(compile_receipts and len(compile_receipts) == compiler['total_invocations'] and
            compiler['all_exit_codes_zero'] is True and compiler['link_invocations'] == 2,
            'ARM exact compiler receipt set is incomplete')
    for path in compile_receipts:
        row = receipt(path)
        output = host_path(row['output'])
        if 'source' not in row:
            output = output.with_suffix('')
        require(within(output, arm_root / 'build') and sha(output) == row['output_sha256'],
                'ARM compile/link actual output SHA differs')
    closure = load(arm_root / 'dependencies/dependency_report.json')
    require(closure['offline_link_and_sql_closure_passed'] is True and not closure['errors'] and
            closure['board_runtime_verified'] is False, 'ARM dependency report did not pass offline closure')
    for role, key in (('main', 'main_sha256'), ('report_renderer', 'renderer_sha256')):
        matches = [row for row in closure['files'] if role in row['roles']]
        require(len(matches) == 1 and matches[0]['sha256'] == arm[key], 'Dependency report executable differs')

    backup = load(args.backup_dir / 'verification.json')
    require(backup['source_inputs'] == len(frozen) and backup['runtime_data_or_authentication_archived'] is False,
            'Backup recovery source/privacy scope differs')
    require(load(args.backup_dir / 'source_snapshot.json') == frozen and
            load(args.backup_dir / 'arm64_verification.json') == arm, 'Archived provenance differs from completed ARM stage')
    application = within(args.backup_dir / backup['application']['path'], args.backup_dir)
    source = within(args.backup_dir / backup['source']['path'], args.backup_dir)
    require(sha(application) == backup['application']['sha256'] == arm['package']['package_sha256'], 'Archived application SHA differs')
    require(sha(source) == backup['source']['sha256'] == arm['source_package']['sha256'], 'Archived source SHA differs')
    backup_manifest = {}
    for line in (args.backup_dir / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        digest, name = line.split('  ', 1)
        require(name not in backup_manifest, 'Duplicate recovery SHA member')
        backup_manifest[name] = digest
        require(sha(within(args.backup_dir / name, args.backup_dir)) == digest, 'Recovery manifest SHA differs: ' + name)
    require({application.name, source.name, 'source_snapshot.json', 'arm64_verification.json',
             'verification.json'}.issubset(backup_manifest), 'Recovery manifest lacks a required paired input')
    package = verify_tar_pair(application, source, frozen, arm)
    require(package['release_id'] == backup['release_id'], 'Backup release differs')
    require(package['application_regular_members'] == backup['application']['files'] == arm['package']['regular_files']
            and package['source_members'] == backup['source']['members'] == arm['source_package']['members']
            and application.stat().st_size == backup['application']['bytes']
            and source.stat().st_size == backup['source']['bytes'], 'Backup member count/byte size differs')

    response = load(args.response_dir / 'verification.json')
    response_inputs = load(args.response_dir / 'inputs.json')
    metrics = load(args.response_dir / 'evidence/metrics.json')
    require(response.get('passed') is True and response['actual_exit'] == 0 and response['sample_count'] == 40,
            'Response receipt did not pass')
    receipt(args.response_dir / 'response.exit.json')
    require(response_inputs['source_snapshot_sha256'] == sha(FREEZE) and
            response_inputs['main_artifacts'] == native['artifacts'], 'Response main/source differs')
    require(response_inputs['driver'] == proof['drivers']['qc_local_response_test'],
            'Response captured driver proof differs from final binding')
    require(response['metrics_sha256'] == sha(args.response_dir / 'evidence/metrics.json') and
            metrics['executable_sha256'] == proof['drivers']['qc_local_response_test']['binary_sha256'],
            'Response metrics/driver fingerprint differs')
    require(len(metrics['samples']) == 40 and all(metrics[name] is True for name in
            ('test_function_passed', 'normal_page_leave', 'qc_worker_closed', 'catalog_worker_closed')),
            'Response actual samples or normal teardown differ')

    resource = load(args.resource_dir / 'verification.json')
    resource_driver = load(args.resource_driver_dir / 'observer.exit.json')
    observed = load(args.resource_dir / 'summary.json')
    require(resource['checks'] and all(value is True for value in resource['checks'].values()), 'Resource actual checks failed')
    require(resource['actual_observer_exit_code'] == resource['actual_child_exit_code'] == 0 and
            resource_driver['actual_observer_exit_code'] == resource_driver['actual_child_exit_code'] == 0,
            'Resource observer/child actual exit differs')
    require(resource_driver['timed_out'] is False and observed['exit_code'] == 0 and
            observed['timed_out'] is False and observed['ordinary_sigterm_requested'] is True,
            'Resource child did not exit by the verified normal path')
    require(resource['source_input_count'] == len(frozen) and
            resource['source_sha256_before'] == resource['source_sha256_after'] == frozen and
            resource['binary_sha256_before'] == resource['binary_sha256_after'] == native['artifacts'],
            'Resource source/main/helper differ')
    require(resource['runtime_or_credentials_archived'] is False, 'Resource privacy scope differs')
    require(host_path(observed['command'][0]).resolve(strict=True) == (NATIVE / 'wsl_release/sn100_shell').resolve(strict=True)
            and all(flag in observed['command'] for flag in ('--quality-control', '--calibration')),
            'Resource observer actually launched a different product/feature set')
    return {'frozen': frozen, 'native': native, 'binding': bind, 'proof': proof,
            'binding_proof_sha256': sha(DRIVERS / 'binding_proof.json'), 'bound_objects': bound_objects, 'suite': suite,
            'suite_results': results, 'windows': windows, 'windows_artifacts': windows_artifacts,
            'arm': arm, 'backup': backup, 'package_audit': package, 'response': response,
            'resource': resource, 'standalone': standalone, 'standalone_root': standalone_root,
            'projects_modules': projects_modules, 'windows_root': windows_root, 'arm_root': arm_root}


def safe_content(path, explicit_source=False):
    require(path.is_file() and not path.is_symlink(), 'Regular evidence file required: ' + str(path))
    require(not any(word in path.name.lower() for word in ('secret', 'bootstrap', 'credential', 'private_key')),
            'Credential-like evidence filename is prohibited')
    allowed = TEXT | MEDIA | (SOURCE if explicit_source else set())
    require(path.suffix.lower() in allowed or path.name == 'SHA256SUMS', 'Evidence extension is not allowlisted')
    require(path.stat().st_size <= 64 * 1024 * 1024, 'Oversized evidence file requires explicit root review')
    with path.open('rb') as stream:
        header = stream.read(32)
    require(not header.startswith((b'SQLite format 3', b'\x7fELF', b'MZ')), 'Runtime database/executable cannot enter evidence')
    if path.suffix.lower() not in MEDIA:
        require(b'\x00' not in path.read_bytes(), 'Binary data under a text extension is prohibited')


def collect_historical(stage, copy):
    """Preserve failed/superseded receipts by their own freeze, never as passes."""
    skipped = []
    roots = []
    original_exits = []
    copied_count = 0
    credential_words = ('secret', 'bootstrap', 'credential', 'private_key', 'id_rsa', 'id_ed25519')
    fixed_binding = {'binding_proof.json', 'test_inputs_before.json', 'test_inputs_after.json',
        'verification.json', 'failure.json'}
    fixed_binding.update('binding_build.' + suffix for suffix in
        ('command.json', 'process.json', 'exit.json', 'exit', 'log'))
    fixed_native = {'final_before.json', 'final_after.json', 'verification.json', 'failure.json'}
    fixed_native.update(phase + '.' + suffix for phase in
        ('snapshot_before', 'build', 'snapshot_after', 'artifact_types') for suffix in
        ('command.json', 'process.json', 'exit.json', 'exit', 'log'))

    def skip(path, reason):
        skipped.append({'path': path.relative_to(WORK).as_posix(), 'reason': reason,
                        'contents_traversed': False})

    def selected_file(path, base, kind, top=False):
        nonlocal copied_count
        if any(word in path.name.lower() for word in credential_words):
            skip(path, 'credential-like name; file not opened')
            return
        executed_source = re.fullmatch(r'[a-zA-Z0-9_]+_executed\.(cpp|py)', path.name) is not None
        if kind == 'native' and top:
            selected = path.name in fixed_native or executed_source
        elif kind == 'binding' and top:
            selected = path.name in fixed_binding or executed_source or re.fullmatch(
                r'[a-z][a-z0-9_]*\.(reuse|failed)\.json', path.name) is not None
        elif kind == 'windows_mixed':
            selected = top and path.suffix.lower() in TEXT
        else:
            parts = path.relative_to(base).parts
            anonymous_media = path.suffix.lower() in {'.png', '.html'} and (
                'evidence' in parts or parts[0] == 'core_shared' or
                (kind == 'module_stage' and path.parent.name == 'reagents_app' and path.name == 'page_loaded.png'))
            selected = path.suffix.lower() in TEXT or anonymous_media or executed_source
        if not selected:
            skip(path, 'unknown or non-allowlisted historical file; file not opened')
            return
        try:
            safe_content(path, explicit_source=executed_source)
        except RuntimeError as error:
            skip(path, 'historical safety check rejected file: ' + str(error))
            return
        copy(path, PurePosixPath('historical') / path.relative_to(WORK).as_posix(),
             explicit_source=executed_source)
        copied_count += 1
        if path.name.endswith('.exit.json'):
            row = load(path)
            original_exits.append({'receipt': path.relative_to(WORK).as_posix(),
                'actual_exit_code': row.get('exit_code'), 'state': row.get('state'),
                'timed_out': row.get('timed_out'), 'argv_and_receipt_rewritten': False})

    def walk(directory, base, kind, top=False):
        for path in sorted(directory.iterdir()):
            if path.is_symlink():
                skip(path, 'symlink; never followed')
            elif path.is_dir():
                name = path.name.lower()
                blocked = name in BLOCKED_DIRS or 'build' in name or 'runtime' in name or any(word in name for word in credential_words)
                # Historical native is root metadata/build.log only. Historical
                # binders additionally admit their raw logs, never build trees.
                restricted_top = top and (
                    kind in ('native', 'windows_mixed') or
                    (kind in ('binding', 'standalone', 'module_stage') and name != 'logs') or
                    (kind == 'modules' and name not in {row[0] for row in PROJECT_MODULE_TARGETS}))
                if blocked or restricted_top:
                    skip(path, 'excluded directory; no recursive traversal')
                elif top and kind == 'modules':
                    walk(path, base, 'module_stage', top=True)
                else:
                    walk(path, base, kind)
            elif path.is_file():
                selected_file(path, base, kind, top)
            else:
                skip(path, 'non-regular historical input')

    native_root = within(WORK / 'native_pre_atomic_01', WORK)
    require(sha(native_root / 'final_before.json') == HISTORICAL_FREEZE_SHA256,
            'Historical native is not the declared pre-atomic e65 source freeze')
    roots.append({'directory': native_root.name, 'kind': 'native', 'source_manifest_sha256': HISTORICAL_FREEZE_SHA256})
    walk(native_root, native_root, 'native', top=True)
    for name in HISTORICAL_BINDINGS:
        base = within(WORK / name, WORK)
        proof = load(base / 'binding_proof.json')
        require(proof['snapshot_sha256'] == HISTORICAL_FREEZE_SHA256,
                'Historical binder is not the declared pre-atomic e65 source freeze: ' + name)
        roots.append({'directory': name, 'kind': 'binding', 'source_manifest_sha256': proof['snapshot_sha256'],
            'recorded_product_objects': proof['native_objects_excluding_main'], 'recorded_drivers': len(proof['drivers'])})
        walk(base, base, 'binding', top=True)
    for name in HISTORICAL_SUITES:
        base = within(WORK / name, WORK)
        roots.append({'directory': name, 'kind': 'failed_or_partial_suite',
                      'historical_source_manifest_sha256': HISTORICAL_FREEZE_SHA256})
        walk(base, base, 'suite', top=True)
    # This failed run used the new atomic-repair production freeze. It must
    # never inherit the four older suites' e65 identity or imply a root cause.
    base = within(WORK / POST_ATOMIC_FAILED_SUITE, WORK)
    roots.append({'directory': base.name, 'kind': 'failed_nonfinal_suite',
        'source_manifest_sha256': POST_ATOMIC_HISTORICAL_FREEZE_SHA256,
        'eligible_for_current_passed_gate': False, 'failure_cause_inferred': False})
    walk(base, base, 'suite', top=True)
    optional_binding = WORK / OPTIONAL_POST_ATOMIC_BINDING
    if optional_binding.exists() or optional_binding.is_symlink():
        base = within(optional_binding, WORK)
        proof = load(base / 'binding_proof.json')
        require(proof['snapshot_sha256'] == POST_ATOMIC_HISTORICAL_FREEZE_SHA256,
                'Optional preserved calibration-reference binder is not the declared57f freeze')
        roots.append({'directory': base.name, 'kind': 'optional_failed_suite_binding',
            'source_manifest_sha256': proof['snapshot_sha256'],
            'recorded_product_objects': proof['native_objects_excluding_main'],
            'recorded_drivers': len(proof['drivers']), 'eligible_for_current_passed_gate': False})
        walk(base, base, 'binding', top=True)
    base = within(WORK / POST_INCLUDE_FAILED_SUITE, WORK)
    roots.append({'directory': base.name, 'kind': 'failed_nonfinal_suite',
        'source_manifest_sha256': POST_INCLUDE_HISTORICAL_FREEZE_SHA256,
        'eligible_for_current_passed_gate': False, 'failure_cause_inferred': False})
    walk(base, base, 'suite', top=True)
    optional_binding = WORK / OPTIONAL_POST_INCLUDE_BINDING
    if optional_binding.exists() or optional_binding.is_symlink():
        base = within(optional_binding, WORK)
        proof = load(within(base / 'binding_proof.json', base))
        require(proof['snapshot_sha256'] == POST_INCLUDE_HISTORICAL_FREEZE_SHA256,
                'Optional preserved login-geometry binder is not the declared5123 freeze')
        roots.append({'directory': base.name, 'kind': 'optional_failed_suite_binding',
            'source_manifest_sha256': proof['snapshot_sha256'],
            'recorded_product_objects': proof['native_objects_excluding_main'],
            'recorded_drivers': len(proof['drivers']), 'eligible_for_current_passed_gate': False})
        walk(base, base, 'binding', top=True)
    base = within(WORK / POST_ATOMIC_CHECKPOINT_SUITE, WORK)
    checkpoint = load(within(base / 'verification.json', base))
    require(checkpoint.get('passed') is True and checkpoint.get('full_suite') is True and
            checkpoint.get('invocations') == 35 and checkpoint.get('omitted_invocations') == [],
            'Declared historical suite06 checkpoint metadata differs')
    roots.append({'directory': base.name, 'kind': 'passed_checkpoint',
        'source_manifest_sha256': POST_ATOMIC_HISTORICAL_FREEZE_SHA256,
        'source_identity_basis': 'Root-confirmed57f checkpoint; copied historical native/binding retain exact provenance',
        'eligible_for_current_passed_gate': False, 'original_verification_passed': checkpoint['passed']})
    walk(base, base, 'suite', top=True)
    for name, kind in OPTIONAL_PRE_INCLUDE_ROOTS:
        candidate = WORK / name
        if not candidate.exists() and not candidate.is_symlink():
            continue
        base = within(candidate, WORK)
        row = {'directory': name, 'kind': 'pre_include_' + kind, 'eligible_for_current_passed_gate': False}
        if kind == 'windows_mixed':
            # The failed initial verification predates narrow diagnostic source
            # edits. Keep its raw scope/exits; never assign one freeze to the tree.
            recorded = load(within(base / 'verification.json', base))
            before, after = base / 'source_before.json', base / 'source_after.json'
            row.update(tree_source_freeze_sha256=None, source_manifest_sha256=None,
                recorded_native_freeze_sha256=recorded.get('native_freeze_sha256'),
                source_before_snapshot_sha256=sha(within(before, base)) if before.is_file() else None,
                source_after_snapshot_sha256=sha(within(after, base)) if after.is_file() else None,
                source_set_unchanged_verified=False, recorded_scope=recorded.get('scope'),
                recorded_driver_exit=recorded.get('exit_code'), recorded_published=recorded.get('published'),
                scope='Initial failed Windows build plus later source-edited diagnostic continuation; no new successful verdict')
        else:
            identity = base / ('binding_proof.json' if kind == 'binding' else
                               'final_before.json' if kind == 'native' else 'verification.json')
            if kind == 'native':
                digest = sha(within(identity, base))
            else:
                recorded = load(within(identity, base))
                digest = recorded['snapshot_sha256' if kind == 'binding' else 'source_snapshot_sha256']
            require(digest == POST_ATOMIC_HISTORICAL_FREEZE_SHA256,
                    'Optional pre-include historical source identity differs: ' + name)
            row['source_manifest_sha256'] = digest
            if kind == 'native':
                recorded = load(within(base / 'verification.json', base))
                require(recorded['snapshot_sha256'] == digest and
                        checkpoint['main_sha256'] == recorded['artifacts']['sn100_shell'] and
                        checkpoint['renderer_sha256'] == recorded['artifacts']['sn100_report_renderer'],
                        'Historical suite06/native checkpoint main/helper identity differs')
                row['recorded_artifacts'] = recorded['artifacts']
            if kind == 'binding':
                row['recorded_product_objects'] = recorded['native_objects_excluding_main']
                row['recorded_drivers'] = len(recorded['drivers'])
        roots.append(row)
        walk(base, base, kind, top=True)
    relocations = []
    relocation_paths = sorted(WORK.glob('*_relocation.json'))
    required_relocations = {'pre_atomic_relocation.json'} | {
        'cumulative_build_' + suffix + '_relocation.json' for suffix in ('01', '02', '03', '04')}
    require(required_relocations.issubset({path.name for path in relocation_paths}),
            'A required original historical relocation receipt is missing')
    for path in relocation_paths:
        require(path.is_file() and not path.is_symlink(), 'Regular historical relocation receipt required')
        safe_content(path)
        copy(path, 'historical/' + path.name)
        copied_count += 1
        relocations.append({'receipt': path.name, 'sha256': sha(path), 'original_records': load(path)})
    require(relocations, 'Historical preservation requires the original relocation receipts')
    source_by_root = {row['directory']: row.get('source_manifest_sha256') or row.get('historical_source_manifest_sha256')
                      for row in roots}
    index = {'schema': 'sn100-multi-freeze-historical-evidence-v2',
        'status': 'Failed/nonfinal or superseded history; each root retains its recorded source scope. '
            'Mixed diagnostic trees have no single asserted freeze. No failure cause is inferred.',
        'source_manifest_sha256_by_root': source_by_root, 'source_inputs': 426,
        'eligible_for_current_passed_gate': False, 'counts_toward_current_suite_invocations': False,
        'raw_argv_exit_and_proof_paths_rewritten': False,
        'path_interpretation': 'Original absolute paths in historical proofs/argv stay byte-exact. '
            'Use the copied relocation receipts to find preserved roots; do not resolve historical /native '
            'paths against the new current native. No historical binaries/objects or runtime trees are copied.',
        'roots': roots, 'relocation_receipts': relocations, 'original_child_exits': original_exits,
        'copied_files': copied_count, 'unknown_or_excluded_inputs': skipped,
        'existing_formal_calibration_and_report_repair_handoffs_modified': False}
    directory = stage / 'historical'
    directory.mkdir(exist_ok=True)
    (directory / 'index.json').write_text(json.dumps(index, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return {'index': 'historical/index.json', 'copied_files': copied_count, 'skipped_inputs': len(skipped),
            'source_manifest_sha256_by_root': source_by_root, 'eligible_for_current_passed_gate': False}


def collect(args, checked):
    target_root = ROOT / '04_references/sn100_host/qc_local_20261025'
    within(args.output_dir, target_root, existing=False)
    require(not args.output_dir.exists(), 'Preserve any prior final evidence; output must be new')
    stage = WORK / ('collect_stage_' + uuid.uuid4().hex)
    within(stage, WORK, existing=False)
    stage.mkdir()
    copied = {}
    excluded_symlinks = []

    def copy(path, relative, explicit_source=False):
        safe_content(path, explicit_source)
        relative = PurePosixPath(str(relative).replace('\\', '/'))
        require(not relative.is_absolute() and '..' not in relative.parts and '\n' not in str(relative), 'Unsafe destination name')
        name = relative.as_posix()
        require(name not in copied, 'Duplicate evidence destination: ' + name)
        destination = stage / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        before = sha(path)
        shutil.copyfile(path, destination)
        require(sha(path) == before == sha(destination), 'Evidence changed while copying: ' + str(path))
        copied[name] = {'source': str(path), 'sha256': before, 'bytes': destination.stat().st_size}

    def walk(directory, prefix, recursive=True):
        require(directory.is_dir(), 'Missing evidence directory: ' + str(directory))
        for path in sorted(directory.iterdir()):
            if path.is_symlink():
                excluded_symlinks.append({'path':str(path), 'reason':'never follow or archive test-created symlinks'})
                continue
            if path.is_dir():
                blocked = path.name.lower() in BLOCKED_DIRS or 'runtime' in path.name.lower()
                if recursive and not blocked:
                    walk(path, PurePosixPath(prefix) / path.name)
            elif path.suffix.lower() in TEXT | MEDIA or path.name == 'SHA256SUMS':
                if not any(word in path.name.lower() for word in ('secret', 'bootstrap', 'credential', 'private_key')):
                    copy(path, PurePosixPath(prefix) / path.name)

    walk(NATIVE, 'native', recursive=False)
    walk(DRIVERS, 'binding', recursive=False)
    walk(DRIVERS / 'logs', 'binding/logs')
    walk(args.suite_dir, 'unified')
    walk(checked['standalone_root'], 'standalone', recursive=False)
    walk(checked['standalone_root'] / 'logs', 'standalone/logs')
    walk(checked['windows_root'], 'platforms/windows', recursive=False)
    walk(checked['arm_root'], 'platforms/arm64', recursive=False)
    walk(checked['arm_root'] / 'compiler_receipts', 'platforms/arm64/compiler_receipts')
    walk(checked['arm_root'] / 'dependencies', 'platforms/arm64/dependencies')
    for name in ('capture_compiler.py', 'trace_build.bash'):
        copy(checked['arm_root'] / name, 'platforms/arm64/executed_driver/' + name, explicit_source=True)
    walk(args.response_dir, 'response')
    walk(args.resource_dir, 'resource')
    walk(args.resource_driver_dir, 'resource/driver')
    for name in ('verification.json', 'source_snapshot.json', 'arm64_verification.json', 'SHA256SUMS'):
        copy(args.backup_dir / name, 'recovery/' + name)
    modules = checked['projects_modules']
    for path in modules['evidence_files']:
        copy(path, 'projects_modules/' + path.relative_to(modules['root']).as_posix())
    historical = collect_historical(stage, copy)

    # Only explicitly known anonymous HTML-test exports may be selected from
    # a runtime subtree; runtime is never recursively traversed, and database,
    # INI, transient snapshots/credentials and helper binaries stay excluded.
    for row in checked['suite_results']:
        if row['driver'] != 'report_remaining_html_test':
            continue
        runtime = args.suite_dir / row['name'] / 'runtime'
        marker = runtime / '.sn100-history-offline'
        require(marker.read_text(encoding='utf-8') == 'SN100 O04 ANONYMOUS OFFLINE DATA v1\n',
                'Explicit HTML exports require their exact anonymous marker')
        for path in sorted((runtime / 'exports').iterdir()):
            if path.is_file() and path.suffix.lower() in {'.html', '.pdf'}:
                copy(path, 'unified/' + row['name'] + '/anonymous_exports/' + path.name)

    # Keep only the exact executed test inputs, not a whole production tree.
    for driver, record in checked['proof']['drivers'].items():
        source = source_for(driver, record)
        copy(source, 'test_sources/' + record['source_relative'], explicit_source=True)
        if source.with_suffix('.pro').is_file():
            project = source.with_suffix('.pro')
            copy(project, 'test_sources/' + project.relative_to(PROGRAM).as_posix(), explicit_source=True)
    fixture = PROGRAM / 'calibration/tests/calibration_fixture.h'
    if fixture.is_file():
        copy(fixture, 'test_sources/calibration/tests/calibration_fixture.h', explicit_source=True)
    for name, expected in modules['test_inputs'].items():
        path = within(PROGRAM / name, PROGRAM)
        require(sha(path) == expected, 'Project/reagent test input changed while collecting')
        copy(path, 'test_sources/' + name, explicit_source=True)
    copy(PROGRAM / 'tests/build_shell_drivers.py', 'driver_sources/build_shell_drivers.py', explicit_source=True)
    driver_root = Path(__file__).resolve().parent
    for path in sorted(driver_root.rglob('*')):
        if path.is_file() and 'audit_originals' not in path.parts and '__pycache__' not in path.parts:
            if path.suffix.lower() in {'.py', '.ps1', '.sh', '.json', '.txt'}:
                copy(path, 'driver_sources/integration/' + path.relative_to(driver_root).as_posix(), explicit_source=True)

    inputs_path = stage / 'copied_inputs.json'
    inputs_path.write_text(json.dumps(copied, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    frozen = checked['frozen']
    native = checked['native']
    verification = {'schema': 'sn100-four-windows-final-evidence-v1', 'passed': True,
        'collected_utc': now(), 'source_inputs': len(frozen), 'source_manifest_sha256': sha(FREEZE),
        'suite_invocations': len(checked['suite_results']),
        'qt_totals_by_invocation': {row['name']: row['totals'] for row in checked['suite_results']},
        'native_artifacts': native['artifacts'], 'windows_artifacts': checked['windows_artifacts'],
        'native_binding_object_verification': checked['bound_objects'],
        'historical_evidence': historical,
        'arm64_artifacts': {name: checked['arm'][key] for name, key in
            (('sn100_shell', 'main_sha256'), ('sn100_report_renderer', 'renderer_sha256'))},
        'standalone_artifacts': {name: row['binary_sha256'] for name, row in checked['standalone']['modules'].items()},
        'additional_project_reagent_test_invocations': len(modules['verification']['tests']),
        'additional_project_reagent_qt_totals': {row['name']: row['totals'] for row in modules['verification']['tests']},
        'additional_project_reagent_standalone_invocations': len(modules['verification']['standalone']),
        'additional_project_reagent_standalone_exits': {row['name']: row['actual_exit'] for row in modules['verification']['standalone']},
        'project_reagent_module_artifacts': {name: row['sha256'] for name, row in modules['verification']['artifacts'].items()},
        'project_reagent_module_evidence': 'projects_modules/verification.json',
        'release_id': checked['package_audit']['release_id'],
        'recovery_directory': str(args.backup_dir), 'archive_files_copied_into_evidence': False,
        'response_sample_count': checked['response']['sample_count'],
        'resource_actual_child_exit': checked['resource']['actual_child_exit_code'],
        'runtime_databases_ini_credentials_objects_or_binaries_copied': False,
        'copied_input_manifest': 'copied_inputs.json', 'copied_input_count': len(copied),
        'excluded_symlinks': excluded_symlinks,
        'verification_basis': 'Existing actual raw child exits, Qt text/XML, bound test source/binary SHA, '
            'native/Windows/ARM source freezes, packaged members, archived pair, response and resource receipts; '
            'no successful values substituted for missing receipts.',
        'pending': ['Original Windows complete behavior/active-theme and real Windows GUI',
            'Original suite_final_05 calibration-reference SIGSEGV direct cause remains unconfirmed; current final pass cannot establish long stability',
            'Original suite_final_07 login1920 containment failure direct cause remains unconfirmed; test-host preparation corrected, no production layout repair claimed',
            'RK screen/input/actual dependencies and instrument/LIS/printing',
            'Clinical calibration/QC algorithm effects and real-result identity chain',
            'Approved resource/same-board RT budgets and long stability',
            'Existing intermittent legacy report query and old 8 h recovery root causes remain separate']}
    (stage / 'verification.json').write_text(json.dumps(verification, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    lines = []
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            lines.append(sha(path) + '  ' + path.relative_to(stage).as_posix() + '\n')
    (stage / 'SHA256SUMS').write_text(''.join(lines), encoding='utf-8')

    def reread(directory):
        require(load(directory / 'verification.json') == verification, 'Final verification reread differs')
        require(load(directory / 'copied_inputs.json') == copied, 'Copied-input manifest reread differs')
        declared = {}
        for line in (directory / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
            digest, name = line.split('  ', 1)
            require(name not in declared, 'Duplicate final SHA entry')
            declared[name] = digest
            require(sha(within(directory / name, directory)) == digest, 'Final evidence reread SHA differs: ' + name)
        actual = {path.relative_to(directory).as_posix() for path in directory.rglob('*') if path.is_file()}
        require(actual == set(declared) | {'SHA256SUMS'}, 'Final exact evidence file set differs')
        return len(actual)

    reread(stage)
    verify_native(frozen)
    require(sha(DRIVERS / 'binding_proof.json') == checked['binding_proof_sha256'],
            'Current binding proof changed during collection')
    require(verify_bound_native_objects(checked['proof'], native, frozen) == checked['bound_objects'],
            'Current native object/source binding changed during collection')
    require(verify_projects_modules(frozen, native) == checked['projects_modules'],
            'Project/reagent module sources/builds/receipts changed during collection')
    args.output_dir.parent.mkdir(parents=True, exist_ok=True)
    require(not args.output_dir.exists(), 'Final directory appeared during collection; preserved staged evidence')
    stage.rename(args.output_dir)
    count = reread(args.output_dir)
    return {'output': str(args.output_dir), 'source_inputs': len(frozen), 'suite_invocations': len(checked['suite_results']),
            'additional_project_reagent_tests': len(modules['verification']['tests']),
            'additional_project_reagent_standalones': len(modules['verification']['standalone']),
            'evidence_files': count, 'verification_sha256': sha(args.output_dir / 'verification.json'),
            'manifest_sha256': sha(args.output_dir / 'SHA256SUMS')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    parser.add_argument('--suite-dir', type=Path, required=True)
    parser.add_argument('--response-dir', type=Path, required=True)
    parser.add_argument('--resource-dir', type=Path, required=True)
    parser.add_argument('--resource-driver-dir', type=Path)
    parser.add_argument('--backup-dir', type=Path, default=ROOT / '07_backups/sn100_host/offline_four_windows_20261025')
    parser.add_argument('--output-dir', type=Path, default=DEFAULT_FINAL)
    parser.add_argument('--expected-invocations', type=int, default=35)
    parser.add_argument('--expected-inputs', type=int)
    args = parser.parse_args()
    require(args.root_go and os.name == 'posix' and os.geteuid() != 0, 'Explicit root go and ordinary WSL user required')
    for name in ('suite_dir', 'response_dir', 'resource_dir'):
        setattr(args, name, within(getattr(args, name), WORK))
    args.resource_driver_dir = within(args.resource_driver_dir or
        args.resource_dir.with_name(args.resource_dir.name + '_driver'), WORK)
    args.backup_dir = within(args.backup_dir, ROOT / '07_backups/sn100_host')
    # Validate every future stage before any official final evidence is created.
    checked = validate(args)
    print(json.dumps(collect(args, checked), ensure_ascii=False, indent=2), flush=True)


if __name__ == '__main__':
    try:
        main()
    except BaseException as error:
        # No pass document is emitted on a missing/failed stage. Retain a unique
        # small failure receipt in temp without replacing earlier collections.
        failure = WORK / ('collection_failure_' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S_') + uuid.uuid4().hex + '.json')
        failure.write_text(json.dumps({'schema': 'sn100-final-collection-failure-v1', 'failed_utc': now(),
            'error_type': type(error).__name__, 'error': str(error), 'passed': False},
            ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        raise
