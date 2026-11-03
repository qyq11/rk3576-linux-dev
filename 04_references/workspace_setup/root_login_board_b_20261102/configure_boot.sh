#!/usr/bin/env bash
# 板B开机等待账户和密码，登录root后使用原有/root；同时允许SSH密码登录。
set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(sha256sum /etc/ssh/sshd_config | cut -d' ' -f1)" = 64325541513d33ea1d2ccd19c77750d458e67e7967fd2e7ef81d92f0aa2ffe21
passwd -S root | grep '^root P '
screen=/etc/systemd/system/getty@tty1.service.d/zz-board-b-password-login.conf
serial=/etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-b-password-login.conf
ssh_config=/etc/ssh/sshd_config.d/20-board-b-root-password.conf
for file in "$screen" "$serial" "$ssh_config"; do test ! -e "$file" && test ! -L "$file"; done
exec 9>/run/lock/rk3576_login_configuration.lock
flock -n 9
keys_before=$(sha256sum /root/.ssh/authorized_keys)
original_serial_before=$(sha256sum /etc/systemd/system/serial-getty@ttyFIQ0.service.d/autologin.conf)
shadow_before=$(sha256sum /etc/shadow)
success=0
finish() {
    code=$?
    trap - EXIT
    if [[ $success != 1 ]]; then
        rm -f -- "$screen" "$serial" "$ssh_config"
        systemctl daemon-reload
        /usr/sbin/sshd -t && systemctl reload ssh || true
        printf 'NEW_LOGIN_FILES_ROLLED_BACK=1\n'
    fi
    exit "$code"
}
trap finish EXIT
mkdir -p "${screen%/*}" "${serial%/*}" "${ssh_config%/*}"
cat > "$screen" <<'CONF'
# 板B屏幕终端开机等待账户与密码，不自动进入命令行。
[Service]
ExecStart=
ExecStart=-/sbin/agetty --noclear - $TERM
CONF
cat > "$serial" <<'CONF'
# 板B三针调试口保留1500000速率，等待账户和密码。
[Service]
ExecStart=
ExecStart=-/sbin/agetty --noclear %I 1500000 linux
CONF
cat > "$ssh_config" <<'CONF'
# 板B允许使用root系统密码远程登录，已有密钥登录继续可用。
PermitRootLogin yes
CONF
chmod 644 "$screen" "$serial" "$ssh_config"
systemctl daemon-reload
/usr/sbin/sshd -t
for unit in getty@tty1.service serial-getty@ttyFIQ0.service; do
    command=$(systemctl show "$unit" -p ExecStart --value)
    printf '%s\n%s\n' "$unit" "$command"
    ! grep -q -- --autologin <<< "$command"
done
effective=$(/usr/sbin/sshd -T)
grep -qx 'permitrootlogin yes' <<< "$effective"
grep -qx 'passwordauthentication yes' <<< "$effective"
grep -qx 'pubkeyauthentication yes' <<< "$effective"
systemctl reload ssh
systemctl is-active --quiet ssh
test "$(sha256sum /root/.ssh/authorized_keys)" = "$keys_before"
test "$(sha256sum /etc/shadow)" = "$shadow_before"
test "$(sha256sum /etc/systemd/system/serial-getty@ttyFIQ0.service.d/autologin.conf)" = "$original_serial_before"
sha256sum "$screen" "$serial" "$ssh_config"
printf 'BOARD=board_b\nKEYS_PRESERVED=1\nORIGINAL_SERIAL_CONFIG_PRESERVED=1\nLOGIN_CONFIGURATION_OK=1\n'
success=1
sync
