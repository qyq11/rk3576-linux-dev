$ErrorActionPreference='Stop'
$workspace=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$folders=@('07_backups/system_image/sources','07_backups/system_image/sources/ubuntu_base','07_backups/system_image/build_host','07_backups/system_image/ubuntu24_server','07_backups/sn100_host/resume_20261030','07_backups/sn100_host/reagent_calendar_20261030')
$checks=[Collections.Generic.List[object]]::new()
foreach($folder in $folders){
    foreach($line in Get-Content -LiteralPath (Join-Path $workspace ($folder+'/SHA256SUMS'))){
        if($line -notmatch '^([0-9a-f]{64})\s+\*?(.+)$'){continue}
        $expected=$Matches[1]; $name=$Matches[2]; $relative=$folder+'/'+$name; $absolute=Join-Path $workspace $relative
        if($name -match '(\.gz|\.wsl|\.part\d{3}|\.img|\.dtb|\.bin)$' -or $name -eq 'Image'){
            if($relative -eq '07_backups/system_image/ubuntu24_server/update.img'){
                $actual=(Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash.ToLowerInvariant(); $method='正式展开镜像实际文件指纹'
            }else{
                $pointer=(git -C $workspace show ":$relative") -join "`n"
                if($LASTEXITCODE -ne 0 -or $pointer -notmatch 'oid sha256:([0-9a-f]{64})'){throw "缺少已暂存的LFS指针：$relative"}
                $actual=$Matches[1]; $method='Git LFS暂存时计算的实际内容指纹'
            }
        }else{
            $actual=(Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash.ToLowerInvariant(); $method='实际文件指纹'
        }
        $checks.Add([pscustomobject]@{path=$relative;sha256=$actual;expected=$expected;method=$method;passed=($actual -eq $expected)})
    }
}
$failed=@($checks|Where-Object {-not $_.passed})
$result=[ordered]@{date='2026-11-02';passed=$checks.Count-$failed.Count;failed=$failed.Count;checks=@($checks.ToArray())}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'archive_verification.json'),($result|ConvertTo-Json -Depth 5)+"`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]@{passed=$result.passed;failed=$result.failed} | ConvertTo-Json -Compress
if($failed.Count){$failed|Select-Object path;exit 1}
