$Host.UI.RawUI.WindowTitle = 'RK3576 Board B - ACA live log'
$taskUtf8 = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = $taskUtf8
$OutputEncoding = $taskUtf8
& ssh.exe -tt -o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=yes -o HostKeyAlias=rk3576-board-b -o UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b root@192.168.3.59 'bash /home/rkshare/programs/aca_flow_test/run_live.sh'
$taskExit = $LASTEXITCODE
Write-Host ""
Write-Host "SSH/program exit: $taskExit"
Write-Host 'The window remains open so you can read the log.'
