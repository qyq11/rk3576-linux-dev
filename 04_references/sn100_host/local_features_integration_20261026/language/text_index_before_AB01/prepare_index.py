"""Text-only evidence indexing. No Qt, compiler, image viewer, /proc probe or signal."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET

HERE=Path(__file__).resolve().parent
BASE=HERE.parent
ROOT=BASE.parents[2]
PROGRAM=ROOT/'06_programs/sn100_host'
DELIVERY=ROOT/'temp/sn100_host/offline_followup_20261026/language_setting_page_20261026'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def relative(path):
    return Path(path).relative_to(ROOT).as_posix()


def wsl(path):
    text=Path(path).as_posix()
    return '/mnt/c/'+text.split(':/',1)[1]


def save(name,value):
    (HERE/name).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def qt_case(directory):
    text=directory/'qt.txt'
    if not text.is_file():return None
    raw=text.read_text(encoding='utf-8')
    totals=re.findall(r'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped',raw)
    parsed=ET.parse(directory/'qt.xml').getroot()
    nodes=list(parsed.iter('testcase'))
    exit_path=directory/('test.exit.json' if (directory/'test.exit.json').is_file() else 'observe.exit.json')
    return {'actual_child_exit':load(exit_path)['actual_exit'],'exit_receipt':relative(exit_path),
        'raw_totals':list(map(int,totals[0])) if len(totals)==1 else None,
        'raw_PASS_rows':re.findall(r'^PASS\s*:\s*(.+)$',raw,re.M),
        'raw_FAIL_rows':re.findall(r'^FAIL!\s*:\s*(.+)$',raw,re.M),
        'raw_SKIP_rows':re.findall(r'^SKIP\s*:\s*(.+)$',raw,re.M),
        'raw_QWARN_rows':re.findall(r'^QWARN\s*:\s*(.+)$',raw,re.M),
        'junit_root_attributes':parsed.attrib,'junit_method_nodes':[node.attrib for node in nodes],
        'junit_failure_error_skip_nodes':[{'case':node.attrib,'tag':child.tag,'attributes':child.attrib}
            for node in nodes for child in node if child.tag in ('failure','error','skipped')],
        'txt':relative(text),'txt_sha256':sha(text),'xml':relative(directory/'qt.xml'),
        'xml_sha256':sha(directory/'qt.xml')}


def main():
    indexed_at=datetime.datetime.now(datetime.timezone.utc).isoformat()
    specs=[
        ('verification_01','run_verification.py',1,71377,'61b127','25353c'),
        ('verification_02','run_verification_v2.py',0,49031,'d41eae','f079ab'),
        ('viewport_observer_01','run_scroll_observer.py',1,None,'9ec73f','9ec73f'),
        ('viewport_observer_02','run_scroll_observer_v2.py',1,59636,'6464f2','d50098'),
        ('viewport_observer_03','run_scroll_observer_v3.py',1,49722,'c19ca7','ef7281'),
        ('viewport_observer_04','run_scroll_observer_v4.py',1,18161,'f63f81','7688e2')]
    attempts=[]
    for name,driver,parent_exit,session,start_chunk,end_chunk in specs:
        directory=BASE/name
        report=load(directory/'verification.json') if (directory/'verification.json').is_file() else None
        receipts=[]
        for path in sorted(directory.rglob('*.exit.json')):
            if any(part in ('runtime','build_runtime','compile_runtime','test_data') for part in path.relative_to(directory).parts):continue
            record=load(path)
            receipts.append({'path':relative(path),'sha256':sha(path),'actual_exit':record.get('actual_exit'),
                'pid':record.get('pid'),'pgid':record.get('pid'),'command':record.get('command'),
                'cwd':record.get('cwd'),'timeout_seconds':record.get('timeout_seconds'),
                'started_unix_ns':record.get('started_unix_ns'),'finished_unix_ns':record.get('finished_unix_ns')})
        groups=report.get('owned_groups',[]) if report else []
        attempts.append({'name':name,'kind':'formal_original_widget_cases' if name.startswith('verification') else 'private_host_scroll_observer',
            'parent_command':['wsl.exe','-d','Ubuntu-24.04','-u','qyq','--','python3','-B',wsl(BASE/driver),
                              '--root-go','--output',wsl(directory)],
            'parent_actual_exit':parent_exit,'parent_actual_exit_basis':'Original direct tool completion in this chat; not inferred from Qt/cleanup',
            'parent_tool_session_id':session,'initial_tool_chunk_id':start_chunk,'completion_tool_chunk_id':end_chunk,
            'parent_OS_PID_PGID':None,'parent_OS_identity_note':'Not exposed by direct host tool; do not fabricate',
            'parent_raw_mixed_WSL_startup_stream_file':None,
            'parent_stream_note':'Mixed-encoding startup notice remains only in original tool output; no ordinary-text reconstruction or new base64 exception',
            'driver_source':relative(BASE/driver),'driver_current_readonly_sha256':sha(BASE/driver),
            'executed_driver_SHA_recorded_at_start':(directory/'driver_inputs.json').is_file(),
            'report_passed':report.get('passed') if report else None,
            'report':relative(directory/'verification.json') if report else None,
            'phase_receipts':receipts,'offscreen':qt_case(directory/'offscreen'),'xcb':qt_case(directory/'xcb'),
            'groups_at_original_completion':groups,'all_owned_groups_recorded_gone':report.get('all_owned_groups_gone') if report else None,
            'group_identity_scope':'Numerical IDs can repeat across WSL lifetimes; gone is the original completion observation, no new probing/signalling performed',
            'source_before_exists':(directory/'source_before.json').is_file() or (directory/'binding_before.json').is_file(),
            'source_after_exists':(directory/'source_after.json').is_file() or (directory/'binding_after.json').is_file()})
    save('attempt_command_exit_group_index.json',{'indexed_at_utc':indexed_at,'attempts':attempts})

    initial=load(DELIVERY/'formal_files_sha256.json')
    current={name:sha(PROGRAM/name) for name in initial}
    assert initial==current
    source_before=load(BASE/'verification_02/source_before.json')
    source_after=load(BASE/'verification_02/source_after.json')
    assert source_before==source_after
    images=[]
    for directory in (BASE/name for name,_,_,_,_,_ in specs if name.startswith('viewport_observer_')):
        for path in sorted(directory.glob('*/evidence/*.png')):
            raw=path.read_bytes()
            assert raw[:8]==b'\x89PNG\r\n\x1a\n'
            images.append({'path':relative(path),'sha256':sha(path),'length_bytes':len(raw),
                'PNG_pixel_dimensions':list(struct.unpack('>II',raw[16:24])),
                'capture':'Actual QWidget::grab from private host; image was not opened/reviewed in this text-only indexing',
                'runtime_QScreen_geometry':None,'runtime_QScreen_availableGeometry':None,
                'runtime_QScreen_name':None,'runtime_QScreen_DPR':None,
                'DPR_not_inferred_from_PNG_dimensions':True,'original_Windows_RK_visual_acceptance':False})
    log=(BASE/'viewport_observer_04/xcb/qt.txt').read_text(encoding='utf-8')
    diagnostic=next(line for line in log.splitlines() if 'CN_VIEWPORT_HIT' in line)
    save('screen_evidence_and_gaps.json',{'PNG_files':images,'last_xcb_diagnostic_original_line':diagnostic,
        'known_from_actual_log':'active=true, host QRect(38,59,900,600), frame QRect(0,0,976,697), page(-177,-257), button global center(831,602), full visibleRegion161x61, widgetAt=null',
        'not_known':['Actual QScreen geometry/name/work area/DPR',
            'Whether ordinary and CN widgets differ under identical native geometry',
            'Direct cause of widgetAt null; no historical cause proven by this failure'],
        'waiting_for_root_AB_receipts':True,'no_new_QScreen_measurement_or_GUI_performed':True})
    save('source_and_artifact_index.json',{'formal_six_original_delivery_sha256':initial,
        'formal_six_readonly_sha256_now':current,'readonly_observation_time_utc':indexed_at,
        'original_formal_source_before_after_equal':True,'formal_dependency_source_count':len(source_before),
        'source_before':relative(BASE/'verification_02/source_before.json'),
        'source_after':relative(BASE/'verification_02/source_after.json'),
        'formal_build_before_after':{'before':relative(BASE/'verification_02/build_before_tests.json'),
            'after':relative(BASE/'verification_02/build_after_tests.json'),
            'equal':load(BASE/'verification_02/build_before_tests.json')==load(BASE/'verification_02/build_after_tests.json')},
        'formal_binary_recorded_sha256':load(BASE/'verification_02/build_after_tests.json')['binary_sha256'],
        'formal_actual_compiled_sources':load(BASE/'verification_02/compiled_sources.json'),
        'initial_origins_and_delivery_status_note':'Six source/origins and original handoff remain initial static-delivery snapshots; runtime evidence is separate. Do not rewrite them into a new tested source identity.',
        'failed_observer_gaps':['No successful-path binding_after for observer02/03/04; do not invent it from this later read-only observation.',
            'No driver_inputs SHA record was saved at observer execution start; versioned script path/current SHA indexed separately.',
            'Observer01 failed stripping precheck before moc/compile/GUI and before verification.json creation; original tool failure is the parent receipt.'],
        'old447_or_old_QM_conclusion':'Old447 checkpoint does not validate these new sources. Old23-byteQt QM has no hits for the two sampled application strings; it is not English or full application switching support.'})

    eligible,excluded,rejected=[],[],[]
    candidates=[BASE/driver for _,driver,_,_,_,_ in specs]
    for attempt_name,_,_,_,_,_ in specs:
        for directory,dirs,files in os.walk(BASE/attempt_name):
            path=Path(directory)
            dirs[:]=[name for name in dirs if name not in ('build','runtime','build_runtime','compile_runtime','test_data')]
            candidates.extend(path/name for name in files)
    # Only this window's six immutable attempts/scripts. Root A/B materials are out of scope until handed over.
    for source in sorted(candidates):
        if source.is_file():
            rel=relative(source)
            if source.suffix.lower() in ('.db','.sqlite','.ini','.secret') or 'bootstrap' in source.name:
                excluded.append({'path':rel,'reason':'Runtime/authentication input; contents not read or hashed'})
                continue
            if source.suffix.lower()=='.png':
                if '/evidence/' in source.as_posix():eligible.append({'path':rel,'category':'anonymous_GUI_PNG_pending_root_AB_visual_review','sha256':sha(source)})
                continue
            if source.suffix.lower() not in ('.json','.txt','.xml','.log','.exit','.py','.cpp','.h','.ui','.pro','.qrc'):
                continue
            raw=source.read_bytes()
            try:raw.decode('utf-8-sig')
            except UnicodeDecodeError:
                rejected.append({'path':rel,'reason':'Not ordinary UTF8 text; raw retained in temp, no new envelope authorized'})
                continue
            if b'\x00' in raw:
                rejected.append({'path':rel,'reason':'NUL in text; raw retained in temp, no new envelope authorized'})
                continue
            category='original_commands_logs_sources_and_metadata'
            if '/viewport_observer_' in source.as_posix() and '/source/' in source.as_posix():
                category='private_observer_CPP_provenance_only_not_formal_six_delivery'
            eligible.append({'path':rel,'category':category,'sha256':sha(source),'length_bytes':len(raw)})
    save('archive_scope.json',{'status':'text_scope_prepared_only_no_archive_copy',
        'eligible_pending_root_AB_review':eligible,'explicit_file_exclusions':excluded,'non_text_rejected':rejected,
        'directory_exclusions':['All build object/binary/generated trees; runtime/build_runtime/compile_runtime/test_data; no DB/INI/auth values read, hashed or archived'],
        'preserve_failures':['verification01 controller1/raw offscreen14/0','observer01 preflight1',
            'observer02 xcb2/1','observer03 xcb2/1','observer04 xcb2/1 and native_hit_state PNG'],
        'merge_rule':'Wait for actual root A/B receipts, then root decides final metadata/PNG archive. No overwrite/relabel of old attempts or initial source receipts.'})
    save('handoff.json',{'schema':'sn100-formal-CN-evidence-preparation-v1','status':'text_index_ready_waiting_root_AB_receipts',
        'formal_original_cases':{'attempt':'verification_02','parent_actual_exit':0,
            'offscreen':[14,0,0],'xcb':[14,0,0],'JUnit_method_nodes_each':11,'data_row_invocations_including_lifecycle':14,
            'note':'Raw named PASS rows cover data rows; JUnit groups them into11 method nodes. Root diagnostic counters and warnings preserved, not zeroed.'},
        'small_host_private_observer':{'offscreen':[3,0,0],'xcb':[2,1,0],
            'native_mouseClick_before_failure':False,'cause':'unknown','all_failed_attempts_preserved':True},
        'all_recorded_owned_groups_gone':all(row['all_owned_groups_recorded_gone'] for row in attempts if row['report'] is not None),
        'slot_released':True,'formal_six_changed':False,'new_Qt_build_GUI_or_screen_probe':False,
        'source_index':'source_and_artifact_index.json','command_exit_groups':'attempt_command_exit_group_index.json',
        'screen_gaps':'screen_evidence_and_gaps.json','archive_scope':'archive_scope.json',
        'not_claimed':['Small-host xcb hit success','Root shell integration','Original Windows/RK visual equivalence','English language completion']})
    manifest={path.name:sha(path) for path in HERE.iterdir() if path.is_file() and path.name!='files_sha256.json'}
    save('files_sha256.json',manifest)
    print(json.dumps({'status':'text_index_only','attempts':len(attempts),'PNG_files_indexed_not_viewed':len(images),
        'eligible_text_or_PNG_files':len(eligible),'rejected_text':len(rejected),
        'formal_six_unchanged':True,'screen_QScreen_DPR_gaps_preserved':True,
        'new_build_GUI_or_live_PID_probe':False,'handoff_sha256':sha(HERE/'handoff.json')},indent=2))


if __name__=='__main__':main()
