$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = 'C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$sourceRoot = Join-Path $repo 'temp/sn100_host/instrument_20261024/source/06_programs/sn100_host'
$work = Join-Path $repo 'temp/sn100_host/instrument_20261024/windows'
$public = Join-Path $repo '04_references/sn100_host/instrument_20261024/windows'
$qtBin = 'C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
$compilerBin = 'C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
$qmake = Join-Path $qtBin 'qmake.exe'
$make = Join-Path $compilerBin 'mingw32-make.exe'
$compiler = Join-Path $compilerBin 'g++.exe'
$oldPath = $env:PATH
$started = [DateTime]::UtcNow.ToString('o')
$results = [ordered]@{ schema = 'sn100-windows-build-v1'; started_utc = $started; scope = 'Instrument offline page/maintenance Windows32 compile/link only, frozen source. No GUI execution, network/device connection, installation or deployment.'; configuration = 'Release / Qt5.14.2 / MinGW7.3 32-bit / C++14'; jobs = 2; published = $false; stages = @(); }
function Save-Json($value, $path) { $value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding utf8 }
function Get-FileRecord($path) {
    $item = Get-Item -LiteralPath $path
    return [ordered]@{ path = $item.FullName.Replace('\','/'); size = $item.Length; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
function Get-SourceSnapshot {
    $files = & rg --files --hidden --no-ignore $sourceRoot
    if ($LASTEXITCODE -ne 0) { throw 'rg source inventory failed' }
    return @($files | Sort-Object | ForEach-Object { Get-FileRecord $_ })
}
function Invoke-Logged($exe, [string[]]$arguments, $directory, $log) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    $start.Arguments = (($arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' }) -join ' ')
    $start.WorkingDirectory = $directory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    if (!$process.Start()) { throw "Cannot start native build process: $exe" }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $text = "[stdout]`r`n" + $stdout.Result + "`r`n[stderr]`r`n" + $stderr.Result
    [IO.File]::WriteAllText($log, $text, (New-Object Text.UTF8Encoding($false)))
    return $process.ExitCode
}
function Get-MakefileSources($dir) {
    $found = @{}
    foreach ($makefile in @('Makefile','Makefile.Release')) {
        $path = Join-Path $dir $makefile
        if (!(Test-Path -LiteralPath $path)) { continue }
        $body = Get-Content -LiteralPath $path -Raw
        foreach ($match in [regex]::Matches($body, '[^\s\x22]+')) {
            $token = $match.Value.TrimEnd(':').Trim('"')
            if ($token -eq '\' -or $token.Contains('$') -or $token.StartsWith('-')) { continue }
            try {
                $candidate = if ([IO.Path]::IsPathRooted($token)) { [IO.Path]::GetFullPath($token) } else { [IO.Path]::GetFullPath((Join-Path $dir $token)) }
                if ($candidate.StartsWith([IO.Path]::GetFullPath($sourceRoot) + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
                    $found[$candidate.Replace('\','/')] = $true
                }
            } catch { }
        }
    }
    return @($found.Keys | Sort-Object)
}
function Get-PeRecord($path) {
    $record = Get-FileRecord $path
    $bytes = [IO.File]::ReadAllBytes($path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a -or [BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x4550) { throw "Invalid PE: $path" }
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    $optional = [BitConverter]::ToUInt16($bytes, $peOffset + 24)
    $record.machine_hex = ('0x{0:x4}' -f $machine)
    $record.optional_header_hex = ('0x{0:x4}' -f $optional)
    $record.architecture = if ($machine -eq 0x14c -and $optional -eq 0x10b) { 'PE32 / Intel i386 / 32-bit Windows' } else { 'unexpected' }
    if ($machine -ne 0x14c -or $optional -ne 0x10b) { throw "Wrong architecture: $path" }
    return $record
}
try {
    if (Test-Path -LiteralPath (Join-Path $work 'shell_build')) { throw 'Fresh build tree is required; shell_build already exists' }
    if (Test-Path -LiteralPath (Join-Path $work 'renderer_build')) { throw 'Fresh build tree is required; renderer_build already exists' }
    $env:PATH = "$compilerBin;$qtBin;$oldPath"
    $toolFiles = @('qmake.exe','moc.exe','uic.exe','rcc.exe') | ForEach-Object { Get-FileRecord (Join-Path $qtBin $_) }
    $toolFiles += @('g++.exe','gcc.exe','mingw32-make.exe','ld.exe','ar.exe','objdump.exe') | ForEach-Object { Get-FileRecord (Join-Path $compilerBin $_) }
    $toolchain = [ordered]@{
        qmake = $qmake; qmake_version = ((& $qmake -v 2>&1) -join "`n"); qmake_version_exit = $LASTEXITCODE
        compiler = $compiler; compiler_target = ((& $compiler -dumpmachine 2>&1) -join "`n"); compiler_target_exit = $LASTEXITCODE
        compiler_version = ((& $compiler -dumpversion 2>&1) -join "`n"); compiler_version_exit = $LASTEXITCODE
        make = $make; make_version = ((& $make --version 2>&1) -join "`n"); make_version_exit = $LASTEXITCODE
        path_scope = 'Temporary PATH in this PowerShell build process, restored in finally. No persistent environment changes.'
        binaries = $toolFiles
    }
    Save-Json $toolchain (Join-Path $work 'toolchain.json')
    $before = Get-SourceSnapshot
    Save-Json $before (Join-Path $work 'all_formal_inputs_before.json')
    $beforeMap = @{}
    foreach ($entry in $before) { $beforeMap[$entry.path] = $entry }
    $actual = @{}
    foreach ($target in @(
        @{ name = 'shell'; project = 'shell/shell.pro'; output = 'sn100_shell.exe' },
        @{ name = 'renderer'; project = 'history/renderer.pro'; output = 'sn100_report_renderer.exe' }
    )) {
        $dir = Join-Path $work ($target.name + '_build')
        New-Item -ItemType Directory -Path $dir | Out-Null
        $qmakeLog = Join-Path $work ($target.name + '_qmake.log')
        $makeLog = Join-Path $work ($target.name + '_make.log')
        $arguments = @((Join-Path $sourceRoot $target.project), '-spec', 'win32-g++', 'CONFIG+=release', 'CONFIG-=debug', 'CONFIG-=debug_and_release')
        $stage = [ordered]@{ target = $target.name; project = $target.project; directory = $dir; qmake_arguments = $arguments; qmake_log = $qmakeLog; make_arguments = @('-j2'); make_log = $makeLog; started_utc = [DateTime]::UtcNow.ToString('o') }
        $stage.qmake_exit = Invoke-Logged $qmake $arguments $dir $qmakeLog
        if ($stage.qmake_exit -ne 0) { $results.stages += $stage; throw "qmake failed: $($target.name), exit $($stage.qmake_exit)" }
        $dependencies = Get-MakefileSources $dir
        if ($dependencies.Count -eq 0) { throw "No actual source dependencies parsed for $($target.name)" }
        $stage.input_count = $dependencies.Count
        Save-Json $dependencies (Join-Path $work ($target.name + '_actual_input_paths.json'))
        foreach ($path in $dependencies) { $actual[$path] = $true }
        $stage.make_exit = Invoke-Logged $make @('-j2') $dir $makeLog
        $stage.finished_utc = [DateTime]::UtcNow.ToString('o')
        $stage.output = Join-Path $dir $target.output
        $results.stages += $stage
        Save-Json $results (Join-Path $work 'windows_result.json')
        if ($stage.make_exit -ne 0) { throw "make failed: $($target.name), exit $($stage.make_exit)" }
        if (!(Test-Path -LiteralPath $stage.output -PathType Leaf)) { throw "Missing new output: $($stage.output)" }
        Write-Output ("Built " + $target.name + ' exit=0')
    }
    $actualBefore = @($actual.Keys | Sort-Object | ForEach-Object {
        if (!$beforeMap.ContainsKey($_)) { throw "Actual input added after snapshot: $_" }
        $beforeMap[$_]
    })
    $actualAfter = @($actual.Keys | Sort-Object | ForEach-Object { Get-FileRecord $_ })
    Save-Json $actualBefore (Join-Path $work 'actual_inputs_before.json')
    Save-Json $actualAfter (Join-Path $work 'actual_inputs_after.json')
    $changes = @($actualAfter | Where-Object { $_.sha256 -ne $beforeMap[$_.path].sha256 -or $_.size -ne $beforeMap[$_.path].size })
    $results.actual_input_count = $actualBefore.Count
    $results.actual_inputs_unchanged = ($changes.Count -eq 0)
    $results.changed_actual_inputs = $changes
    if ($changes.Count -ne 0) { throw 'Actual build inputs changed while compiling; pair will not publish' }
    foreach ($required in @('shell/instrument_shell.cpp','instrument/instrument_page.cpp','instrument/instrument_page.h','instrument/maintenance_store.cpp','instrument/maintenance_store.h','instrument/frmdriverset.ui','instrument/instrument_resources.qrc','shell/device_status_shell.cpp','protocol/status_replay.cpp','protocol/status_replay.h','protocol/status_session.cpp','protocol/status_session.h','protocol/frame_codec.cpp','protocol/frame_codec.h')) {
        $path = [IO.Path]::GetFullPath((Join-Path $sourceRoot $required)).Replace('\','/')
        if (!$actual.ContainsKey($path)) { throw "Required instrument/replay input missing from actual dependency closure: $required" }
    }
    $toolAfter = @($toolFiles | ForEach-Object { Get-FileRecord $_.path })
    Save-Json $toolAfter (Join-Path $work 'toolchain_after.json')
    $toolChanged = @()
    foreach ($entry in $toolAfter) {
        $initial = @($toolFiles | Where-Object { $_.path -eq $entry.path })[0]
        if ($initial.sha256 -ne $entry.sha256 -or $initial.size -ne $entry.size) { $toolChanged += $entry }
    }
    $results.toolchain_unchanged = ($toolChanged.Count -eq 0)
    if ($toolChanged.Count -ne 0) { throw 'Toolchain changed during build; no pair publication' }
    foreach ($stage in $results.stages) {
        $importExit = Invoke-Logged (Join-Path $compilerBin 'objdump.exe') @('-p',$stage.output) $work (Join-Path $work ($stage.target + '_pe_imports.log'))
        if ($importExit -ne 0) { throw 'PE import inspection failed; no pair publication' }
    }
    $stageOutputs = @($results.stages | ForEach-Object { Get-PeRecord $_.output })
    $publish = Join-Path $work 'release'
    New-Item -ItemType Directory -Path $publish | Out-Null
    foreach ($output in $stageOutputs) { Copy-Item -LiteralPath $output.path -Destination $publish }
    $results.outputs = @($stageOutputs | ForEach-Object { Get-PeRecord (Join-Path $publish ([IO.Path]::GetFileName($_.path))) })
    $results.published = $true
    $results.pair_publish_condition = 'Both fresh qmake/make builds exit 0, all actual formal input SHA256 unchanged, both executables verified PE32/i386 before same-directory pair publish.'
    $results.exit_code = 0
} catch {
    $results.failure = $_.Exception.Message
    $results.exit_code = 1
    Write-Output $results.failure
} finally {
    $env:PATH = $oldPath
    $results.path_restored = ($env:PATH -ceq $oldPath)
    $results.finished_utc = [DateTime]::UtcNow.ToString('o')
    Save-Json $results (Join-Path $work 'windows_result.json')
    New-Item -ItemType Directory -Path $public -Force | Out-Null
    $evidenceFiles = @(Get-ChildItem -LiteralPath $work -File | Where-Object { $_.Extension -in @('.log','.json','.ps1') })
    foreach ($file in $evidenceFiles) { Copy-Item -LiteralPath $file.FullName -Destination $public }
    Save-Json @($evidenceFiles | ForEach-Object { Get-FileRecord $_.FullName }) (Join-Path $public 'evidence_manifest.json')
}
exit $results.exit_code
