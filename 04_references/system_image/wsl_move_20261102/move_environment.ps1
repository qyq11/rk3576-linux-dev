$ErrorActionPreference='Stop'
$workspace=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$distro='RK3576-Build-22.04'
$sourceDirectory=[IO.Path]::GetFullPath((Join-Path $workspace '06_programs/rk3576_system/wsl_workspace'))
$destination=[IO.Path]::GetFullPath('D:\WSL\RK3576-Build-22.04')
if(-not $sourceDirectory.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)){throw '源目录不在当前工程内。'}
if(-not $destination.StartsWith('D:\WSL\',[StringComparison]::OrdinalIgnoreCase)){throw '目标不在指定D盘WSL目录内。'}
if(Test-Path -LiteralPath $destination){throw '目标已存在，停止以避免覆盖。'}
$registration=@(Get-ChildItem -LiteralPath 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Lxss'|ForEach-Object{Get-ItemProperty -LiteralPath $_.PSPath}|Where-Object DistributionName -eq $distro)
if($registration.Count -ne 1 -or $registration[0].BasePath.TrimEnd('\') -ne $sourceDirectory){throw 'WSL登记位置与预计源目录不一致。'}
$parent=Split-Path -Parent $destination
if(Test-Path -LiteralPath $parent){if((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint){throw '目标父目录是链接，请先核对实际位置。'}}else{New-Item -ItemType Directory -Path $parent|Out-Null}
$source=Join-Path $sourceDirectory 'ext4.vhdx'
$beforeFile=Get-Item -LiteralPath $source
if($beforeFile.Attributes -band [IO.FileAttributes]::ReparsePoint){throw '源虚拟磁盘为链接。'}
$before=[ordered]@{distro=$distro;source_directory=$sourceDirectory;source_disk=$source;destination_directory=$destination;bytes=$beforeFile.Length;registry_key=$registration[0].PSChildName;default_uid=$registration[0].DefaultUid;started_at_utc=(Get-Date).ToUniversalTime().ToString('o')}
wsl.exe --terminate $distro *> (Join-Path $PSScriptRoot 'terminate.log')
if($LASTEXITCODE -ne 0){throw '停止指定WSL环境失败，未移动。'}
Write-Output 'STAGE=计算停机后的完整虚拟磁盘指纹'
for($attempt=1;$attempt -le 12;$attempt++){
    try{
        $before['sha256']=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        break
    }catch [System.IO.IOException]{
        if($attempt -eq 12){throw}
        Write-Output "等待WSL释放磁盘文件，第$attempt 次。"
        Start-Sleep -Seconds 5
    }
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'disk_before.json'),($before|ConvertTo-Json -Depth 4)+"`n",[Text.UTF8Encoding]::new($false))
Write-Output 'STAGE=通过WSL管理功能迁移至D盘'
wsl.exe --manage $distro --move $destination *> (Join-Path $PSScriptRoot 'wsl_move.log')
$moveExit=$LASTEXITCODE
if($moveExit -ne 0){throw "WSL迁移命令退出$moveExit，请保留现场核对。"}
$afterRegistration=Get-ItemProperty -LiteralPath $registration[0].PSPath
$newBase=$afterRegistration.BasePath.TrimEnd('\')
if($newBase.StartsWith('\\?\')){$newBase=$newBase.Substring(4)}
if([IO.Path]::GetFullPath($newBase) -ne $destination){throw '迁移后登记路径未指向目标。'}
$target=Join-Path $destination 'ext4.vhdx'
$afterFile=Get-Item -LiteralPath $target
Write-Output 'STAGE=在首次启动前核对迁移后的完整磁盘指纹'
$afterHash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
$result=[ordered]@{distro=$distro;source_disk=$source;destination_disk=$target;source_bytes=$beforeFile.Length;destination_bytes=$afterFile.Length;source_sha256=$before.sha256;destination_sha256=$afterHash;whole_disk_matches=($before.sha256 -eq $afterHash -and $beforeFile.Length -eq $afterFile.Length);base_path=$newBase;default_uid_before=$before.default_uid;default_uid_after=$afterRegistration.DefaultUid;registry_key_unchanged=($afterRegistration.PSChildName -eq $before.registry_key);source_disk_still_exists=(Test-Path -LiteralPath $source);move_exit=$moveExit;checked_before_first_boot=$true;completed_at_utc=(Get-Date).ToUniversalTime().ToString('o')}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'disk_move_result.json'),($result|ConvertTo-Json -Depth 4)+"`n",[Text.UTF8Encoding]::new($false))
$result|ConvertTo-Json -Compress
if(-not $result.whole_disk_matches -or -not $result.registry_key_unchanged -or $result.default_uid_before -ne $result.default_uid_after){throw '迁移保留核对未通过，暂不启动环境。'}
