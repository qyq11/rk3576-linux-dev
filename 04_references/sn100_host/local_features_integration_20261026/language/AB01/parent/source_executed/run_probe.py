"""Prepare-only controller; execution requires root --root-go and a new output.
One xcb A/B invocation only. No device/network/production data or global setup.
"""
from pathlib import Path
import argparse, hashlib, json, os, signal, subprocess, sys, time
import xml.etree.ElementTree as ET

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[3]
FORMAL=REPO/'06_programs/sn100_host'
SAFE_ENV=('DISPLAY','WAYLAND_DISPLAY','XDG_SESSION_TYPE','XDG_CURRENT_DESKTOP',
 'QT_QPA_PLATFORM','QT_SCALE_FACTOR','QT_AUTO_SCREEN_SCALE_FACTOR','QT_ENABLE_HIGHDPI_SCALING',
 'QT_SCREEN_SCALE_FACTORS','QT_FONT_DPI','QT_DEVICE_PIXEL_RATIO','GDK_SCALE','GDK_DPI_SCALE')
PRIVATE_REQUIRED={'hit_ab_probe.cpp','hit_ab_probe.pro','run_probe.py','ab_contract.json'}


def required_formal():
    paths={FORMAL/p for p in ('ui/language_setting_page.cpp','ui/language_setting_page.h',
        'ui/frmlanguagesetting.ui','ui/cusmessagebox.cpp','ui/cusmessagebox.h','ui/cusmessagebox.ui',
        'ui/cusdialogmask.cpp','ui/cusdialogmask.h','ui/cusdialogmask.ui','resources.qrc')}
    for entry in ET.parse(FORMAL/'resources.qrc').getroot().iter('file'):
        path=(FORMAL/entry.text).resolve(strict=True)
        if not path.is_relative_to(FORMAL):raise ValueError('QRC member outside formal source root')
        paths.add(path)
    return {p.relative_to(REPO).as_posix() for p in paths}


def digest(path):
    path=path.resolve(strict=True)
    assert path.is_file(),path
    b=path.read_bytes()
    return {'path':path.relative_to(REPO).as_posix(),'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b)}


def save(path,data):
    with path.open('x',encoding='utf-8') as stream:
        json.dump(data,stream,ensure_ascii=False,indent=2);stream.write('\n')


def group_state(pgid):
    try: os.killpg(pgid,0);return True
    except ProcessLookupError:return False
    except PermissionError:return None


def cleanup_group(process,pgid):
    receipt={'pgid':pgid,'term_sent':False,'kill_sent':False,'errors':[]}
    state=group_state(pgid)
    # Parent exit alone never proves its compiler/child group has gone.
    for sig,bound,key in ((signal.SIGTERM,3,'term_sent'),(signal.SIGKILL,2,'kill_sent')):
        if state is False:break
        if state is None:
            receipt['errors'].append('owned group state unavailable');break
        try:os.killpg(pgid,sig);receipt[key]=True
        except ProcessLookupError:pass
        except OSError as error:receipt['errors'].append(str(error))
        deadline=time.monotonic()+bound
        while time.monotonic()<deadline:
            process.poll();state=group_state(pgid)
            if state is not True:break
            time.sleep(.02)
    process.poll();state=group_state(pgid)
    receipt.update({'actual_exit_after_cleanup':process.returncode,'group_gone':state is False,'state_known':state is not None})
    return receipt


def execute(argv,cwd,env,timeout,stem):
    receipt={'argv':[str(x) for x in argv],'cwd':str(cwd),'timeout_seconds':timeout,
             'started_monotonic_ns':time.monotonic_ns(),'actual_exit':None,'timeout':False,'exception':None}
    process=None
    with stem.with_suffix('.log').open('xb') as log:
        try:
            process=subprocess.Popen(receipt['argv'],cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            receipt.update({'pid':process.pid,'pgid':process.pid})
            deadline=time.monotonic()+timeout
            while process.poll() is None:
                if time.monotonic()>=deadline:
                    receipt['timeout']=True;break
                time.sleep(.02)
            receipt['exit_before_cleanup']=process.poll()
        except BaseException as error:
            receipt['exception']=repr(error)
        finally:
            if process is not None:
                receipt['cleanup']=cleanup_group(process,process.pid)
                receipt['actual_exit']=process.returncode
                # Timeout stays a failure even if it races a late zero exit.
            else:receipt['cleanup']={'group_gone':False,'state_known':False,'note':'no child successfully created'}
            receipt['finished_monotonic_ns']=time.monotonic_ns()
            save(stem.with_suffix('.exit.json'),receipt)
    good=(receipt['actual_exit']==0 and not receipt['timeout'] and receipt['exception'] is None
          and receipt['cleanup']['group_gone'] and receipt['cleanup']['state_known']
          and not receipt['cleanup'].get('term_sent') and not receipt['cleanup'].get('kill_sent'))
    return receipt,good


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go',action='store_true',help='root separately granted build/GUI slot')
    parser.add_argument('--output-dir',required=True)
    parser.add_argument('--jobs',type=int,choices=(1,2),default=2)
    args=parser.parse_args()
    if not args.root_go:parser.error('No execution without explicit --root-go')
    if os.name!='posix':parser.error('Use WSL/Linux owned-PGID controller; no Windows fallback')
    supplied=Path(args.output_dir)
    if not supplied.is_absolute():parser.error('--output-dir must be absolute')
    output=supplied.resolve()
    if output.exists() or not output.parent.is_dir() or not output.is_relative_to(HERE):
        parser.error('output must be a new resolved child under this private preparation directory')
    manifest_path=HERE/'preparation.json';manifest_bytes=manifest_path.read_bytes()
    manifest_sha=hashlib.sha256(manifest_bytes).hexdigest();manifest=json.loads(manifest_bytes)
    roster=manifest['source_inputs']
    private_paths={Path(x['path']).relative_to(HERE.relative_to(REPO)).as_posix()
                   for x in roster if (REPO/x['path']).resolve().is_relative_to(HERE)}
    if private_paths!=PRIVATE_REQUIRED:parser.error('Manifest private source set is incomplete or unexpected')
    if len(roster)!=len({x['path'] for x in roster}):parser.error('Duplicate manifest input')
    expected_formal=required_formal()
    if set(manifest['required_formal_paths'])!=expected_formal:parser.error('Manifest required formal set was changed')
    observed_formal={x['path'] for x in roster if not (REPO/x['path']).resolve().is_relative_to(HERE)}
    if expected_formal!=observed_formal:parser.error('Formal dependency roster differs from required set')
    for x in roster:
        p=(REPO/x['path']).resolve(strict=True)
        if not (p.is_relative_to(HERE) or p.is_relative_to(FORMAL)):parser.error('Source outside private/formal source roots')
        if digest(p)!=x:parser.error('Preparation source changed: '+str(p))
    output.mkdir();build=output/'build';build.mkdir();logs=output/'logs';logs.mkdir()
    runtime=output/'runtime';runtime.mkdir();evidence=output/'evidence';evidence.mkdir()
    (output/'preparation_executed.json').write_bytes(manifest_bytes)
    save(output/'source_before.json',roster)
    env=os.environ.copy();safe_before={k:env.get(k) for k in SAFE_ENV}
    overrides={'QT_QPA_PLATFORM':'xcb','QT_SCALE_FACTOR':'1','QT_AUTO_SCREEN_SCALE_FACTOR':'0',
               'SN100_HIT_AB_EVIDENCE':str(evidence)}
    for name,folder in [('XDG_RUNTIME_DIR','xdg'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),
                        ('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('TMP','tmp'),('TEMP','tmp')]:
        p=runtime/folder;p.mkdir(mode=0o700,exist_ok=True);overrides[name]=str(p)
    env.update(overrides)
    save(output/'environment.json',{'selected_inherited_safe':safe_before,'overrides':overrides,
                                   'selected_effective_safe':{k:env.get(k) for k in SAFE_ENV}})
    verification={'schema':'sn100-private-hit-ab-verification-v1','passed':False,
        'preparation_sha256':manifest_sha,'source_before':roster,'stages':[],
        'gui_process_limit_seconds':25,'cleanup_term_kill_seconds':[3,2],
        'scope':'Only one ordinary-button/CN diagnostic pair; no compositor/physical-pointer/RK proof',
        'error':None}
    try:
        for name,argv,bound in [('qmake',['/usr/bin/qmake','CONFIG+=release','CONFIG-=debug',str(HERE/'hit_ab_probe.pro')],30),
                               ('make',['/usr/bin/make','-j'+str(args.jobs)],120)]:
            result,good=execute(argv,build,env,bound,logs/name);verification['stages'].append(result)
            if not good:raise RuntimeError(name+' failed; raw receipt retained')
            if name=='qmake':verification['makefile_before']=digest(build/'Makefile')
        binary=build/'hit_ab_probe';verification['binary_before']=digest(binary)
        # Exactly one GUI call. Parent has25s before TERM3s/KILL2s cleanup, at
        # most30s owned process-group interval; source has24s shared A/B budget.
        result,good=execute([str(binary),'ordinaryControl','cnPage','-v1','-o',str(output/'qt.txt')+',txt',
                             '-o',str(output/'qt.xml')+',junitxml'],output,env,25,logs/'probe')
        verification['stages'].append(result)
        verification['probe_owned_interval_seconds']=(result['finished_monotonic_ns']-result['started_monotonic_ns'])/1e9
        arms=[]
        for name in ('control','cn'):
            p=evidence/(name+'.json')
            arms.append(json.loads(p.read_text(encoding='utf-8')) if p.is_file() else {'arm':name,'outcome':'missing_receipt'})
        verification['arms']=arms
        outcomes=[x['outcome'] for x in arms]
        if any(x.startswith('prerequisite_unmet') for x in outcomes):classification='screen_precondition_unmet_not_page_conclusion'
        elif outcomes==['passed','passed']:classification='both_passed_this_once'
        elif outcomes[0]!='passed':classification='control_not_passed_no_CN_specific_attribution'
        else:classification='CN_not_passed_after_control_pass_no_automatic_root_cause'
        verification['classification']=classification
        verification['binary_after']=digest(binary)
        verification['makefile_after']=digest(build/'Makefile')
        if not good or outcomes!=['passed','passed'] or verification['probe_owned_interval_seconds']>30:
            raise RuntimeError('A/B not fully passed or over30s; no retry or attribution')
        verification['passed']=True
    except BaseException as error:
        verification['error']=repr(error)
    finally:
        after=[]
        try:after=[digest(REPO/x['path']) for x in roster]
        except BaseException as error:verification['source_after_error']=repr(error)
        verification['source_after']=after
        verification['sources_equal']=after==roster
        try:verification['preparation_unchanged']=manifest_path.read_bytes()==manifest_bytes
        except BaseException as error:
            verification['preparation_unchanged']=False
            verification['preparation_after_error']=repr(error)
        verification['all_owned_groups_gone']=bool(verification['stages']) and all(x['cleanup']['group_gone'] for x in verification['stages'])
        verification['passed']=bool(verification['passed'] and verification['sources_equal'] and
            verification['preparation_unchanged'] and verification['all_owned_groups_gone'] and
            verification.get('binary_before')==verification.get('binary_after') and
            verification.get('makefile_before')==verification.get('makefile_after'))
        save(output/'verification.json',verification)
    if not verification['passed']:raise SystemExit(1)

if __name__=='__main__':main()
