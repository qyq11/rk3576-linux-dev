#!/usr/bin/env bash
# 板A开机进入root命令行；只新增两份本地终端设置，不改原登录模板。
set -euo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
exec 9>/run/lock/rk3576_login_configuration.lock
flock -n 9
screen=/etc/systemd/system/getty@tty1.service.d/zz-board-a-root-autologin.conf
serial=/etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-a-root-autologin.conf
test ! -e "$screen"
test ! -e "$serial"
test -x /sbin/agetty
test -c /dev/tty1
test -c /dev/ttyFIQ0
test "$(getent passwd root | cut -d: -f6)" = /root
printf '[original_login_settings]\n'
systemctl is-enabled getty@tty1.service
systemctl is-enabled serial-getty@ttyFIQ0.service
sha256sum /etc/systemd/system/serial-getty@ttyFIQ0.service.d/autologin.conf
systemctl cat getty@tty1.service serial-getty@ttyFIQ0.service
rollback() {
    local saved_exit=$?
    trap - ERR
    rm -f -- "$screen" "$serial"
    systemctl daemon-reload
    printf 'login_configuration_rolled_back=%s\n' "$saved_exit"
    exit "$saved_exit"
}
trap rollback ERR
install -d -m 755 /etc/systemd/system/getty@tty1.service.d
cat >"$screen" <<'CONFIG'
# 板A屏幕接键盘时，开机自动以root进入/root。
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear - $TERM
CONFIG
cat >"$serial" <<'CONFIG'
# 板A原三针调试控制台沿用1500000速率，自动登录账户改为root。
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I 1500000 linux
CONFIG
chmod 644 "$screen" "$serial"
systemctl daemon-reload
printf '\n[new_login_settings]\n'
systemctl show getty@tty1.service serial-getty@ttyFIQ0.service -p Id -p ExecStart -p DropInPaths
systemctl show getty@tty1.service -p ExecStart --value | grep -F -- '--autologin root'
systemctl show serial-getty@ttyFIQ0.service -p ExecStart --value | grep -F -- '--autologin root'
sha256sum "$screen" "$serial"
sync
trap - ERR
printf 'login_configuration_exit=0\n'
