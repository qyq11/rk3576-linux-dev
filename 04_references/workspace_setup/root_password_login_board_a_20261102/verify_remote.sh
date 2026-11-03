#!/usr/bin/env bash
# 重启后核对真实终端在等待登录，不再自动生成root命令行。
set -euo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
test "$(cat /proc/sys/kernel/random/boot_id)" != 3bdb939a-ec87-4ed2-a3fe-029ec5fa19a9
printf '[identity_after_reboot]\n'
tr -d '\000' </proc/device-tree/serial-number
printf '\n'
cat /proc/sys/kernel/random/boot_id
uname -a
cat /sys/kernel/realtime
printf '\n[password_login_services]\n'
for specification in 'tty1 getty@tty1.service' 'ttyFIQ0 serial-getty@ttyFIQ0.service'; do
    read -r terminal unit <<<"$specification"
    systemctl is-active "$unit"
    command=$(systemctl show "$unit" -p ExecStart --value)
    printf 'TERMINAL=%s\n%s\n' "$terminal" "$command"
    if grep -q -- '--autologin' <<<"$command"; then exit 1; fi
    process_list=$(ps -t "$terminal" -o pid=,comm=,args=)
    printf '%s\n' "$process_list"
    grep -q 'agetty' <<<"$process_list"
    if awk '$2=="bash" {found=1} END {exit !found}' <<<"$process_list"; then
        printf '发现已登录命令行，不能认定该终端仍在等待登录\n'
        exit 1
    fi
    printf 'WAITING_FOR_LOGIN_%s=PASS\n' "$terminal"
done
printf '\n[local_sessions]\n'
who
printf '\n[screen_login_prompt]\n'
if test -r /dev/vcs1 && test -r /dev/vcsa1; then
    columns=$(od -An -j1 -N1 -tu1 /dev/vcsa1 | tr -d ' ')
    fold -w "$columns" /dev/vcs1 | tail -n 10
    grep -q 'login:' /dev/vcs1
    printf '\nSCREEN_LOGIN_PROMPT=PASS\n'
fi
printf '\n[preserved_interface_settings]\n'
for number in 3 4 7 8 9; do test -c "/dev/ttyS$number"; done
test "$(tr -d '\000' </proc/device-tree/can@2ac00000/status)" = okay
ip -details link show can0
sha256sum /boot/overlays/overlays.ini /boot/overlays/.ubootenv
printf '\n[configuration_files]\n'
sha256sum /etc/systemd/system/getty@tty1.service.d/zz-board-a-password-login.conf /etc/systemd/system/serial-getty@ttyFIQ0.service.d/zz-board-a-password-login.conf /etc/systemd/system/serial-getty@ttyFIQ0.service.d/autologin.conf
sshd -T | grep -E '^(permitrootlogin|pubkeyauthentication) '
printf '\nverify_exit=0\n'
