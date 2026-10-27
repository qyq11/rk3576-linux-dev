param([switch]$RootGo)
$ErrorActionPreference = 'Stop'
if (!$RootGo) { throw 'Root must confirm diagnostic processes/source writers have stopped' }
$repo = 'C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$taskBase = (Resolve-Path -LiteralPath (Join-Path $repo 'temp/sn100_host/qc_local_20261025')).Path
$program = (Resolve-Path -LiteralPath (Join-Path $repo '06_programs/sn100_host')).Path
$prefix = $taskBase + [IO.Path]::DirectorySeparatorChar
$names = @(
    @('native', 'native_pre_windows_include_01'),
    @('cumulative_build', 'cumulative_build_pre_windows_include_01'),
    @('standalone_final', 'standalone_pre_windows_include_01'),
    @('projects_modules_final', 'projects_modules_pre_windows_include_01'),
    @('platforms/windows', 'platforms/windows_pre_include_01')
)
$moves = @()
foreach ($pair in $names) {
    $from = [IO.Path]::GetFullPath((Join-Path $taskBase $pair[0]))
    $to = [IO.Path]::GetFullPath((Join-Path $taskBase $pair[1]))
    foreach ($path in @($from, $to)) {
        if (!$path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escapes task root' }
    }
    if (!(Test-Path -LiteralPath $from -PathType Container) -or (Test-Path -LiteralPath $to)) { throw 'Expected existing source and fresh destination' }
    if ((Get-Item -LiteralPath $from).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Do not move a linked tree' }
    $moves += [ordered]@{original_directory=$from;preserved_directory=$to;completed=$false}
}
$receipt = Join-Path $taskBase 'pre_windows_include_relocation.json'
if (Test-Path -LiteralPath $receipt) { throw 'Preserve prior relocation receipt' }
$native = Get-Content -LiteralPath (Join-Path $taskBase 'native/verification.json') -Raw | ConvertFrom-Json
if (!$native.passed -or $native.snapshot_sha256 -ne '57f3b03290cc3635a5772a7169d22c1713f164186ab3b368028a0f35f8b710ed') { throw 'Expected completed57f native checkpoint' }
foreach ($entry in $native.artifacts.PSObject.Properties) {
    $path = Join-Path $taskBase ('native/wsl_release/' + $entry.Name)
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) { throw 'Historical native executable changed' }
}
foreach ($entry in $native.fresh_objects.PSObject.Properties) {
    $path = Join-Path $taskBase ('native/' + $entry.Name)
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value.sha256) { throw 'Historical native object changed' }
}
$binding = Join-Path $taskBase 'cumulative_build'
$proof = Get-Content -LiteralPath (Join-Path $binding 'binding_proof.json') -Raw | ConvertFrom-Json
if ($proof.snapshot_sha256 -ne $native.snapshot_sha256) { throw 'Historical binding differs' }
foreach ($entry in $proof.drivers.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $binding $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value.binary_sha256) { throw 'Historical test executable changed' }
    $source = Join-Path $program $entry.Value.source_relative
    $expected = $entry.Value.source_sha256
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Executed test source changed' }
    $target = Join-Path $binding ($entry.Name + '_executed.cpp')
    if (Test-Path -LiteralPath $target) {
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Preserve differing source image' }
    } else { Copy-Item -LiteralPath $source -Destination $target }
}
foreach ($pair in @(
    @('temp/sn100_host/qc_local_20261025/integration_drivers/run_suite.py', 'run_suite_executed.py'),
    @('temp/sn100_host/qc_local_20261025/integration_drivers/build_test_drivers.py', 'build_test_drivers_executed.py'),
    @('06_programs/sn100_host/tests/build_shell_drivers.py', 'build_shell_drivers_executed.py')
)) {
    $source = Join-Path $repo $pair[0]
    $target = Join-Path $binding $pair[1]
    if (Test-Path -LiteralPath $target) { throw 'Do not overwrite executed driver snapshot' }
    Copy-Item -LiteralPath $source -Destination $target
}
foreach ($pair in @(
    @('freeze_and_build.py', 'native/freeze_and_build_executed.py'),
    @('run_standalone.py', 'standalone_final/run_standalone_executed.py'),
    @('run_projects_modules.py', 'projects_modules_final/run_projects_modules_executed.py'),
    @('common.py', 'projects_modules_final/common_executed.py')
)) {
    $source = Join-Path $taskBase ('integration_drivers/' + $pair[0])
    $target = Join-Path $taskBase $pair[1]
    if (Test-Path -LiteralPath $target) { throw 'Do not overwrite historical driver source' }
    Copy-Item -LiteralPath $source -Destination $target
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) { throw 'Copied historical driver differs' }
}
$record = [ordered]@{
    reason='Preserve completed57f WSL checks and original failed Windows build before explicit QVariant include compatibility freeze'
    source_manifest_sha256=$native.snapshot_sha256
    original_proof_argv_and_exits_rewritten=$false
    windows_scope='Original failed build plus subsequent source-changing diagnostic; no uniform tree freeze or final build success claimed'
    started_utc=[DateTime]::UtcNow.ToString('o');moves=$moves
}
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receipt -Encoding utf8
foreach ($move in $moves) {
    Move-Item -LiteralPath $move.original_directory -Destination $move.preserved_directory
    $move.completed=$true
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receipt -Encoding utf8
}
$record.finished_utc=[DateTime]::UtcNow.ToString('o')
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receipt -Encoding utf8
[PSCustomObject]@{preserved_directories=$moves.Count;exact_test_source_images=@($proof.drivers.PSObject.Properties).Count;receipt=$receipt} | ConvertTo-Json
