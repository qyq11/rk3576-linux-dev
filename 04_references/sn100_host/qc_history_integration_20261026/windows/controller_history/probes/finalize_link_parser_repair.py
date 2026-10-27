"""Bind the actual read-only parser unit and prepare a fresh windows_04 command."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

BASE=Path(__file__).resolve().parent
DRIVERS=BASE.parent
WORK=DRIVERS.parent
ROOT=WORK.parents[2]
OUT=DRIVERS/'platform_static_review_07'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def save(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def row(path):return {'path':path.relative_to(WORK).as_posix(),'bytes':path.stat().st_size,'sha256':sha(path)}

def main():
    assert not OUT.exists()
    OUT.mkdir()
    manifest_path=DRIVERS/'platform_preparation.json'
    assert sha(manifest_path)=='976438fbd443adbd8c78d68f1b412393b918850d0a45c83eca59693443e1511c'
    manifest=load(manifest_path)
    affected={'integration_drivers/windows/makefile_audit.ps1','integration_drivers/windows/build_windows.ps1'}
    for item in manifest['controller_inputs']:
        path=WORK/item['path']
        if item['path'] in affected:item.update(bytes=path.stat().st_size,sha256=sha(path))
        else:assert sha(path)==item['sha256'],path
    assert sha(DRIVERS/'windows/native_job.cs')=='b07e52b19171564ba083bfeb3ca6e3fa7237e3c4c6b4fbfeec6bcf2805f9b53d'
    unit=BASE/'link_parser_run_01'
    result=load(unit/'verification.json')
    native=load(unit/'parser.exit.json')
    assert result['passed'] and result['positive_fixtures']==3 and result['parser_cases']==14
    assert result['current03_linked_objects']==result['current03_all_rule_objects']
    assert native['actual_exit']==0 and not native['timed_out']
    before=load(unit/'original_inputs_before.json')
    assert before==load(unit/'original_inputs_after.json')
    for name,record in before.items():
        assert sha(Path(name))==record['sha256'] and Path(name).stat().st_size==record['bytes'],name
    failed=WORK/'platforms/windows_03'
    qmake=load(failed/'shell_qmake.log.exit.json')
    make=load(failed/'shell_make.log.exit.json')
    assert qmake['exit_code']==0 and make['exit_code']==0
    assert qmake['owned_job_stopped'] and make['owned_job_stopped']
    assert not (failed/'renderer_build').exists()
    backup=DRIVERS/'audit_originals/pre_windows_expected_link_target_01'
    preimages=[row(backup/name) for name in ('makefile_audit.ps1','build_windows.ps1','platform_preparation.json')]
    assert [item['sha256'] for item in preimages]==[
        'ee52104803f438c32bfc1df822e4a0d74b0835fc94457d00ea53e7b17b638954',
        '8dbc10d61ae164c5d652aee1ea72ad20abd48d4ef67c0628c41d62854b4d7ed5',
        '976438fbd443adbd8c78d68f1b412393b918850d0a45c83eca59693443e1511c']
    log=(failed/'shell_make.log').read_text(encoding='utf-8-sig')
    previous_candidates=[line for line in log.splitlines() if re.search(r'^(?:[^\s]*[/\\])?g\+\+(?:\.exe)?\s+(?!-c\s)',line)]
    assert len(previous_candidates)==2
    observation=DRIVERS/'observations/windows_third_parent_failure.json'
    assert not observation.exists()
    save(observation,{'kind':'post_hoc_parent_tool_observation_not_original_OS_process_receipt',
        'source':'Parent root task message; tool chunk2ea3f4','observed_parent_exit_code':1,
        'parent_OSPID':None,'parent_OSPID_not_exposed':True,'tool_exposed':['exit1','traceback'],
        'original_parent_verification':row(failed/'verification.json'),
        'original_qmake_child_receipt':row(failed/'shell_qmake.log.exit.json'),'original_qmake_exit':0,
        'original_make_child_receipt':row(failed/'shell_make.log.exit.json'),'original_make_exit':0,
        'original_make_OSPID':make['pid'],'original_make_job_stopped':True,
        'report_helper_build_started':False,'whole_platform_pass_claimed':False,
        'previous_selector_candidates':previous_candidates,'old_selector_count':2,
        'error':'Old negative -c filter admitted the real -E moc_predefs preprocessor command alongside final link',
        'correction':'Positive expected executable -o identity plus explicit non-link mode exclusion; exact object/source checks retained',
        'exact_affected_preimages':preimages,'original_record_and_build_members_unchanged':row(unit/'original_inputs_after.json')})
    files=('windows/build_windows.ps1','windows/file_helpers.ps1','windows/makefile_audit.ps1')
    paths=','.join("'"+str(DRIVERS/name).replace("'","''")+"'" for name in files)
    script="$paths=@("+paths+");$n=0;foreach($p in $paths){$t=$null;$e=$null;[System.Management.Automation.Language.Parser]::ParseFile($p,[ref]$t,[ref]$e)|Out-Null;$n+=@($e).Count;$e};if($n){exit 1};Write-Output 'Static parser errors0; no controller/Add-Type executed';exit 0"
    argv=[shutil.which('pwsh'),'-NoProfile','-Command',script]
    save(OUT/'powershell_parse.command.json',{'argv':argv,'cwd':str(ROOT),'timeout_seconds':15,'scope':'Parser only'})
    parsed=subprocess.run(argv,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15,check=False)
    save(OUT/'powershell_parse.exit.json',{'actual_exit':parsed.returncode})
    (OUT/'powershell_parse.stdout.log').write_bytes(parsed.stdout)
    (OUT/'powershell_parse.stderr.log').write_bytes(parsed.stderr)
    assert parsed.returncode==0
    freeze=WORK/'native/final_before.json'
    frozen=load(freeze)
    assert len(frozen)==447 and sha(freeze)==manifest['freeze']['sha256']
    assert all(sha(ROOT/'06_programs/sn100_host'/name)==expected for name,expected in frozen.items())
    assert not (WORK/'platforms/windows_04').exists()
    previous_command=manifest['commands']['windows']
    command=previous_command.replace('-RunName windows_03','-RunName windows_04')
    assert command!=previous_command
    manifest['commands']['windows']=command
    manifest['fresh_outputs']=['platforms/windows_04','platforms/arm64']
    manifest['status']='ready_not_executed_windows_04_after_actual_readonly_parser_unit'
    manifest['third_windows_parent_failure']=row(observation)
    manifest['link_parser_unit']={
        'actual_OS_exit':row(unit/'parser.exit.json'),'verification':row(unit/'verification.json'),
        'original_inputs_before':row(unit/'original_inputs_before.json'),'original_inputs_after':row(unit/'original_inputs_after.json'),
        'original_files_unchanged':len(before),'positive_real_log_fixtures':3,'total_cases':14,
        'current03_objects':result['current03_linked_objects'],'current03_expected_rules':result['current03_all_rule_objects'],
        'counts_derived_from_actual_files_not_build_configuration':True,'CSharp_helper_loaded_or_compiled':False,
        'qmake_make_gcc_or_GUI_executed':False,'source_freeze_unchanged':447,
        'historical426_scope':'Parser consistency against preserved historical Makefile/log/response/compile mapping; not current447 rebuild proof',
        'exact_preimages':preimages}
    manifest['additional_differences'].append('Link parser positively matches explicit expected executable output, excludes non-link compiler modes;14 actual read-only parser cases pass with exact object/source consistency unchanged')
    save(manifest_path,manifest)
    save(OUT/'verification.json',{'status':'ready_not_executed_windows_04','manifest_sha256':sha(manifest_path),
        'actual_parser_unit_exit':native['actual_exit'],'actual_parser_cases':14,'formal_source_inputs_unchanged':447,
        'original_failed03_and_historical426_files_unchanged':len(before),'no_CSharp_AddType_qmake_make_gcc_GUI_execution':True,
        'CSharp_helper_unchanged':sha(DRIVERS/'windows/native_job.cs'),'ARM_controller_unchanged':True,
        'next_command':command})
    text=(DRIVERS/'RUN_PLATFORM_COMMANDS.txt').read_text(encoding='utf-8').replace(previous_command,command,1)
    text+='\nWindows_03 original qmake0/make0 but parent1 parser failure retained. Target-aware link parser passes14 read-only cases on actual03+historical426 logs; windows_04 must be a fresh full build, no resume/object reuse.\n'
    (DRIVERS/'RUN_PLATFORM_COMMANDS.txt').write_text(text,encoding='utf-8')
    print(json.dumps({'status':manifest['status'],'manifest_sha256':sha(manifest_path),
        'makefile_audit_sha256':sha(DRIVERS/'windows/makefile_audit.ps1'),
        'controller_sha256':sha(DRIVERS/'windows/build_windows.ps1'),'next_command':command},indent=2))

if __name__=='__main__':main()
