$ErrorActionPreference = 'Stop'
$credential = Import-Clixml -LiteralPath 'C:/Users/52400/.rk3576-local/credentials/board_a/root.credential.clixml'
if ($credential.UserName -cne 'root') { throw '凭据账户不是root。' }
$secret = $credential.GetNetworkCredential().Password
if ([string]::IsNullOrEmpty($secret) -or $secret.Contains("`r") -or $secret.Contains("`n")) { throw '凭据格式异常。' }
$start = [Diagnostics.ProcessStartInfo]::new('C:/Windows/System32/OpenSSH/ssh.exe')
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @('-T','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','StrictHostKeyChecking=yes','root@192.168.100.123')) { $start.ArgumentList.Add($argument) }
$start.ArgumentList.Add('set -e; test "$(tr -d ''\000'' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a; test "$(id -u)" = 0; /usr/sbin/chpasswd')
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
try {
    [void]$process.Start()
    $outputTask = $process.StandardOutput.ReadToEndAsync()
    $errorTask = $process.StandardError.ReadToEndAsync()
    # 密码只经加密SSH连接的标准输入交给系统工具，不放进命令、脚本或项目日志。
    $process.StandardInput.Write('root:' + $secret + "`n")
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(20000)) { $process.Kill($true); throw '密码设置超时。' }
    $output = $outputTask.GetAwaiter().GetResult()
    $errors = $errorTask.GetAwaiter().GetResult()
    $sanitized = ($output + $errors).Replace($secret, '[REDACTED]')
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'password_set.log'), $sanitized, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'password_set_exit.txt'), [string]$process.ExitCode)
    if ($process.ExitCode -ne 0) { throw '板A root密码设置未通过，详见不含密码的错误日志。' }
    Write-Output '板A root密码已按本机加密凭据设置，系统工具退出0。'
} finally {
    if (-not $process.HasExited) { $process.Kill($true) }
    $process.Dispose()
    $secret = $null
    $credential = $null
}
