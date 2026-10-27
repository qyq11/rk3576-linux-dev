$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 required' }
$repo = 'C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$sourceRoot = Join-Path $repo '06_programs/sn100_host'
$original = Join-Path $repo 'temp/sn100_host/qc_local_20261025/platforms/windows'
$evidence = Join-Path $repo '04_references/sn100_host/report_remaining_20261025/windows_include_diagnostic_20261026'
$rendererBuild = Join-Path $repo 'temp/sn100_host/report_remaining_20261025/windows_include_renderer_diagnostic_20261026'
$qtBin = 'C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
$compilerBin = 'C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
$qmake = Join-Path $qtBin 'qmake.exe'
$make = Join-Path $compilerBin 'mingw32-make.exe'
$helpers = Join-Path $repo 'temp/sn100_host/qc_local_20261025/integration_drivers/windows'
. (Join-Path $helpers 'file_helpers.ps1')
. (Join-Path $helpers 'makefile_audit.ps1')

function Invoke-Diagnostic($exe, [string[]]$arguments, $directory, $log) {
    if (Test-Path -LiteralPath ($log + '.command.json')) { throw 'Receipt already exists' }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $exe
    foreach ($argument in $arguments) { $info.ArgumentList.Add($argument) }
    $info.WorkingDirectory = $directory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $record = [ordered]@{executable=$exe;arguments=$arguments;cwd=$directory;started_utc=[DateTime]::UtcNow.ToString('o');timeout_ms=600000;diagnostic_only=$true}
    Save-Json $record ($log + '.command.json')
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $started = $false
    $killed = $false
    $errorText = $null
    $actualExit = $null
    try {
        if (!$process.Start()) { throw 'Native process did not start' }
        $started = $true
        Save-Json @{pid=$process.Id;state='running';started_utc=$record.started_utc} ($log + '.process.json')
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(600000)) { throw 'Diagnostic exceeded ten-minute bound' }
    } catch {
        $errorText = $_.Exception.Message
        throw
    } finally {
        if ($started -and !$process.HasExited) { $process.Kill($true); $killed=$true; $process.WaitForExit(10000) | Out-Null }
        if ($started -and $process.HasExited) { $actualExit=$process.ExitCode }
        $state = if (!$started) { 'not_started' } elseif ($process.HasExited) { 'finished' } else { 'exit_unknown' }
        if ($started -and $process.HasExited) { [IO.File]::WriteAllText($log, "[stdout]`n"+$stdout.Result+"`n[stderr]`n"+$stderr.Result, [Text.UTF8Encoding]::new($false)) }
        $receipt=[ordered]@{pid=$(if ($started) {$process.Id} else {$null});state=$state;exit_code=$actualExit;owned_tree_killed=$killed;error=$errorText;started_utc=$record.started_utc;finished_utc=[DateTime]::UtcNow.ToString('o')}
        Save-Json $receipt ($log + '.exit.json')
        Save-Json $receipt ($log + '.process.json')
        $process.Dispose()
    }
    return $actualExit
}

function Get-DiagnosticSnapshot {
    $map=[ordered]@{}
    foreach ($name in ($freeze.PSObject.Properties.Name | Sort-Object)) {
        $map[$name]=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $sourceRoot $name)).Hash.ToLowerInvariant()
    }
    return $map
}

$result=[ordered]@{schema='sn100-report-windows-include-diagnostic-v1';started_utc=[DateTime]::UtcNow.ToString('o');host_version=$PSVersionTable.PSVersion.ToString();diagnostic_only=$true;final_fresh_build=$false;published=$false;gui_run=$false;registry_enumeration=$false;device_opened=$false;stages=@();exit_code=$null}
$savedEnvironment=@{}
foreach ($name in @('PATH','QMAKESPEC','QMAKEPATH','QMAKEFEATURES','QT_PLUGIN_PATH','QML2_IMPORT_PATH')) { $savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
try {
    $active=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -in @('mingw32-make.exe','g++.exe','cc1plus.exe','qmake.exe')})
    if ($active.Count) { throw 'Another build process is active; diagnostic slot not exclusive' }
    $env:PATH="$compilerBin;$qtBin;"+$savedEnvironment['PATH']
    foreach ($name in @('QMAKESPEC','QMAKEPATH','QMAKEFEATURES','QT_PLUGIN_PATH','QML2_IMPORT_PATH')) { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
    $freezePath=Join-Path $repo 'temp/sn100_host/qc_local_20261025/native/final_before.json'
    $freeze=Get-Content -LiteralPath $freezePath -Raw | ConvertFrom-Json
    $result.prior_freeze_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $freezePath).Hash.ToLowerInvariant()
    $before=Get-DiagnosticSnapshot
    Save-Json $before (Join-Path $evidence 'source_after_include_before_build.json')
    $delta=@($before.Keys | Where-Object {$before[$_] -cne $freeze.$_})
    Save-Json $delta (Join-Path $evidence 'changed_since_prior_freeze.json')
    if ($delta.Count -ne 1 -or $delta[0] -cne 'history/report_remaining_store.cpp') { throw 'Unexpected source differences from prior freeze' }
    $result.changed_since_prior_freeze=$delta
    $tools=Get-Content -LiteralPath (Join-Path $original 'toolchain_before.json') -Raw | ConvertFrom-Json
    $actualTools=@($tools | ForEach-Object {Get-FileRecord $_.path})
    Save-Json $actualTools (Join-Path $evidence 'toolchain_checked.json')
    for ($i=0; $i -lt $tools.Count; $i++) { if ($tools[$i].sha256 -cne $actualTools[$i].sha256) {throw 'Toolchain changed'} }
    $shellBuild=Join-Path $original 'shell_build'
    $makefile=Join-Path $shellBuild 'Makefile'
    $oldFlags=Get-Content -LiteralPath (Join-Path $original 'shell_flags.json') -Raw | ConvertFrom-Json
    $flags=Get-MakeFields $shellBuild
    foreach ($name in $flags.Keys) { if ($flags[$name] -cne $oldFlags.$name) {throw "Original shell Makefile field changed: $name"} }
    if ($flags.QMAKE -cne $qmake.Replace('/','\') -or $flags.CXXFLAGS -notmatch 'gnu\+\+1y' -or $flags.DEFINES -notmatch 'QT_NO_DEBUG') {throw 'Unexpected Qt/compiler/configuration'}
    Save-Json @{fields=$flags;makefile=(Get-FileRecord $makefile);original_object_count=@(Get-ChildItem -LiteralPath (Join-Path $shellBuild '.obj') -Filter '*.o').Count;mode='continue original failed tree, diagnostic only'} (Join-Path $evidence 'shell_makefile_checked.json')
    $result.qmake_version_exit=Invoke-Diagnostic $qmake @('-v') $evidence (Join-Path $evidence 'qmake_version.log')
    if ($result.qmake_version_exit -ne 0) {throw 'qmake version command failed'}
    $stage=[ordered]@{target='shell';kind='continued original failed tree';make_exit=(Invoke-Diagnostic $make @('-j2') $shellBuild (Join-Path $evidence 'shell_continue_01.log'))}
    $result.stages += $stage
    if ($stage.make_exit -ne 0) {throw 'Shell diagnostic failed; inspect next actual error before changing more sources'}
    $stage.output=Get-PeRecord (Join-Path $shellBuild 'sn100_shell.exe')
    if (Test-Path -LiteralPath $rendererBuild) {throw 'Fresh diagnostic renderer directory required'}
    New-Item -ItemType Directory -Path $rendererBuild | Out-Null
    $stage=[ordered]@{target='renderer';kind='separate fresh diagnostic tree';qmake_exit=(Invoke-Diagnostic $qmake @((Join-Path $sourceRoot 'history/renderer.pro'),'-spec','win32-g++','CONFIG+=release','CONFIG-=debug','CONFIG-=debug_and_release') $rendererBuild (Join-Path $evidence 'renderer_qmake_01.log'))}
    $result.stages += $stage
    if ($stage.qmake_exit -ne 0) {throw 'Renderer qmake failed'}
    Save-Json @{fields=(Get-MakeFields $rendererBuild);makefile=(Get-FileRecord (Join-Path $rendererBuild 'Makefile'))} (Join-Path $evidence 'renderer_makefile_checked.json')
    $stage.make_exit=Invoke-Diagnostic $make @('-j2') $rendererBuild (Join-Path $evidence 'renderer_make_01.log')
    if ($stage.make_exit -ne 0) {throw 'Renderer diagnostic failed; inspect next actual error before changing more sources'}
    $stage.output=Get-PeRecord (Join-Path $rendererBuild 'sn100_report_renderer.exe')
    $result.exit_code=0
} catch {
    $result.error=$_.Exception.Message
    $result.exit_code=1
} finally {
    if ($before) {
        $after=Get-DiagnosticSnapshot
        Save-Json $after (Join-Path $evidence 'source_after_build.json')
        $changed=@($before.Keys | Where-Object {$before[$_] -cne $after[$_]})
        $result.changed_during_build=$changed
        if ($changed.Count) {$result.exit_code=1; $result.error='Source changed during diagnostic'}
    }
    foreach ($name in $savedEnvironment.Keys) {[Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process')}
    $result.finished_utc=[DateTime]::UtcNow.ToString('o')
    Save-Json $result (Join-Path $evidence 'diagnostic_verification.json')
}
$result | ConvertTo-Json -Depth 8
exit $result.exit_code
