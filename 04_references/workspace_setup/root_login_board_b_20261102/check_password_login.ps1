$ErrorActionPreference = 'Stop'
$credential = Import-Clixml -LiteralPath 'C:/Users/52400/.rk3576-local/credentials/board_b/root.credential.clixml'
if ($credential.UserName -cne 'root') { throw '凭据账户不是root。' }
$secret = $credential.GetNetworkCredential().Password
$start = [Diagnostics.ProcessStartInfo]::new('C:/Windows/System32/OpenSSH/ssh.exe')
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @('-tt','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile=C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/board_b/20261102_deploy/known_hosts','root@192.168.100.123','/bin/login root')) { $start.ArgumentList.Add($argument) }
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
$transcript = [Text.StringBuilder]::new()
$phase = 'password_prompt'
$passwordPromptSeen = $false
$timer = [Diagnostics.Stopwatch]::StartNew()
try {
    [void]$process.Start()
    $errorsTask = $process.StandardError.ReadToEndAsync()
    $buffer = [char[]]::new(1024)
    $read = $process.StandardOutput.ReadAsync($buffer, 0, $buffer.Length)
    while ($timer.Elapsed.TotalSeconds -lt 25) {
        if (-not $read.Wait(200)) { continue }
        $count = $read.GetAwaiter().GetResult()
        if ($count -eq 0) { break }
        [void]$transcript.Append($buffer, 0, $count)
        $plain = [regex]::Replace($transcript.ToString(), '\x1B\[[0-?]*[ -/]*[@-~]', '')
        if ($plain -match '(?i)Login incorrect|Authentication failure') { throw '提供的root密码登录失败，本次不继续尝试。' }
        if ($phase -eq 'password_prompt' -and $plain -match '(?i)(Password|密码)[:：]\s*$') {
            $passwordPromptSeen = $true
            $process.StandardInput.Write($secret + "`n")
            $process.StandardInput.Flush()
            $phase = 'root_prompt'
        } elseif ($phase -eq 'root_prompt' -and $plain -match 'root@[^\r\n]*#\s*$') {
            $process.StandardInput.Write("printf '\nROOT_PASSWORD_LOGIN_OK\n'; id -u; pwd; exit`n")
            $process.StandardInput.Flush()
            $phase = 'exit'
        } elseif ($phase -eq 'password_prompt' -and $plain -match 'root@[^\r\n]*#\s*$') {
            throw '未经过密码提示就进入shell，不能算密码登录通过。'
        }
        $read = $process.StandardOutput.ReadAsync($buffer, 0, $buffer.Length)
    }
    if (-not $process.HasExited -and -not $process.WaitForExit(2000)) { $process.Kill($true); throw '密码登录核对超时。' }
    $errors = $errorsTask.GetAwaiter().GetResult()
    $raw = $transcript.ToString()
    $plain = [regex]::Replace($raw, '\x1B\[[0-?]*[ -/]*[@-~]', '')
    if (-not $passwordPromptSeen -or $phase -ne 'exit' -or $process.ExitCode -ne 0 -or $plain -notmatch '(?m)^ROOT_PASSWORD_LOGIN_OK\r?$' -or $plain -notmatch '(?m)^0\r?$' -or $plain -notmatch '(?m)^/root\r?$') { throw '密码、root身份或目录结果没有全部核对通过。' }
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'password_login_check_exit.txt'), '0')
    Write-Output '实际观察到Password提示；输入本机加密记录中的密码后，root身份、用户编号0和/root核对通过。'
} finally {
    if (-not $process.HasExited) { $process.Kill($true) }
    $safe = $transcript.ToString().Replace($secret, '[REDACTED]')
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'password_login_check.log'), $safe, [Text.UTF8Encoding]::new($false))
    $process.Dispose()
    $secret = $null
    $credential = $null
}
