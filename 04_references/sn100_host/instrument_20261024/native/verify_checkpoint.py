"""Read-only verification of the frozen instrument/O09 checkpoint; archive evidence only.

No build, GUI, socket, production database, account secret or mutable formal tree is used.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import xml.etree.ElementTree as ET

repo = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
work = repo / 'temp/sn100_host/instrument_20261024'
source = work / 'source/06_programs/sn100_host'
native_out = repo / '04_references/sn100_host/instrument_20261024/native'
gui_out = native_out.parent / 'final_gui'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def copy_evidence(origin, target):
    origin, target = Path(origin), Path(target)
    if target.exists():
        if sha(origin) == sha(target):
            return
        raise RuntimeError('Different evidence destination already exists: ' + str(target))
    target.parent.mkdir(exist_ok=True, parents=True)
    shutil.copy2(origin, target)
    if sha(origin) != sha(target):
        raise RuntimeError('Evidence copy SHA mismatch: ' + str(origin))


def manifest(directory):
    records = [{'path': p.relative_to(directory).as_posix(), 'size': p.stat().st_size, 'sha256': sha(p)}
               for p in sorted(directory.rglob('*')) if p.is_file() and p.name != 'manifest.json']
    write_json(directory / 'manifest.json', {'schema': 'sn100-evidence-manifest-v1', 'files': records})
    for record in records:
        assert sha(directory / record['path']) == record['sha256']
    return len(records)


sync = read_json(work / 'final_sync.json')
expected = sync['final_sha256']
actual = {p.relative_to(source).as_posix(): sha(p) for p in source.rglob('*')
          if p.is_file() and p.suffix not in ('.md', '.pyc') and '__pycache__' not in p.parts}
assert len(actual) == len(expected) == 468 and actual == expected
checkpoint = read_json(work / 'checkpoint_inputs.json')
o09_checkpoint = read_json(repo / 'temp/sn100_host/o09_loopback/native/interim_after.json')
assert len(checkpoint) == len(o09_checkpoint) == 348 and checkpoint == o09_checkpoint
assert all(actual.get(name) == value for name, value in checkpoint.items())
assert read_json(work / 'checkpoint_verification.json')['differences'] == []

native_audit = read_json(work / 'native_final_audit.json')
native_raw = read_json(work / 'native_final_result.json')
flags = read_json(work / 'native_final_flags.json')
assert native_audit['exit_code'] == native_raw['exit_code'] == 0
assert native_audit['inputs_unchanged'] and native_raw['inputs_unchanged']
assert all(value['identical'] and value['before'] == value['after'] and not value['clean_required']
           for value in flags.values())

build_log = (work / 'logs/native_final_build.log').read_text()
compiled = re.findall(r'^g\+\+ -c .*? -o (\S+\.o)(?:\s|$)', build_log, re.M)
assert compiled == native_audit['compiled_objects'] and len(compiled) == 17
objects = {str(p): sha(p) for p in (work / 'native').rglob('*.o')}
assert len(objects) == 168 and objects == native_audit['objects_sha256']
assert len(objects) - len(compiled) == native_audit['objects_not_recompiled'] == 151
assert native_raw['reused_object_count'] == native_audit['same_sha_as_initial_count'] == 155
assert len(native_raw['changed_or_added_objects']) == native_audit['sha_changed_or_added_count'] == 13

# Independently cover every changed source/header dependency in each current
# Makefile. The 0.19-second Makefile clock warning is retained, not erased.
changed = set(sync['changed_or_added'])
initial_linked = read_json(native_out.parent / 'initial_gui/production_objects.json')
initial_shell_objects = {Path(name).name for name in initial_linked} | {'main.o'}
dependency_checks = {}
for folder in ('wsl_release', 'wsl_renderer_release'):
    build = work / 'native' / folder
    body = re.sub(r'\\\r?\n\s*', ' ', (build / 'Makefile').read_text())
    required = {}
    new_shell_objects = []
    generated_rules = {}
    for match in re.finditer(r'^(\S+):[ \t]*([^\r\n]*)', body, re.M):
        if match.group(1).startswith(('.moc/', '.ui/', '.rcc/')):
            generated_rules[(build / match.group(1)).resolve()] = match.group(2).split()

    def affecting_sources(tokens, visited=None):
        visited = set() if visited is None else visited
        result = set()
        for token in tokens:
            path = (build / token).resolve()
            if path in visited:
                continue
            visited.add(path)
            try:
                relative = path.relative_to(source).as_posix()
            except ValueError:
                relative = None
            if relative in changed:
                result.add(relative)
            elif path in generated_rules:
                result.update(affecting_sources(generated_rules[path], visited))
        return result

    rule_count = 0
    for match in re.finditer(r'^(\S+\.o):\s*([^\r\n]+)', body, re.M):
        rule_count += 1
        affecting = affecting_sources(match.group(2).split())
        if affecting:
            required[match.group(1)] = sorted(set(affecting))
        if folder == 'wsl_release' and Path(match.group(1)).name not in initial_shell_objects:
            new_shell_objects.append(match.group(1))
    assert rule_count in (137, 31)
    all_required = set(required) | set(new_shell_objects)
    missing = sorted(all_required - set(compiled))
    assert not missing, (folder, missing)
    if folder == 'wsl_release':
        assert all_required == set(compiled)
    # The qmake flags in the file are independently compared with the recorded
    # gate; a source sync may not silently alter compiler or include settings.
    current_flags = {key: re.search(r'^' + key + r'\s*=\s*(.*)$', body, re.M).group(1)
                     for key in ('CC', 'CXX', 'DEFINES', 'CFLAGS', 'CXXFLAGS', 'INCPATH')}
    assert current_flags == flags[folder]['after']
    dependency_checks[folder] = {'object_rules': rule_count, 'changed_dependencies_requiring_compile': required,
                                'new_shell_objects_requiring_compile': sorted(new_shell_objects),
                                'total_required_compilations': len(all_required),
                                'missing_compilations': missing}

linked = read_json(work / 'production_test/production_objects.json')
main_objects = {name: value for name, value in objects.items()
                if '/native/wsl_release/.obj/' in name and not name.endswith('/main.o')}
assert len(linked) == 136 and linked == main_objects
test_build = work / 'production_test'
test_makefile = re.sub(r'\\\r?\n\s*', ' ', (test_build / 'Makefile').read_text())
test_objects = re.search(r'^OBJECTS\s*=\s*(.+)$', test_makefile, re.M).group(1).split()
test_object_paths = {(test_build / item).resolve() for item in test_objects}
test_driver_object = test_build / '.test_objects/instrument_page_test.o'
assert test_driver_object in test_object_paths
assert test_object_paths - {test_driver_object} == {Path(path) for path in linked}
test_sources = {(test_build / token).resolve() for token in
                re.search(r'^SOURCES\s*=\s*(.+)$', test_makefile, re.M).group(1).split()}
assert source / 'tests/instrument_page_test.cpp' in test_sources
test_build_log = (work / 'logs/production_test_build.log').read_text()
test_compile_sources = {(test_build / match.group(1)).resolve() for match in
                        re.finditer(r'^g\+\+ -c .*? -o \S+\.o\s+(\S+\.cpp)\s*$', test_build_log, re.M)}
assert source / 'tests/instrument_page_test.cpp' in test_compile_sources
same_initial_main = {name: value for name, value in linked.items() if initial_linked.get(name) == value}
rebuilt_same = [name for name in same_initial_main if '.obj/' + Path(name).name in compiled]
assert len(rebuilt_same) == 4

outputs = {}
for name, expected_sha in native_audit['outputs'].items():
    executable = work / 'native/wsl_release' / name
    header = executable.read_bytes()[:64]
    assert header[:4] == b'\x7fELF' and header[4] == 2 and header[5] == 1
    assert struct.unpack_from('<H', header, 18)[0] == 62
    assert sha(executable) == expected_sha == native_raw['outputs'][name]
    outputs[name] = {'sha256': expected_sha, 'size': executable.stat().st_size,
                     'format': 'ELF64 little-endian x86-64'}

for name in ('final_sync.json', 'checkpoint_inputs.json', 'checkpoint_verification.json',
             'native_final_audit.json', 'native_final_flags.json', 'native_final_result.json',
             'build_native_final.py', 'audit_native_final.py', 'check_checkpoint.py', 'build_production_test.py'):
    copy_evidence(work / name, native_out / name)
copy_evidence(work / 'logs/native_final_build.log', native_out / 'native_final_build.log')
copy_evidence(work / 'production_test/production_objects.json', native_out / 'gui_linked_production_objects.json')
copy_evidence(work / 'production_test/production_test.pro', native_out / 'gui_production_test.pro')
copy_evidence(work / 'logs/production_test_build.log', native_out / 'gui_production_test_build.log')

interface_names = ('instrument/instrument_page.h', 'instrument/instrument_page.cpp',
                   'instrument/maintenance_store.h', 'instrument/maintenance_store.cpp',
                   'instrument/frmdriverset.ui', 'shell/instrument_shell.cpp', 'shell/frmmain.h',
                   'protocol/status_loopback.h', 'protocol/status_loopback.cpp',
                   'tests/instrument_page_test.cpp')
interface_hashes = {name: actual[name] for name in interface_names}
write_json(native_out / 'interface_checkpoint_sha256.json', interface_hashes)

qpa_results = {}
test_function_names = None
for platform in ('offscreen', 'xcb'):
    qa = work / 'qa/final'
    text = (qa / (platform + '.txt')).read_text()
    totals = re.search(r'Totals:\s+(\d+) passed,\s+(\d+) failed,\s+(\d+) skipped,\s+(\d+) blacklisted,\s+(\d+)ms', text)
    assert totals and tuple(map(int, totals.groups()[:4])) == (10, 0, 0, 0)
    assert int((qa / (platform + '.exit')).read_text()) == 0
    document = ET.parse(qa / (platform + '.xml')).getroot()
    functions = document.findall('TestFunction')
    incidents = document.findall('.//Incident')
    assert len(functions) == len(incidents) == 10
    assert all(incident.attrib.get('type') == 'pass' for incident in incidents)
    names = [function.attrib['name'] for function in functions]
    assert 'readOnlySourceReplacementKeepsPermissionsAndExpires' in names
    if test_function_names is not None:
        assert names == test_function_names
    test_function_names = names
    for extension in ('txt', 'xml', 'exit', 'console'):
        copy_evidence(qa / (platform + '.' + extension), gui_out / (platform + '.' + extension))
    images = []
    for png in sorted((qa / platform).glob('*.png')):
        header = png.read_bytes()[:24]
        assert header[:8] == b'\x89PNG\r\n\x1a\n'
        width, height = struct.unpack_from('>II', header, 16)
        assert (width, height) == (1600, 900)
        copy_evidence(png, gui_out / platform / png.name)
        images.append({'name': png.name, 'width': width, 'height': height, 'sha256': sha(png)})
    assert len(images) == 10
    metrics = read_json(qa / platform / 'navigation_metrics.json')
    assert metrics['operations'] == 7 and 'excludes paint' in metrics['scope']
    copy_evidence(qa / platform / 'navigation_metrics.json', gui_out / platform / 'navigation_metrics.json')
    qpa_results[platform] = {'pass': 10, 'fail': 0, 'skip': 0, 'blacklisted': 0, 'exit_code': 0,
                             'duration_ms': int(totals.group(5)), 'screenshots': images,
                             'navigation_operations': 7,
                             'maximum_synchronous_click_ms': metrics['maximum_sync_click_ns'] / 1e6,
                             'navigation_scope': metrics['scope']}

driver = work / 'production_test/instrument_page_test'
gui_verification = {
    'schema': 'sn100-instrument-final-gui-verification-v1',
    'verified_utc': datetime.now(timezone.utc).isoformat(),
    'source_scope': 'Private frozen source matches final_sync.final_sha256; current formal tree has continued work and was not compared.',
    'checkpoint_source_count': 468, 'o09_shared_package_input_count': 348,
    'driver_sha256': sha(driver), 'driver_source_sha256': actual['tests/instrument_page_test.cpp'],
    'driver_object_sha256': sha(test_driver_object),
    'actual_test_makefile_links_exactly_checkpoint_objects_plus_test_driver': True,
    'linked_production_object_count': 136, 'linked_object_sha256_matches_native_checkpoint': True,
    'tests': test_function_names, 'qpa': qpa_results,
    'visual_scope': 'Original .ui byte identity and source-derived styles; these WSL screenshots do not verify the active original Windows theme or RK display/input.',
    'manual_visual_inspection': {'xcb/page_1.png': 'Unknown hardware state and temperatures visible; 1600x900 host scrolls the unchanged original canvas.',
                                 'xcb/maintenance_saved.png': 'Six manual maintenance marks visible; washing/shutdown rows have no fabricated completion mark.'},
    'not_verified': ['Windows GUI behavior/active theme', 'RK display/input/resources', 'real instrument actions', 'physical input/paint latency', 'long-term/shared-control budgets'],
    'runtime_data_archived': False,
}
write_json(gui_out / 'verification.json', gui_verification)

native_verification = {
    'schema': 'sn100-instrument-native-checkpoint-verification-v1',
    'verified_utc': datetime.now(timezone.utc).isoformat(),
    'source_scope': gui_verification['source_scope'],
    'source_sha256_matches_final_sync': True, 'source_count': len(actual),
    'checkpoint_matches_validated_o09_inputs': True, 'checkpoint_input_count': len(checkpoint),
    'interface_source_hashes_verified': True, 'flags_gate_recomputed_from_makefiles': True,
    'current_object_count': len(objects), 'current_object_sha256_verified': True,
    'actual_compiler_invocations': len(compiled), 'objects_not_recompiled': len(objects) - len(compiled),
    'same_sha_as_initial_reported_count': 155,
    'same_sha_as_initial_interpretation': '155 is equal-SHA count, not make reuse. Four actually recompiled main objects remained byte-identical.',
    'initial_gui_main_object_baseline_count': len(initial_linked),
    'same_sha_from_archived_initial_main_baseline': len(same_initial_main),
    'recompiled_but_same_sha_from_archived_initial_main_baseline': rebuilt_same,
    'changed_dependency_object_coverage': dependency_checks,
    'clock_warning_preserved': "Makefile modification time 0.19 s in the future; Clock skew detected. Actual changed-source/header dependency coverage independently passed without another build.",
    'main_helper_outputs': outputs,
    'gui_linked_object_count': len(linked), 'gui_linked_object_sha256_verified': True,
    'gui_actual_makefile_link_set_verified': True,
    'no_build_or_gui_started_by_this_audit': True, 'runtime_data_archived': False,
}
write_json(native_out / 'verification.json', native_verification)
native_files = manifest(native_out)
gui_files = manifest(gui_out)
print(json.dumps({'source_count': len(actual), 'checkpoint_inputs': len(checkpoint),
                  'current_native_objects_verified': len(objects), 'gui_linked_objects_verified': len(linked),
                  'compiler_invocations': len(compiled), 'not_recompiled': 151, 'rebuilt_but_same_sha': len(rebuilt_same),
                  'qpa': {key: {'pass': value['pass'], 'fail': value['fail'], 'max_sync_click_ms': value['maximum_synchronous_click_ms']} for key, value in qpa_results.items()},
                  'native_manifest_files': native_files, 'gui_manifest_files': gui_files}, ensure_ascii=False))
