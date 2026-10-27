"""Link QtTest driver to the exact freshly built production objects."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

work = Path(__file__).resolve().parent
build = work/'native/wsl_release'
source = work/'source/06_programs/sn100_host'
target = work/'production_test'
target.mkdir(exist_ok=True)
makefile=(build/'Makefile').read_text().replace('\\\n',' ')
match=re.search(r'^OBJECTS\s*=\s*(.+)$',makefile,re.M)
if not match: raise SystemExit('No production object inventory')
objects=[build/p for p in match.group(1).split()]
if set(objects)!=set((build/'.obj').glob('*.o')):
    raise SystemExit('Unexpected unlisted production objects')
objects=[p for p in objects if p.name!='main.o']
if not objects or any(not p.is_file() for p in objects): raise SystemExit('Missing production object')
hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in objects}
(target/'production_objects.json').write_text(json.dumps(hashes,indent=2)+'\n')
project='''QT += core gui widgets sql testlib printsupport xml qml charts svg network
CONFIG += c++14 console testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = instrument_page_test
MOC_DIR = .moc
OBJECTS_DIR = .test_objects
'''
project+='INCLUDEPATH += '+str(source)+'\n'
project+='SOURCES += '+str(source/'tests/instrument_page_test.cpp')+'\n'
project+='OBJECTS += '+' '.join(map(str,objects))+'\n'
(target/'production_test.pro').write_text(project)
with (work/'logs/production_test_build.log').open('wb') as log:
    subprocess.run(['/usr/lib/qt5/bin/qmake',str(target/'production_test.pro'),'-spec','linux-g++',
                    'CONFIG+=release','CONFIG-=debug'],cwd=target,stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run(['make','-j2'],cwd=target,stdout=log,stderr=subprocess.STDOUT,check=True)
assert hashes=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in objects}
print(json.dumps({'linked_production_objects':len(objects),'excluded':'main.o',
                  'unchanged_object_sha256':True,'test_driver':'QtTest Release; production objects unchanged'}))
