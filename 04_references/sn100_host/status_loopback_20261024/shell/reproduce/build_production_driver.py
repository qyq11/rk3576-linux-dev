"""Compile only the QtTest driver and link the exact frozen production object inventory."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

root = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
work = root / 'temp/sn100_host/o09_loopback/shell_test'
build = root / 'temp/sn100_host/o09_loopback/native/wsl_release'
target = work / 'production_test_driver'
target.mkdir(exist_ok=True)
application = build / 'sn100_shell'
expected = '0ac098cbb533dc2af7a250e1675a7d168e8e6f4176064e5651523dfe8bb2d7d8'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert sha(application) == expected
snapshot_command = ['python3', str(root / '06_programs/sn100_host/shell/package_arm64.py'), 'snapshot']
subprocess.run(snapshot_command + ['--output', str(target / 'source_before.json')], check=True)
baseline = json.loads((root / 'temp/sn100_host/o09_loopback/native/interim_after.json').read_text())
assert json.loads((target / 'source_before.json').read_text()) == baseline
assert len(baseline) == 348

makefile = (build / 'Makefile').read_text().replace('\\\n', ' ')
match = re.search(r'^OBJECTS\s*=\s*(.+)$', makefile, re.M)
assert match, 'No exact production OBJECTS inventory'
objects = [build / name for name in match.group(1).split()]
assert len(objects) == len(set(objects)) and all(path.is_file() for path in objects)
assert all(path.resolve().is_relative_to(build.resolve()) for path in objects)
assert sum(path.name == 'main.o' for path in objects) == 1
assert any(path.name == 'read_only_messages.o' for path in objects)
hashes = {str(path.relative_to(build)): sha(path) for path in objects}
linked = [path for path in objects if path.name != 'main.o']
unused = sorted(str(path.relative_to(build)) for path in (build / '.obj').glob('*.o') if path not in objects)
(target / 'production_objects_before.json').write_text(json.dumps(hashes, indent=2) + '\n')

source = root / '06_programs/sn100_host'
project = '''QT += core gui widgets sql testlib printsupport xml qml charts svg network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = status_loopback_shell_test
DEFINES += NO_BARCODE HAVE_QT5
MOC_DIR = .moc
OBJECTS_DIR = .test_objects
'''
project += 'INCLUDEPATH += ' + str(source) + '\n'
project += 'SOURCES += ' + str(source / 'tests/status_loopback_shell_test.cpp') + '\n'
project += 'OBJECTS += ' + ' '.join(map(str, linked)) + '\n'
(target / 'production_driver.pro').write_text(project)
with (target / 'build.log').open('wb') as log:
    subprocess.run(['/usr/lib/qt5/bin/qmake', str(target / 'production_driver.pro'), '-spec', 'linux-g++',
                    'CONFIG+=release', 'CONFIG-=debug'], cwd=target, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(['make', '-j2'], cwd=target, stdout=log, stderr=subprocess.STDOUT, check=True)
after = {str(path.relative_to(build)): sha(path) for path in objects}
assert hashes == after and sha(application) == expected
subprocess.run(snapshot_command + ['--output', str(target / 'source_after.json')], check=True)
assert json.loads((target / 'source_after.json').read_text()) == baseline
driver_makefile = (target / 'Makefile').read_text()
flags = {}
for name in ('CXXFLAGS', 'DEFINES', 'INCPATH', 'LFLAGS', 'LIBS'):
    value = re.search(r'^' + name + r'\s*=\s*(.+)$', driver_makefile, re.M)
    flags[name] = value.group(1) if value else ''
result = {'method': 'QtTest driver linked to exact production objects; not a full clean QtTest business rebuild',
          'application': str(application), 'application_sha256': expected,
          'source_snapshot_entries': len(baseline), 'source_snapshot_matches_interim_after': True,
          'production_object_count': len(objects), 'linked_object_count': len(linked),
          'only_excluded_object': 'main.o', 'unused_object_files_not_linked': unused,
          'production_objects_unchanged': True, 'production_sha256_by_object': after,
          'driver_flags': flags, 'driver_sha256': sha(target / 'status_loopback_shell_test'),
          'test_source_sha256': sha(source / 'tests/status_loopback_shell_test.cpp'),
          'formal_test_pro_sha256': sha(source / 'tests/status_loopback_shell_test.pro'),
          'clean_build': {'state': 'Interrupted by root instruction; no test execution or pass claimed',
                          'make_pid':1684,'signal':'SIGINT','verified_cwd':str(work / 'wsl_build'),
                          'log':str(work / 'build.log')},
          'driver_only_build_exit':0}
(target / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({key:result[key] for key in ('linked_object_count','source_snapshot_matches_interim_after',
                                            'production_objects_unchanged','driver_sha256')}))
