#!/usr/bin/env bash
# 仅撤回板B本次新增的三个登录开关，不恢复旧密码或修改用户文件。
set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(sha256sum /etc/ssh/sshd_config | cut -d' ' -f1)" = 64325541513d33ea1d2ccd19c77750d458e67e7967fd2e7ef81d92f0aa2ffe21
screen=/etc/systemd/system/getty@tty1.service.d/zz-board-b-password-login.conf
serial=/etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-b-password-login.conf
ssh_config=/etc/ssh/sshd_config.d/20-board-b-root-password.conf
test "$(sha256sum "$screen" | cut -d' ' -f1)" = 51865bcf86198e56796aa56a207420b1e1b6cde90829bb83294ffb70ab4decb5
test "$(sha256sum "$serial" | cut -d' ' -f1)" = d5f765189c4a84515c1fe3e449519f4a23cc7224400d4a7ab4dc1d610e0f7aac
test "$(sha256sum "$ssh_config" | cut -d' ' -f1)" = 2b63d1e538f3209fae481e1f7aa3d248ccc90c8e4e53a1bd8b29ceea23908792
rm -- "$screen" "$serial" "$ssh_config"
systemctl daemon-reload
/usr/sbin/sshd -t
systemctl reload ssh
sync
printf 'BOARD_B_LOGIN_SWITCHES_REMOVED=1\nROOT_PASSWORD_UNCHANGED=1\nREBOOT_STILL_REQUIRED_FOR_LOCAL_TERMINALS=1\n'
