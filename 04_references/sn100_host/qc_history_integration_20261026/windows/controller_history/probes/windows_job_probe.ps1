param([ValidateSet('windows','wsl')][string]$Mode,[string]$Output)
$ErrorActionPreference='Stop'
$repo='C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$driverRoot=Split-Path $PSScriptRoot -Parent
function Save($name,$value){$value|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $Output $name) -Encoding utf8}
if(!(Test-Path -LiteralPath $Output -PathType Container)){throw 'Fresh parent-owned probe directory required'}
$helper=Join-Path $driverRoot 'windows/native_job.cs'
Save 'helper_input.json' ([ordered]@{path=$helper;sha256=(Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash.ToLowerInvariant();CSharp_compile_authorized=$true})
Add-Type -Path $helper
function Run-Native($name,$exe,[string[]]$arguments){
    $log=Join-Path $Output ($name+'.stdout.log')
    Save ($name+'.command.json') ([ordered]@{executable=$exe;arguments=$arguments;cwd=$repo;timeout_seconds=15;stdout=$log;stderr=($log+'.stderr.log')})
    $native=[Sn100FrozenBuildJob]::Run($exe,$arguments,$repo,$log,15)
    # Actual native child exit is persisted before JSON/output/snapshot checks.
    Save ($name+'.exit.json') $native
    if($native.ExitCode-ne 0 -or $native.TimedOut -or $native.OwnedTreeKilled -or !$native.OwnedJobStopped -or $native.ActiveProcessesAfter-ne 0 -or $native.Error -or $native.CleanupError){throw ('Native child failed or required residual termination: '+$name)}
    return $log
}
if($Mode-eq 'windows'){
    $values=[string[]]@('','plain','with space','C:\tail\','inside"quote','中文参数','slash\"quote')
    $method=[Sn100FrozenBuildJob].GetMethod('Quote',[Reflection.BindingFlags]'NonPublic,Static')
    $reflection=@()
    foreach($value in $values){
        $quoted=[string]$method.Invoke($null,[object[]]@($value))
        $reflection+=[ordered]@{input=$value;quoted=$quoted;characters=@($quoted.ToCharArray()|ForEach-Object{[int]$_});first_character=[int][char]$quoted[0]}
    }
    Save 'quote_reflection.json' $reflection
    if(@($reflection|Where-Object{$_.first_character-ne 34}).Count){throw 'Unexpected Quote leading character'}
    $python='C:/Users/52400/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
    $code='import json,sys; print(json.dumps(sys.argv[1:],ensure_ascii=True))'
    $log=Run-Native 'python_argv' $python ([string[]]@('-B','-c',$code)+$values)
    $actual=@(Get-Content -LiteralPath $log -Raw|ConvertFrom-Json)
    if($actual.Count-ne $values.Count){throw 'Windows Python argv count differs'}
    for($n=0;$n-lt $values.Count;++$n){if($actual[$n]-cne $values[$n]){throw ('Windows Python argv differs at '+$n)}}
    Save 'verification.json' ([ordered]@{passed=$true;original_argv_exact=$true;quote_first_character=34;cases=$values.Count;native_exit=0;job_active_after=0})
}else{
    $wsl=Join-Path ([Environment]::SystemDirectory) 'wsl.exe'
    $linuxProbe='/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/qc_history_integration_20261026/integration_drivers/probes/wsl_snapshot_probe.py'
    $snapshot=(Join-Path $Output 'snapshot.json').Replace('\','/').Replace('C:/','/mnt/c/')
    $wslArguments=[string[]]@('-d','Ubuntu-24.04','-u','qyq','--','python3','-B',$linuxProbe,'--output',$snapshot,'probe_token','中文参数','--marker=ok')
    $log=Run-Native 'wsl_snapshot' $wsl $wslArguments
    $data=Get-Content -LiteralPath $log -Raw|ConvertFrom-Json
    if(!$data.passed -or $data.user-cne 'qyq' -or $data.distro-cne 'Ubuntu-24.04' -or $data.source_inputs-ne 447){throw 'WSL route/snapshot identity differs'}
    Save 'verification.json' ([ordered]@{passed=$true;native_exit=0;job_active_after=0;WSL_options_consumed_as_options=$true;Linux_snapshot_completed_and_returned=$true;Linux_owned_process_group_stopped_not_separately_proved=$true;probe=$data})
}
Write-Output ($Mode+' helper probe passed')
