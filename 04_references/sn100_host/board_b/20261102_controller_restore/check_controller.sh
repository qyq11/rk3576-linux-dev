#!/usr/bin/env bash
# 只读核对恢复后的原主控，不连接其业务端口。
set -Eeuo pipefail
test "$(tr -d '\0\n' </sys/firmware/devicetree/base/serial-number)" = 8314b1e8ffd0321f
unit=sn100-original-controller-board-b.service
audit=/home/rkshare/programs/aca_flow_test/runs/controller_restore_20261102
live=$(cat /home/rkshare/programs/aca_flow_test/runs/correct_05/latest_live_path.txt)
case "$live" in /home/rkshare/programs/aca_flow_test/runs/correct_05/live_*) ;; *) exit 2;; esac
printf 'SNAPSHOT_AT='; date -Is
printf 'LIVE_DIRECTORY=%s\n' "$live"
printf '\nSERVICE_STATE\n'
systemctl show "$unit" -p Id -p ActiveState -p SubState -p MainPID -p ControlGroup \
    -p ExecMainStartTimestamp -p ExecMainExitTimestamp -p ExecMainCode -p ExecMainStatus \
    -p Result -p Restart -p MemoryCurrent -p TasksCurrent
count=0
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    if test "$path" = /userdata/shared/ACA_V1.0OffLineD-500; then
        pid=${exe#/proc/}; pid=${pid%/exe}
        count=$((count+1))
        printf '\nCORRECT_CONTROLLER_PID=%s\nEXE=%s\n' "$pid" "$path"
        ps -p "$pid" -o pid,ppid,pgid,sid,tty,lstart,etime,%cpu,rss,nlwp,args
        printf 'CWD='; readlink "/proc/$pid/cwd"
        printf 'CGROUP='; cat "/proc/$pid/cgroup"
        printf 'ENV_LIBRARY_PATH='; tr '\0' '\n' <"/proc/$pid/environ" | grep '^LD_LIBRARY_PATH='
        sha256sum "/proc/$pid/exe"
    fi
done
printf '\nCORRECT_CONTROLLER_COUNT=%s\n' "$count"
test "$count" = 1
printf '\nPORT_5000\n'; ss -ltnp 'sport = :5000'
ss -H -ltn 'sport = :5000' | grep -q .
printf '\nORIGINAL_CONFIGURATION_HASH_CHECK\n'
sha256sum -c "$audit/configuration_before.sha256"
printf '\nORIGINAL_LAUNCHER_HASH_CHECK\n'
sha256sum -c "$audit/original_launcher.sha256"
printf '\nHOST_CURRENT_CHECK\n'
current=$(readlink -f /home/rkshare/programs/sn100_shell/current)
printf '%s\n' "$current"
test "$current" = "$(cat "$audit/host_current_before.txt")"
printf '\nPRODUCT_HOST_PROCESSES\n'
ps -eo pid,ppid,args | grep -E '/sn100_shell|status_login_connected_rk_review' | grep -v grep || true
printf '\nLIVE_LOG_FILES\n'
find "$live" -maxdepth 1 -type f -printf '%f %s %TY-%Tm-%TdT%TH:%TM:%TS\n' | sort
printf '\nLIVE_LOG_HEAD\n'; head -n 48 "$live/program.log"
printf '\nLIVE_LOG_TAIL\n'; tail -n 34 "$live/program.log"
printf '\nOLD_LOG_HASH\n'
sha256sum /home/rkshare/programs/aca_flow_test/runs/correct_05/live_20261102_173807/program.log
printf '\nEXIT_STATUS_NOTE=SERVICE_STILL_RUNNING_NO_FINAL_EXIT_CODE\n'
