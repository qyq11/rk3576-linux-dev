import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

work=Path(__file__).resolve().parent
program=work/'source/06_programs/sn100_host'
native=work/'native'
initial=work/'native_initial'
initial.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def flags(path):
    body=path.read_text().replace('\\\n',' ')
    return {key:re.search(r'^'+key+r'\s*=\s*(.*)$',body,re.M).group(1)
            for key in ('CC','CXX','DEFINES','CFLAGS','CXXFLAGS','INCPATH')}
def sources():
    return {p.relative_to(program).as_posix():sha(p) for p in program.rglob('*')
            if p.is_file() and p.suffix not in ('.md','.pyc') and '__pycache__' not in p.parts}
before=sources()
assert before==json.loads((work/'final_sync.json').read_text())['final_sha256']
for name in ('sn100_shell','sn100_report_renderer'):
    shutil.copy2(native/'wsl_release'/name,initial/name)
old_objects={str(p):sha(p) for p in native.rglob('*.o')}
environment=os.environ.copy()
for key in ('QMAKESPEC','QMAKEPATH','QMAKEFEATURES','QT_PLUGIN_PATH','QML2_IMPORT_PATH'):
    environment.pop(key,None)
environment.update(PATH='/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin',
                   SN100_SHELL_WORK_DIR=str(native),SN100_JOBS='2')
gate={}
for build,project in (('wsl_renderer_release','history/renderer.pro'),('wsl_release','shell/shell.pro')):
    directory=native/build
    old=flags(directory/'Makefile')
    with (work/'logs'/('final_'+build+'_qmake.log')).open('wb') as log:
        subprocess.run(['/usr/lib/qt5/bin/qmake',str(program/project),'-spec','linux-g++',
            'CONFIG+=release','CONFIG-=debug','DESTDIR='+str(native/'wsl_report_stage')],
            cwd=directory,env=environment,stdout=log,stderr=subprocess.STDOUT,check=True)
    new=flags(directory/'Makefile')
    gate[build]={'before':old,'after':new,'identical':old==new,'clean_required':old!=new}
    if old!=new:
        with (work/'logs'/('final_'+build+'_clean.log')).open('wb') as log:
            subprocess.run(['make','clean'],cwd=directory,env=environment,stdout=log,stderr=subprocess.STDOUT,check=True)
(work/'native_final_flags.json').write_text(json.dumps(gate,indent=2)+'\n')
with (work/'logs/native_final_build.log').open('wb') as log:
    subprocess.run(['bash',str(program/'shell/wsl.sh'),'integrated-build'],
                   env=environment,stdout=log,stderr=subprocess.STDOUT,check=True)
assert before==sources()
new_objects={str(p):sha(p) for p in native.rglob('*.o')}
result={'exit_code':0,'source_count':len(before),'inputs_unchanged':True,
        'flags_gate':gate,'reused_object_count':sum(old_objects.get(p)==v for p,v in new_objects.items()),
        'changed_or_added_objects':[p for p,v in new_objects.items() if old_objects.get(p)!=v],
        'outputs':{name:sha(native/'wsl_release'/name) for name in ('sn100_shell','sn100_report_renderer')}}
(work/'native_final_result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('flags_gate','changed_or_added_objects')}))
