#!/usr/bin/env bash
# 状态专项结束后的只读核对，并固定本轮检查文件的指纹。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
run="$base/verification/status_login_05_20261102/runs/auto_01"
test "$(readlink "$base/current")" = releases/e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270
count=0
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    case "$path" in
        */sn100_shell|*/status_login*review*) printf 'UNEXPECTED_APP_PROCESS=%s\n' "$exe"; exit 3 ;;
        /userdata/shared/ACA_V1.0OffLineD-500) count=$((count+1)); controller_pid=${exe#/proc/}; controller_pid=${controller_pid%/exe} ;;
    esac
done
test "$count" = 1 && test "$controller_pid" = 79082
test "$(sha256sum /proc/$controller_pid/exe | cut -d' ' -f1)" = ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
ss -H -ltnp 'sport = :5000' | grep -q "pid=$controller_pid,"
cd "$run"
test ! -e remote_capture.sha256
find . -type f ! -name remote_capture.sha256 -print0 | sort -z | xargs -0 -r sha256sum >remote_capture.sha256
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nCONTROLLER_PID=%s\nSN100_PROCESS_COUNT=0\n' "$controller_pid"
printf 'CURRENT=%s\n' "$(readlink "$base/current")"
printf 'CAPTURE_FILES=%s\nPROGRAM_STARTED=0\n' "$(wc -l <remote_capture.sha256)"
date -Is
