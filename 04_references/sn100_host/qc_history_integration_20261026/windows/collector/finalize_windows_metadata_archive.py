"""Finalize text/metadata archive by reading existing receipts only; no subprocesses."""
import base64
import hashlib
import json
from pathlib import Path

HERE=Path(__file__).resolve().parent
WORK=HERE.parent
ROOT=WORK.parents[2]
DEST=ROOT/'04_references/sn100_host/qc_history_integration_20261026/windows'
STAGE=WORK/'windows_archive_preparation'

def digest(data):return hashlib.sha256(data).hexdigest()
def sha(path):return digest(path.read_bytes())
def load(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def save(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def safe(data):
    text=data.decode('utf-8-sig')
    assert not any(ord(char)<32 and char not in '\t\r\n' for char in text)

def main():
    prep_path=HERE/'platform_preparation.json'
    expected_prep='91e0d6a83a8cf092db09988be3991788a047d2b9377ec9047de6d9027d070dc2'
    assert sha(prep_path)==expected_prep
    prep=load(prep_path)
    for item in prep['controller_inputs']:assert sha(WORK/item['path'])==item['sha256']
    mapping=load(DEST/'archive_mapping.json')
    assert not mapping['excluded']
    assert not (DEST/'handoff.json').exists()
    proof=load(DEST/'attempts/windows_04/verification.json')
    assert proof['exit_code']==0 and proof['published'] and proof['source_snapshot_entries']==447
    assert proof['sources_tools_external_inputs_unchanged'] and proof['source_matches_native_freeze']
    assert len(proof['stages'])==2
    artifacts=[]
    for stage in proof['stages']:
        assert all(stage[key]==0 for key in ('qmake_exit','make_exit','import_exit'))
        assert stage['reused_objects']==0
        assert stage['pe']['machine_hex']=='0x014c' and stage['pe']['optional_header_hex']=='0x010b'
        artifacts.append({'role':stage['target'],'fresh_objects':stage['compiled_objects'],
            'producer_recorded_sha256':stage['pe']['sha256'],'bytes':stage['pe']['size'],
            'architecture':stage['pe']['architecture'],'binary_rehashed_or_copied_by_collector':False})
    assert artifacts[0]['producer_recorded_sha256']=='37b18d3bd138a4b356f2f1c9cb1bc2afae7dbe606c21b21ed7a7c7c474f46681'
    assert artifacts[1]['producer_recorded_sha256']=='48f6c34f056c9ae08d3cf27f67121f7a9b6dbf4851e31e1ab2979e60e3193169'
    original_command=prep['commands']['windows']
    prefix=original_command.removesuffix(' -RunName windows_04')
    observations=[]
    for name,code,chunk in [('windows',1,'f6aa72'),('windows_02',1,'76f8ed'),('windows_03',1,'2ea3f4'),('windows_04',0,'f3b57c')]:
        directory=DEST/'attempts'/name
        reported_command=prefix if name=='windows' else prefix+' -RunName '+name
        item={'attempt':name,'observed_parent_exit':code,'parent_tool_chunk':chunk,
            'reported_invocation':reported_command,'invocation_source':'Root task reports and prepared invocation for the named attempt',
            'kind':'post_hoc_parent_tool_observation_not_original_OS_process_receipt',
            'parent_OSPID':None,'parent_OSPID_not_exposed':True,'parent_raw_OS_argv_receipt_available':False,
            'original_verification':'attempts/'+name+'/verification.json',
            'original_child_commands_and_exits':'attempts/'+name+'/*.command.json and *.exit.json',
            'original_semantic_verification_exit':load(directory/'verification.json')['exit_code']}
        assert item['original_semantic_verification_exit']==code
        observations.append(item)
    save(DEST/'parent_invocations_observations.json',{'scope':'Four actual parent outcomes reported by root; native child receipts are retained separately without being rewritten','observations':observations})
    audited=load(STAGE/'audited_byte_logs.json')
    assert len(audited)==11
    save(DEST/'audited_captured_byte_logs.json',{'ordinary_UTF8_and_NUL_rules_unchanged':True,'explicit_per_path_SHA_length_allowlist':audited,
        'authorization':'Root explicitly authorized ten fully reviewed WSL notice logs plus exactly one90-byte CP936 anonymous argv capture',
        'not_claimed':'Base64 JSON is captured bytes, not a Unicode restoration, altered stdout, or exit receipt'})
    reconstructed=DEST/'controller_history/reconstructed_run02/native_job.cs'
    assert sha(reconstructed)=='5e8043a4d708e3b40a04f15cd598371595a123524fa374204e1d5c83bb78e5c7'
    save(DEST/'controller_history/reconstructed_run02/provenance.json',{
        'kind':'Derived exact reconstruction, not an original standalone file preserved at run02',
        'method':'Reverse only the later normal-exit-grace patch from final helper text; require full SHA match to actual run02 pre-Add-Type helper_input.json',
        'actual_run02_input_sha256':sha(reconstructed),'final_helper_base_sha256':sha(HERE/'windows/native_job.cs'),
        'reconstruction_compiled_or_executed':False})
    bindings=[]
    for item in prep['controller_inputs']:
        archived=DEST/'final_controller_inputs'/item['path']
        assert sha(archived)==item['sha256'] and sha(WORK/item['path'])==item['sha256']
        bindings.append({'source':item['path'],'bytes':item['bytes'],'sha256':item['sha256'],
            'archive':'final_controller_inputs/'+item['path'],'read_back_exact':True})
    assert sha(DEST/'final_controller_inputs/platform_preparation.json')==expected_prep
    save(DEST/'final_controller_source_manifest.json',{'execution_input_preparation_sha256':expected_prep,
        'original_preparation_status_intentionally_unchanged':True,'ARM_active_inputs_not_modified':True,'inputs':bindings})
    handoff={
        'schema':'sn100-qc-history-windows-metadata-archive-v1','status':'Windows32_full_compile_link_passed_GUI_not_verified',
        'source_freeze_entries':447,'source_freeze_sha256':'1486686b182dc8d1da55c52b45c91ea72f1b8c41677676cedf16d72b3fc4f401',
        'native_binding':'native_binding/verification.json','successful_attempt':'attempts/windows_04/verification.json',
        'actual_parent_outcomes':[1,1,1,0],'parent_outcome_scope':'Root post-hoc tool observation; unknown parent OSPID explicitly null, no invented full raw parent receipt',
        'native_child_exit_chain':'Original command/exit files preserved; child results are saved before parser verification; Windows03 make0 remains distinct from parent parser failure1',
        'artifacts_from_original_proof':artifacts,'fresh_object_reuse':False,
        'host_probe_runs':['controller_history/probes/run_01_current_quote/summary.json','controller_history/probes/run_02_minimal_argument_quote/summary.json','controller_history/probes/run_03_normal_exit_grace/summary.json'],
        'host_probe_history':'Run01 retains WSL127 and the Windows stdout decoding-harness failure; run02 fixes argument boundaries but exposes immediate Job accounting cleanup; run03 actual Windows7-argv and WSL447 probes0 with no forced termination',
        'parser_unit':'controller_history/probes/link_parser_run_01/verification.json','parser_actual_cases':14,
        'parser_scope':'Actual Windows03 main and historical426 main/helper Makefile/log/response/compile maps plus strict refusal cases; not a rerun of Qt or historical programs',
        'byte_wrapper_scope':{'files':11,'encoding_interpretation_for_review_only':True,'original_bytes_unchanged_in_temp':True,
            'no_claim_original_Unicode_restored':True,'no_generic_stdout_exception':True},
        'build_closure_GUI_or_probe_executions_by_archive_task':0,'executables_or_objects_rehashed_by_archive_task':0,
        'no_executable_object_DB_INI_authentication_or_runtime_data_copied':True,
        'execution_inputs_read_back_unchanged':'final_controller_source_manifest.json',
        'process_stop_scope':'WinJobStopped means only the owned Windows Job active-process count0; it is not separate proof of WSL Linux process-group stop',
        'Windows_GUI_verified':False,'RK_or_instrument_verified':False,'resource_budget_or_clinical_verified':False,
        'ARM_current_run_or_closure_result_claimed':False,'ARM_active_91e_input_snapshot_untouched':True}
    save(DEST/'handoff.json',handoff)
    records=[]
    for path in sorted(DEST.rglob('*')):
        if not path.is_file():continue
        assert path.suffix.lower() not in ('.exe','.o','.dll','.db','.sqlite','.ini','.a')
        data=path.read_bytes();safe(data)
        if path.name.endswith('.captured_bytes.json'):
            value=load(path);original=base64.b64decode(value['base64'])
            assert len(original)==value['original_bytes'] and digest(original)==value['original_sha256']
        records.append({'path':path.relative_to(DEST).as_posix(),'bytes':len(data),'sha256':digest(data)})
    save(DEST/'archive_manifest.json',{'schema':'sn100-text-evidence-sha256-v1','manifest_excludes_itself':True,'files':records})
    assert all(sha(DEST/item['path'])==item['sha256'] and (DEST/item['path']).stat().st_size==item['bytes'] for item in records)
    assert sha(prep_path)==expected_prep
    assert all(sha(WORK/item['path'])==item['sha256'] for item in prep['controller_inputs'])
    receipt={'status':'metadata_archive_read_back_passed','files_listed':len(records),'all_archive_members_including_manifest':len(records)+1,
        'handoff_sha256':sha(DEST/'handoff.json'),'archive_manifest_sha256':sha(DEST/'archive_manifest.json'),
        'final_source_manifest_sha256':sha(DEST/'final_controller_source_manifest.json'),
        'active_preparation_unchanged_sha256':sha(prep_path),'active_controller_inputs_unchanged':True,
        'new_build_probe_closure_GUI_executions':0,'executable_or_object_hashes_recomputed':0}
    save(STAGE/'archive_verification.json',receipt)
    print(json.dumps(receipt,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
