#!/usr/bin/env python3
"""Fresh formal CN page test. Root slot required; no shared source edits."""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import time
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PROGRAM = ROOT/'06_programs/sn100_host'
HELPERS = ROOT/'temp/sn100_host/offline_followup_20261026/samples_reset'
sys.path.insert(0,str(HELPERS))
from run_reset_slice import require, run, save, sha
from run_workflow import inventory, make_words


def qt_result(case,expected):
    raw=(case/'qt.txt').read_text(encoding='utf-8')
    totals=re.findall(r'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped',raw)
    passed=re.findall(r'^PASS\s*:\s*LanguageSettingPageTest::(.+)$',raw,re.M)
    names=['initTestCase()','originalCanvasAndSingleChineseChoice()',
        'applyReturnsWithOriginalAsynchronousWarning()','repeatedApplyCannotStackCards()',
        'warningCloseControls(escape)','warningCloseControls(original-sure)','warningCloseControls(original-top-x)',
        'hidingOrClosingPageDismissesPrompt(hide-during-open-animation)',
        'hidingOrClosingPageDismissesPrompt(close-during-open-animation)',
        'switchingStackDismissesPrompt()','destroyingPageDeletesPromptAndAnimation()',
        'readOnlyPageDoesNotPersistOrSwitchApplicationState()','hiddenPageCannotOpenFeedback()','cleanupTestCase()']
    xml=ET.parse(case/'qt.xml').getroot()
    cases=list(xml.iter('testcase'))
    failures=[{'case':node.attrib,'incident':child.attrib,'tag':child.tag}
        for node in cases for child in node if child.tag in ('failure','error','skipped')]
    methods={name.split('(')[0] for name in names}
    result={'totals':list(map(int,totals[0])) if len(totals)==1 else None,'raw_pass_rows':passed,
        'expected_data_rows':names,'junit_root_attributes':xml.attrib,'junit_method_count':len(cases),
        'junit_cases':[node.attrib for node in cases],'failure_error_skipped_nodes':failures,
        'warnings':re.findall(r'^QWARN\s*:\s*(.+)$',raw,re.M),
        'interpretation':'Qt data rows verified from raw PASS rows; JUnit groups data rows under method nodes. Root diagnostic counters retained separately.',
        'qt_txt_sha256':sha(case/'qt.txt'),'qt_xml_sha256':sha(case/'qt.xml')}
    save(case/'qt_verification.json',result)
    require(result['totals']==[expected,0,0] and len(passed)==expected and sorted(passed)==sorted(names)
            and not failures and {node.get('name') for node in cases}==methods and len(cases)==len(methods)
            and all(node.get('result')=='pass' for node in cases),'Named Qt data-row/JUnit outcome mismatch')
    return result


def environment(runtime,qpa):
    env = dict(os.environ)
    removed = []
    for key in tuple(env):
        if key.startswith(('SN100_','QT_','QML','QMAKE','CCACHE')) or key in (
            'MAKEFLAGS','MFLAGS','GNUMAKEFLAGS','MAKELEVEL','CC','CXX','LD','AR','LD_PRELOAD',
            'LD_LIBRARY_PATH','LIBRARY_PATH','CPATH','CPLUS_INCLUDE_PATH','C_INCLUDE_PATH','COMPILER_PATH','GCC_EXEC_PREFIX'):
            removed.append(key)
            del env[key]
    overrides = {'QT_QPA_PLATFORM':qpa,'QT_SCALE_FACTOR':'1','QT_AUTO_SCREEN_SCALE_FACTOR':'0'}
    for key,name in (('XDG_RUNTIME_DIR','xdg'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),
                     ('XDG_CACHE_HOME','cache'),('TMPDIR','tmp')):
        path = runtime/name
        path.mkdir(mode=0o700)
        overrides[key]=str(path)
    overrides['TMP']=overrides['TEMP']=overrides['TMPDIR']
    overrides['SN100_LANGUAGE_PAGE_TEST_ROOT']=str(runtime/'test_data')
    env.update(overrides)
    return env,{'overrides':overrides,'removed_inherited_variable_names':removed}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root-go',action='store_true',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    import pwd
    require(args.root_go and os.name=='posix' and os.geteuid()!=0 and pwd.getpwuid(os.geteuid()).pw_name=='qyq'
            and os.environ.get('WSL_DISTRO_NAME')=='Ubuntu-24.04','Explicit slot and Ubuntu-24.04/qyq required')
    require(args.output.is_absolute() and args.output.parent.resolve()==HERE and not args.output.exists(),
            'Fresh direct-child attempt required; preserve failures')
    prepared_path=ROOT/'temp/sn100_host/offline_followup_20261026/language_setting_page_20261026/verification_preparation.json'
    prepared=json.loads(prepared_path.read_text(encoding='utf-8'))
    inputs=dict(prepared['dependency_sha256'],**prepared['source_sha256'])
    def source_state():
        result={name:sha(PROGRAM/name) for name in inputs}
        require(result==inputs,'Formal CN source/dependency changed from delivered preparation')
        return result
    before=source_state()
    drivers={str(path):sha(path) for path in (Path(__file__).resolve(),HELPERS/'run_reset_slice.py',HELPERS/'run_workflow.py',prepared_path)}
    args.output.mkdir(mode=0o700)
    build,logs,control_runtime=(args.output/name for name in ('build','logs','build_runtime'))
    for path in (build,logs,control_runtime): path.mkdir()
    for name in inputs:
        if (PROGRAM/name).suffix in ('.h','.cpp','.ui','.pro','.qrc','.json'):
            target=args.output/'executed_sources'/name
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes((PROGRAM/name).read_bytes())
    save(args.output/'source_before.json',before)
    save(args.output/'driver_inputs.json',drivers)
    report={'schema':'sn100-formal-language-page-tests-v1','passed':False,'source_inputs':len(inputs),
        'formal_source_sha256':prepared['source_sha256'],'phases':[],'tests':[],
        'scope':'Fresh formal target original widget cases; no root shell/Windows/RK visual equivalence',
        'small_viewport_scroll_test_in_original_cases':False,'source_edits':False}
    save(args.output/'verification.json',report)
    def phase(name,command,cwd,where,timeout):
        print('CN_PHASE_START '+name,flush=True)
        receipt=run(name,command,cwd,where,timeout)
        report['phases'].append(receipt)
        save(args.output/'verification.json',report)
        print('CN_PHASE_EXIT '+name+' '+str(receipt['actual_exit']),flush=True)
        return receipt
    try:
        env,env_meta=environment(control_runtime,'offscreen')
        os.environ.clear();os.environ.update(env)
        save(args.output/'build_environment.json',env_meta)
        phase('qmake_version',['/usr/lib/qt5/bin/qmake','-v'],build,logs,30)
        phase('compiler_version',['/usr/bin/g++','-dumpfullversion','-dumpversion'],build,logs,30)
        pro=PROGRAM/'tests/language_setting_page_test.pro'
        phase('qmake',['/usr/lib/qt5/bin/qmake',str(pro),'-o',str(build/'Makefile'),'-spec','linux-g++',
                       'CONFIG+=release','CONFIG-=debug'],build,logs,60)
        report['make_started_unix_ns']=time.time_ns()
        phase('make',['/usr/bin/make','-C',str(build),'-j2'],build,logs,600)
        binary=build/'language_setting_page_test'
        raw=binary.read_bytes()[:20]
        require(raw[:5]==b'\x7fELF\x02' and int.from_bytes(raw[18:20],'little')==62,'WSL ELF64 x86_64 required')
        require(make_words(build/'Makefile','TARGET')==['language_setting_page_test'],'Wrong target')
        built=inventory(build,binary)
        require(set(make_words(build/'Makefile','OBJECTS'))==set(built['objects_sha256']),'Actual object set differs')
        proof={}
        for word in make_words(build/'Makefile','SOURCES'):
            path=(build/word).resolve(strict=True)
            if path.is_relative_to(PROGRAM.resolve()):
                name=path.relative_to(PROGRAM.resolve()).as_posix()
                require(inputs.get(name)==sha(path),'Unbound compiled source '+name)
                proof[name]=sha(path)
            else:
                require(path.is_relative_to(build.resolve()),'Compile source escapes private build')
        required={'ui/language_setting_page.cpp','tests/language_setting_page_test.cpp','ui/cusmessagebox.cpp','ui/cusdialogmask.cpp'}
        require(required.issubset(proof),'Missing actual formal page/test/warning/mask compile source')
        save(args.output/'compiled_sources.json',proof)
        save(args.output/'build_before_tests.json',built)
        save(args.output/'actual_object_timestamps.json',{name:(build/name).stat().st_mtime_ns for name in built['objects_sha256']})
        for qpa in ('offscreen','xcb'):
            case=args.output/qpa
            runtime=case/'runtime'
            case.mkdir();runtime.mkdir()
            env,env_meta=environment(runtime,qpa)
            os.environ.clear();os.environ.update(env)
            save(case/'environment.json',env_meta)
            command=[str(binary),'-v1','-o',str(case/'qt.txt')+',txt','-o',str(case/'qt.xml')+',junitxml']
            receipt=phase('test',command,case,case,120)
            parsed=qt_result(case,14)
            report['tests'].append({'qpa':qpa,'actual_exit':receipt['actual_exit'],**parsed})
            require(inventory(build,binary)==built,'Fresh artifacts changed during tests')
            source_state()
        save(args.output/'build_after_tests.json',inventory(build,binary))
        require(all(sha(path)==digest for path,digest in drivers.items()),'Driver/preparation changed')
        report['passed']=True
    except BaseException as error:
        report.update(error_type=type(error).__name__,error=str(error),passed=False)
        raise
    finally:
        save(args.output/'source_after.json',source_state())
        groups=[]
        for path in sorted(args.output.rglob('*.exit.json')):
            row=json.loads(path.read_text(encoding='utf-8'))
            if not row.get('pid'): continue
            try:
                os.killpg(row['pid'],0);gone=False
            except ProcessLookupError:
                gone=True
            groups.append({'receipt':str(path),'pid':row['pid'],'pgid':row['pid'],
                           'actual_exit':row['actual_exit'],'gone':gone})
        report['owned_groups']=groups
        report['all_owned_groups_gone']=all(row['gone'] for row in groups)
        report['passed']=report['passed'] and report['all_owned_groups_gone']
        save(args.output/'verification.json',report)
    require(report['passed'],'CN formal test verification failed')
    print(json.dumps({'passed':True,'verification':str(args.output/'verification.json')}),flush=True)


if __name__=='__main__':
    main()
