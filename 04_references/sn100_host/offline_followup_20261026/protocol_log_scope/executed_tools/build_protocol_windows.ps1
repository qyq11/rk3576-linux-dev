param([switch]$RootGo)
$ErrorActionPreference='Stop'
if (!$RootGo -or $PSVersionTable.PSVersion.Major -lt 7) {throw 'PS7 and explicit slot required'}
$repo='C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$program=Join-Path $repo '06_programs/sn100_host'
$out=Join-Path $repo 'temp/sn100_host/offline_followup_20261026/protocol_log_scope/windows'
if (Test-Path -LiteralPath $out) {throw 'Fresh output directory required'}
New-Item -ItemType Directory -Path $out | Out-Null
. (Join-Path $repo 'temp/sn100_host/qc_local_20261025/integration_drivers/windows/file_helpers.ps1')
$native=Get-Content -LiteralPath (Join-Path $repo 'temp/sn100_host/offline_followup_20261026/protocol_log_scope/verification.json') -Raw | ConvertFrom-Json
if (!$native.passed) {throw 'Completed native checks required'}
$before=[ordered]@{}
foreach ($property in $native.source_inputs.PSObject.Properties) {
    $actual=(Get-FileHash -LiteralPath (Join-Path $program $property.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $property.Value) {throw ('Native source changed: '+$property.Name)}
    $before[$property.Name]=$actual
}
Save-Json $before (Join-Path $out 'source_before.json')
$oldPath=$env:PATH
$result=[ordered]@{scope='Windows32 fresh protocol-soak compile only; no Windows process/connection/device execution';passed=$false;reused_objects=0;source_inputs=$before}
try {
    $qt='C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
    $cc='C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
    $env:PATH="$cc;$qt;$oldPath"
    $result.qmake_exit=Invoke-Logged (Join-Path $qt 'qmake.exe') @((Join-Path $program 'protocol/soak.pro'),'CONFIG+=release','CONFIG-=debug','CONFIG-=debug_and_release') $out (Join-Path $out 'qmake.log')
    if ($result.qmake_exit -ne 0) {throw 'Windows qmake failed'}
    $result.make_exit=Invoke-Logged (Join-Path $cc 'mingw32-make.exe') @('-j2') $out (Join-Path $out 'make.log')
    if ($result.make_exit -ne 0) {throw 'Windows make failed'}
    $binaries=@(Get-ChildItem -LiteralPath $out -Filter 'sn100_protocol_soak.exe' -File -Recurse)
    if ($binaries.Count -ne 1) {throw 'One fresh Windows binary required'}
    $result.binary=Get-PeRecord $binaries[0].FullName
    if ($result.binary.architecture -ne 'PE32 / Intel i386 / 32-bit Windows') {throw 'Wrong PE architecture'}
    foreach ($name in $before.Keys) {
        if ((Get-FileHash -LiteralPath (Join-Path $program $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $before[$name]) {throw ('Source changed during Windows compile: '+$name)}
    }
    $result.source_unchanged=$true
    $result.passed=$true
} finally {
    $env:PATH=$oldPath
    Save-Json $result (Join-Path $out 'verification.json')
}
$result | ConvertTo-Json -Depth 6
