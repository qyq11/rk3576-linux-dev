$ErrorActionPreference = 'Stop'
$credential = Import-Clixml -LiteralPath 'C:/Users/52400/.rk3576-local/credentials/board_a/root.credential.clixml'
if ($credential.UserName -cne 'root') { throw '本机加密凭据账户不是root。' }
$secret = $credential.GetNetworkCredential().Password
$start = [Diagnostics.ProcessStartInfo]::new('C:/Windows/System32/wsl.exe')
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @('-d','Ubuntu-24.04','-u','qyq','--','python3','/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/root_session/board_a/20261102_ssh_password/check_ssh_password.py','192.168.100.123','192.168.3.38')) {
    $start.ArgumentList.Add($argument)
}
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
try {
    [void]$process.Start()
    $outputTask = $process.StandardOutput.ReadToEndAsync()
    $errorTask = $process.StandardError.ReadToEndAsync()
    # 只通过进程标准输入传入现有密码，不重设密码，也不保存明文。
    $process.StandardInput.Write($secret + "`n")
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(55000)) { $process.Kill($true); throw '密码登录测试超时。' }
    $safeOutput = ($outputTask.GetAwaiter().GetResult() + $errorTask.GetAwaiter().GetResult()).Replace($secret, '[REDACTED]')
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'password_only_login.log'), $safeOutput, [Text.UTF8Encoding]::new($false))
    $safeOutput
    if ($process.ExitCode -ne 0) { throw 'SSH密码登录测试未通过，日志已脱敏。' }
    'WIRED_AND_WIFI_PASSWORD_LOGIN=PASS'
} finally {
    if (-not $process.HasExited) { $process.Kill($true) }
    $process.Dispose()
    $secret = $null
    $credential = $null
}
