#!/usr/bin/env bash
set -euo pipefail
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
printf '[identity]\n'
id
uname -a
cat /proc/sys/kernel/random/boot_id
ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub
printf '\n[login_and_console]\n'
getent passwd root lckfb
passwd -S root
cat /proc/cmdline
cat /sys/class/tty/console/active
cat /sys/class/tty/tty0/active
who
loginctl list-sessions --no-legend
systemctl get-default
systemctl list-units 'getty@*' 'serial-getty@*' 'display-manager*' --all --no-pager
systemctl is-enabled getty@tty1.service || true
systemctl cat getty@tty1.service
systemctl show getty@tty1.service -p FragmentPath -p DropInPaths -p ExecStart -p ActiveState -p MainPID
printf '\n[existing_dropins]\n'
ls -la /etc/systemd/system/getty@tty1.service.d /etc/systemd/system/getty.target.wants/getty@tty1.service 2>&1 || true
printf '\n[root_ssh_access]\n'
stat -c '%a %U %G %n' /root /root/.ssh /root/.ssh/authorized_keys 2>&1 || true
sshd -T | grep -E '^(permitrootlogin|pubkeyauthentication|authorizedkeysfile|passwordauthentication) '
printf '\n[active_applications]\n'
ps -eo pid,user,tty,args | grep -E 'sn100|[X]org|[w]eston|[b]ash|[a]getty' || true
printf '\n[agetty_version]\n'
agetty --version
printf '\npreflight_exit=0\n'
