param([switch]$RootGo)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use PS7 from the first build step' }
if (!$RootGo) { throw 'Explicit -RootGo is required after all developers stop writes and heavy work' }
$repo = 'C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$sourceRoot = Join-Path $repo '06_programs/sn100_host'
$work = Join-Path $repo 'temp/sn100_host/qc_local_20261025/platforms/windows'
$nativeFreeze = if ($env:SN100_NATIVE_FREEZE) { $env:SN100_NATIVE_FREEZE } else { Join-Path $repo 'temp/sn100_host/qc_local_20261025/native/final_before.json' }
$qtBin = 'C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
$compilerBin = 'C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
$qmake = Join-Path $qtBin 'qmake.exe'
$make = Join-Path $compilerBin 'mingw32-make.exe'
. (Join-Path $PSScriptRoot 'file_helpers.ps1')
. (Join-Path $PSScriptRoot 'makefile_audit.ps1')

function Get-FrozenSnapshot($output) {
    $wslOutput = $output.Replace('\','/').Replace('C:/','/mnt/c/')
    $packageScript = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/package_arm64.py'
    $snapshotLog = $output + '.snapshot.log'
    $snapshotExit = Invoke-Logged 'wsl.exe' @('-d','Ubuntu-24.04','-u','qyq','--','python3','-B',$packageScript,'snapshot','--output',$wslOutput) $repo $snapshotLog
    if ($snapshotExit -ne 0) { throw 'Source snapshot failed' }
    return Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
}

function Assert-IdenticalSnapshot($a, $b) {
    $namesA = @($a.PSObject.Properties.Name | Sort-Object)
    $namesB = @($b.PSObject.Properties.Name | Sort-Object)
    if (($namesA -join "`n") -cne ($namesB -join "`n")) { throw 'Source snapshot input set differs' }
    foreach ($name in $namesA) {
        if ($a.$name -cne $b.$name) { throw "Source changed: $name" }
    }
}

if (!(Test-Path -LiteralPath $nativeFreeze -PathType Leaf)) { throw 'Wait for the completed native final source freeze' }
if (Test-Path -LiteralPath $work) { throw 'Fresh Windows evidence/build directory required' }
New-Item -ItemType Directory -Path $work | Out-Null
$savedEnvironment = @{}
foreach ($name in @('PATH','QMAKESPEC','QMAKEPATH','QMAKEFEATURES','QT_PLUGIN_PATH','QML2_IMPORT_PATH')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$result = [ordered]@{
    schema='sn100-four-windows-windows-full-build-v1'; started_utc=[DateTime]::UtcNow.ToString('o')
    scope='Fresh Windows32 full main and report-renderer compile/link; no GUI, connection or deployment'
    reused_objects=0; published=$false; stages=@(); host_version=$PSVersionTable.PSVersion.ToString(); windows_gui_verified=$false; actual_registry_enumeration_performed=$false; device_opened=$false; deployed=$false
}
try {
    $env:PATH = "$compilerBin;$qtBin;" + $savedEnvironment['PATH']
    foreach ($name in @('QMAKESPEC','QMAKEPATH','QMAKEFEATURES','QT_PLUGIN_PATH','QML2_IMPORT_PATH')) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $frozen = Get-Content -LiteralPath $nativeFreeze -Raw | ConvertFrom-Json
    $freezeSha = (Get-FileHash -LiteralPath $nativeFreeze -Algorithm SHA256).Hash.ToLowerInvariant()
    $result.native_freeze_path = $nativeFreeze.Replace('\','/')
    $result.native_freeze_sha256 = $freezeSha
    if (@($frozen.PSObject.Properties).Count -eq 0) { throw 'Empty formal source freeze' }
    foreach ($required in @('quality_control/qc_store.cpp','quality_control/quality_control_page.cpp','calibration/calibration_store.cpp','calibration/calibration_page.cpp')) {
        if ($required -notin $frozen.PSObject.Properties.Name) { throw ('Current source absent from freeze: ' + $required) }
    }
    $before = Get-FrozenSnapshot (Join-Path $work 'source_before.json')
    Assert-IdenticalSnapshot $frozen $before
    $tools = @($qmake,$make,(Join-Path $compilerBin 'g++.exe'),(Join-Path $compilerBin 'gcc.exe'),
        (Join-Path $compilerBin 'as.exe'),(Join-Path $compilerBin 'ld.exe'),(Join-Path $compilerBin 'objdump.exe'),
        (Join-Path $qtBin 'moc.exe'),(Join-Path $qtBin 'uic.exe'),(Join-Path $qtBin 'rcc.exe'),
        (Join-Path $qtBin 'Qt5Core.dll'))
    $toolBefore = @($tools | ForEach-Object { Get-FileRecord $_ })
    Save-Json $toolBefore (Join-Path $work 'toolchain_before.json')
    $toolBeforeMap = Get-Map $toolBefore
    $versions = @()
    foreach ($query in @(
        @{name='qmake'; exe=$qmake; arguments=@('-v')},
        @{name='compiler'; exe=(Join-Path $compilerBin 'g++.exe'); arguments=@('--version')},
        @{name='make'; exe=$make; arguments=@('--version')},
        @{name='objdump'; exe=(Join-Path $compilerBin 'objdump.exe'); arguments=@('--version')}
    )) {
        $log = Join-Path $work ($query.name + '_version.log')
        $code = Invoke-Logged $query.exe $query.arguments $work $log
        if ($code -ne 0) { throw "Tool version query failed: $($query.name)" }
        $versions += [ordered]@{tool=$query.name;exit_code=$code;log=[IO.Path]::GetFileName($log)}
    }
    Save-Json $versions (Join-Path $work 'tool_versions.json')
    $externalBefore = @{}
    foreach ($target in @(
        @{name='shell'; project='shell/shell.pro'; output='sn100_shell.exe'},
        @{name='renderer'; project='history/renderer.pro'; output='sn100_report_renderer.exe'}
    )) {
        $name = $target.name
        $directory = Join-Path $work ($name + '_build')
        New-Item -ItemType Directory -Path $directory | Out-Null
        $stage = [ordered]@{target=$name;started_utc=[DateTime]::UtcNow.ToString('o');reused_objects=0}
        $stage.qmake_exit = Invoke-Logged $qmake @((Join-Path $sourceRoot $target.project),'-spec','win32-g++',
            'CONFIG+=release','CONFIG-=debug','CONFIG-=debug_and_release') $directory (Join-Path $work ($name+'_qmake.log'))
        if ($stage.qmake_exit -ne 0) { $result.stages += $stage; throw "qmake failed: $name" }
        $flags = Get-MakeFields $directory
        Save-Json $flags (Join-Path $work ($name+'_flags.json'))
        if ($name -eq 'shell' -and $flags.LIBS -notmatch '-ladvapi32') { throw 'LIS registry query requires explicit system Advapi32 import' }
        if ($flags.LIBS -match 'Qt5SerialPort') { throw 'Unexpected SerialPort dependency in zero-open settings slice' }
        $targetExternalBefore = @((Get-MakeExternalInputs $directory) | ForEach-Object { Get-FileRecord $_ })
        Save-Json $targetExternalBefore (Join-Path $work ($name+'_external_inputs_before.json'))
        foreach ($record in $targetExternalBefore) { $externalBefore[$record.path] = $record }
        # Preserve original complete prebuild map before compiler starts.
        Save-Json @($externalBefore.Keys | Sort-Object | ForEach-Object { $externalBefore[$_] }) (Join-Path $work 'external_inputs_before.json')
        $actualInputs = @(Get-MakefileSources $directory)
        Save-Json $actualInputs (Join-Path $work ($name+'_actual_input_paths.json'))
        $stage.make_exit = Invoke-Logged $make @('-j2') $directory (Join-Path $work ($name+'_make.log'))
        if ($stage.make_exit -ne 0) { $result.stages += $stage; throw "Full make failed: $name" }
        $commands = @(Get-CompileCommands (Join-Path $work ($name+'_make.log')) $directory)
        $rules = @(Get-ObjectRules $directory)
        $compiledMap = @{}
        foreach ($command in $commands) { $compiledMap[$command.object] = $true }
        foreach ($rule in $rules) {
            if (!$compiledMap.ContainsKey($rule.object)) { throw "Fresh object not compiled: $($rule.object)" }
        }
        if ($commands.Count -ne $rules.Count) { throw "Object/compile command count mismatch: $name" }
        Save-Json $commands (Join-Path $work ($name+'_compiled_commands.json'))
        Save-Json $rules (Join-Path $work ($name+'_object_rules.json'))
        $coff = @()
        foreach ($rule in $rules) {
            $record = Get-FileRecord $rule.object
            $stream = [IO.File]::OpenRead($rule.object)
            try {
                $header = New-Object byte[] 2
                if ($stream.Read($header,0,2) -ne 2) { throw 'Truncated COFF object' }
            } finally { $stream.Dispose() }
            $machine = [BitConverter]::ToUInt16($header,0)
            if ($machine -ne 0x14c) { throw "Wrong object architecture: $($rule.object)" }
            $record.machine_hex = '0x014c'
            $coff += $record
        }
        Save-Json $coff (Join-Path $work ($name+'_coff_objects.json'))
        Save-Json @(Get-BuildArtifacts $directory) (Join-Path $work ($name+'_artifacts.json'))
        $stage.compiled_objects = $commands.Count
        $stage.output = Join-Path $directory $target.output
        $stage.pe = Get-PeRecord $stage.output
        $stage.import_exit = Invoke-Logged (Join-Path $compilerBin 'objdump.exe') @('-p',$stage.output) $directory (Join-Path $work ($name+'_imports.log'))
        if ($stage.import_exit -ne 0) { throw "Import inspection failed: $name" }
        $importText = Get-Content -LiteralPath (Join-Path $work ($name+'_imports.log')) -Raw
        $stage.imported_dlls = @([regex]::Matches($importText,'DLL Name:\s*(\S+)') | ForEach-Object { $_.Groups[1].Value })
        if ($stage.imported_dlls -contains 'Qt5SerialPort.dll') { throw 'Product unexpectedly imports SerialPort DLL' }
        if ($name -eq 'shell') {
            $stage.lis_registry_query_symbols_present = @('RegOpenKeyExW','RegEnumValueW','RegCloseKey' | Where-Object { $importText.Contains($_) })
        }
        $stage.finished_utc = [DateTime]::UtcNow.ToString('o')
        $result.stages += $stage
        Save-Json $result (Join-Path $work 'verification.json')
    }
    $after = Get-FrozenSnapshot (Join-Path $work 'source_after.json')
    Assert-IdenticalSnapshot $before $after
    if ((Get-FileHash -LiteralPath $nativeFreeze -Algorithm SHA256).Hash.ToLowerInvariant() -cne $freezeSha) { throw 'Final source freeze changed during Windows build' }
    $toolAfter = @($tools | ForEach-Object { Get-FileRecord $_ })
    Save-Json $toolAfter (Join-Path $work 'toolchain_after.json')
    foreach ($record in $toolAfter) {
        if ($toolBeforeMap[$record.path].sha256 -cne $record.sha256) { throw "Toolchain changed: $($record.path)" }
    }
    $externalAfter = @($externalBefore.Keys | Sort-Object | ForEach-Object { Get-FileRecord $_ })
    foreach ($record in $externalAfter) {
        if ($externalBefore[$record.path].sha256 -cne $record.sha256) { throw "External input changed: $($record.path)" }
    }
    Save-Json @($externalBefore.Keys | Sort-Object | ForEach-Object { $externalBefore[$_] }) (Join-Path $work 'external_inputs_before.json')
    Save-Json $externalAfter (Join-Path $work 'external_inputs_after.json')
    # Validate EACH target's original prebuild set, including common paths
    # that the helper's union-map insertion could otherwise overwrite.
    foreach ($targetName in @('shell','renderer')) {
        $targetBefore = @(Get-Content -LiteralPath (Join-Path $work ($targetName+'_external_inputs_before.json')) -Raw | ConvertFrom-Json)
        $targetAfter = @($targetBefore | ForEach-Object { Get-FileRecord $_.path })
        $targetMap = Get-Map $targetBefore
        foreach ($record in $targetAfter) {
            if ($targetMap[$record.path].sha256 -cne $record.sha256) { throw "Target external input changed: $targetName $($record.path)" }
        }
        Save-Json $targetAfter (Join-Path $work ($targetName+'_external_inputs_after.json'))
    }
    $publish = Join-Path $work 'final_release'
    New-Item -ItemType Directory -Path $publish | Out-Null
    foreach ($stage in $result.stages) { Copy-Item -LiteralPath $stage.output -Destination $publish }
    $result.outputs = @($result.stages | ForEach-Object { Get-PeRecord (Join-Path $publish ([IO.Path]::GetFileName($_.output))) })
    $result.source_snapshot_entries = @($before.PSObject.Properties).Count
    $result.source_matches_native_freeze = $true
    $result.sources_tools_external_inputs_unchanged = $true
    $result.complete_external_prebuild_hashes_persisted = $true
    $result.each_native_child_exit_persisted_before_parser = $true
    $result.QtSerialPort_linked = $false
    $result.published = $true
    $result.exit_code = 0
} catch {
    $result.error = $_.Exception.Message
    $result.exit_code = 1
    throw
} finally {
    $result.finished_utc = [DateTime]::UtcNow.ToString('o')
    Save-Json $result (Join-Path $work 'verification.json')
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
}
$result | ConvertTo-Json -Depth 12
