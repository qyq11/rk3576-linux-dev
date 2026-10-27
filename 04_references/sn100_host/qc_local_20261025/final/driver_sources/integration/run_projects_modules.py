#!/usr/bin/env python3
"""Serial fresh project/reagent module verification; explicit --root-go only.

Importing this driver performs no build, process launch or filesystem write.
Run after the cumulative suite has stopped. All six qmake builds and anonymous
runtimes are new; existing native binaries, evidence and sources are read only.
"""
import argparse
from contextlib import closing
import os
from pathlib import Path
import re
import shlex
import sqlite3
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zlib

from common import (WORK, ROOT, PROGRAM, NATIVE, FREEZE, load, now, require,
                    save, sha, stop_owned_group, verify_native)


STAGE = WORK / 'projects_modules_final'
EXPECTED_FREEZE = '5123bf1442472450bbde23a59fc47287dc85ead34e006c523a96a016ac76cf29'
EXPECTED_STORE = 'b234dd9d2609fc1b424396a2d9244f4dc2a9b4929fb0f9dae46a12685141702f'
PROJECT_READY = '项目/单位已读取；当前仅保存本地配置，未同步仪器。'
TARGETS = (
    ('project_advanced', 'projects/tests/project_advanced_test.pro', 'project_advanced_test', 11),
    ('project_basis', 'projects/tests/projects_test.pro', 'projects_test', 9),
    ('reagent_batches', 'reagents/tests/reagent_batch_test.pro', 'reagent_batch_test', 11),
    ('reagent_basis', 'reagents/tests/tests.pro', 'test_reagents', 11),
    ('projects_app', 'projects/projects.pro', 'sn100_projects', None),
    ('reagents_app', 'reagents/reagents.pro', 'sn100_reagents', None),
)
TEST_INPUTS = (
    'projects/tests/project_advanced_test.pro', 'projects/tests/project_advanced_test.cpp',
    'projects/tests/projects_test.pro', 'projects/tests/projects_test.cpp',
    'reagents/tests/reagent_batch_test.pro', 'reagents/tests/reagent_batch_test.cpp',
    'reagents/tests/tests.pro', 'reagents/tests/test_reagents.cpp',
)


def file_sha(path):
    path = Path(path)
    return sha(path) if path.is_file() and not path.is_symlink() else None


def binding_state(frozen, native):
    """Actual file observations, including on failure; never hash runtime DBs."""
    return {
        'source_inputs': len(frozen), 'source_snapshot_sha256': file_sha(FREEZE),
        'source_sha256': {name: file_sha(PROGRAM / name) for name in frozen},
        'test_inputs_sha256': {name: file_sha(PROGRAM / name) for name in TEST_INPUTS},
        'driver_inputs_sha256': {str(path): file_sha(path) for path in
                                (Path(__file__).resolve(), Path(__file__).with_name('common.py'))},
        'native_verification_sha256': file_sha(NATIVE / 'verification.json'),
        'native_makefile_sha256': file_sha(NATIVE / 'wsl_release/Makefile'),
        'main_artifacts': {name: file_sha(NATIVE / 'wsl_release' / name)
                           for name in ('sn100_shell', 'sn100_report_renderer')},
        'native_objects_sha256': {name: file_sha(NATIVE / name) for name in native['fresh_objects']},
    }


def checkpoint(path, frozen, native, baseline):
    observed = binding_state(frozen, native)
    save(path, observed)
    require(observed == baseline, 'Source/test/driver/native binding changed; see ' + str(path))
    verify_native(frozen)  # Also checks the formal inventory for added/removed inputs.
    return observed


def run_phase(name, command, cwd, logs, env, timeout):
    """Preserve the original command/process/exit/log before parsing anything."""
    record = {'name': name, 'command': list(map(str, command)), 'cwd': str(cwd),
              'started_utc': now(), 'timeout_seconds': timeout,
              'stdout_stderr': str(logs / (name + '.log'))}
    save(logs / (name + '.command.json'), record)
    print('[projects_modules] start ' + str(cwd.parent.name) + '/' + name, flush=True)
    started = time.monotonic()
    process, failure, cleanup = None, None, None
    try:
        with (logs / (name + '.log')).open('wb') as stream:
            process = subprocess.Popen(record['command'], cwd=cwd, env=env,
                                       start_new_session=True, stdout=stream, stderr=subprocess.STDOUT)
            save(logs / (name + '.process.json'), {'pid': process.pid, 'pgid': process.pid,
                                                   'state': 'running'})
            process.wait(timeout=timeout)
    except BaseException as error:
        failure = error
        if process is not None:
            cleanup = stop_owned_group(process)
    record.update(exit_code=process.returncode if process is not None else None,
                  timed_out=isinstance(failure, subprocess.TimeoutExpired),
                  state='not_started' if process is None else 'finished',
                  seconds=time.monotonic() - started, finished_utc=now(),
                  pid=process.pid if process is not None else None, cleanup=cleanup,
                  exit_receipt=str(logs / (name + '.exit.json')))
    if failure:
        record.update(exception_type=type(failure).__name__, exception=str(failure))
    save(logs / (name + '.exit.json'), record)
    (logs / (name + '.exit')).write_text(str(record['exit_code']) + '\n', encoding='ascii')
    save(logs / (name + '.process.json'), {'pid': record['pid'], 'pgid': record['pid'],
                                          'state': record['state'], 'exit_code': record['exit_code']})
    if failure:
        raise failure
    require(record['exit_code'] == 0 and not record['timed_out'],
            name + ' failed; original exit/log retained in ' + str(logs))
    print('[projects_modules] finished ' + str(cwd.parent.name) + '/' + name + ' exit=0', flush=True)
    return record


def private_environment(runtime):
    removed = []
    env = dict(os.environ)
    for key in tuple(env):
        if key.startswith(('SN100_', 'QT_', 'QMAKE', 'QML', 'CCACHE')) or key in (
                'MAKEFLAGS', 'MFLAGS', 'GNUMAKEFLAGS', 'MAKELEVEL', 'CC', 'CXX', 'LD', 'AR',
                'LD_PRELOAD', 'LD_LIBRARY_PATH', 'LIBRARY_PATH', 'CPATH', 'CPLUS_INCLUDE_PATH',
                'C_INCLUDE_PATH', 'COMPILER_PATH', 'GCC_EXEC_PREFIX'):
            removed.append(key)
            del env[key]
    overrides = {'QT_QPA_PLATFORM': 'offscreen', 'QT_SCALE_FACTOR': '1',
                 'QT_AUTO_SCREEN_SCALE_FACTOR': '0', 'QT_LOGGING_TO_CONSOLE': '1'}
    for key, directory in (('XDG_RUNTIME_DIR', 'xdg-runtime'), ('XDG_CONFIG_HOME', 'config'),
                           ('XDG_CACHE_HOME', 'cache'), ('XDG_DATA_HOME', 'data'), ('TMPDIR', 'tmp')):
        path = runtime / directory
        path.mkdir(mode=0o700)
        overrides[key] = str(path)
    overrides['TMP'] = overrides['TEMP'] = overrides['TMPDIR']
    batch_root = runtime / 'batch-root'
    batch_root.mkdir()
    overrides['SN100_BATCH_TEST_ROOT'] = str(batch_root)
    env.update(overrides)
    return env, {'overrides': overrides, 'removed_inherited_variable_names': sorted(removed),
                 'parallel_jobs_maximum': 2, 'runtime_values_hashed': False}


def make_words(makefile, variable):
    text = makefile.read_text(encoding='utf-8').replace('\\\n', ' ')
    rows = re.findall(r'^' + re.escape(variable) + r'\s*=\s*(.*)$', text, re.MULTILINE)
    require(len(rows) == 1, 'Expected one Makefile variable: ' + variable)
    return shlex.split(rows[0])


def build_inventory(build, binary):
    objects = {p.relative_to(build).as_posix(): sha(p)
               for p in sorted(build.rglob('*.o')) if p.is_file()}
    generated = {p.relative_to(build).as_posix(): sha(p) for p in sorted(build.rglob('*'))
                 if p.is_file() and (p.parent.name in ('.moc', '.ui', '.rcc')
                                     or p.name in ('Makefile', '.qmake.stash'))}
    return {'binary_sha256': file_sha(binary), 'objects_sha256': objects,
            'generated_sha256': generated}


def verify_build(build, binary, frozen, started_ns):
    makefile = build / 'Makefile'
    text = makefile.read_text(encoding='utf-8')
    require('-O2' in text and '-g ' not in text, 'Fresh Makefile must use Release flags')
    require(make_words(makefile, 'TARGET') == [binary.name], 'Unexpected qmake TARGET')
    objects = make_words(makefile, 'OBJECTS')
    require(objects and len(set(objects)) == len(objects), 'Missing/duplicate Makefile OBJECTS')
    expected_objects = {Path(word).as_posix() for word in objects}
    inventory = build_inventory(build, binary)
    require(set(inventory['objects_sha256']) == expected_objects,
            'Actual fresh object set differs from Makefile OBJECTS')
    for word in objects:
        path = (build / word).resolve(strict=True)
        require(path.is_relative_to(build.resolve()), 'Object escapes private build: ' + word)
        require(path.stat().st_mtime_ns + 2_000_000_000 >= started_ns,
                'Object timestamp predates its fresh build: ' + word)
    require(binary.is_file() and os.access(binary, os.X_OK), 'Fresh executable missing')
    require(binary.stat().st_mtime_ns + 2_000_000_000 >= started_ns, 'Executable predates build')
    source_rows = []
    for word in make_words(makefile, 'SOURCES'):
        path = (build / word).resolve(strict=True)
        if path.is_relative_to(PROGRAM.resolve()):
            name = path.relative_to(PROGRAM.resolve()).as_posix()
            require(name in frozen and sha(path) == frozen[name], 'Unfrozen qmake source: ' + name)
            source_rows.append({'source_relative': name, 'sha256': sha(path), 'generated': False})
        else:
            require(path.is_relative_to(build.resolve()), 'Unexpected external qmake source: ' + str(path))
            source_rows.append({'source_relative': path.relative_to(build).as_posix(),
                                'sha256': sha(path), 'generated': True})
    require(any(row['source_relative'] == 'projects/projects_store.cpp' for row in source_rows),
            'The repaired ProjectsStore was not a source of this target')
    return inventory, {'sources': source_rows, 'objects': objects,
                       'fresh_empty_build_directory': True, 'object_timestamp_tolerance_seconds': 2,
                       'makefile_sha256': sha(makefile), 'binary_sha256': sha(binary)}


def verify_qt(stage, expected):
    txt, xml = stage / 'qt.txt', stage / 'qt.xml'
    text = txt.read_text(encoding='utf-8', errors='replace')
    totals = re.findall(r'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped', text)
    root = ET.parse(xml).getroot()
    cases = list(root.iter('testcase'))
    bad = [dict(case.attrib) for case in cases
           if any(child.tag in ('failure', 'error', 'skipped') for child in case)]
    parsed = {'raw_txt': str(txt), 'raw_junitxml': str(xml), 'txt_sha256': sha(txt),
              'junitxml_sha256': sha(xml), 'totals': list(map(int, totals[0])) if len(totals) == 1 else None,
              'junit_testcases': [dict(case.attrib) for case in cases],
              'junit_failure_error_skipped_cases': bad, 'junit_root_attributes': dict(root.attrib),
              'expected_passes_including_init_cleanup': expected,
              'junit_diagnostic_count_attributes_are_not_business_failures': True}
    save(stage / 'qt_verification.json', parsed)
    require(parsed['totals'] == [expected, 0, 0], 'Unexpected/failed/skipped Qt totals: ' + str(stage))
    require(len(cases) == expected and not bad, 'JUnit cases incomplete or failed: ' + str(stage))
    require('initTestCase' in {case.get('name') for case in cases}
            and 'cleanupTestCase' in {case.get('name') for case in cases}, 'Qt lifecycle cases missing')
    return parsed


def verify_catalog(directory, reagent):
    """Read only this invocation's newly created anonymous catalog after close."""
    marker, database = directory / '.sn100-catalog-offline-v1', directory / 'catalog.db'
    require(marker.is_file() and not marker.is_symlink() and database.is_file()
            and not database.is_symlink(), 'Fresh catalog/marker missing or linked')
    require(marker.read_text(encoding='utf-8').strip() == 'sn100-catalog-offline-v1', 'Catalog marker mismatch')
    with closing(sqlite3.connect(database.as_uri() + '?mode=ro', uri=True)) as connection:
        metadata = dict(connection.execute('SELECT key,value FROM catalog_metadata'))
        units = dict(connection.execute('SELECT unit,ratio FROM option_unit'))
        projects = list(connection.execute('SELECT project_id,project_en_name FROM project_base'))
        results = list(connection.execute('SELECT result_type,result_name,unit_name FROM project_result ORDER BY result_type'))
        rates = list(connection.execute('SELECT sample_rate,sample_option,sample_default_option,xuhao FROM project_sample_rate ORDER BY xuhao'))
        tables = {row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        integrity = [row[0] for row in connection.execute('PRAGMA integrity_check')]
        foreign_key_errors = list(connection.execute('PRAGMA foreign_key_check'))
        counts = {name: connection.execute('SELECT COUNT(*) FROM ' + name).fetchone()[0]
                  for name in ('project_advanced', 'catalog_project_reference')}
        if reagent:
            required_tables = ('reagent_list', 'reagent_bottle_type', 'reagent_batch_group',
                               'reagent_batch_metadata', 'catalog_batch_reference', 'reagent_barcode',
                               'reagent_setting', 'project_action_reagent_reference')
            require(set(required_tables).issubset(tables), 'Reagent schema incomplete')
            counts.update({name: connection.execute('SELECT COUNT(*) FROM ' + name).fetchone()[0]
                           for name in required_tables})
    require(metadata.get('schema') == 'sn100-catalog-offline-v1'
            and metadata.get('project_advanced_schema') == 'sn100-project-advanced-local-v1', 'Catalog schema mismatch')
    require(not reagent or metadata.get('reagent_batch_metadata') == 'v1', 'Batch schema mismatch')
    require(units == {'s': 1.0, 'a.u.': 1.0, 'a.u.x10': 10.0}, 'Explicit anonymous unit seed missing')
    require(projects == [('OFFLINE-001', 'DEMO_A')], 'Unexpected anonymous project seed')
    require(results == [('Calibration', 'DEMO_A_C', 'a.u.'), ('Time', 'DEMO_A', 's')], 'Anonymous result seed mismatch')
    require(rates == [('OFFLINE_RATE_A', 1, 1, 1), ('OFFLINE_RATE_B', 1, 0, 2)], 'Anonymous sample-rate seed mismatch')
    require(all(value == 0 for value in counts.values()), 'Fresh unused tables should be empty')
    require(integrity == ['ok'] and not foreign_key_errors, 'Fresh catalog integrity/reference check failed')
    return {'directory': str(directory), 'opened_read_only_after_app_exit': True,
            'marker': 'sn100-catalog-offline-v1', 'metadata': metadata,
            'project_rows': len(projects), 'unit_rows': len(units), 'result_rows': len(results),
            'sample_rate_rows': len(rates), 'unused_table_counts': counts,
            'integrity_check': integrity, 'foreign_key_errors': foreign_key_errors,
            'runtime_database_hashed': False,
            'hole_rows_are_not_seeded_by_reagent_initialize': True if reagent else None}


def verify_ready_png(path):
    """The existing CLI only saves this file after the real loaded/ready signal."""
    data = path.read_bytes()
    require(data.startswith(b'\x89PNG\r\n\x1a\n'), 'Ready screenshot is not PNG')
    offset, tags, dimensions = 8, [], None
    while offset < len(data):
        require(offset + 12 <= len(data), 'Truncated readiness PNG')
        length, = struct.unpack('>I', data[offset:offset + 4])
        tag, payload = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + length]
        require(offset + 12 + length <= len(data), 'Truncated readiness PNG chunk')
        crc, = struct.unpack('>I', data[offset + 8 + length:offset + 12 + length])
        require(zlib.crc32(tag + payload) & 0xffffffff == crc, 'Readiness PNG CRC mismatch')
        if tag == b'IHDR':
            dimensions = list(struct.unpack('>II', payload[:8]))
        tags.append(tag.decode('ascii'))
        offset += 12 + length
        if tag == b'IEND':
            break
    require(dimensions == [1751, 968] and 'IDAT' in tags and tags[-1:] == ['IEND']
            and offset == len(data), 'Readiness PNG incomplete or wrong page size')
    return {'path': str(path), 'sha256': sha(path), 'dimensions': dimensions,
            'readiness_signal': 'ReagentStore::loaded -> ReagentsPage::readyChanged(true) -> CLI screenshot',
            'positive_stdout_marker_exists': False, 'visual_acceptance_performed': False}


def source_contract():
    main = (PROGRAM / 'reagents/main.cpp').read_text(encoding='utf-8-sig')
    page = (PROGRAM / 'reagents/reagents_page.cpp').read_text(encoding='utf-8-sig')
    store = (PROGRAM / 'reagents/reagent_store.cpp').read_text(encoding='utf-8-sig')
    project_main = (PROGRAM / 'projects/main.cpp').read_text(encoding='utf-8-sig')
    project_page = (PROGRAM / 'projects/project_page.cpp').read_text(encoding='utf-8-sig')
    require(PROJECT_READY in project_page and '&ProjectPage::statusChanged' in project_main
            and 'qInfo().noquote() << message' in project_main, 'Project readiness stdout contract changed')
    require('emit loaded(snapshot);' in store and '&ReagentStore::loaded' in page
            and 'emit readyChanged(true);' in page and '&ReagentsPage::readyChanged' in main
            and 'if (ready)' in main and 'page.grab().save(screenshot)' in main,
            'Existing reagent ready-triggered screenshot contract changed')
    require('"quit-after-ms"' in main and '"exit-after-ms"' in project_main
            and 'Qt::BlockingQueuedConnection' in main and 'Qt::BlockingQueuedConnection' in project_main
            and 'reagentThread.wait();' in main and 'projectThread.wait();' in main
            and 'thread.wait();' in project_main, 'Standalone CLI/ordinary close contract changed')
    paths = ('projects/main.cpp', 'projects/project_page.cpp', 'reagents/main.cpp',
             'reagents/reagents_page.cpp', 'reagents/reagent_store.cpp')
    return {'source_sha256': {name: sha(PROGRAM / name) for name in paths},
            'projects_stdout_marker': PROJECT_READY,
            'reagents_readiness': 'Existing --screenshot branch runs 300ms after readyChanged(true), emitted only on loaded snapshot',
            'normal_close': 'Existing 1800ms QWidget::close timer; actual exit=0 after blocking store closes and thread waits',
            'invented_stdout_close_or_load_markers': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go', action='store_true', required=True)
    args = parser.parse_args()
    require(args.root_go and sys.platform == 'linux' and os.geteuid() != 0,
            'Explicit --root-go and ordinary Linux WSL user required')
    import pwd
    require(pwd.getpwuid(os.geteuid()).pw_name == 'qyq'
            and 'microsoft' in Path('/proc/sys/kernel/osrelease').read_text().lower(),
            'Run only as ordinary WSL user qyq')
    require(os.environ.get('WSL_DISTRO_NAME') == 'Ubuntu-24.04', 'Ubuntu-24.04 WSL required')
    require(not STAGE.exists() and not STAGE.is_symlink(), 'Fresh projects_modules_final required; preserve prior attempts')
    frozen = load(FREEZE)
    require(len(frozen) == 426 and sha(FREEZE) == EXPECTED_FREEZE
            and frozen.get('projects/projects_store.cpp') == EXPECTED_STORE,
            'Exact repaired 426-input/5123bf14 source freeze required')
    native = verify_native(frozen)
    baseline = binding_state(frozen, native)
    require(baseline['source_sha256'] == frozen, 'Actual source differs from freeze')
    qmake = Path('/usr/lib/qt5/bin/qmake')
    require(qmake.is_file() and Path('/usr/bin/make').is_file(), 'Existing native Qt5 qmake/make required; no installation')
    STAGE.mkdir(mode=0o700)
    report = {'schema': 'sn100-project-modules-final-v1', 'passed': False,
              'started_utc': now(), 'source_inputs': len(frozen),
              'source_snapshot_sha256': sha(FREEZE), 'main_artifacts': baseline['main_artifacts'],
              'checks': {}, 'stages': [], 'tests': [], 'standalone': [], 'artifacts': {},
              'scope': 'WSL native Release/offscreen anonymous local tests only',
              'board_or_instrument_verified': False, 'parallel_jobs_maximum': 2}
    save(STAGE / 'verification.json', report)
    save(STAGE / 'binding_before.json', baseline)
    save(STAGE / 'source_before.json', baseline['source_sha256'])
    save(STAGE / 'test_inputs_before.json', baseline['test_inputs_sha256'])
    failure = None
    try:
        save(STAGE / 'source_contract.json', source_contract())
        for name, pro, target, expected in TARGETS:
            stage = STAGE / name
            stage.mkdir()
            build, runtime, logs = stage / 'build', stage / 'runtime', stage / 'logs'
            for directory in (build, runtime, logs):
                directory.mkdir()
            record = {'name': name, 'qmake_project': pro, 'target': target,
                      'kind': 'test' if expected is not None else 'standalone',
                      'passed': False, 'phases': []}
            report['stages'].append(record)
            save(STAGE / 'verification.json', report)
            binary = build / target
            save(stage / 'build_before.json', build_inventory(build, binary))
            checkpoint(stage / 'binding_before.json', frozen, native, baseline)
            env, env_receipt = private_environment(runtime)
            save(stage / 'environment.json', env_receipt)
            build_start = time.time_ns()
            try:
                command = [str(qmake), str(PROGRAM / pro), '-o', str(build / 'Makefile'),
                           '-spec', 'linux-g++', 'CONFIG+=release', 'CONFIG-=debug']
                record['phases'].append(run_phase('qmake', command, build, logs, env, 120))
                record['phases'].append(run_phase('make', ['/usr/bin/make', '-C', str(build), '-j2'],
                                                   build, logs, env, 900))
                inventory, proof = verify_build(build, binary, frozen, build_start)
                save(stage / 'build_after_make.json', inventory)
                save(stage / 'build_proof.json', proof)
                record['binary_sha256'] = inventory['binary_sha256']
                report['artifacts'][target] = {'path': str(binary), 'sha256': record['binary_sha256'],
                                                'qmake_project': pro, 'freshly_built': True}
                checkpoint(stage / 'binding_before_run.json', frozen, native, baseline)
                if expected is not None:
                    command = [str(binary), '-v1', '-o', str(stage / 'qt.txt') + ',txt',
                               '-o', str(stage / 'qt.xml') + ',junitxml']
                    receipt = run_phase('test', command, runtime, logs, env, 240)
                    record['phases'].append(receipt)
                    parsed = verify_qt(stage, expected)
                    report['tests'].append({'name': name, 'target': target, 'actual_exit': receipt['exit_code'],
                                             'binary_sha256': record['binary_sha256'], **parsed})
                else:
                    catalog = runtime / 'catalog'
                    require(not catalog.exists(), 'Standalone must create a new anonymous catalog')
                    command = [str(binary), '--data-dir', str(catalog), '--seed-demo', '--test-engineer']
                    ready_png = stage / 'page_loaded.png'
                    if name == 'projects_app':
                        command += ['--exit-after-ms', '1800']
                    else:
                        require(not ready_png.exists(), 'Ready screenshot must be new')
                        command += ['--quit-after-ms', '1800', '--screenshot', str(ready_png)]
                    receipt = run_phase('app', command, runtime, logs, env, 30)
                    record['phases'].append(receipt)
                    require(receipt['seconds'] >= 1.5, 'App exited before its ordinary 1800ms close timer')
                    output = (logs / 'app.log').read_text(encoding='utf-8', errors='replace')
                    diagnostics = ('QThread: Destroyed while thread is still running',
                                   'QSqlDatabasePrivate::database: requested database does not belong',
                                   'QSqlDatabasePrivate::removeDatabase:', 'QSqlQuery::exec: database not open',
                                   'QSqlQuery::prepare: database not open', 'ASSERT failure', 'ASSERT:',
                                   'Segmentation fault', 'terminate called', '初始化试剂', '读取试剂', '试剂存储线程错误')
                    found = [token for token in diagnostics if token in output]
                    require(not found, 'Standalone diagnostic failure: ' + repr(found))
                    if name == 'projects_app':
                        require(output.count(PROJECT_READY) == 1, 'Real project-loaded stdout marker must occur once')
                        readiness = {'stdout_marker': PROJECT_READY, 'count': output.count(PROJECT_READY)}
                    else:
                        readiness = verify_ready_png(ready_png)
                    catalog_receipt = verify_catalog(catalog, name == 'reagents_app')
                    app_result = {'name': name, 'target': target, 'actual_exit': receipt['exit_code'],
                                  'binary_sha256': record['binary_sha256'], 'seconds': receipt['seconds'],
                                  'timer_ms': 1800, 'close_mode': 'existing guarded QWidget::close timer',
                                  'timeout_cleanup_used': receipt['cleanup'] is not None,
                                  'readiness': readiness, 'rejected_diagnostics_observed': found,
                                  'catalog': catalog_receipt, 'app_log_sha256': sha(logs / 'app.log')}
                    save(stage / 'standalone_verification.json', app_result)
                    report['standalone'].append(app_result)
                after = build_inventory(build, binary)
                save(stage / 'build_after_run.json', after)
                require(after == inventory, 'Fresh objects/generated files/binary changed during execution')
                checkpoint(stage / 'binding_after.json', frozen, native, baseline)
                record['checks'] = {'fresh_release_build': True, 'objects_and_binary_unchanged': True,
                                    'source_test_driver_native_binding_unchanged': True, 'actual_run_exit_zero': True}
                record['passed'] = True
            except BaseException as error:
                record.update(exception_type=type(error).__name__, exception=str(error))
                raise
            finally:
                # Even unsuccessful builds/runs retain their actual partial artifacts and source observations.
                save(stage / 'build_observed_final.json', build_inventory(build, binary))
                save(stage / 'binding_observed_final.json', binding_state(frozen, native))
                save(stage / 'verification.json', record)
                save(STAGE / 'verification.json', report)
        checkpoint(STAGE / 'binding_after.json', frozen, native, baseline)
        for record in report['stages']:
            stage = STAGE / record['name']
            current = build_inventory(stage / 'build', stage / 'build' / record['target'])
            save(stage / 'build_at_final.json', current)
            require(current == load(stage / 'build_after_make.json'), 'Earlier stage artifact changed before final gate')
        report['checks'] = {'all_six_fresh_release_builds': len(report['stages']) == 6
                            and all(row['passed'] for row in report['stages']),
                            'four_complete_qt_suites': len(report['tests']) == 4,
                            'two_loaded_standalones_closed_normally': len(report['standalone']) == 2,
                            'six_artifact_sha256_present': len(report['artifacts']) == 6,
                            'source_426_test_driver_main_helper_native_objects_unchanged': True,
                            'fresh_objects_and_binaries_unchanged_through_final': True}
        require(all(report['checks'].values()), 'Final project/reagent coverage gate incomplete')
        report['passed'] = True
    except BaseException as error:
        failure = error
        report.update(passed=False, exception_type=type(error).__name__, exception=str(error))
    finally:
        observed = binding_state(frozen, native)
        save(STAGE / 'binding_observed_final.json', observed)
        save(STAGE / 'source_after.json', observed['source_sha256'])
        save(STAGE / 'test_inputs_after.json', observed['test_inputs_sha256'])
        if observed != baseline:
            report['passed'] = False
            report['binding_changed_at_final'] = True
        report['finished_utc'] = now()
        save(STAGE / 'verification.json', report)
    if failure:
        raise failure
    require(report['passed'], 'Project/reagent final binding changed')
    print('PROJECTS_MODULES_FINAL_VERIFIED ' + str(STAGE / 'verification.json'), flush=True)


if __name__ == '__main__':
    main()
