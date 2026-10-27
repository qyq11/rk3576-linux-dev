"""Archive only root-authorized completed CN/AB01 evidence. No compiler/GUI/process probe."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import xml.etree.ElementTree as ET

HERE=Path(__file__).resolve().parent
BASE=HERE.parent
ROOT=BASE.parents[2]
PROGRAM=ROOT/'06_programs/sn100_host'
DEST=ROOT/'04_references/sn100_host/local_features_integration_20261026/language'
DELIVERY=ROOT/'temp/sn100_host/offline_followup_20261026/language_setting_page_20261026'
AB01='c01785a2de5e13e6e24479b769a079c6970351338442ea1c43b944ea729560a7'
AB02='7aa031c063ba346d3fd9d86a465d36e14f9d1053eaba01e9a0833eee348c6beb'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def rel(path):
    return Path(path).relative_to(ROOT).as_posix()


def save(path,value):
    Path(path).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def main():
    assert not DEST.exists(), 'Archive destination already exists; preserve it'
    assert sha(BASE/'hit_ab_parent_01/handoff.json')==AB01
    assert sha(BASE/'hit_ab_preparation_02/handoff.json')==AB02
    ab=load(BASE/'hit_ab_parent_01/handoff.json')
    assert ab['parent']['actual_exit']==1
    assert [stage['actual_exit'] for stage in ab['raw_stages']]==[0,0,2]
    assert all(stage['cleanup']['group_gone'] for stage in ab['raw_stages'])
    assert all(not arm['click_injection_attempted'] and arm['stage']=='screen_precondition'
               and not arm['frame_in_actual_screen_available'] for arm in ab['arms'])
    formal=load(DELIVERY/'formal_files_sha256.json')
    assert all(sha(PROGRAM/name)==digest for name,digest in formal.items())
    old_scope=load(HERE/'archive_scope.json')
    candidates=[]
    for record in old_scope['eligible_pending_root_AB_review']:
        source=ROOT/record['path']
        assert sha(source)==record['sha256'], 'Indexed own evidence changed: '+str(source)
        candidates.append((source,Path('own_attempts')/source.relative_to(BASE),'original_own_attempt'))
    for path in sorted(HERE.iterdir()):
        if path.is_file():
            candidates.append((path,Path('text_index_before_AB01')/path.name,'pre_AB_text_index_snapshot'))
    for name,digest in formal.items():
        candidates.append((PROGRAM/name,Path('formal_source_tested_snapshot')/name,'formal_six_source_unchanged'))
    for path in sorted((BASE/'hit_ab_parent_01').rglob('*')):
        if path.is_file():
            candidates.append((path,Path('AB01/parent')/path.relative_to(BASE/'hit_ab_parent_01'),'AB01_executed_parent'))
    for path in sorted((BASE/'hit_ab_preparation').iterdir()):
        if path.is_file():
            candidates.append((path,Path('AB01/preparation')/path.name,'AB01_original_preparation_snapshot'))
    run=BASE/'hit_ab_preparation/run_01'
    for directory,dirs,files in os.walk(run):
        dirs[:]=[name for name in dirs if name not in ('build','runtime','test_data','data','config','cache','tmp')]
        for name in files:
            path=Path(directory)/name
            candidates.append((path,Path('AB01/run_01')/path.relative_to(run),'AB01_executed_raw_evidence'))
    candidates.append((run/'build/Makefile',Path('AB01/run_01/build/Makefile'),'AB01_actual_Makefile_text_only'))
    candidates.append((BASE/'hit_ab_preparation_02/handoff.json',Path('AB02_prepared_only/handoff.json'),'AB02_prepared_snapshot_no_execution_receipt'))

    allowed,excluded,seen=[],[],set()
    text_extensions={'.json','.txt','.xml','.log','.exit','.py','.cpp','.h','.ui','.pro','.qrc','.diff'}
    for source,target,category in candidates:
        assert target.as_posix() not in seen, target
        seen.add(target.as_posix())
        assert source.is_file() and not source.is_symlink()
        assert not any(part in ('runtime','build_runtime','compile_runtime','test_data') for part in source.relative_to(ROOT).parts)
        if source.suffix.lower() in ('.db','.sqlite','.ini','.secret') or 'bootstrap' in source.name:
            excluded.append({'source':rel(source),'reason':'runtime/auth file; contents not read or hashed'})
            continue
        raw=source.read_bytes()
        digest=hashlib.sha256(raw).hexdigest()
        if source.suffix.lower()=='.png':
            assert raw[:8]==b'\x89PNG\r\n\x1a\n'
            metadata={'pixel_dimensions':list(struct.unpack('>II',raw[16:24])),
                'capture':'QWidget content grab only; not compositor/physical-screen/cursor/occlusion evidence',
                'anonymous_content':True,'visual_original_Windows_RK_equivalence':False}
        else:
            assert source.suffix.lower() in text_extensions or source.name=='Makefile', source
            errors=[]
            try:decoded=raw.decode('utf-8-sig')
            except UnicodeDecodeError:errors.append('invalid_UTF8');decoded=None
            if b'\x00' in raw:errors.append('NUL_bytes_present')
            if errors:
                excluded.append({'source':rel(source),'intended_archive':target.as_posix(),'length_bytes':len(raw),
                    'sha256':digest,'nul_bytes':raw.count(b'\x00'),'reasons':errors,
                    'raw_retained_in_temp':True,'bytes_encoded_or_Unicode_reconstructed':False,
                    'new_byte_exception_or_envelope_used':False})
                continue
            if source.suffix.lower()=='.json':json.loads(decoded)
            if source.suffix.lower() in ('.xml','.ui','.qrc'):ET.fromstring(decoded)
            metadata={'text_gate':'Strict UTF-8(-BOM allowed), no NUL; JSON/XML/UI/QRC additionally parsed',
                      'raw_bytes_preserved_without_normalization':True}
        allowed.append({'source':source,'target':target,'category':category,'length_bytes':len(raw),
                        'sha256':digest,'metadata':metadata})
    assert len(excluded)==1 and excluded[0]['source']==rel(BASE/'hit_ab_parent_01/controller.log'), excluded
    assert excluded[0]['length_bytes']==153 and excluded[0]['nul_bytes']==42
    DEST.mkdir(parents=True)
    copied=[]
    for item in allowed:
        source,target=item['source'],DEST/item['target']
        assert not target.exists()
        assert sha(source)==item['sha256'] and source.stat().st_size==item['length_bytes']
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,target)
        actual=target.read_bytes()
        assert len(actual)==item['length_bytes'] and hashlib.sha256(actual).hexdigest()==item['sha256']
        copied.append({'source':rel(source),'archive':item['target'].as_posix(),'category':item['category'],
            'length_bytes':item['length_bytes'],'sha256':item['sha256'],'readback_length_SHA_match':True,
            **item['metadata']})
    save(DEST/'copied_files_len_sha256.json',{'schema':'sn100-language-exact-copy-manifest-v1',
        'files':copied,'every_file_read_back':True})
    save(DEST/'exclusions.json',{'schema':'sn100-language-archive-exclusions-v1','excluded':excluded,
        'directories_never_archived_or_runtime_values_hashed':['build objects/binaries/generated files','runtime','build_runtime','compile_runtime','test_data'],
        'raw_log_remains_at_temp':True,'new_base64_or_byte_exception':False,
        'AB01_original_manifest_may_reference_excluded_log':'Original source manifests preserved byte-identically; this archive copy manifest/exclusions define actual saved files.'})
    save(DEST/'screen_scope.json',{'old_five_PNGs':'Only QWidget content grabs. No compositor/cursor/physical occlusion proof.',
        'old_small_xcb04':'active=true/full button visible region/widgetAt=null; QScreen/work area/DPR were not recorded then.',
        'AB01_observation_only':{'actual_screen':'rdp-0','availableGeometry':[0,308,2560,1600],
            'frameGeometry':[0,0,976,697],'actualscreen_widget_window_grab_DPR':1,
            'both_frames_within_available':False,'both_buttons_within_available':True,
            'both_arms':'PRECONDITION_UNMET','native_hit_or_click_performed':False},
        'no_retrospective_filling_old_screen_gaps':True,'old_widgetAt_cause_proven':False,
        'AB02_included_scope':'Prepared-only handoff snapshot, no execution or success claimed'})
    pngs=[item for item in copied if item['archive'].endswith('.png')]
    assert len(pngs)==7
    save(DEST/'handoff.json',{'schema':'sn100-language-local-evidence-archive-v1',
        'status':'archived_completed_own_attempts_plus_AB01_AB02_prepared_only',
        'archived_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'formal_original_widget_cases':{'attempt':'verification_02','host_parent_actual_exit':0,
            'qmake_make_actual_exit':[0,0],'offscreen':[14,0,0],'xcb':[14,0,0],
            'data_rows_include_framework_lifecycle':True,'JUnit_grouped_method_nodes_each':11,
            'first_verification01':'Actual build0/offscreen14/0, controller1 due data-row/JUnit-method count assumption; all original evidence retained'},
        'private_small_host_cases':{'offscreen':[3,0,0],'xcb':[2,1,0],
            'original_failures_and_all_attempts_preserved':True,'old_widgetAt_cause':'unknown',
            'small_xcb_native_click_success_claimed':False},
        'AB01':{'source_handoff_sha256':AB01,'parent_actual_exit':1,'qmake_make_actual_exit':[0,0],
            'Qt_actual_exit':2,'Qt_totals':[2,2,0],'outcome':'Both arms screen PRECONDITION_UNMET before hit/click',
            'source32_tools9_before_after_equal':True,'PGIDs_recorded_gone':[365,380,421],
            'old_failure_cause_or_CN_defect_inferred':False},
        'AB02':{'source_handoff_sha256':AB02,'scope':'prepared-only snapshot','executed_evidence_included':False,'pending_root_slot':True},
        'all_original_recorded_owned_groups_gone':True,'no_new_live_process_probe_or_signal':True,
        'formal_six_current_SHA256':formal,'formal_six_current_equal_tested_source':True,
        'formal_six_or_shared_sources_edited':False,'build_GUI_or_resource_run_by_archiver':False,
        'copied_files':len(copied),'PNG_count':len(pngs),'PNG_split':{'old_widget_content_grabs':5,'AB01_widget_content_grabs':2},
        'PNG_claim':'Anonymous QWidget content only; no compositor physical-screen equivalence.',
        'file_verification':'Every copied file length/SHA-256 compared and read back; see copied_files_len_sha256.json',
        'excluded_mixed_WSL_log':excluded,'runtime_DB_INI_authentication_archived':False,
        'old23byte_QM':'Two sampled business strings unhit; not English or full-application translation completion.',
        'old447_checkpoint_or_new_unified_build_claim':'Old447 retained as prior checkpoint; this module archive does not claim the pending new unified/platform build passed.',
        'remaining':['Small XCB native-hit cause unknown','AB02 controlled-host run pending',
                     'Unified integration/new platform results, original Windows/RK visual and target inputs remain separate'],
        'writing_stopped_after_handoff':True,'active_processes_from_this_window':0})
    manifest={p.relative_to(DEST).as_posix():{'length_bytes':p.stat().st_size,'sha256':sha(p)}
        for p in sorted(DEST.rglob('*')) if p.is_file()}
    save(DEST/'archive_files_sha256.json',manifest)
    for name,item in manifest.items():
        path=DEST/name
        assert path.stat().st_size==item['length_bytes'] and sha(path)==item['sha256']
    assert all(sha(PROGRAM/name)==digest for name,digest in formal.items())
    print(json.dumps({'archived':True,'copied_exact_files':len(copied),'manifest_files_excluding_itself':len(manifest),
        'content_PNGs':len(pngs),'mixed_log_excluded_raw_bytes':153,'formal_six_unchanged':True,
        'all_copy_lengths_SHAs_read_back':True,'AB02':'prepared_only',
        'handoff_sha256':sha(DEST/'handoff.json'),'archive_manifest_sha256':sha(DEST/'archive_files_sha256.json'),
        'no_compile_GUI_or_child_process_started':True},indent=2))


if __name__=='__main__':main()
