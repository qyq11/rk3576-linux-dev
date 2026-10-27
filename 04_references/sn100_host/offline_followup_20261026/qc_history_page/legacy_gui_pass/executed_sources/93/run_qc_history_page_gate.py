import argparse, hashlib, json, os, re, runpy, shlex
from pathlib import Path
import xml.etree.ElementTree as ET

WORK=Path(__file__).resolve().parent
ROOT=WORK.parents[2]
PROGRAM=ROOT/'06_programs/sn100_host'
HELPER=WORK/'qc_history_slice/run_slice.py'
execute=runpy.run_path(str(HELPER),run_name='qc_history_owned_process_helpers')['execute']
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):Path(path).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def with_environment(name,argv,output,cwd,timeout,environment):
    previous=dict(os.environ)
    try:
        os.environ.clear();os.environ.update(environment)
        return execute(name,argv,output,cwd,timeout)
    finally:
        os.environ.clear();os.environ.update(previous)
def inputs(build,project):
    files={project,HELPER,Path(__file__).resolve()}
    text=(build/'Makefile').read_text().replace('\\\n',' ')
    for candidate in re.findall(re.escape(str(PROGRAM))+r'/[^\s\\:$()]+',text):
        path=Path(candidate.rstrip(',;'))
        if path.is_file() and path.resolve().is_relative_to(PROGRAM):files.add(path.resolve())
    for path in list(files):
        if path.suffix=='.qrc':
            for item in ET.parse(path).findall('.//file'):
                member=(path.parent/item.text).resolve(strict=True)
                if not member.is_relative_to(PROGRAM):raise RuntimeError('Resource escapes formal program')
                files.add(member)
    for path in files:
        if path.suffix.lower() in ('.db','.ini','.secret') or path.stat().st_size>1048576:
            raise RuntimeError('Unexpected runtime/large input: '+str(path))
    return {str(path):{'sha256':sha(path),'bytes':path.stat().st_size} for path in sorted(files)}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root-go',required=True,action='store_true')
    parser.add_argument('--attempt',required=True)
    parser.add_argument('stage',choices=['core','new_ui','legacy_ui'])
    args=parser.parse_args()
    if not args.root_go or os.geteuid()==0 or not re.fullmatch(r'[a-zA-Z0-9_-]+',args.attempt):raise RuntimeError('Explicit ordinary-user gate required')
    output=WORK/args.attempt/args.stage
    if output.exists():raise RuntimeError('Preserve previous attempt')
    output.mkdir(parents=True)
    build=output/'build';build.mkdir()
    stem={'core':'qc_history_core_test','new_ui':'qc_history_page_test','legacy_ui':'qc_local_page_test'}[args.stage]
    project=PROGRAM/'tests'/(stem+'.pro')
    pre_qmake=sha(project)
    execute('qmake',['/usr/lib/qt5/bin/qmake',str(project),'CONFIG+=release','CONFIG-=debug'],output,build,60)
    if sha(project)!=pre_qmake:raise RuntimeError('Project changed during qmake')
    before=inputs(build,project);save(output/'inputs_before.json',before)
    for index,(name,row) in enumerate(before.items()):
        original=Path(name);copy=output/'executed_sources'/str(index)/original.name
        copy.parent.mkdir(parents=True);copy.write_bytes(original.read_bytes())
        if sha(copy)!=row['sha256']:raise RuntimeError('Source copy differs')
    passed=False;results=[]
    try:
        execute('make',['make','-j2'],output,build,300)
        binary=build/stem;save(output/'artifact.json',{'path':str(binary),'sha256':sha(binary)})
        if args.stage=='core':
            execute('test',[str(binary)],output,build,60)
            result=json.loads((output/'test.log').read_text())
            if result.get('case_count')!=20 or result.get('failed_checks')!=0:raise RuntimeError('Core test report failed')
            save(output/'test_results.json',result);results.append(result)
        else:
            for qpa in ('offscreen','xcb'):
                stage=output/qpa;stage.mkdir()
                env=dict(os.environ,QT_QPA_PLATFORM=qpa,QT_SCALE_FACTOR='1',QT_AUTO_SCREEN_SCALE_FACTOR='0',SN100_WSL_IME='off')
                if args.stage=='new_ui':env.update(SN100_QC_HISTORY_PAGE_ROOT=str(stage/'runtime'),SN100_QC_HISTORY_PAGE_EVIDENCE=str(stage/'evidence'))
                else:env.update(SN100_QC_TEST_ROOT=str(stage/'runtime'),SN100_QC_EVIDENCE=str(stage/'evidence'))
                recorded=['QT_QPA_PLATFORM','QT_SCALE_FACTOR','QT_AUTO_SCREEN_SCALE_FACTOR','SN100_WSL_IME']
                recorded+=['SN100_QC_HISTORY_PAGE_ROOT','SN100_QC_HISTORY_PAGE_EVIDENCE'] if args.stage=='new_ui' else ['SN100_QC_TEST_ROOT','SN100_QC_EVIDENCE']
                save(stage/'environment.json',{key:env[key] for key in recorded})
                with_environment('test',[str(binary),'-v1','-o',str(stage/'qt.txt')+',txt','-o',str(stage/'qt.xml')+',junitxml'],stage,build,120,env)
                text=(stage/'qt.txt').read_text();totals=re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',text)
                if not totals or totals[-1][1:]!=('0','0'):raise RuntimeError('Qt GUI failure/skips')
                results.append({'qpa':qpa,'totals':list(map(int,totals[-1]))})
                print(qpa,text[-2500:],flush=True)
        passed=True
    finally:
        after={name:{'sha256':sha(name),'bytes':Path(name).stat().st_size} for name in before}
        save(output/'inputs_after.json',after)
        save(output/'verification.json',{'passed':passed and before==after,'source_inputs':len(before),'source_before_after_equal':before==after,'stage':args.stage,'results':results,'scope':'Independent current formal sources only; not old432 native, RK, clinical or budget acceptance'})
    if before!=after:raise RuntimeError('Stage inputs changed')
    print(json.dumps({'stage':args.stage,'passed':True,'results':results}),flush=True)
if __name__=='__main__':main()
