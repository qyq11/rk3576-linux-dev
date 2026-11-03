#!/usr/bin/env bash
# 取消板A屏幕及三针调试口自动登录；密码由独立输入通道设置，不写入本脚本。
set -euo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
cd /home/rkshare/programs/root_password_login_board_a_20261102
exec 9>/run/lock/rk3576_login_configuration.lock
flock -n 9
sha256sum -c deployment_inputs.sha256
screen_dir=/etc/systemd/system/getty@tty1.service.d
serial_dir=/etc/systemd/system/serial-getty@ttyFIQ0.service.d
old_screen=$screen_dir/zz-board-a-root-autologin.conf
old_serial=$serial_dir/zz-board-a-root-autologin.conf
new_screen=$screen_dir/zz-board-a-password-login.conf
new_serial=$serial_dir/zz-board-a-password-login.conf
cmp -s tty1_autologin.before.conf "$old_screen"
cmp -s ttyFIQ0_autologin.before.conf "$old_serial"
test ! -e "$new_screen"
test ! -e "$new_serial"
passwd -S root | grep '^root P '
rollback() {
    local saved_exit=$?
    trap - ERR
    cp -- tty1_autologin.before.conf "$old_screen"
    cp -- ttyFIQ0_autologin.before.conf "$old_serial"
    chmod 644 "$old_screen" "$old_serial"
    rm -f -- "$new_screen" "$new_serial"
    systemctl daemon-reload
    sync
    printf 'configuration_rolled_back=%s\n' "$saved_exit"
    exit "$saved_exit"
}
trap rollback ERR
install -m 644 tty1_password_login.conf "$new_screen"
install -m 644 ttyFIQ0_password_login.conf "$new_serial"
rm -- "$old_screen" "$old_serial"
systemctl daemon-reload
printf '[effective_password_login_commands]\n'
for unit in getty@tty1.service serial-getty@ttyFIQ0.service; do
    effective=$(systemctl show "$unit" -p ExecStart --value)
    printf '%s\n%s\n' "$unit" "$effective"
    if grep -q -- '--autologin' <<<"$effective"; then
        printf '发现仍有效的自动登录命令，回退\n'
        false
    fi
done
sha256sum "$new_screen" "$new_serial" "$serial_dir/autologin.conf"
systemctl show getty@tty1.service serial-getty@ttyFIQ0.service -p Id -p DropInPaths
sync
trap - ERR
printf 'configuration_exit=0\n'
