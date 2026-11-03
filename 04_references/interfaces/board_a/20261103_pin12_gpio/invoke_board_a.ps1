param(
    [Parameter(Mandatory = $true)][string]$ScriptFile,
    [Parameter(Mandatory = $true)][string]$LogFile
)
$ErrorActionPreference = 'Stop'
# 仅复用电脑已信任的板A主机密钥；不改known_hosts，不接受其他板卡密钥。
$gpioSshOptions = @('-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
    '-o', 'HostKeyAlias=192.168.100.123', '-o', 'ConnectTimeout=8')
$gpioScript = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $ScriptFile)).Replace("`r`n", "`n")
$gpioBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($gpioScript))
$gpioRemoteCommand = "printf '%s' '$gpioBase64' | base64 -d | bash"
$ErrorActionPreference = 'Continue'
$gpioOutput = & ssh @gpioSshOptions root@192.168.3.38 $gpioRemoteCommand 2>&1
$gpioExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$gpioText = [string]::Join("`n", @($gpioOutput | ForEach-Object { $_.ToString() })) + "`n"
[IO.File]::WriteAllText([IO.Path]::GetFullPath($LogFile), $gpioText, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText([IO.Path]::GetFullPath($LogFile + '.exit'), "$gpioExit`n", [Text.UTF8Encoding]::new($false))
Write-Output $gpioText
Write-Output "SSH_EXIT=$gpioExit"
exit $gpioExit
