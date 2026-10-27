"""Build only the login QtTest driver against the exact frozen production objects."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--source-snapshot', type=Path, required=True)
parser.add_argument('--expected-sha', required=True)
args = parser.parse_args()
root = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
source = root / '06_programs/sn100_host'
work = root / 'temp/sn100_host/o02_login/shell_test'
target = work / 'production_test_driver'
assert args.build.is_absolute() and args.source_snapshot.is_absolute()
assert re.fullmatch('[0-9a-f]{64}', args.expected_sha)
assert not target.exists(), 'Refuse to overwrite a previous production-driver result'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
build = args.build.resolve(strict=True)
application = build / 'sn100_shell'
assert sha(application) == args.expected_sha
baseline = json.loads(args.source_snapshot.read_text())
target.mkdir(parents=True)
snapshot_command = ['python3', str(source / 'shell/package_arm64.py'), 'snapshot']
subprocess.run(snapshot_command + ['--output', str(target / 'source_before.json')], check=True)
assert json.loads((target / 'source_before.json').read_text()) == baseline

makefile = (build / 'Makefile').read_text().replace('\\\n', ' ')
match = re.search(r'^OBJECTS\s*=\s*(.+)$', makefile, re.M)
assert match
objects = [build / name for name in match.group(1).split()]
assert len(objects) == len(set(objects)) and all(path.is_file() for path in objects)
assert all(path.resolve().is_relative_to(build) for path in objects)
assert sum(path.name == 'main.o' for path in objects) == 1
for name in ('maskdlglogin.o', 'moc_maskdlglogin.o', 'account_store.o', 'frmmain.o'):
    assert any(path.name == name for path in objects), 'Missing production login dependency: ' + name
hashes = {str(path.relative_to(build)): sha(path) for path in objects}
linked = [path for path in objects if path.name != 'main.o']
test_sources = {name: sha(source / 'tests' / name) for name in
                ('password_login_shell_test.cpp', 'password_login_shell_test.pro')}
(target / 'production_objects_before.json').write_text(json.dumps(hashes, indent=2) + '\n')
project = '''QT += core gui widgets sql testlib printsupport xml qml charts svg network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = password_login_shell_test
DEFINES += NO_BARCODE HAVE_QT5
MOC_DIR = .moc
OBJECTS_DIR = .test_objects
'''
project += 'INCLUDEPATH += ' + str(source) + '\n'
project += 'SOURCES += ' + str(source / 'tests/password_login_shell_test.cpp') + '\n'
project += 'OBJECTS += ' + ' '.join(map(str, linked)) + '\n'
(target / 'production_driver.pro').write_text(project)
with (target / 'build.log').open('wb') as log:
    subprocess.run(['/usr/lib/qt5/bin/qmake', str(target / 'production_driver.pro'), '-spec', 'linux-g++',
                    'CONFIG+=release', 'CONFIG-=debug'], cwd=target, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(['make', '-j2'], cwd=target, stdout=log, stderr=subprocess.STDOUT, check=True)
after = {str(path.relative_to(build)): sha(path) for path in objects}
assert hashes == after and sha(application) == args.expected_sha
assert test_sources == {name: sha(source / 'tests' / name) for name in test_sources}
subprocess.run(snapshot_command + ['--output', str(target / 'source_after.json')], check=True)
assert json.loads((target / 'source_after.json').read_text()) == baseline
flags = {}
driver_makefile = (target / 'Makefile').read_text()
for name in ('CXXFLAGS', 'DEFINES', 'INCPATH', 'LFLAGS', 'LIBS'):
    value = re.search(r'^' + name + r'\s*=\s*(.+)$', driver_makefile, re.M)
    flags[name] = value.group(1) if value else ''
result = {'method': 'Compile QtTest driver only; link exact final production object inventory except main.o',
          'application': str(application), 'application_sha256': args.expected_sha,
          'production_snapshot': str(args.source_snapshot), 'source_snapshot_entries': len(baseline),
          'source_snapshot_before_after_equal': True, 'production_object_count': len(objects),
          'linked_object_count': len(linked), 'only_excluded_object': 'main.o',
          'production_objects_unchanged': True, 'production_sha256_by_object': after,
          'test_source_sha256': test_sources, 'driver_flags': flags,
          'driver_sha256': sha(target / 'password_login_shell_test'), 'build_exit_code': 0}
(target / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({key: result[key] for key in ('application_sha256', 'linked_object_count',
                                             'production_objects_unchanged', 'driver_sha256')}, indent=2))
