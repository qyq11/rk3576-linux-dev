param([string]$Output)
$ErrorActionPreference='Stop'
$repo='C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$sourceRoot=Join-Path $repo '06_programs/sn100_host'
$drivers=Split-Path $PSScriptRoot -Parent
. (Join-Path $drivers 'windows/file_helpers.ps1')
. (Join-Path $drivers 'windows/makefile_audit.ps1')
function Save($name,$value){$value|ConvertTo-Json -Depth 30|Set-Content -LiteralPath (Join-Path $Output $name) -Encoding utf8}
$cases=@()
function Case($name,[scriptblock]$action,[bool]$success,[string]$expectedError=''){
    $returned=$null;$threw=$false;$message=$null
    try{$returned=&$action}catch{$threw=$true;$message=$_.Exception.Message}
    $case=[ordered]@{name=$name;expected_success=$success;parser_returned=(!$threw);exception=$message;expected_error=$expectedError;kind='in-process parser call, not an OS exit receipt'}
    Save ($name+'.parser_result.json') $case
    $script:cases+=$case
    if($success-eq $threw -or (!$success -and $message-notmatch $expectedError)){throw ('Parser case expectation failed: '+$name+' '+$message)}
    return $returned
}
$fixtures=@(
    @{name='current03_main';base='temp/sn100_host/qc_history_integration_20261026/platforms/windows_03';target='shell';exe='sn100_shell.exe'},
    @{name='historical426_main';base='temp/sn100_host/qc_local_20261025/platforms/windows';target='shell';exe='sn100_shell.exe'},
    @{name='historical426_renderer';base='temp/sn100_host/qc_local_20261025/platforms/windows';target='renderer';exe='sn100_report_renderer.exe'}
)
foreach($fixture in $fixtures){
    $base=Join-Path $repo $fixture.base
    $directory=Join-Path $base ($fixture.target+'_build')
    $log=Join-Path $base ($fixture.target+'_make.log')
    $rules=@(Get-ObjectRules $directory)
    $commands=@(Get-CompileCommands $log $directory)
    $savedRules=@(Get-Content -LiteralPath (Join-Path $base ($fixture.target+'_object_rules.json')) -Raw|ConvertFrom-Json)
    $savedCommands=@(Get-Content -LiteralPath (Join-Path $base ($fixture.target+'_compiled_commands.json')) -Raw|ConvertFrom-Json)
    if($rules.Count-ne $savedRules.Count -or $commands.Count-ne $savedCommands.Count){throw 'Actual Makefile/compile roster count changed'}
    for($n=0;$n-lt $rules.Count;++$n){
        if($rules[$n].object-cne $savedRules[$n].object -or ($rules[$n].inputs -join "`n")-cne ($savedRules[$n].inputs -join "`n")){throw 'Actual Makefile rule differs from preserved mapping'}
    }
    for($n=0;$n-lt $commands.Count;++$n){
        if($commands[$n].object-cne $savedCommands[$n].object -or $commands[$n].source-cne $savedCommands[$n].source -or $commands[$n].command-cne $savedCommands[$n].command){throw 'Actual compile source/command differs from preserved mapping'}
    }
    Save ($fixture.name+'.function_call.json') ([ordered]@{function='Get-LinkObjectMap';log=$log;directory=$directory;expected_target=$fixture.exe;Makefile=(Get-FileRecord (Join-Path $directory 'Makefile'));raw_log=(Get-FileRecord $log);rule_count=$rules.Count;compile_count=$commands.Count})
    $map=Case $fixture.name {Get-LinkObjectMap $log $directory $rules $commands $fixture.exe} $true
    Save ($fixture.name+'.link_source_map.json') $map
    if($map.objects.Count-ne $rules.Count){throw 'Linked object count differs from all expected rules'}
    if($fixture.name-eq 'current03_main'){
        $mainRules=$rules;$mainCommands=$commands;$mainDirectory=$directory;$mainLog=$log;$mainMap=$map
    }
}
$originalLines=@(Get-Content -LiteralPath $mainLog)
$link=$mainMap.command
$duplicate=Join-Path $Output 'negative_duplicate.log'
($originalLines+@($link))|Set-Content -LiteralPath $duplicate -Encoding utf8
Case 'negative_duplicate' {Get-LinkObjectMap $duplicate $mainDirectory $mainRules $mainCommands 'sn100_shell.exe'} $false 'Expected one actual final linker command'|Out-Null
$missing=Join-Path $Output 'negative_missing.log'
@($originalLines|Where-Object{$_-cne $link})|Set-Content -LiteralPath $missing -Encoding utf8
Case 'negative_missing' {Get-LinkObjectMap $missing $mainDirectory $mainRules $mainCommands 'sn100_shell.exe'} $false 'Expected one actual final linker command'|Out-Null
$preprocessor=Join-Path $Output 'negative_actual_preprocessor_only.log'
$predefs=@($originalLines|Where-Object{$_-match '^g\+\+\s' -and $_-match '(?:^| )-E(?: |$)'})
if($predefs.Count-ne 1){throw 'Expected one preserved actual preprocessor command in current03 log'}
$predefs|Set-Content -LiteralPath $preprocessor -Encoding utf8
Case 'negative_actual_preprocessor_only' {Get-LinkObjectMap $preprocessor $mainDirectory $mainRules $mainCommands 'sn100_shell.exe'} $false 'Expected one actual final linker command'|Out-Null
foreach($mode in @('-c','-E','-S','-M','-MM','-fsyntax-only')){
    $name='negative_mode_'+$mode.TrimStart('-')
    $path=Join-Path $Output ($name+'.log')
    $link.Replace('g++ ','g++ '+$mode+' ')|Set-Content -LiteralPath $path -Encoding utf8
    Case $name {Get-LinkObjectMap $path $mainDirectory $mainRules $mainCommands 'sn100_shell.exe'} $false 'Expected one actual final linker command'|Out-Null
}
Case 'negative_wrong_target' {Get-LinkObjectMap $mainLog $mainDirectory $mainRules $mainCommands 'sn100_report_renderer.exe'} $false 'Expected one actual final linker command'|Out-Null
$shortRules=@($mainRules|Select-Object -Skip 1)
Case 'negative_object_set_mismatch' {Get-LinkObjectMap $mainLog $mainDirectory $shortRules $mainCommands 'sn100_shell.exe'} $false 'Actual linked objects differ from exactly this target fresh rules'|Out-Null
Save 'verification.json' ([ordered]@{passed=$true;positive_fixtures=$fixtures.Count;parser_cases=$cases.Count;cases=$cases;actual_unique_link=$mainMap.command;current03_linked_objects=$mainMap.objects.Count;current03_all_rule_objects=$mainRules.Count;object_consistency_guard_unchanged=$true;no_native_helper_loaded=$true;no_qmake_gcc_make_or_application_executed=$true})
Write-Output ('Parser cases passed: '+$cases.Count)
