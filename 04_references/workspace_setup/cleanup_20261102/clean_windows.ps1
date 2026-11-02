$ErrorActionPreference='Stop'
$workspace=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$plan=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'windows_plan.json') -Raw|ConvertFrom-Json
$verification=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'archive_verification.json') -Raw|ConvertFrom-Json
if($verification.failed -ne 0 -or $plan.root -ne $workspace){throw '归档校验或工作目录不一致。'}
$tempRoot=Join-Path $workspace 'temp'
$deleted=0; $bytes=0L
foreach($entry in $plan.files){
    $target=[IO.Path]::GetFullPath((Join-Path $workspace $entry.path))
    if(-not $target.StartsWith($tempRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw '拒绝项目temp以外的路径。'}
    $file=Get-Item -LiteralPath $target
    if($file.PSIsContainer -or $file.Length -ne $entry.bytes -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "文件状态变化，停止：$target"}
    $parent=$file.Directory
    while($parent -and $parent.FullName.Length -ge $tempRoot.Length){
        if($parent.Attributes -band [IO.FileAttributes]::ReparsePoint){throw '拒绝经过目录链接删除。'}
        $parent=$parent.Parent
    }
    Remove-Item -LiteralPath $target -Force
    $deleted++; $bytes+=$entry.bytes
}
$remaining=@($plan.files|Where-Object{Test-Path -LiteralPath (Join-Path $workspace $_.path)})
$result=[ordered]@{date='2026-11-02';deleted=$deleted;bytes=$bytes;remaining=$remaining.Count;recursive_directory_deletion=$false}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'windows_result.json'),($result|ConvertTo-Json)+"`n",[Text.UTF8Encoding]::new($false))
$result|ConvertTo-Json -Compress
if($remaining.Count){exit 1}
