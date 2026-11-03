#!/usr/bin/env bash
# 只新增板A的SSH登录补充配置，不修改密码、密钥或其他业务配置。
set -Eeuo pipefail
export LC_ALL=C
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
destination=/etc/ssh/sshd_config.d/20-board-a-root-password.conf
test ! -e "$destination"
test "$(sha256sum /etc/ssh/sshd_config | cut -d' ' -f1)" = 64325541513d33ea1d2ccd19c77750d458e67e7967fd2e7ef81d92f0aa2ffe21
shadow_before=$(sha256sum /etc/shadow)
keys_before=$(sha256sum /root/.ssh/authorized_keys)
samba_before=$(sha256sum /etc/samba/smb.conf)
main_before=$(sha256sum /etc/ssh/sshd_config)
success=0
finish() {
    status=$?
    trap - EXIT
    if ((success == 0)); then
        rm -f -- "$destination"
        /usr/sbin/sshd -t && systemctl reload ssh || true
        printf 'ADDED_SSH_CONFIG_ROLLED_BACK=YES\n'
    fi
    exit "$status"
}
trap finish EXIT
install -o root -g root -m 644 /run/board-a-root-password-20261102.conf "$destination"
/usr/sbin/sshd -t
for context in \
    user=root,addr=192.168.100.210,host=192.168.100.210,laddr=192.168.100.123,lport=22 \
    user=root,addr=192.168.3.210,host=192.168.3.210,laddr=192.168.3.38,lport=22; do
    effective=$(/usr/sbin/sshd -T -C "$context")
    grep -qx 'permitrootlogin yes' <<< "$effective"
    grep -qx 'passwordauthentication yes' <<< "$effective"
    grep -qx 'pubkeyauthentication yes' <<< "$effective"
done
systemctl reload ssh
systemctl is-active --quiet ssh
test "$(sha256sum /etc/shadow)" = "$shadow_before"
test "$(sha256sum /root/.ssh/authorized_keys)" = "$keys_before"
test "$(sha256sum /etc/samba/smb.conf)" = "$samba_before"
test "$(sha256sum /etc/ssh/sshd_config)" = "$main_before"
success=1
printf 'board=board_a\nserial=3bf8222fe1417f6a\n'
date -Is
printf 'SYSTEM_PASSWORDS_UNCHANGED=PASS\nEXISTING_ROOT_KEYS_UNCHANGED=PASS\nSAMBA_CONFIG_UNCHANGED=PASS\nMAIN_SSH_CONFIG_UNCHANGED=PASS\n'
systemctl is-active ssh
/usr/sbin/sshd -T | grep -E '^(permitrootlogin|passwordauthentication|pubkeyauthentication) '
sha256sum "$destination"
printf 'SSH_PASSWORD_CONFIG=PASS\n'
