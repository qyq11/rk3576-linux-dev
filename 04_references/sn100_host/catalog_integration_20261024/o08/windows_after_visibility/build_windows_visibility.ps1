param([Parameter(Mandatory=$true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedSampleSha)
$ErrorActionPreference='Stop'
$workspace=(Resolve-Path (Join-Path $PSScriptRoot '../../../../')).Path
$program=Join-Path $workspace '06_programs/sn100_host'
$integration=Split-Path -Parent $PSScriptRoot
$logs=Join-Path $workspace 'temp/sn100_host/logs'
$original=Join-Path $PSScriptRoot 'o08'
$sealed=Join-Path $PSScriptRoot 'o08_visibility'
$qtBin='C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
$compilerBin='C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
$qmake=Join-Path $qtBin 'qmake.exe'
$make=Join-Path $compilerBin 'mingw32-make.exe'
$sampleSource=Join-Path $program 'samples/sample_page.cpp'
$sampleTarget='.obj/sample_page.o'
$samplePrerequisite='../../../../06_programs/sn100_host/samples/sample_page.cpp'
$savedPath=$env:Path
$env:Path=$qtBin+';'+$compilerBin+';'+$savedPath
$result=[ordered]@{phase='O08 visibility repair';status='started';base='Passed O08 full -B -j2 build';build_strategy='One content-verified cpp object forced through its exact Makefile prerequisite and target; main relinked; helper retained after identical-input/flags/binary checks';application_executed=$false;board_connected=$false;targets=@()}

function Get-Flags([string]$BuildDirectory)
{
    return @(Get-Content -LiteralPath (Join-Path $BuildDirectory 'Makefile') | Where-Object {$_ -match '^(CC|CXX|DEFINES|CFLAGS|CXXFLAGS|INCPATH|LINKER|LFLAGS|LIBS)\s*='})
}

function Compare-Inputs($Before,$After)
{
    if($Before.Count -ne $After.Count) {throw 'Formal input set size changed.'}
    $lookup=@{}
    foreach($entry in $After) {$lookup[$entry.path]=$entry.sha256}
    return @(foreach($entry in $Before)
    {
        if(-not $lookup.ContainsKey($entry.path)) {throw "Formal input path changed: $($entry.path)"}
        if($lookup[$entry.path] -cne $entry.sha256)
        {
            [pscustomobject]@{path=$entry.path;before_sha256=$entry.sha256;after_sha256=$lookup[$entry.path]}
        }
    })
}

function Assert-OriginalSeal
{
    foreach($entry in Get-Content -LiteralPath (Join-Path $original 'sha256.json') -Raw | ConvertFrom-Json)
    {
        if((Get-FileHash -LiteralPath (Join-Path $original $entry.name) -Algorithm SHA256).Hash -cne $entry.sha256) {throw "Original O08 sealed evidence changed: $($entry.name)"}
    }
}

try
{
    Assert-OriginalSeal
    # Reuse only these exact functions from the SHA-verified full-build script.
    $tokens=$null
    $errors=$null
    $ast=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $original 'build_windows.ps1'),[ref]$tokens,[ref]$errors)
    if($errors.Count -ne 0) {throw 'Original build script cannot be parsed.'}
    foreach($name in @('Get-ProjectInputs','Get-PEMachine'))
    {
        $function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
        if(-not $function) {throw "Missing sealed build function: $name"}
        . ([scriptblock]::Create($function.Extent.Text))
    }
    $baselinePath=Join-Path $PSScriptRoot 'visibility_baseline.json'
    if((Get-FileHash -LiteralPath $baselinePath -Algorithm SHA256).Hash -cne '57EFF2EB053BB0B04981FEE10D48BF108D1DCB941A69CFDF03601A34F92F7B5F') {throw 'Original object/flag baseline changed.'}
    $baseline=Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
    if((Get-FileHash -LiteralPath $sampleSource -Algorithm SHA256).Hash -ine $ExpectedSampleSha) {throw 'Sample source differs from the explicitly frozen revision.'}
    New-Item -ItemType Directory -Force $sealed | Out-Null
    foreach($name in @('sn100_shell.exe','sn100_report_renderer.exe'))
    {
        $path=Join-Path $sealed $name
        if(Test-Path -LiteralPath $path) {Remove-Item -LiteralPath $path}
    }
    $currentInputs=@{}
    foreach($target in @(
        @{name='shell';project='shell/shell.pro';executable='sn100_shell.exe'},
        @{name='renderer';project='history/renderer.pro';executable='sn100_report_renderer.exe'}))
    {
        $build=Join-Path $integration ('windows_'+$target.name)
        $baseTarget=@($baseline.targets | Where-Object {$_.target -eq $target.name})[0]
        if(((Get-Flags $build) -join "`n") -cne ($baseTarget.flags -join "`n")) {throw "Flags changed before qmake: $($target.name); full rebuild required."}
        foreach($entry in $baseTarget.objects)
        {
            if((Get-FileHash -LiteralPath (Join-Path (Join-Path $build '.obj') $entry.name) -Algorithm SHA256).Hash -cne $entry.sha256) {throw "Object tree changed before incremental build: $($target.name)/$($entry.name)"}
        }
        $exe=Join-Path (Join-Path $build 'bin') $target.executable
        $baseExe=Get-FileHash -LiteralPath (Join-Path $original $target.executable) -Algorithm SHA256
        if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -cne $baseExe.Hash) {throw 'Working executable no longer matches the full-build baseline.'}
        $qmakeLog=Join-Path $logs ('windows_catalog_visibility_'+$target.name+'_qmake.log')
        Push-Location $build
        try
        {
            & $qmake '-nocache' (Join-Path $program $target.project) '-spec' 'win32-g++' 'CONFIG+=release' 'CONFIG-=debug' 'CONFIG-=debug_and_release' ('DESTDIR='+(Join-Path $build 'bin').Replace('\','/')) *> $qmakeLog
            $exitCode=$LASTEXITCODE
            if($exitCode -ne 0) {throw "qmake failed: $($target.name), $exitCode"}
        }
        finally {Pop-Location}
        $flags=Get-Flags $build
        if(($flags -join "`n") -cne ($baseTarget.flags -join "`n")) {throw "Effective flags changed after qmake: $($target.name); full rebuild required."}
        $inputs=Get-ProjectInputs $build
        $previous=Get-Content -LiteralPath (Join-Path $original ($target.name+'_inputs_final.json')) -Raw | ConvertFrom-Json
        $differences=Compare-Inputs $previous $inputs
        if($target.name -eq 'shell')
        {
            if($differences.Count -ne 1 -or $differences[0].path -ine $sampleSource -or $differences[0].after_sha256 -ine $ExpectedSampleSha) {throw 'The main input change is not exactly the frozen sample_page.cpp.'}
        }
        elseif($differences.Count -ne 0) {throw 'Helper inputs changed; helper must be rebuilt.'}
        $inputs | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $sealed ($target.name+'_inputs_visibility.json')) -Encoding utf8
        $currentInputs[$target.name]=$inputs
        $result.targets+=,[ordered]@{target=$target.name;qmake_exit=$exitCode;input_count=$inputs.Count;input_differences=$differences;flags_unchanged=$true;effective_flags=$flags;binary_baseline_sha256=$baseExe.Hash;qmake_log=$qmakeLog}
    }
    $shellBuild=Join-Path $integration 'windows_shell'
    $makefile=Get-Content -LiteralPath (Join-Path $shellBuild 'Makefile') -Raw
    if($makefile -notmatch '(?m)^\.obj/sample_page\.o:\s+\.\./\.\./\.\./\.\./06_programs/sn100_host/samples/sample_page\.cpp\s') {throw 'Exact Makefile object target/prerequisite pair changed.'}
    $linkTarget=([regex]::Match($makefile,'(?m)^DESTDIR_TARGET\s*=\s*([^\r\n]+)').Groups[1].Value).Trim()
    if($linkTarget -notin @('bin\sn100_shell.exe','bin/sn100_shell.exe')) {throw 'Unexpected exact main link target.'}
    $result.make_object_target=$sampleTarget
    $result.make_forced_prerequisite=$samplePrerequisite
    $result.make_link_target=$linkTarget
    $compileLog=Join-Path $logs 'windows_catalog_visibility_shell_compile.log'
    $linkLog=Join-Path $logs 'windows_catalog_visibility_shell_link.log'
    Push-Location $shellBuild
    try
    {
        # GNU make -W forces this declared prerequisite regardless of mtime.
        & $make '-j2' '-W' $samplePrerequisite $sampleTarget *> $compileLog
        $result.compile_exit=$LASTEXITCODE
        if($result.compile_exit -ne 0) {throw "Exact object compilation failed: $($result.compile_exit)"}
        $commands=@(Get-Content -LiteralPath $compileLog | Where-Object {$_ -match '^g\+\+ -c '})
        if($commands.Count -ne 1 -or $commands[0] -notmatch 'sample_page\.cpp\s*$') {throw 'The exact-object log does not contain exactly one sample_page compilation.'}
        $mainExe=Join-Path $shellBuild 'bin/sn100_shell.exe'
        Remove-Item -LiteralPath $mainExe
        & $make '-j2' $linkTarget *> $linkLog
        $result.link_exit=$LASTEXITCODE
        if($result.link_exit -ne 0) {throw "Main relink failed: $($result.link_exit)"}
        if(@(Get-Content -LiteralPath $linkLog | Where-Object {$_ -match '^g\+\+ -c '}).Count -ne 0) {throw 'Unexpected extra compilation during main relink.'}
    }
    finally {Pop-Location}
    $objectDifferences=@()
    foreach($target in $baseline.targets)
    {
        $build=Join-Path $integration ('windows_'+$target.target)
        if(((Get-Flags $build) -join "`n") -cne ($target.flags -join "`n")) {throw 'Effective flags changed during the incremental build.'}
        if(@(Get-ChildItem -LiteralPath (Join-Path $build '.obj') -File -Filter '*.o').Count -ne $target.objects.Count) {throw 'Object set changed during the incremental build.'}
        $actual=Get-ProjectInputs $build
        if((Compare-Inputs $currentInputs[$target.target] $actual).Count -ne 0) {throw 'Source inputs changed during the incremental build.'}
        foreach($entry in $target.objects)
        {
            $after=(Get-FileHash -LiteralPath (Join-Path (Join-Path $build '.obj') $entry.name) -Algorithm SHA256).Hash
            if($after -cne $entry.sha256) {$objectDifferences+=,[pscustomobject]@{target=$target.target;object=$entry.name;before_sha256=$entry.sha256;after_sha256=$after}}
        }
    }
    if($objectDifferences.Count -ne 1 -or $objectDifferences[0].target -ne 'shell' -or $objectDifferences[0].object -ne 'sample_page.o') {throw 'Object SHA differences are not exactly the repaired cpp object.'}
    $result.object_differences=$objectDifferences
    $result.compile_log=$compileLog
    $result.link_log=$linkLog
    foreach($target in $result.targets)
    {
        $name=if($target.target -eq 'shell') {'sn100_shell.exe'} else {'sn100_report_renderer.exe'}
        $binary=Join-Path (Join-Path $integration ('windows_'+$target.target)) ('bin/'+$name)
        $target.architecture=Get-PEMachine $binary
        $target.bytes=(Get-Item -LiteralPath $binary).Length
        $target.sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
        if($target.target -eq 'renderer' -and $target.sha256 -cne $target.binary_baseline_sha256) {throw 'Unchanged helper binary SHA differs from full-build baseline.'}
        $target.action=if($target.target -eq 'shell') {'One cpp rebuilt and main relinked'} else {'Retained unchanged; no helper compilation executed'}
        Copy-Item -LiteralPath $binary -Destination (Join-Path $sealed $name) -Force
        if((Get-FileHash -LiteralPath (Join-Path $sealed $name) -Algorithm SHA256).Hash -cne $target.sha256) {throw 'Sealed pair binary mismatch.'}
        Write-Output ($target.target+': '+$target.action+'; '+$target.architecture+'; SHA256='+$target.sha256)
    }
    Assert-OriginalSeal
    foreach($target in $result.targets)
    {
        $actual=Get-ProjectInputs (Join-Path $integration ('windows_'+$target.target))
        if((Compare-Inputs $currentInputs[$target.target] $actual).Count -ne 0) {throw 'Source inputs changed before final pair sealing.'}
    }
    Copy-Item -LiteralPath $baselinePath -Destination (Join-Path $sealed 'object_flags_baseline.json') -Force
    Copy-Item -LiteralPath (Join-Path $original 'toolchain.json') -Destination (Join-Path $sealed 'toolchain.json') -Force
    Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $sealed 'build_windows_visibility.ps1') -Force
    $result.status='passed'
    $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $sealed 'verification_visibility.json') -Encoding utf8
    @(Get-ChildItem -LiteralPath $sealed -File | Where-Object {$_.Name -ne 'sha256.json'} | Sort-Object Name | ForEach-Object {[pscustomobject]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}}) | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $sealed 'sha256.json') -Encoding utf8
}
catch
{
    $result.status='failed'
    $result.failure=$_.Exception.Message
    New-Item -ItemType Directory -Force $sealed | Out-Null
    $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $sealed 'failure_visibility.json') -Encoding utf8
    Write-Output ('WINDOWS_VISIBILITY_FAILED: '+$_.Exception.Message)
    exit 1
}
finally {$env:Path=$savedPath}
