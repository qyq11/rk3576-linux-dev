$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = 'C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$sourceRoot = Join-Path $repo '06_programs/sn100_host'
$work = Join-Path $repo 'temp/sn100_host/o02_login/platforms/windows'
$nativeFreeze = Join-Path $repo 'temp/sn100_host/o02_login/native/after.json'
$qtBin = 'C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
$compilerBin = 'C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
$qmake = Join-Path $qtBin 'qmake.exe'
$make = Join-Path $compilerBin 'mingw32-make.exe'
. (Join-Path $PSScriptRoot 'file_helpers.ps1')
. (Join-Path $PSScriptRoot 'makefile_audit.ps1')

function Get-FrozenSnapshot($output) {
    $wslOutput = $output.Replace('\','/').Replace('C:/','/mnt/c/')
    $packageScript = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell/package_arm64.py'
    & wsl.exe -d Ubuntu-24.04 -u qyq -- python3 $packageScript snapshot --output $wslOutput
    if ($LASTEXITCODE -ne 0) { throw 'Source snapshot failed' }
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
    schema='sn100-login-windows-full-build-v1'; started_utc=[DateTime]::UtcNow.ToString('o')
    scope='Fresh Windows32 full main and report-renderer compile/link; no GUI, connection or deployment'
    reused_objects=0; published=$false; stages=@()
}
try {
    $env:PATH = "$compilerBin;$qtBin;" + $savedEnvironment['PATH']
    foreach ($name in @('QMAKESPEC','QMAKEPATH','QMAKEFEATURES','QT_PLUGIN_PATH','QML2_IMPORT_PATH')) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    $frozen = Get-Content -LiteralPath $nativeFreeze -Raw | ConvertFrom-Json
    $before = Get-FrozenSnapshot (Join-Path $work 'source_before.json')
    Assert-IdenticalSnapshot $frozen $before
    $tools = @($qmake,$make,(Join-Path $compilerBin 'g++.exe'),(Join-Path $compilerBin 'gcc.exe'),
        (Join-Path $compilerBin 'as.exe'),(Join-Path $compilerBin 'ld.exe'),
        (Join-Path $qtBin 'moc.exe'),(Join-Path $qtBin 'uic.exe'),(Join-Path $qtBin 'rcc.exe'),
        (Join-Path $qtBin 'Qt5Core.dll'))
    $toolBefore = @($tools | ForEach-Object { Get-FileRecord $_ })
    Save-Json $toolBefore (Join-Path $work 'toolchain_before.json')
    $toolBeforeMap = Get-Map $toolBefore
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
        if ($stage.qmake_exit -ne 0) { throw "qmake failed: $name" }
        Save-Json (Get-MakeFields $directory) (Join-Path $work ($name+'_flags.json'))
        foreach ($path in (Get-MakeExternalInputs $directory)) { $externalBefore[$path] = Get-FileRecord $path }
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
        Save-Json @(Get-BuildArtifacts $directory) (Join-Path $work ($name+'_artifacts.json'))
        $stage.compiled_objects = $commands.Count
        $stage.output = Join-Path $directory $target.output
        $stage.pe = Get-PeRecord $stage.output
        $stage.import_exit = Invoke-Logged (Join-Path $compilerBin 'objdump.exe') @('-p',$stage.output) $directory (Join-Path $work ($name+'_imports.log'))
        if ($stage.import_exit -ne 0) { throw "Import inspection failed: $name" }
        $stage.finished_utc = [DateTime]::UtcNow.ToString('o')
        $result.stages += $stage
        Save-Json $result (Join-Path $work 'verification.json')
    }
    $after = Get-FrozenSnapshot (Join-Path $work 'source_after.json')
    Assert-IdenticalSnapshot $before $after
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
    $publish = Join-Path $work 'final_release'
    New-Item -ItemType Directory -Path $publish | Out-Null
    foreach ($stage in $result.stages) { Copy-Item -LiteralPath $stage.output -Destination $publish }
    $result.outputs = @($result.stages | ForEach-Object { Get-PeRecord (Join-Path $publish ([IO.Path]::GetFileName($_.output))) })
    $result.source_snapshot_entries = @($before.PSObject.Properties).Count
    $result.source_matches_native_freeze = $true
    $result.sources_tools_external_inputs_unchanged = $true
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
