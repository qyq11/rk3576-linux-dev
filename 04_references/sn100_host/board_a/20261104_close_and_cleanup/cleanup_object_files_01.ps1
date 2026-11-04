$ErrorActionPreference = 'Stop'
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$intendedTemp = Join-Path $workspaceRoot 'temp/sn100_host'
$manifestPath = Join-Path $PSScriptRoot 'object_cleanup_before_01.json'
$resultPath = Join-Path $PSScriptRoot 'object_cleanup_result_01.json'
if ((Test-Path -LiteralPath $manifestPath) -or (Test-Path -LiteralPath $resultPath)) { throw '保留已有清理原件，不重复执行' }
$specifications = @(
    @{Path='workflow_next_20261103/integration_11_01/arm64_build';Count=293},
    @{Path='workflow_next_20261103/integration_12_01/arm64_build';Count=293},
    @{Path='workflow_next_20261103/integration_14_01/arm64_build';Count=297},
    @{Path='workflow_next_20261103/integration_15_01/arm64_build';Count=297},
    @{Path='resume_dispatch_20261103/sample_checker15_exec_02_01/build_01';Count=1},
    @{Path='resume_dispatch_20261103/sample_checker15_board_a_exec_01_01/build_01';Count=1}
)
$records = [System.Collections.Generic.List[object]]::new()
foreach ($spec in $specifications) {
    $absoluteDirectory = [System.IO.Path]::GetFullPath((Join-Path $intendedTemp $spec.Path))
    if (-not $absoluteDirectory.StartsWith($intendedTemp + [System.IO.Path]::DirectorySeparatorChar,[System.StringComparison]::OrdinalIgnoreCase)) { throw '目录越出本次temp范围' }
    $directory = Get-Item -LiteralPath $absoluteDirectory
    if (-not $directory.PSIsContainer -or ($directory.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) { throw '清理目录不是真实目录' }
    $files = @(Get-ChildItem -LiteralPath $absoluteDirectory -Filter '*.o' -File)
    if ($files.Count -ne $spec.Count) { throw '对象数量与核定不符，未删除' }
    foreach ($file in $files) {
        $absoluteFile = [System.IO.Path]::GetFullPath($file.FullName)
        if ([System.IO.Path]::GetDirectoryName($absoluteFile) -ne $absoluteDirectory -or ($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) { throw '文件越界或为链接' }
        $header = [byte[]]::new(64)
        $stream = [System.IO.File]::OpenRead($absoluteFile)
        try { $read = $stream.Read($header,0,64) } finally { $stream.Dispose() }
        if ($read -ne 64 -or $header[0] -ne 127 -or $header[1] -ne 69 -or $header[2] -ne 76 -or $header[3] -ne 70 -or $header[4] -ne 2 -or $header[5] -ne 1 -or $header[16] -ne 1 -or $header[18] -ne 183 -or $header[19] -ne 0) { throw '并非已核ARM64可重定位对象，未删除' }
        $records.Add([pscustomobject]@{Path=$absoluteFile;Bytes=$file.Length;SHA256=(Get-FileHash -LiteralPath $absoluteFile -Algorithm SHA256).Hash.ToLowerInvariant()})
    }
}
if ($records.Count -ne 1182) { throw '清理总数不符' }
$manifest = @{Scope='Only six explicitly named build directories and only ARM64 .o files; no recursive directory deletion';UserRequested=$true;Files=$records;AllFourBuildLocksHeldExternally=$true}
[System.IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
$removed = 0
$bytes = [long]0
foreach ($record in $records) {
    $existing = Get-Item -LiteralPath $record.Path
    if ($existing.Length -ne $record.Bytes -or ($existing.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) { throw '删除前实物变化，保留剩余文件' }
    Remove-Item -LiteralPath $record.Path -ErrorAction Stop
    if (Test-Path -LiteralPath $record.Path) { throw '删除后文件仍在' }
    $removed++
    $bytes += $record.Bytes
}
$result = @{DeletedFiles=$removed;DeletedBytes=$bytes;ManifestSHA256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant();SourceAndEvidenceDeleted=$false;RecursiveDeletion=$false;Preserved='Source, candidate revisions, logs, provenance, sysroot and all formal backups';NeedsFreshBuildBeforeObjectReuse=$true}
[System.IO.File]::WriteAllText($resultPath,($result|ConvertTo-Json -Depth 6),[System.Text.UTF8Encoding]::new($false))
$result | ConvertTo-Json -Depth 6
