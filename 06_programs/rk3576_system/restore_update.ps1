param([Parameter(Mandatory = $true)][string]$ArchiveDirectory)
$ErrorActionPreference = 'Stop'

# 从正式压缩归档恢复烧录文件；校验通过后才改为update.img，不覆盖已有镜像。
$archiveRoot = (Resolve-Path -LiteralPath $ArchiveDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $archiveRoot 'image_manifest.json') -Raw | ConvertFrom-Json
$packed = Join-Path $archiveRoot 'update.img.gz'
$output = Join-Path $archiveRoot 'update.img'
if (Test-Path -LiteralPath $output) {
    if ((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.update_img.sha256) {
        throw '已有update.img与归档指纹不同，已停止，请先核对文件。'
    }
    Write-Output "现有镜像指纹一致：$output"
    return
}
if ((Get-FileHash -LiteralPath $packed -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.compressed.sha256) {
    throw '压缩包指纹不一致，已停止，请重新取得完整归档。'
}
$partial = Join-Path $archiveRoot 'update.img.partial'
$sourceStream = [System.IO.File]::OpenRead($packed)
try {
    $gzipStream = [System.IO.Compression.GZipStream]::new($sourceStream, [System.IO.Compression.CompressionMode]::Decompress)
    try {
        $destinationStream = [System.IO.File]::Open($partial, [System.IO.FileMode]::CreateNew)
        try { $gzipStream.CopyTo($destinationStream) } finally { $destinationStream.Dispose() }
    } finally { $gzipStream.Dispose() }
} finally { $sourceStream.Dispose() }
if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.update_img.sha256) {
    throw '解压后的指纹不一致，保留update.img.partial供核查，未生成烧录文件。'
}
Move-Item -LiteralPath $partial -Destination $output
Write-Output "镜像已恢复并核对：$output"
