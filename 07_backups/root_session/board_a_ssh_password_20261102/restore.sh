#!/usr/bin/env bash
# 在板A撤回本次远程密码登录开关，保留系统密码及原有密钥连接。
set -Eeuo pipefail
export LC_ALL=C
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
archive_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
destination=/etc/ssh/sshd_config.d/20-board-a-root-password.conf
test "$(sha256sum /etc/ssh/sshd_config | cut -d' ' -f1)" = 64325541513d33ea1d2ccd19c77750d458e67e7967fd2e7ef81d92f0aa2ffe21
test "$(sha256sum "$destination" | cut -d' ' -f1)" = eaff3cf883cb3b1cdd3590c36f51033bb7e43b7083f7ed0cf2ed013dbb9a6908
cmp -s "$destination" "$archive_dir/20-board-a-root-password.conf"
success=0
finish() {
    status=$?
    trap - EXIT
    if ((success == 0)); then
        install -o root -g root -m 644 "$archive_dir/20-board-a-root-password.conf" "$destination"
        /usr/sbin/sshd -t && systemctl reload ssh || true
    fi
    exit "$status"
}
trap finish EXIT
rm -- "$destination"
/usr/sbin/sshd -t
systemctl reload ssh
systemctl is-active --quiet ssh
/usr/sbin/sshd -T | grep -Eq '^permitrootlogin (without-password|prohibit-password)$'
success=1
printf 'BOARD_A_SSH_PREVIOUS_LOGIN_POLICY_RESTORED=PASS\n'
