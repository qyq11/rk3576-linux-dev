#!/usr/bin/env python3
"""Fresh formal alarm-page build, then original eight cases offscreen/xcb serially."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import sqlite3
import sys
import time
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
FORMAL = ROOT / '06_programs/sn100_host'
CASES = [
    'originalGeometryRecentTwenty',
    'calendarDraftThenFullIntervalLastBatchBeforeCompletion',
    'finalBatchCapacityCancellationNeverCompletesPartial',
    'utf8TextCapacityClearsInsteadOfPartialSuccess',
    'cancelButtonAfterRealSqlTerminalReleasesGuiAppend',
    'calendarEscapeKeepsResultAndLegalEmptyIsComplete',
    'missingExistingSourceDoesNotCreateOrFakeEmpty',
    'closeQueuedBeforeWorkerReturnsDropsLateRows']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def bind_sources():
    document = json.loads((HERE / 'source_manifest.json').read_text())
    require(len(document['files']) == 80, 'Reviewed source closure count changed')
    actual = {}
    for name, row in document['files'].items():
        path = ROOT / name
        require(path.is_file() and path.stat().st_size == row['bytes'] and sha(path) == row['sha256'],
                'Source/UI/resource changed: ' + name)
        actual[name] = row['sha256']
    for item in document['glob_inventories']:
        pattern = ROOT / item['pattern']
        found = sorted(path.resolve(strict=True).relative_to(ROOT).as_posix() for path in pattern.parent.glob(pattern.name) if path.is_file())
        require(found == sorted(item['files']), 'qmake wildcard input set changed: ' + item['pattern'])
    return actual, document


def bind_tools():
    document = json.loads((HERE / 'tools_manifest.json').read_text())
    actual = {}
    for name, row in document['tools'].items():
        path = Path(row['path'])
        require(path.is_file() and sha(path) == row['sha256'], 'Installed tool/resource changed: ' + name)
        actual[name] = {'path': row['path'], 'sha256': row['sha256']}
    for row in document['optional']:
        path = Path(row['path'])
        require(path.is_file() == row['present'], 'Optional resource presence changed: ' + str(path))
        if row['present']:
            require(sha(path) == row['sha256'], 'Optional resource changed: ' + str(path))
        actual[str(path)] = {'present': row['present'], 'sha256': row['sha256']}
    return actual, document


def activity():
    conflicts = []
    names = re.compile(r'^(make|gmake|qmake|moc|uic|rcc|cc1plus|collect2|ld|ld.bfd|g\+\+|.*-g\+\+(-[0-9]+)?)$')
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            pid = int(proc.name)
            if pid == os.getpid():
                continue
            executable = (proc / 'exe').resolve(strict=True)
            name = executable.name
            if names.fullmatch(name) or executable.is_relative_to(ROOT / 'temp'):
                conflicts.append({'pid': pid, 'pgid': os.getpgid(pid), 'executable': str(executable)})
        except (OSError, ValueError):
            continue
    return conflicts


def parse_qt(path):
    root = ET.parse(path).getroot()
    require(root.tag == 'TestCase' and root.attrib.get('name') == 'AlarmHistoryPageTest', 'Unexpected Qt XML')
    records, messages = [], []
    for function in root.findall('TestFunction'):
        name = function.attrib['name']
        for incident in function.findall('Incident'):
            records.append({'function': name, 'type': incident.attrib['type'],
                            'data': incident.findtext('DataTag', default=''),
                            'description': incident.findtext('Description', default='')})
        for message in function.findall('Message'):
            messages.append({'function': name, 'type': message.attrib['type'],
                             'text': message.findtext('Description', default='')})
    counts = {}
    for row in records:
        counts[row['function']] = counts.get(row['function'], 0) + 1
    expected = {name: 1 for name in CASES + ['initTestCase', 'cleanupTestCase']}
    shutdown = []
    pattern = re.compile(r'ALARM_HISTORY_PAGE_STORE_CLOSED\s+(true|false)\s+WORKER_JOINED\s+(true|false)\s+'
                         r'SQL_ROW_IMAGE_UNCHANGED\s+(true|false)\s+NORMAL_PAGE_CLOSE\s+(true|false)')
    for message in messages:
        match = pattern.search(message['text'])
        if match:
            shutdown.append({'function': message['function'], 'store_closed': match[1] == 'true',
                             'worker_joined': match[2] == 'true', 'SQL_unchanged_flag': match[3] == 'true',
                             'normal_page_close': match[4] == 'true'})
    normal = len(shutdown) == 8 and {row['function'] for row in shutdown} == set(CASES) and all(
        row['store_closed'] and row['worker_joined'] and row['normal_page_close'] and row['SQL_unchanged_flag']
        for row in shutdown)
    return {'records': records, 'messages': messages, 'actual_counts': counts, 'expected_counts': expected,
            'counts_match': counts == expected, 'business_cases': sum(row['function'] in CASES for row in records),
            'Qt_total': len(records), 'passed': sum(row['type'] == 'pass' for row in records),
            'failed': sum(row['type'] not in ('pass', 'skip') for row in records),
            'skipped': sum(row['type'] == 'skip' for row in records), 'shutdown_receipts': shutdown,
            'normal_shutdown_and_SQL_flags': normal,
            'SQL_flag_note': 'Seven seeded fixtures compare complete seven-field QVariant row images; missing-source fixture flag is vacuous and is not an eighth SQL comparison.'}


def audit_anonymous(root):
    receipts = []
    for file in sorted(root.rglob('*.db')):
        resolved = file.resolve(strict=True)
        require(resolved.is_relative_to(root.resolve()) and not file.is_symlink() and file.name == 'anonymous_history.db',
                'Unexpected anonymous database artifact')
        before = sha(file)
        with sqlite3.connect(file.as_uri() + '?mode=ro', uri=True, timeout=0.25) as connection:
            connection.execute('PRAGMA query_only=ON')
            rows = connection.execute('SELECT id,error_code,error_describe,measure,date_time,corrective_action,use_user '
                                      'FROM abnormal_log ORDER BY id').fetchall()
        connection.close()
        require(len(rows) == 26 and sha(file) == before, 'Anonymous post-run metadata differs or wrote database')
        row_bytes = json.dumps(rows, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        receipts.append({'path': str(file), 'file_sha256_before_read': before, 'file_sha256_after_read': sha(file),
                         'row_count': len(rows), 'post_row_image_sha256_json': hashlib.sha256(row_bytes).hexdigest(),
                         'post_rows': rows})
    require(len(receipts) == 7, 'Expected exactly seven retained seeded fixture databases')
    require(len({row['post_row_image_sha256_json'] for row in receipts}) == 1, 'Retained fixture row images differ')
    return {'metadata_read_only': True, 'receipts': receipts,
            'equality_basis': 'Before-to-after equality is asserted/logged by original Qt Fixture.stop. These are additional post-run JSON encodings, not the Qt QDataStream encoding.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--slot-id', required=True)
    parser.add_argument('--inputs-sha256', required=True)
    args = parser.parse_args()
    require(args.slot_id == 'alarm_history_formal_page_20261026_01' and os.geteuid() != 0, 'Wrong slot/user')
    require(sha(HERE / 'controller_inputs.json') == args.inputs_sha256, 'Reviewed controller input manifest changed')
    inputs = json.loads((HERE / 'controller_inputs.json').read_text())
    for name, digest in inputs['files'].items():
        require(sha(HERE / name) == digest, 'Controller/manifest changed: ' + name)
    spec = importlib.util.spec_from_file_location('page_bounded_process', HERE / 'bounded_process.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    out = HERE / 'run_01'
    require(not out.exists(), 'Fresh run_01 required; preserve first failure, no rerun')
    out.mkdir()
    build = out / 'build'
    scratch = out / 'tmp'
    for directory in (build, scratch):
        directory.mkdir()
    deadline = time.monotonic() + 360
    environment = os.environ.copy()
    environment.update(TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch), SQLITE_TMPDIR=str(scratch))
    save(out / 'invocation.json', {'argv': sys.argv, 'parent_pid': os.getpid(), 'slot_id': args.slot_id,
                                  'deadline_seconds': 360, 'make_jobs': 2, 'root_statement': 'No other compile/GUI after internal A/B release'})
    save(out / 'environment_base.json', {'overrides': {key: environment[key] for key in ('TMPDIR','TMP','TEMP','SQLITE_TMPDIR')},
         'inherited': {key: environment[key] for key in ('DISPLAY','WAYLAND_DISPLAY','XDG_RUNTIME_DIR','QT_PLUGIN_PATH','LD_LIBRARY_PATH','LANG') if key in environment},
         'XAUTHORITY_present_only': bool(environment.get('XAUTHORITY')), 'whole_environment_dumped': False})
    source_before = tool_before = py = None
    summary = {'schema': 'sn100-formal-alarm-page-run-v1', 'passed': False, 'QPA': {}, 'controller_error': None}
    try:
        source_before, source_doc = bind_sources()
        tool_before, tool_doc = bind_tools()
        save(out / 'source_before.json', source_before)
        save(out / 'tools_before.json', tool_before)
        current = activity()
        save(out / 'activity_before.json', {'conflicting_project_GUI_or_build_processes': current})
        require(not current, 'Other active build/project executable; do not consume GUI slot')
        tools = {name: row['path'] for name, row in tool_doc['tools'].items()}
        py = {'executable': str(Path(sys.executable).resolve()), 'sha256': sha(Path(sys.executable).resolve()),
              'version': sys.version, 'sqlite3_version': sqlite3.sqlite_version}
        import _sqlite3
        py.update(sqlite3_extension=_sqlite3.__file__, sqlite3_extension_sha256=sha(_sqlite3.__file__))
        save(out / 'python_runtime.json', py)
        def run(name, argv, timeout, env=None):
            return helper.run(name, argv, out, build, environment if env is None else env, timeout, deadline)
        query = run('qt_query', [tools['qmake'], '-query'], 5)
        require(query['exit_code'] == 0, 'Qt query failed')
        qt = dict(line.split(':',1) for line in (out/'qt_query.stdout.log').read_text().splitlines() if ':' in line)
        require(qt['QT_VERSION'] == tool_doc['Qt_required'], 'Qt version differs')
        for label, pattern, expected_font in [('regular','Noto Sans CJK SC',tools['NotoRegular']),
                                               ('bold','Noto Sans CJK SC:style=Bold',tools['NotoBold'])]:
            record = run('font_'+label, [tools['fc_match'],'-f','%{file}',pattern], 5)
            require(record['exit_code'] == 0 and Path((out/('font_'+label+'.stdout.log')).read_text().strip()).resolve() == Path(expected_font).resolve(),
                    'Actual Noto font selection differs')
        compiler_version = run('compiler_version', [tools['compiler'],'--version'], 5)
        require(compiler_version['exit_code'] == 0, 'Compiler version failed')
        qmake = run('qmake', [tools['qmake'],'-o',str(build/'Makefile'),str(FORMAL/'tests/alarm_history_page_test.pro'),
                             'CONFIG+=release','CONFIG-=debug','SN100_FORMAL_ROOT='+str(FORMAL)], 30)
        require(qmake['exit_code'] == 0, 'qmake failed; stop without a test')
        text = (build/'Makefile').read_text()
        logical = re.sub(r'\\\n',' ',text)
        assignments = {}
        for variable, tool_name in [('CXX','compiler'),('MOC','moc'),('UIC','uic'),('RCC','rcc')]:
            match = re.search(r'^'+variable+r'\s*=\s*(.+)$',logical,re.MULTILINE)
            require(match is not None, 'Missing actual Makefile assignment '+variable)
            command = shlex.split(match[1])
            require(len(command) == 1, 'Unexpected tool wrapper')
            actual_tool = helper.executable(command[0])
            require(actual_tool['sha256'] == tool_doc['tools'][tool_name]['sha256'], 'Makefile tool differs: '+variable)
            assignments[variable] = actual_tool
        match = re.search(r'^SOURCES\s*=\s*(.+)$',logical,re.MULTILINE)
        require(match is not None, 'Makefile SOURCES missing')
        actual_sources = []
        generated = []
        for name in shlex.split(match[1]):
            path = (build/name).resolve()
            if path.is_relative_to(build): generated.append(str(path)); continue
            require(path.is_relative_to(ROOT), 'Compiled source outside this repository')
            actual_sources.append(path.relative_to(ROOT).as_posix())
        require(sorted(actual_sources) == sorted(source_doc['qmake_categories']['SOURCES']), 'Actual qmake18-source roster differs')
        save(out/'makefile.json', {'sha256':sha(build/'Makefile'),'actual_source_inputs':actual_sources,
                                  'generated_sources':generated,'tools':assignments})
        make = run('make',[tools['make'],'-C',str(build),'-j2'],120)
        require(make['exit_code'] == 0, 'Fresh compile failed; no old binary may run')
        binary = build/'alarm_history_page_test'
        require(binary.is_file(), 'Fresh executable missing')
        save(out/'binary.json', {'path':str(binary),'sha256':sha(binary)})
        ldd = run('ldd',[tools['ldd'],str(binary)],5)
        require(ldd['exit_code'] == 0, 'ldd failed')
        deps = (out/'ldd.stdout.log').read_text()
        require('not found' not in deps and 'libQt5Widgets' in deps and 'libQt5Sql' in deps, 'Wrong/missing GUI-SQL dependencies')
        save(out/'runtime_dependencies.json', {path:sha(Path(path).resolve()) for path in re.findall(r'=> (/\S+)',deps)})
        for qpa in ('offscreen','xcb'):
            root = out/'runtime'/qpa
            root.mkdir(parents=True)
            logs = out/qpa
            logs.mkdir()
            env = environment.copy()
            env.update(QT_QPA_PLATFORM=qpa,SN100_ALARM_PAGE_TEST_ROOT=str(root))
            if qpa == 'xcb': require(bool(env.get('DISPLAY')), 'XCB needs the existing DISPLAY; no Xvfb/fallback is created')
            save(logs/'environment.json', {'overrides':{'QT_QPA_PLATFORM':qpa,'SN100_ALARM_PAGE_TEST_ROOT':str(root)},
                                          'DISPLAY':env.get('DISPLAY'),'runtime_root_initial_entries':list(root.iterdir())})
            result = run('test_'+qpa,[str(binary),'-o',str(logs/'qt.txt')+',txt',
                         '-o',str(logs/'qt_xunit.xml')+',xunitxml','-o',str(logs/'qt.xml')+',xml'],90,env)
            actual = parse_qt(logs/'qt.xml')
            save(logs/'qt_results.json',actual)
            summary['QPA'][qpa] = {'actual_exit':result['exit_code'],'result':actual}
            require(result['exit_code'] == 0 and actual['counts_match'] and actual['business_cases'] == 8 and
                    actual['passed'] == 10 and actual['failed'] == 0 and actual['skipped'] == 0 and
                    actual['normal_shutdown_and_SQL_flags'], 'Original GUI/SQL cases or teardown failed: '+qpa)
            save(logs/'anonymous_SQL_post_images.json',audit_anonymous(root))
            require(sha(binary) == json.loads((out/'binary.json').read_text())['sha256'], 'Executable changed between backends')
        summary['passed'] = set(summary['QPA']) == {'offscreen','xcb'}
    except BaseException as error:
        summary.update(passed=False,controller_error=type(error).__name__,message=str(error))
    finally:
        try:
            source_after,_ = bind_sources()
            tool_after,_ = bind_tools()
            save(out/'source_after.json',source_after)
            save(out/'tools_after.json',tool_after)
            summary['source_unchanged'] = source_after == source_before
            summary['tools_unchanged'] = tool_after == tool_before
            summary['controller_inputs_unchanged'] = sha(HERE/'controller_inputs.json') == args.inputs_sha256 and all(
                sha(HERE/name) == digest for name,digest in inputs['files'].items())
            summary['Python_runtime_unchanged'] = py is not None and sha(py['executable']) == py['sha256'] and \
                sha(py['sqlite3_extension']) == py['sqlite3_extension_sha256']
            save(out/'python_runtime_after.json', {'unchanged':summary['Python_runtime_unchanged']})
        except BaseException as error:
            summary.update(passed=False,source_unchanged=False,postcheck_error=str(error))
        processes = []
        for file in sorted(out.glob('*.exit.json')):
            row = json.loads(file.read_text())
            processes.append({'step':file.name,'pid':row['pid'],'exit_code':row['exit_code'],
                              'group_gone':row['pid'] is not None and not helper.exists(row['pid'],group=True)})
        summary['processes'] = processes
        summary['all_owned_groups_gone'] = bool(processes) and all(row['group_gone'] for row in processes)
        summary['passed'] = summary['passed'] and summary.get('source_unchanged',False) and summary.get('tools_unchanged',False) and \
            summary.get('controller_inputs_unchanged',False) and summary.get('Python_runtime_unchanged',False) and summary['all_owned_groups_gone']
        save(out/'verification.json',summary)
        print(json.dumps({'passed':summary['passed'],'error':summary['controller_error'],'message':summary.get('message'),
                          'verification':str(out/'verification.json')}))
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
