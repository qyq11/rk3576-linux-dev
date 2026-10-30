$ErrorActionPreference = 'Stop'
$manifestPath = Join-Path (Get-Location).Path '04_references/sn100_host/cleanup_20261030/cleanup_manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$workspace = [IO.Path]::GetFullPath((Get-Location).Path)
if ($workspace -ne $manifest.root) { throw 'Workspace differs from the checked cleanup manifest.' }
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $workspace 'temp')) + [IO.Path]::DirectorySeparatorChar
$checkedFiles = [Collections.Generic.List[object]]::new()
foreach ($entry in $manifest.files) {
    $absolute = [IO.Path]::GetFullPath((Join-Path $workspace $entry.path))
    if (-not $absolute.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "Outside temp: $absolute" }
    $item = Get-Item -LiteralPath $absolute -Force
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Unexpected directory or link: $absolute" }
    if ($item.Length -ne $entry.bytes) { throw "File size changed: $absolute" }
    $checkedFiles.Add([pscustomobject]@{ Absolute = $absolute; Entry = $entry })
}
Write-Output "Validated $($checkedFiles.Count) file paths inside $allowedRoot"
$deletedCount = 0
$deletedBytes = [long]0
foreach ($checked in $checkedFiles) {
    Remove-Item -LiteralPath $checked.Absolute -Force
    $deletedCount++
    $deletedBytes += $checked.Entry.bytes
    if (($deletedCount % 5000) -eq 0) { Write-Output "Deleted $deletedCount generated files" }
}
$remaining = @($checkedFiles | Where-Object { Test-Path -LiteralPath $_.Absolute })
$result = [ordered]@{
    date = '2026-10-30'
    deleted_files = $deletedCount
    deleted_bytes = $deletedBytes
    remaining_candidates = $remaining.Count
    deletion_method = 'Remove-Item -LiteralPath; individual files only; all absolute paths verified within project temp'
    preserved = @('formal source', 'reference originals', 'build dependencies and sysroots', 'runtime data', 'verification evidence', 'formal backups')
    product_build_or_rk_run = $false
}
$resultPath = Join-Path $workspace '04_references/sn100_host/cleanup_20261030/result.json'
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding utf8NoBOM
$result | ConvertTo-Json -Depth 5
if ($remaining.Count -ne 0) { throw 'Some cleanup candidates still exist.' }
