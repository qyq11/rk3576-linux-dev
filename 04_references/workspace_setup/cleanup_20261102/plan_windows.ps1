$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$tempRoot = Join-Path $workspace 'temp'
$items = [System.Collections.Generic.List[object]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
function Add-Candidate($file, [string]$reason) {
    $absolute = [IO.Path]::GetFullPath($file.FullName)
    if (-not $absolute.StartsWith($tempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw '清理路径超出项目temp目录。' }
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw '不清理链接目标。' }
    if ($seen.Add($absolute)) { $items.Add([pscustomobject]@{path=$absolute.Substring($workspace.Length+1).Replace('\','/'); bytes=$file.Length; reason=$reason}) }
}
foreach ($task in @('resume_20261030','reagent_calendar_20261030')) {
    $taskRoot = Join-Path $tempRoot ('sn100_host/' + $task)
    foreach ($directory in Get-ChildItem -LiteralPath $taskRoot -Directory) {
        if ($directory.Name -match '^(arm64_build|build|standalone_arm64_build.*|original_.*_test_build|qmake_.*check)$') {
            foreach ($file in Get-ChildItem -LiteralPath $directory.FullName -File -Recurse) {
                if ($file.Extension -in @('.o','.obj','.moc') -or $file.Name -match '^(moc_|qrc_|ui_)' -or $file.Name -match '^(Makefile(\..*)?|\.qmake\.stash)$') {
                    Add-Candidate $file '可由源码和构建脚本重新生成的编译对象或Qt生成文件'
                } elseif ($file.Length -gt 4 -and $file.Extension -eq '') {
                    $stream = $file.OpenRead()
                    try { $header = [byte[]]::new(4); [void]$stream.Read($header,0,4) } finally { $stream.Dispose() }
                    if ([Convert]::ToHexString($header) -eq '7F454C46') { Add-Candidate $file '临时ARM64应用或测试程序；正式匹配应用和源码已归档' }
                }
            }
        }
    }
    foreach ($stage in Get-ChildItem -LiteralPath (Join-Path $taskRoot 'packages') -Directory -Filter 'stage_*') {
        foreach ($file in Get-ChildItem -LiteralPath $stage.FullName -File -Recurse) {
            Add-Candidate $file '应用打包暂存副本；正式匹配应用归档已核对'
        }
    }
}
$obsolete = Get-Item -LiteralPath (Join-Path $tempRoot 'system_image_20261030/build_candidate/update.img')
Add-Candidate $obsolete '最终目录归属修正前的候选镜像；正式最终镜像保留并已核对'
foreach ($path in @('sn100_host/arm64_Packages','sn100_host/arm64_Packages.xz','system_image_20261030/go_releases.json')) {
    $file = Get-Item -LiteralPath (Join-Path $tempRoot $path) -ErrorAction SilentlyContinue
    if ($file) { Add-Candidate $file '软件仓库索引缓存；已安装依赖和正式安装输入保留' }
}
$plan = [ordered]@{date='2026-11-02'; root=$workspace; scope='仅项目temp内已列出的生成文件，逐文件删除；保留依赖、运行数据和证据'; files=@($items.ToArray()); bytes=($items|Measure-Object bytes -Sum).Sum; count=$items.Count}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'windows_plan.json'), ($plan|ConvertTo-Json -Depth 5) + "`n", [Text.UTF8Encoding]::new($false))
[pscustomobject]@{files=$items.Count; bytes=$plan.bytes; reasons=@($items|Group-Object reason|ForEach-Object{[pscustomobject]@{reason=$_.Name; files=$_.Count; bytes=($_.Group|Measure-Object bytes -Sum).Sum}})} | ConvertTo-Json -Depth 4
