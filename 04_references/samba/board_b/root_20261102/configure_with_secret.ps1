# 密码从本机项目外的加密记录读取，只通过SSH标准输入交给板B。
$ErrorActionPreference='Stop'
$credential=Import-Clixml -LiteralPath 'C:/Users/52400/.rk3576-local/credentials/board_b/root.credential.clixml'
if ($credential.UserName -cne 'root') { throw '凭据账户不是root。' }
$secret=$credential.GetNetworkCredential().Password
$start=[Diagnostics.ProcessStartInfo]::new('C:/Windows/System32/OpenSSH/ssh.exe')
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$start.RedirectStandardInput=$true
$start.RedirectStandardOutput=$true
$start.RedirectStandardError=$true
foreach($argument in @('-T','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile=C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/board_b/20261102_deploy/known_hosts','root@192.168.100.123','bash /run/board_b_samba_root_20261102.sh')) {
    $start.ArgumentList.Add($argument)
}
$process=[Diagnostics.Process]::new()
$process.StartInfo=$start
try {
    [void]$process.Start()
    $stdout=$process.StandardOutput.ReadToEndAsync()
    $stderr=$process.StandardError.ReadToEndAsync()
    $process.StandardInput.Write($secret+"`n")
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(30000)) { $process.Kill($true); throw '共享配置超时。' }
    $safe=($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()).Replace($secret,'[REDACTED]')
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'configure.log'),$safe,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'configure_exit.txt'),[string]$process.ExitCode)
    if ($process.ExitCode -ne 0) { throw '板B共享配置失败，已保存脱敏日志。' }
    'BOARD_B_ROOT_SAMBA_CONFIGURED=1'
} finally {
    if (-not $process.HasExited) { $process.Kill($true) }
    $process.Dispose()
    $secret=$null
    $credential=$null
}
