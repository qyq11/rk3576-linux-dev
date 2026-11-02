$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\07_backups\system_image\sources\official_sdk_36d3aadf_20261030.tar.gz'))
$expected = 'b9ba2d6f458ca488b672a3e0bfd3851c0734bbcb2f4c7660a1e48c9f59d83a91'
$sourceDirectory = Split-Path -Parent $source
$sourceLength = (Get-Item -LiteralPath $source).Length
$buffer = New-Object byte[] (8MB)
$parts = [Collections.Generic.List[object]]::new()
$wholeHash = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
$inputStream = [IO.File]::OpenRead($source)
try {
    $index = 1
    while ($inputStream.Position -lt $sourceLength) {
        $partPath = $source + '.part' + $index.ToString('D3')
        if (Test-Path -LiteralPath $partPath) { throw "Part already exists: $partPath" }
        $partStream = [IO.File]::Open($partPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $partHash = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
        $written = [long]0
        try {
            while ($written -lt 1GB -and $inputStream.Position -lt $sourceLength) {
                $count = [int][math]::Min($buffer.Length, (1GB - $written))
                $read = $inputStream.Read($buffer, 0, $count)
                if ($read -le 0) { throw 'Unexpected end of source archive' }
                $partStream.Write($buffer, 0, $read)
                $partHash.AppendData($buffer, 0, $read)
                $wholeHash.AppendData($buffer, 0, $read)
                $written += $read
            }
        } finally { $partStream.Dispose() }
        $digest = [Convert]::ToHexString($partHash.GetHashAndReset()).ToLowerInvariant()
        $partHash.Dispose()
        $readback = (Get-FileHash -LiteralPath $partPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($readback -ne $digest) { throw "Part readback mismatch: $partPath" }
        $parts.Add([ordered]@{name=[IO.Path]::GetFileName($partPath); bytes=$written; sha256=$digest})
        Write-Output "PART_VERIFIED=$index BYTES=$written"
        $index++
    }
} finally { $inputStream.Dispose() }
$actual = [Convert]::ToHexString($wholeHash.GetHashAndReset()).ToLowerInvariant()
$wholeHash.Dispose()
if ($actual -ne $expected) { throw 'Whole archive reconstruction hash mismatch' }
if (($parts | Measure-Object -Property bytes -Sum).Sum -ne $sourceLength) { throw 'Part lengths do not match source' }
$metadata = [ordered]@{ archive=[IO.Path]::GetFileName($source); bytes=$sourceLength; sha256=$actual; chunk_bytes=1GB; parts=$parts }
$metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $sourceDirectory 'source_archive_parts.json') -Encoding utf8
$parts | ForEach-Object { $_.sha256 + '  ' + $_.name } | Set-Content -LiteralPath (Join-Path $sourceDirectory 'PART_SHA256SUMS') -Encoding ascii
# 已逐卷读回核对，合并后的指纹也与原归档一致，才移除本次生成的重复整包。
Remove-Item -LiteralPath $source
Write-Output "SOURCE_PARTS_VERIFIED=$($parts.Count) TOTAL_BYTES=$sourceLength"
