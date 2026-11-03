#!/usr/bin/env bash
set -euo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
printf '[identity]\n'
tr -d '\000' </proc/device-tree/serial-number
printf '\n'
cat /proc/sys/kernel/random/boot_id
ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub
uname -a
printf '\n[root_account_state]\n'
getent passwd root
passwd -S root
printf '\n[effective_login_services]\n'
systemctl cat getty@tty1.service serial-getty@ttyFIQ0.service
systemctl show getty@tty1.service serial-getty@ttyFIQ0.service -p Id -p ExecStart -p DropInPaths -p ActiveState
printf '\n[affected_files]\n'
sha256sum /etc/systemd/system/getty@tty1.service.d/zz-board-a-root-autologin.conf /etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-a-root-autologin.conf /etc/systemd/system/serial-getty@ttyFIQ0.service.d/autologin.conf
test ! -e /etc/systemd/system/getty@tty1.service.d/zz-board-a-password-login.conf
test ! -e /etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-a-password-login.conf
printf '\n[local_login_policy]\n'
cat /etc/pam.d/login
if test -f /etc/securetty; then grep -E '^(tty1|ttyFIQ0)$' /etc/securetty || true; fi
printf '\n[sessions_and_applications]\n'
who
ps -eo pid,user,tty,args | grep -E '[s]n100|[a]getty|[b]ash' || true
printf '\npreflight_exit=0\n'
