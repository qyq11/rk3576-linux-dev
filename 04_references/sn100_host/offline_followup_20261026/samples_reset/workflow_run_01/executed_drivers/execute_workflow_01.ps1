$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$native = Join-Path $repoRoot 'temp/sn100_host/offline_followup_20261026/native'
$control = Join-Path $PSScriptRoot 'workflow_parent_01'
$output = Join-Path $PSScriptRoot 'workflow_01'
if ((Test-Path -LiteralPath $control) -or (Test-Path -LiteralPath $output)) { throw 'Fresh parent/workflow directories required' }
New-Item -ItemType Directory -Path $control | Out-Null
function Save-Json($name, $value) {
    $value | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $control $name) -Encoding utf8NoBOM
}
function Digest($path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() }
function Native-State {
    $freezePath = Join-Path $native 'final_before.json'
    $frozen = Get-Content -LiteralPath $freezePath -Raw | ConvertFrom-Json -AsHashtable
    $report = Get-Content -LiteralPath (Join-Path $native 'verification.json') -Raw | ConvertFrom-Json -AsHashtable
    if (-not $report.passed -or $report.actual_exit -ne 0 -or $frozen.Count -ne 432 -or $report.source_inputs -ne $frozen.Count) { throw 'Completed actual432 native receipt required' }
    $freezeSha = Digest $freezePath
    if ($freezeSha -ne $report.snapshot_sha256 -or $freezeSha -ne '9ff28e905fd4df82330fb58b529c208a489b32987692a4131e67e3d86db5fc00') { throw 'New frozen source identity mismatch' }
    $sources = [ordered]@{}
    foreach ($entry in $frozen.GetEnumerator()) {
        $actual = Digest (Join-Path $repoRoot ('06_programs/sn100_host/' + $entry.Key))
        if ($actual -ne $entry.Value) { throw ('Frozen source changed: ' + $entry.Key) }
        $sources[$entry.Key] = $actual
    }
    $artifacts = [ordered]@{}
    foreach ($name in @('sn100_shell','sn100_report_renderer')) {
        $actual = Digest (Join-Path $native ('wsl_release/' + $name))
        if ($actual -ne $report.artifacts[$name]) { throw ('Native artifact changed: ' + $name) }
        $artifacts[$name] = $actual
    }
    $makefile = Digest (Join-Path $native 'wsl_release/Makefile')
    if ($makefile -ne $report.makefile_sha256) { throw 'Native Makefile mismatch' }
    if ($report.fresh_objects.Count -ne 201) { throw 'Expected actual201 fresh native objects' }
    $objects = [ordered]@{}
    foreach ($entry in $report.fresh_objects.GetEnumerator()) {
        $actual = Digest (Join-Path $native $entry.Key)
        if (-not $entry.Value.created_after_start -or $actual -ne $entry.Value.sha256) { throw ('Native object mismatch: ' + $entry.Key) }
        $objects[$entry.Key] = $actual
    }
    return [ordered]@{source_inputs=$frozen.Count;source_snapshot_sha256=$freezeSha;native_verification_sha256=(Digest (Join-Path $native 'verification.json'));source_sha256=$sources;main_artifacts=$artifacts;native_makefile_sha256=$makefile;native_objects_sha256=$objects;workflow_driver_sha256=(Digest (Join-Path $PSScriptRoot 'run_workflow.py'));core_helper_sha256=(Digest (Join-Path $PSScriptRoot 'run_reset_slice.py'))}
}
$wslBase = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$argv = @('-d','Ubuntu-24.04','-u','qyq','--','python3','-B',"$wslBase/temp/sn100_host/offline_followup_20261026/samples_reset/run_workflow.py",'--root-go','--output',"$wslBase/temp/sn100_host/offline_followup_20261026/samples_reset/workflow_01",'--snapshot',"$wslBase/temp/sn100_host/offline_followup_20261026/native/final_before.json")
$command = [ordered]@{executable='C:/Windows/System32/wsl.exe';argv=$argv;cwd=$repoRoot;started_utc=[DateTime]::UtcNow.ToString('o');stdout='workflow.log';stderr='workflow.stderr.log';process_kind='actual Windows WSL launcher forwarding Python exit; Linux qmake/make/test PID and PGID are in child receipts';parent_powershell_pid=$PID}
Save-Json 'workflow.command.json' $command
$process = $null
$actualExit = $null
$failure = $null
$passed = $false
$before = $null
try {
    $before = Native-State
    Save-Json 'native_binding_before.json' $before
    Write-Output ('NATIVE_BINDING_VERIFIED source=' + $before.source_inputs + ' objects=' + $before.native_objects_sha256.Count)
    $process = Start-Process -FilePath $command.executable -ArgumentList $argv -WorkingDirectory $repoRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $control 'workflow.log') -RedirectStandardError (Join-Path $control 'workflow.stderr.log') -PassThru
    Save-Json 'workflow.process.json' ([ordered]@{state='running';windows_wsl_launcher_pid=$process.Id;parent_powershell_pid=$PID})
    $process.WaitForExit()
    $process.Refresh()
    $actualExit = $process.ExitCode
} catch {
    $failure = $_.Exception.Message
} finally {
    Save-Json 'workflow.exit.json' ([ordered]@{actual_exit=$actualExit;windows_wsl_launcher_pid=if($process){$process.Id}else{$null};finished_utc=[DateTime]::UtcNow.ToString('o');exception=$failure;state=if($null -eq $process){'not_started'}else{'finished'}})
    [string]$actualExit | Set-Content -LiteralPath (Join-Path $control 'workflow.exit') -Encoding ascii
    Save-Json 'workflow.process.json' ([ordered]@{state=if($null -eq $process){'not_started'}else{'finished'};windows_wsl_launcher_pid=if($process){$process.Id}else{$null};actual_exit=$actualExit})
}
try {
    $after = Native-State
    Save-Json 'native_binding_after.json' $after
    if (($before | ConvertTo-Json -Depth 15 -Compress) -ne ($after | ConvertTo-Json -Depth 15 -Compress)) { throw 'Native/source binding changed during workflow' }
    if ($null -eq $actualExit -or $actualExit -ne 0) { throw ('Actual workflow parent exit: ' + $actualExit) }
    $result = Get-Content -LiteralPath (Join-Path $output 'verification.json') -Raw | ConvertFrom-Json
    if (-not $result.passed -or $result.tests.Count -ne 4) { throw 'Workflow did not complete four successful invocations' }
    $passed = $true
} catch {
    $failure = if($failure){$failure + '; ' + $_.Exception.Message}else{$_.Exception.Message}
}
Save-Json 'verification.json' ([ordered]@{schema='sn100-sample-workflow-parent-v1';passed=$passed;actual_workflow_exit=$actualExit;exception=$failure;source_and_native_unchanged=($null -ne $before -and (Test-Path -LiteralPath (Join-Path $control 'native_binding_after.json')));source_edits_performed=$false;native_main_executed=$false;finished_utc=[DateTime]::UtcNow.ToString('o')})
Get-Content -LiteralPath (Join-Path $control 'workflow.log') -ErrorAction SilentlyContinue
Write-Output ('WORKFLOW_PARENT_DONE passed=' + $passed + ' actual_exit=' + $actualExit)
if ($passed) { exit 0 }
if ($null -ne $actualExit -and $actualExit -ne 0) { exit $actualExit }
exit 1
