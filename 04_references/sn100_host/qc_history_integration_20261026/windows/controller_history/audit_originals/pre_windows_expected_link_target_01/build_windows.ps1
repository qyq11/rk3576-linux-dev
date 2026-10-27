param([switch]$RootGo,[ValidatePattern('^windows(?:_[A-Za-z0-9_-]+)?$')][string]$RunName='windows')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use PS7 from the first build step' }
if (!$RootGo) { throw 'Explicit -RootGo is required after all developers stop writes and heavy work' }
$repo = 'C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$sourceRoot = Join-Path $repo '06_programs/sn100_host'
$cohort = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work = Join-Path $cohort ('platforms/'+$RunName)
$preparationPath = Join-Path $PSScriptRoot '../platform_preparation.json'
$preparation = Get-Content -LiteralPath $preparationPath -Raw | ConvertFrom-Json
$nativeFreeze = Join-Path $cohort 'native/final_before.json'
$nativeProof = Join-Path $cohort 'native/verification.json'
$qtBin = 'C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
$compilerBin = 'C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
$qmake = Join-Path $qtBin 'qmake.exe'
$make = Join-Path $compilerBin 'mingw32-make.exe'
. (Join-Path $PSScriptRoot 'file_helpers.ps1')
. (Join-Path $PSScriptRoot 'makefile_audit.ps1')

function Get-FrozenSnapshot($output) {
    # The installed WSL package also registers application aliases. Select the
    # previously exercised system executable without changing global PATH.
    $systemWsl = Join-Path ([Environment]::SystemDirectory) 'wsl.exe'
    if (!(Test-Path -LiteralPath $systemWsl -PathType Leaf)) { throw 'Existing system WSL executable required' }
    $application=@(Get-Command -Name $systemWsl -CommandType Application -ErrorAction Stop)
    if ($application.Count -ne 1 -or ![string]::Equals([IO.Path]::GetFullPath($application[0].Source),[IO.Path]::GetFullPath($systemWsl),[StringComparison]::OrdinalIgnoreCase)) { throw 'System WSL path is not the unique selected application' }
    $wslBefore=Get-FileRecord $systemWsl
    Save-Json $wslBefore ($output+'.wsl_executable_before.json')
    $wslOutput = $output.Replace('\','/').Replace('C:/','/mnt/c/')
    $packageScript = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/package_arm64.py'
    $snapshotLog = $output + '.snapshot.log'
    $snapshotExit = Invoke-Logged $systemWsl @('-d','Ubuntu-24.04','-u','qyq','--','python3','-B',$packageScript,'snapshot','--output',$wslOutput) $repo $snapshotLog
    $wslAfter=Get-FileRecord $systemWsl
    Save-Json $wslAfter ($output+'.wsl_executable_after.json')
    if ($wslBefore.sha256 -cne $wslAfter.sha256) { throw 'System WSL executable changed during snapshot' }
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

function Assert-NativeProof {
    if (!(Test-Path -LiteralPath $nativeProof -PathType Leaf)) { throw 'Wait for the new completed native verification proof' }
    $proof = Get-Content -LiteralPath $nativeProof -Raw | ConvertFrom-Json
    $freezeHash = (Get-FileHash -LiteralPath $nativeFreeze -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($freezeHash -cne $preparation.freeze.sha256) { throw 'Wrong explicit QC-history source freeze' }
    if (!$proof.passed -or $proof.actual_exit -ne 0 -or $proof.snapshot_sha256 -cne $freezeHash -or
        $proof.source_inputs -ne $preparation.freeze.entries -or !$proof.source_before_after_equal -or
        $proof.parallel_jobs_maximum -ne 2) { throw 'Native verification is not bound to this successful fresh source build' }
    $native = Join-Path $cohort 'native'
    if ($proof.makefile_sha256 -cne (Get-FileHash -LiteralPath (Join-Path $native 'wsl_release/Makefile') -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'Native Makefile changed' }
    foreach ($name in @('sn100_shell','sn100_report_renderer')) {
        if ($proof.artifacts.$name -cne (Get-FileHash -LiteralPath (Join-Path $native ('wsl_release/'+$name)) -Algorithm SHA256).Hash.ToLowerInvariant()) { throw ('Native pair changed: '+$name) }
    }
    $objects=@($proof.fresh_objects.PSObject.Properties)
    if (!$objects.Count) { throw 'Native fresh-object proof missing' }
    foreach ($object in $objects) {
        if (!$object.Value.created_after_start -or $object.Value.sha256 -cne
            (Get-FileHash -LiteralPath (Join-Path $native $object.Name) -Algorithm SHA256).Hash.ToLowerInvariant()) { throw ('Native fresh-object changed: '+$object.Name) }
    }
    return Get-FileRecord $nativeProof
}
if (!(Test-Path -LiteralPath $nativeFreeze -PathType Leaf)) { throw 'Wait for the explicit new source freeze' }
$nativeBinding = Assert-NativeProof
foreach ($input in $preparation.controller_inputs) {
    if ($input.sha256 -cne (Get-FileHash -LiteralPath (Join-Path $cohort $input.path) -Algorithm SHA256).Hash.ToLowerInvariant()) { throw ('Prepared controller input changed: '+$input.path) }
}
foreach ($input in $preparation.prior_passed_receipts) {
    if ($input.sha256 -cne (Get-FileHash -LiteralPath (Join-Path $repo $input.path) -Algorithm SHA256).Hash.ToLowerInvariant()) { throw ('Historical input receipt changed: '+$input.path) }
}
if (Test-Path -LiteralPath $work) { throw 'Fresh Windows evidence/build directory required' }
New-Item -ItemType Directory -Path $work | Out-Null
$preparationBinding = Get-FileRecord $preparationPath
Copy-Item -LiteralPath $preparationPath -Destination (Join-Path $work 'platform_preparation_input.json')
Save-Json $preparation.controller_inputs (Join-Path $work 'controller_inputs_before.json')
$savedEnvironment = @{}
foreach ($name in @('PATH','QMAKESPEC','QMAKEPATH','QMAKEFEATURES','QT_PLUGIN_PATH','QML2_IMPORT_PATH')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$result = [ordered]@{
    schema='sn100-qc-history-windows-full-build-v1'; started_utc=[DateTime]::UtcNow.ToString('o')
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
    $result.native_verification_before = $nativeBinding
    $result.native_freeze_path = $nativeFreeze.Replace('\','/')
    $result.native_freeze_sha256 = $freezeSha
    if (@($frozen.PSObject.Properties).Count -ne $preparation.freeze.entries) { throw 'Empty formal source freeze' }
    foreach ($required in @('quality_control/qc_store.cpp','quality_control/quality_control_page.cpp','calibration/calibration_store.cpp','calibration/calibration_page.cpp',
        'quality_control/qc_history_core.cpp','quality_control/qc_history_reply.h','quality_control/qc_history_presenter.cpp','quality/statistics.cpp')) {
        if ($required -notin $frozen.PSObject.Properties.Name) { throw ('Current source absent from freeze: ' + $required) }
    }
    $before = Get-FrozenSnapshot (Join-Path $work 'source_before.json')
    Assert-IdenticalSnapshot $frozen $before
    $tools = @($qmake,$make,(Join-Path $compilerBin 'g++.exe'),(Join-Path $compilerBin 'gcc.exe'),
        (Join-Path $compilerBin 'as.exe'),(Join-Path $compilerBin 'ld.exe'),(Join-Path $compilerBin 'objdump.exe'),
        (Join-Path $qtBin 'moc.exe'),(Join-Path $qtBin 'uic.exe'),(Join-Path $qtBin 'rcc.exe'),
        (Join-Path $qtBin 'Qt5Core.dll'),
        (Join-Path $compilerBin '../libexec/gcc/i686-w64-mingw32/7.3.0/cc1plus.exe'),
        (Join-Path $compilerBin '../libexec/gcc/i686-w64-mingw32/7.3.0/collect2.exe'))
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
        $makefileBefore = Get-FileRecord (Join-Path $directory 'Makefile')
        $freshStarted = [DateTime]::UtcNow.Ticks
        $stage.make_exit = Invoke-Logged $make @('-j2') $directory (Join-Path $work ($name+'_make.log')) 900
        if ($stage.make_exit -ne 0) { $result.stages += $stage; throw "Full make failed: $name" }
        $commands = @(Get-CompileCommands (Join-Path $work ($name+'_make.log')) $directory)
        $rules = @(Get-ObjectRules $directory)
        $compiledMap = @{}
        foreach ($command in $commands) { $compiledMap[$command.object] = $true }
        foreach ($rule in $rules) {
            if (!$compiledMap.ContainsKey($rule.object)) { throw "Fresh object not compiled: $($rule.object)" }
        }
        if ($commands.Count -ne $rules.Count) { throw "Object/compile command count mismatch: $name" }
        foreach ($command in $commands) {
            $rule = @($rules | Where-Object { $_.object -ceq $command.object })
            if ($rule.Count -ne 1 -or !$command.source -or $command.source -cnotin $rule[0].inputs) { throw ('Object/source dependency mismatch: '+$command.object) }
            if ($command.command -notmatch '(?:^| )-O2(?: |$)' -or $command.command -notmatch '(?:^| )-std=gnu\+\+1y(?: |$)|(?:^| )-std=c\+\+14(?: |$)' -or $command.command -notmatch '(?:^| )-DQT_NO_DEBUG(?: |$)') { throw ('Release/C++14 flags missing: '+$command.object) }
            if ((Get-Item -LiteralPath $command.object).LastWriteTimeUtc.Ticks -le $freshStarted) { throw ('Object predates fresh make: '+$command.object) }
        }
        if ($name -eq 'shell') {
            foreach ($requiredSource in @('quality_control/qc_store.cpp','quality_control/qc_history_core.cpp','quality_control/qc_history_presenter.cpp','quality_control/cus_quality_chart.cpp','quality_control/qualitychart.cpp','quality/statistics.cpp')) {
                $expectedSource=[IO.Path]::GetFullPath((Join-Path $sourceRoot $requiredSource)).Replace('\','/')
                if (@($commands | Where-Object { $_.source -ceq $expectedSource }).Count -ne 1) { throw ('Expected exactly one fresh QC source compile: '+$requiredSource) }
            }
        }
        $makefileAfter = Get-FileRecord (Join-Path $directory 'Makefile')
        if ($makefileBefore.sha256 -cne $makefileAfter.sha256) { throw 'Makefile changed during make' }
        $stage.makefile_before=$makefileBefore
        $stage.makefile_after=$makefileAfter
        Save-Json $commands (Join-Path $work ($name+'_compiled_commands.json'))
        Save-Json $rules (Join-Path $work ($name+'_object_rules.json'))
        $linkMap=Get-LinkObjectMap (Join-Path $work ($name+'_make.log')) $directory $rules $commands
        Save-Json $linkMap (Join-Path $work ($name+'_link_source_map.json'))
        $stage.link_source_map=$name+'_link_source_map.json'
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
    $nativeAfter = Assert-NativeProof
    if ($nativeAfter.sha256 -cne $nativeBinding.sha256) { throw 'Native verification changed during Windows build' }
    $result.native_verification_after = $nativeAfter
    if ((Get-FileRecord $preparationPath).sha256 -cne $preparationBinding.sha256) { throw 'Platform preparation changed during Windows build' }
    foreach ($input in $preparation.controller_inputs) {
        if ($input.sha256 -cne (Get-FileHash -LiteralPath (Join-Path $cohort $input.path) -Algorithm SHA256).Hash.ToLowerInvariant()) { throw ('Controller input changed during build: '+$input.path) }
    }
    $result.preparation_input=$preparationBinding
    Save-Json $preparation.controller_inputs (Join-Path $work 'controller_inputs_after.json')
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
