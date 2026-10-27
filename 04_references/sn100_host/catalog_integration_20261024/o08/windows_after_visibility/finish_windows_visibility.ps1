$ErrorActionPreference='Stop'
$workspace=(Resolve-Path (Join-Path $PSScriptRoot '../../../../')).Path
$program=Join-Path $workspace '06_programs/sn100_host'
$integration=Split-Path -Parent $PSScriptRoot
$logs=Join-Path $workspace 'temp/sn100_host/logs'
$original=Join-Path $PSScriptRoot 'o08'
$sealed=Join-Path $PSScriptRoot 'o08_visibility'
$qtBin='C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
$compilerBin='C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
$make=Join-Path $compilerBin 'mingw32-make.exe'
$expectedSampleSha='6E8FB2B95EEC1160A49D7C9499147C781E5CB44FACA8C342E0DB97853E5977CE'
$savedPath=$env:Path
$env:Path=$qtBin+';'+$compilerBin+';'+$savedPath

function Assert-SameInputs($Before,$After)
{
    if($Before.Count -ne $After.Count) {throw 'Input set size changed.'}
    $lookup=@{}
    foreach($entry in $After) {$lookup[$entry.path]=$entry.sha256}
    foreach($entry in $Before)
    {
        if(-not $lookup.ContainsKey($entry.path) -or $lookup[$entry.path] -cne $entry.sha256) {throw "Input changed during the recovery: $($entry.path)"}
    }
}

function Get-Flags([string]$BuildDirectory)
{
    return @(Get-Content -LiteralPath (Join-Path $BuildDirectory 'Makefile') | Where-Object {$_ -match '^(CC|CXX|DEFINES|CFLAGS|CXXFLAGS|INCPATH|LINKER|LFLAGS|LIBS)\s*='})
}

function Get-ObjectDifferences($Baseline)
{
    return @(foreach($target in $Baseline.targets)
    {
        $build=Join-Path $integration ('windows_'+$target.target)
        if(@(Get-ChildItem -LiteralPath (Join-Path $build '.obj') -File -Filter '*.o').Count -ne $target.objects.Count) {throw 'Object set changed.'}
        if(((Get-Flags $build) -join "`n") -cne ($target.flags -join "`n")) {throw 'Effective flags changed.'}
        foreach($entry in $target.objects)
        {
            $after=(Get-FileHash -LiteralPath (Join-Path (Join-Path $build '.obj') $entry.name) -Algorithm SHA256).Hash
            if($after -cne $entry.sha256) {[pscustomobject]@{target=$target.target;object=$entry.name;before_sha256=$entry.sha256;after_sha256=$after}}
        }
    })
}

try
{
    foreach($entry in Get-Content -LiteralPath (Join-Path $original 'sha256.json') -Raw | ConvertFrom-Json)
    {
        if((Get-FileHash -LiteralPath (Join-Path $original $entry.name) -Algorithm SHA256).Hash -cne $entry.sha256) {throw 'Original O08 seal changed.'}
    }
    $tokens=$null
    $errors=$null
    $ast=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $original 'build_windows.ps1'),[ref]$tokens,[ref]$errors)
    if($errors.Count) {throw 'Original helper functions cannot be parsed.'}
    foreach($name in @('Get-ProjectInputs','Get-PEMachine'))
    {
        $function=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
        . ([scriptblock]::Create($function.Extent.Text))
    }
    $previous=Get-Content -LiteralPath (Join-Path $sealed 'failure_visibility.json') -Raw | ConvertFrom-Json
    if($previous.status -ne 'failed' -or $previous.compile_exit -ne 0 -or $previous.link_exit -ne 2) {throw 'This recovery requires the preserved successful compile and failed target invocation.'}
    $baselinePath=Join-Path $PSScriptRoot 'visibility_baseline.json'
    if((Get-FileHash -LiteralPath $baselinePath -Algorithm SHA256).Hash -cne '57EFF2EB053BB0B04981FEE10D48BF108D1DCB941A69CFDF03601A34F92F7B5F') {throw 'Object/flag baseline changed.'}
    $baseline=Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
    $inputSnapshots=@{}
    foreach($target in $previous.targets)
    {
        if($target.qmake_exit -ne 0 -or -not $target.flags_unchanged) {throw 'The earlier qmake/flag gate did not pass.'}
        $build=Join-Path $integration ('windows_'+$target.target)
        $inputs=Get-Content -LiteralPath (Join-Path $sealed ($target.target+'_inputs_visibility.json')) -Raw | ConvertFrom-Json
        Assert-SameInputs $inputs (Get-ProjectInputs $build)
        $originalInputs=Get-Content -LiteralPath (Join-Path $original ($target.target+'_inputs_final.json')) -Raw | ConvertFrom-Json
        $lookup=@{}
        foreach($entry in $inputs) {$lookup[$entry.path]=$entry.sha256}
        if($originalInputs.Count -ne $inputs.Count) {throw 'Full-build input set changed.'}
        $differences=@(foreach($entry in $originalInputs)
        {
            if(-not $lookup.ContainsKey($entry.path)) {throw 'Full-build input path changed.'}
            if($lookup[$entry.path] -cne $entry.sha256) {[pscustomobject]@{path=$entry.path;before_sha256=$entry.sha256;after_sha256=$lookup[$entry.path]}}
        })
        if($target.target -eq 'shell')
        {
            if($differences.Count -ne 1 -or $differences[0].path -ine (Join-Path $program 'samples/sample_page.cpp') -or $differences[0].after_sha256 -cne $expectedSampleSha) {throw 'Source delta is not exactly the frozen cpp.'}
        }
        elseif($differences.Count) {throw 'Helper input changed.'}
        $inputSnapshots[$target.target]=$inputs
    }
    $objectsBefore=Get-ObjectDifferences $baseline
    if($objectsBefore.Count -ne 1 -or $objectsBefore[0].target -ne 'shell' -or $objectsBefore[0].object -ne 'sample_page.o') {throw 'The earlier compile did not change exactly one expected object.'}
    $compileLog=Join-Path $logs 'windows_catalog_visibility_shell_compile.log'
    $compileCommands=@(Get-Content -LiteralPath $compileLog | Where-Object {$_ -match '^g\+\+ -c '})
    if($compileCommands.Count -ne 1 -or $compileCommands[0] -notmatch 'sample_page\.cpp\s*$') {throw 'The earlier compile log is not exactly one sample_page cpp compilation.'}
    $failedLink=Join-Path $logs 'windows_catalog_visibility_shell_link.log'
    if((Get-Content -LiteralPath $failedLink -Raw) -notmatch "No rule to make target 'bin\\sn100_shell\.exe'") {throw 'The preserved failure is not the known Makefile target spelling issue.'}
    $shellBuild=Join-Path $integration 'windows_shell'
    $makefile=Get-Content -LiteralPath (Join-Path $shellBuild 'Makefile') -Raw
    $linkTarget=[regex]::Match($makefile,'(?m)^(bin/sn100_shell\.exe):').Groups[1].Value
    if($linkTarget -ne 'bin/sn100_shell.exe') {throw 'Exact declared forward-slash link target was not found.'}
    $mainExe=Join-Path $shellBuild 'bin/sn100_shell.exe'
    if(Test-Path -LiteralPath $mainExe) {throw 'Unexpected main executable already exists before recovery.'}
    $linkLog=Join-Path $logs 'windows_catalog_visibility_shell_link_recovery.log'
    Push-Location $shellBuild
    try
    {
        & $make '-j2' $linkTarget *> $linkLog
        $linkExit=$LASTEXITCODE
        if($linkExit -ne 0) {throw "Recovered main link failed: $linkExit"}
    }
    finally {Pop-Location}
    if(@(Get-Content -LiteralPath $linkLog | Where-Object {$_ -match '^g\+\+ -c '}).Count) {throw 'Unexpected compilation during recovered link.'}
    $objectsAfter=Get-ObjectDifferences $baseline
    if(($objectsBefore | ConvertTo-Json -Depth 5 -Compress) -cne ($objectsAfter | ConvertTo-Json -Depth 5 -Compress)) {throw 'An object changed during link recovery.'}
    $targets=@(foreach($target in $previous.targets)
    {
        $build=Join-Path $integration ('windows_'+$target.target)
        Assert-SameInputs $inputSnapshots[$target.target] (Get-ProjectInputs $build)
        $name=if($target.target -eq 'shell') {'sn100_shell.exe'} else {'sn100_report_renderer.exe'}
        $exe=Join-Path $build ('bin/'+$name)
        $sha=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
        if($target.target -eq 'renderer' -and $sha -cne $target.binary_baseline_sha256) {throw 'Unchanged helper binary differs from baseline.'}
        $architecture=Get-PEMachine $exe
        Copy-Item -LiteralPath $exe -Destination (Join-Path $sealed $name) -Force
        if((Get-FileHash -LiteralPath (Join-Path $sealed $name) -Algorithm SHA256).Hash -cne $sha) {throw 'Copied binary differs.'}
        [pscustomobject]@{target=$target.target;action=if($target.target -eq 'shell') {'One cpp rebuilt, main relinked'} else {'Retained unchanged; not recompiled'};input_count=$target.input_count;input_differences=$target.input_differences;flags_unchanged=$true;effective_flags=$target.effective_flags;qmake_exit=0;architecture=$architecture;bytes=(Get-Item -LiteralPath $exe).Length;sha256=$sha;baseline_binary_sha256=$target.binary_baseline_sha256}
    })
    $verification=[ordered]@{
        phase='O08 visibility repair'
        status='passed'
        baseline='Passed O08 full -B -j2 build, metadata and original pair preserved'
        strategy='Exact Makefile prerequisite -W forced one cpp; main relinked; identical helper inputs/flags/objects and actual binary retained'
        source_sha256=$expectedSampleSha
        compile_exit=0
        compile_commands=1
        compile_log=$compileLog
        first_link_invocation_exit=2
        first_link_failure='DESTDIR_TARGET variable used a backslash, whereas the actual make rule uses a forward slash; this was a temporary orchestration-script error, not a source/linker error'
        failed_link_log=$failedLink
        recovery_link_target=$linkTarget
        recovery_link_exit=$linkExit
        recovery_link_compile_commands=0
        recovery_link_log=$linkLog
        object_differences=$objectsAfter
        targets=$targets
        application_executed=$false
        board_connected=$false
    }
    Copy-Item -LiteralPath $baselinePath -Destination (Join-Path $sealed 'object_flags_baseline.json') -Force
    Copy-Item -LiteralPath (Join-Path $original 'toolchain.json') -Destination (Join-Path $sealed 'toolchain.json') -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build_windows_visibility.ps1') -Destination (Join-Path $sealed 'build_windows_visibility.ps1') -Force
    Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $sealed 'finish_windows_visibility.ps1') -Force
    $verification | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $sealed 'verification_visibility.json') -Encoding utf8
    @(Get-ChildItem -LiteralPath $sealed -File | Where-Object {$_.Name -ne 'sha256.json'} | Sort-Object Name | ForEach-Object {[pscustomobject]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}}) | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $sealed 'sha256.json') -Encoding utf8
    $targets | Select-Object target,architecture,bytes,sha256 | ConvertTo-Json
}
catch
{
    Write-Output ('WINDOWS_VISIBILITY_RECOVERY_FAILED: '+$_.Exception.Message)
    exit 1
}
finally {$env:Path=$savedPath}
