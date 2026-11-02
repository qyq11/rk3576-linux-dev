$ErrorActionPreference='Stop'
$directory=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\07_backups\system_image\sources'))
$archiveName='official_sdk_36d3aadf_20261030.tar.gz'
$original=Join-Path $directory $archiveName
$expected='b9ba2d6f458ca488b672a3e0bfd3851c0734bbcb2f4c7660a1e48c9f59d83a91'
$expectedLength=[long]9515085096
$files=@(Get-ChildItem -LiteralPath $directory -Filter "$archiveName.part???" | Sort-Object Name)
if ($files.Count -ne 9) { throw 'Expected exactly 9 source parts' }
$whole=[Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
$buffer=New-Object byte[] (8MB)
$parts=[Collections.Generic.List[object]]::new()
$total=[long]0
for ($i=0; $i -lt $files.Count; $i++) {
    $f=$files[$i]
    if ($f.Name -ne ($archiveName+'.part'+($i+1).ToString('D3'))) { throw 'Part ordering mismatch' }
    $hash=[Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
    $stream=[IO.File]::OpenRead($f.FullName)
    try { while (($read=$stream.Read($buffer,0,$buffer.Length)) -gt 0) { $hash.AppendData($buffer,0,$read); $whole.AppendData($buffer,0,$read); $total+=$read } } finally { $stream.Dispose() }
    $digest=[Convert]::ToHexString($hash.GetHashAndReset()).ToLowerInvariant()
    $hash.Dispose()
    $parts.Add([pscustomobject]@{name=$f.Name;bytes=$f.Length;sha256=$digest})
}
$actual=[Convert]::ToHexString($whole.GetHashAndReset()).ToLowerInvariant()
$whole.Dispose()
if ($actual -ne $expected -or $total -ne $expectedLength) { throw 'Reconstructed archive differs from original' }
[ordered]@{archive=$archiveName;bytes=$total;sha256=$actual;chunk_bytes=1GB;parts=$parts.ToArray()} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $directory 'source_archive_parts.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $directory 'SHA256SUMS') -Destination (Join-Path $directory 'WHOLE_ARCHIVE_SHA256SUMS')
$checks=@($parts | ForEach-Object { $_.sha256+'  '+$_.name })
$checks+='6eb023056abdd12581f1c55f9d5beb709cc36e8f9dce96a459a4b8be4d4676f3  manifest_pinned.xml'
$checks | Set-Content -LiteralPath (Join-Path $directory 'SHA256SUMS') -Encoding ascii
Remove-Item -LiteralPath $original
Write-Output "SOURCE_PARTS_VERIFIED=$($parts.Count) TOTAL_BYTES=$total SHA256=$actual"
