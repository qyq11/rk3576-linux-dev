#!/usr/bin/env bash
# 重启后读取真实登录进程，不通过额外提权伪造本地登录结果。
set -euo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
test "$(cat /proc/sys/kernel/random/boot_id)" != 046347e9-a651-4392-af42-61fc37d00bcd
printf '[identity_after_reboot]\n'
tr -d '\000' </proc/device-tree/serial-number
printf '\n'
cat /proc/sys/kernel/random/boot_id
uname -a
cat /sys/kernel/realtime
printf '\n[real_local_login_sessions]\n'
who
loginctl list-sessions --no-legend
for terminal in tty1 ttyFIQ0; do
    printf '\nTERMINAL=%s\n' "$terminal"
    # 查找该终端已经自动启动的交互式Bash，并核对它的真实身份及当前目录。
    pid=$(ps -t "$terminal" -o pid=,comm= | awk '$2=="bash" {print $1}')
    test -n "$pid"
    test "$(printf '%s\n' "$pid" | wc -l)" = 1
    printf 'SHELL_PID=%s\n' "$pid"
    grep -E '^(Name|Uid|Gid):' "/proc/$pid/status"
    test "$(awk '/^Uid:/ {print $2}' "/proc/$pid/status")" = 0
    current_dir=$(readlink -f "/proc/$pid/cwd")
    printf 'SHELL_CWD=%s\n' "$current_dir"
    test "$current_dir" = /root
    tr '\000' '\n' <"/proc/$pid/environ" | grep -E '^(HOME|USER|LOGNAME)='
    tr '\000' '\n' <"/proc/$pid/environ" | grep -Fx HOME=/root
    printf 'LOCAL_ROOT_CHECK_%s=PASS\n' "$terminal"
done
printf '\n[root_ssh_login]\n'
id
printf 'PWD='; pwd
printf 'HOME=%s\n' "$HOME"
test "$HOME" = /root
test "$PWD" = /root
printf '\n[login_configuration]\n'
systemctl show getty@tty1.service serial-getty@ttyFIQ0.service -p Id -p ExecStart -p DropInPaths -p ActiveState
sha256sum /etc/systemd/system/getty@tty1.service.d/zz-board-a-root-autologin.conf /etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-a-root-autologin.conf /etc/systemd/system/serial-getty@ttyFIQ0.service.d/autologin.conf
printf '\n[interface_configuration_preserved]\n'
for n in 3 4 7 8 9; do test -c "/dev/ttyS$n"; ls -l "/dev/ttyS$n"; done
test "$(tr -d '\000' </proc/device-tree/can@2ac00000/status)" = okay
ip -details link show can0
sha256sum /boot/overlays/overlays.ini /boot/overlays/.ubootenv
printf '\nverify_exit=0\n'
