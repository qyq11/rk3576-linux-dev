#!/usr/bin/env bash
set -uo pipefail
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$work"
exec > >(tee reboot.log) 2>&1
host=lckfb@192.168.100.123
opts=(-T -o BatchMode=yes -o ConnectTimeout=3 -o ServerAliveInterval=3 -o ServerAliveCountMax=1)
ssh "${opts[@]}" "$host" 'cat /proc/sys/kernel/random/boot_id' > boot_id.before || exit $?
before=$(cat boot_id.before)
printf 'BOOT_ID_BEFORE=%s\n' "$before"
ssh "${opts[@]}" "$host" 'sync; sudo -n systemctl reboot'
request=$?
printf 'REBOOT_REQUEST_SSH_EXIT=%s\n' "$request"
[[ $request == 0 || $request == 255 ]] || exit "$request"
for ((attempt=1; attempt<=50; attempt++)); do
    sleep 3
    after=$(ssh "${opts[@]}" "$host" 'cat /proc/sys/kernel/random/boot_id' 2>/dev/null) || continue
    if [[ $after =~ ^[a-f0-9-]{36}$ && $after != "$before" ]]; then
        printf '%s\n' "$after" > boot_id.after
        printf 'BOOT_ID_AFTER=%s\n' "$after"
        ssh "${opts[@]}" "$host" 'uname -a; printf "PREEMPT_RT="; cat /sys/kernel/realtime; uptime; readlink -f /home/rkshare/programs/sn100_shell/current'
        printf 'BOARD_REBOOT_VERIFIED=1\n'
        printf '0\n' > reboot.exit
        exit 0
    fi
done
printf 'BOARD_REBOOT_WAIT_TIMEOUT=1\n'
printf '1\n' > reboot.exit
exit 1
