"""Bind completed bounded helper probes; no further execution."""
import hashlib
import json
from pathlib import Path
import subprocess
import shutil

BASE=Path(__file__).resolve().parent
DRIVERS=BASE.parent
WORK=DRIVERS.parent
ROOT=WORK.parents[2]
OUT=DRIVERS/'platform_static_review_06'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def save(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def row(path):return {'path':path.relative_to(WORK).as_posix(),'bytes':path.stat().st_size,'sha256':sha(path)}


def main():
    assert not OUT.exists()
    OUT.mkdir()
    manifest=load(DRIVERS/'platform_preparation.json')
    assert sha(DRIVERS/'platform_preparation.json')=='d3753e945bf5eeb976921aa385ba9ab586cfcb615588fa32a92ce2ab0ae732c5'
    helper=DRIVERS/'windows/native_job.cs'
    for item in manifest['controller_inputs']:
        source=WORK/item['path']
        if source==helper:item.update(bytes=source.stat().st_size,sha256=sha(source))
        else:assert sha(source)==item['sha256'],source
    tested=BASE/'run_03_normal_exit_grace'
    for mode,name in (('windows','python_argv'),('wsl','wsl_snapshot')):
        assert load(tested/mode/'helper_input.json')['sha256']==sha(helper)
        parent=load(tested/mode/'parent.exit.json')
        child=load(tested/mode/(name+'.exit.json'))
        assert parent['actual_exit']==0 and not parent['timed_out']
        assert child['ExitCode']==0 and not child['TimedOut'] and not child['OwnedTreeKilled']
        assert child['AssignedBeforeResume'] and child['OwnedJobStopped'] and child['ActiveProcessesAfter']==0
        assert child['NormalExitGraceMilliseconds']<=500
    failed=WORK/'platforms/windows_02'
    preserved=load(tested/'windows_02_members_before.json')
    assert preserved=={p.name:sha(p) for p in failed.iterdir() if p.is_file()}
    raw_stderr=failed/'source_before.json.snapshot.log.stderr.log'
    assert raw_stderr.stat().st_size==173 and raw_stderr.read_bytes()==(tested/'windows_02_original_stderr.raw').read_bytes()
    native_exit=load(failed/'source_before.json.snapshot.log.exit.json')
    assert native_exit['exit_code']==127 and native_exit['pid']==64004
    backup=DRIVERS/'audit_originals/pre_windows_job_quote_01'
    assert sha(backup/'native_job.cs')=='380e2b871cadbd455c443e19ac7345c459c32d11f58d23ef9e9a81bdabdcf147'
    assert sha(backup/'platform_preparation.json')=='d3753e945bf5eeb976921aa385ba9ab586cfcb615588fa32a92ce2ab0ae732c5'
    observation=DRIVERS/'observations/windows_second_parent_failure.json'
    assert not observation.exists()
    save(observation,{'kind':'post_hoc_parent_tool_observation_not_original_OS_process_receipt',
        'source':'Parent root task message; tool chunk76f8ed','observed_parent_exit_code':1,
        'tool_exposed':['exit1','traceback'],'parent_OSPID':None,'parent_OSPID_not_exposed':True,
        'original_native_child_receipt':row(failed/'source_before.json.snapshot.log.exit.json'),
        'native_child_exit_code':127,'native_child_OSPID':64004,
        'original_stderr':row(raw_stderr),'original_stderr_raw_copy':row(tested/'windows_02_original_stderr.raw'),
        'all_original_attempt_members_unchanged':preserved,
        'exact_preimages':[row(backup/'native_job.cs'),row(backup/'platform_preparation.json')],
        'initial_leading_backslash_hypothesis_rejected':'Source literal and actual reflection both establish first character34',
        'actual_argument_boundary_fix':'Run quotes only required tokens; Quote primitive remains unchanged',
        'separate_cleanup_defect':'Immediately after parent signaling Job accounting still active; 19/12ms observed grace reaches0 without termination'})
    paths=','.join("'"+str(DRIVERS/name).replace("'","''")+"'" for name in
                   ('windows/build_windows.ps1','windows/file_helpers.ps1','windows/makefile_audit.ps1'))
    script="$paths=@("+paths+");$n=0;foreach($p in $paths){$t=$null;$e=$null;[System.Management.Automation.Language.Parser]::ParseFile($p,[ref]$t,[ref]$e)|Out-Null;$n+=@($e).Count;$e};if($n){exit 1};Write-Output 'Static parser errors0; no controller executed';exit 0"
    argv=[shutil.which('pwsh'),'-NoProfile','-Command',script]
    save(OUT/'powershell_parse.command.json',{'argv':argv,'cwd':str(ROOT),'timeout_seconds':15,'scope':'Parser API only'})
    result=subprocess.run(argv,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15,check=False)
    save(OUT/'powershell_parse.exit.json',{'actual_exit':result.returncode})
    (OUT/'powershell_parse.stdout.log').write_bytes(result.stdout)
    (OUT/'powershell_parse.stderr.log').write_bytes(result.stderr)
    assert result.returncode==0
    freeze=WORK/'native/final_before.json'
    frozen=load(freeze)
    assert len(frozen)==447 and sha(freeze)==manifest['freeze']['sha256']
    assert all(sha(ROOT/'06_programs/sn100_host'/name)==value for name,value in frozen.items())
    assert not (WORK/'platforms/windows_03').exists()
    previous_command=manifest['commands']['windows']
    command=previous_command.replace('-RunName windows_02','-RunName windows_03')
    assert command!=previous_command
    manifest['commands']['windows']=command
    manifest['fresh_outputs']=['platforms/windows_03','platforms/arm64']
    manifest['status']='ready_not_executed_windows_03_after_bounded_helper_probes'
    manifest['windows_native_job_status']='Actually Add-Type compiled and tested in fresh pwsh; Windows seven exact argv cases and WSL qyq/Ubuntu447 source snapshot both actual0; no Qt/full controller executed by helper repair'
    manifest['second_windows_parent_failure']=row(observation)
    manifest['helper_probe_evidence']={
        'initial_current_quote_attempt':row(BASE/'run_01_current_quote/summary.json'),
        'quote_on_demand_argv_success_with_cleanup_gap':row(BASE/'run_02_minimal_argument_quote/summary.json'),
        'strict_argv_and_no_termination_success':row(tested/'summary.json'),
        'windows_child':row(tested/'windows/python_argv.exit.json'),
        'wsl_child':row(tested/'wsl/wsl_snapshot.exit.json'),
        'quote_reflection':row(tested/'windows/quote_reflection.json'),
        'windows_verification':row(tested/'windows/verification.json'),
        'wsl_verification':row(tested/'wsl/verification.json'),
        'source_snapshot':row(tested/'wsl/snapshot.json'),
        'explicit_scope':'Only host helper argv and read-only source snapshot; Windows Job stopped does not separately prove Linux process-group stop',
        'outer_timeout_seconds':30,'native_child_timeout_seconds':15,'normal_exit_grace_bound_ms':500,
        'observed_normal_exit_grace_ms':[19,12],'no_guard_or_timeout_relaxed':True,
        'source_inputs_unchanged':447,'qmake_make_GUI_install_deploy_performed':False,
    }
    manifest['additional_differences']+=['Quote primitive first34 verified, erroneous leading-backslash hypothesis rejected',
        'Run quotes only tokens requiring it; WSL bare -d/-u/-- options now consumed correctly',
        'Normal parent exit receives bounded500ms monotonic Job-accounting grace; strict no-residual-termination guard retained']
    save(DRIVERS/'platform_preparation.json',manifest)
    save(OUT/'verification.json',{'status':'ready_not_executed_windows_03','manifest_sha256':sha(DRIVERS/'platform_preparation.json'),
        'helper_sha256':sha(helper),'powershell_parse_actual_exit':0,'formal_source_inputs_unchanged':447,
        'source_snapshot_sha256':sha(freeze),'Windows_and_WSL_helper_probe_actual_exit':[0,0],
        'CSharp_actually_compiled':True,'qmake_make_GUI_or_full_controller_executed':False,
        'parent_observation':row(observation),'next_command':command})
    text=(DRIVERS/'RUN_PLATFORM_COMMANDS.txt').read_text(encoding='utf-8').replace(previous_command,command,1)
    text+='\nWindows_02 actual parent exit1/native WSL127 preserved; original Quote first34, quoted ordinary switches were incompatible with WSL.\nHelper run_03 now actually compiles C# and passes seven Windows argv values plus WSL qyq/Ubuntu447 snapshot, child0/Jobactive0/no termination. Full windows_03 controller remains not executed.\n'
    (DRIVERS/'RUN_PLATFORM_COMMANDS.txt').write_text(text,encoding='utf-8')
    print(json.dumps({'status':manifest['status'],'manifest_sha256':sha(DRIVERS/'platform_preparation.json'),
        'helper_sha256':sha(helper),'next_command':command,'slot_released':True},indent=2))


if __name__=='__main__':main()
