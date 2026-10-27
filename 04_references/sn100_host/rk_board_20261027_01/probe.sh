#!/usr/bin/env bash
set -uo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
exec > >(tee probe.log) 2>&1
date -u +%FT%TZ
printf '\nWSL_BUILD_PROCESSES\n'
ps -eo pid,etime,comm,args | grep -E 'cc1plus|aarch64-linux|[m]ake -j|[n]ative_03' | head -20
printf '\nBOARD\n'
ssh -o BatchMode=yes -o ConnectTimeout=8 lckfb@192.168.100.123 /bin/bash <<'REMOTE'
set -u
uname -a
id
printf 'PREEMPT_RT='; cat /sys/kernel/realtime
printf '\nDISPLAY\n'
for f in /sys/class/drm/card*-*/status /sys/class/drm/card*-*/modes; do printf '%s: ' "$f"; cat "$f"; done
ls -l /dev/dri /dev/tty7 /dev/input/by-id 2>/dev/null
printf '\nENVIRONMENT\n'
df -h /home/rkshare
command -v qmake >/dev/null && qmake -v
command -v fc-match >/dev/null && fc-match -f '%{family}\n' 'Noto Sans CJK SC:lang=zh-cn'
printf '\nDEPLOYMENT\n'
if test -e /home/rkshare/programs/sn100_shell; then ls -ld /home/rkshare/programs/sn100_shell; ls -l /home/rkshare/programs/sn100_shell/current; else printf 'NO_EXISTING_SN100_DEPLOYMENT\n'; fi
test -w /home/rkshare/programs && printf 'DEPLOY_ROOT_WRITABLE=1\n'
sudo -n true && printf 'EXISTING_NONINTERACTIVE_SUDO=1\n'
printf '\nDISPLAY_OWNERS\n'
sudo -n fuser /dev/tty7 /dev/dri/card0 2>&1 || true
printf '\nAPPLICATION_PROCESSES\n'
pgrep -a -f 'sn100|qt_demo' || true
printf '\nTHERMAL\n'
for f in /sys/class/thermal/thermal_zone*/temp; do printf '%s: ' "$f"; cat "$f"; done
printf 'PROBE_COMPLETE=1\n'
REMOTE
code=$?
printf 'SSH_PROBE_EXIT=%s\n' "$code"
exit "$code"
