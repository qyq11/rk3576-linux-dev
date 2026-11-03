$Host.UI.RawUI.WindowTitle = 'RK3576 Board B - SN500 upper TX RX'
Write-Host '板B SN500上位机实际收发日志。关闭本窗口只停止查看，不停止上位机。' -ForegroundColor Cyan
$sshOptions = @('-o','BatchMode=yes','-o','ConnectTimeout=8','-o','StrictHostKeyChecking=yes','-o','HostKeyAlias=rk3576-board-b','-o','UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b')
& ssh @sshOptions root@192.168.3.59 'tail -n 25 -F /home/rkshare/programs/sn100_shell/runs/20261102_185728_56320/app.log'
