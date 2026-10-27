$ErrorActionPreference='Stop'
$repo='C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$sourceRoot=Join-Path $repo 'temp/sn100_host/instrument_20261024/source/06_programs/sn100_host'
$work=Join-Path $repo 'temp/sn100_host/instrument_20261024/windows'
. (Join-Path $work 'incremental_helpers.ps1')
. (Join-Path $work 'incremental_contract.ps1')
$contractDirectory=Join-Path $work 'initial_contract'
if (Test-Path -LiteralPath (Join-Path $contractDirectory 'contract.json')) { throw 'Initial contract already recorded' }
$initialResult=Get-Content -LiteralPath (Join-Path $work 'windows_result.json') -Raw | ConvertFrom-Json
if (!$initialResult.published -or $initialResult.exit_code -ne 0) { throw 'Initial pair did not pass' }
$targets=@()
$external=@{}
foreach ($name in @('shell','renderer')) {
    $directory=Join-Path $work ($name+'_build')
    Copy-Item -LiteralPath (Join-Path $directory 'Makefile') -Destination (Join-Path $contractDirectory ($name+'_Makefile'))
    $rules=@(Get-ObjectRules $directory)
    $artifacts=@(Get-BuildArtifacts $directory)
    Save-Json $rules (Join-Path $contractDirectory ($name+'_object_rules.json'))
    Save-Json $artifacts (Join-Path $contractDirectory ($name+'_artifacts.json'))
    foreach ($path in (Get-MakeExternalInputs $directory)) { $external[$path]=$true }
    $targets += [ordered]@{name=$name;directory=$directory;fields=(Get-MakeFields $directory);object_count=$rules.Count;artifact_count=$artifacts.Count}
}
$externalRecords=@($external.Keys | Sort-Object | ForEach-Object { Get-FileRecord $_ })
Save-Json $externalRecords (Join-Path $contractDirectory 'external_inputs.json')
Save-Json ([ordered]@{schema='sn100-windows-incremental-initial-v1';recorded_utc=[DateTime]::UtcNow.ToString('o');source_root=$sourceRoot;targets=$targets;external_inputs=$externalRecords.Count;initial_result_sha256=(Get-FileRecord (Join-Path $work 'windows_result.json')).sha256;scope='Read-only contract capture after successful first pair. No qmake/make, source write, GUI or device execution.'}) (Join-Path $contractDirectory 'contract.json')
Write-Output ('Initial incremental contract recorded; external inputs='+$externalRecords.Count)
