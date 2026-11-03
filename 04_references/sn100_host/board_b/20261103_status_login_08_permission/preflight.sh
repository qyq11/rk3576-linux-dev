#!/usr/bin/env bash
# 仅核原主控、程序选择和现有进程，不启动/停止任何程序。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
printf 'CURRENT_RELEASE=%s\n' "$(readlink "$base/current")"
controller_count=0
controller_pid=
application_count=0
for item in /proc/[0-9]*/exe; do
    target=$(readlink "$item" || true)
    case "$target" in
        /userdata/shared/ACA_V1.0OffLineD-500)
            controller_count=$((controller_count+1)); controller_pid=${item#/proc/}; controller_pid=${controller_pid%/exe} ;;
        */sn100_shell|*/status_login*review*) application_count=$((application_count+1)); printf 'APPLICATION_EXE=%s\n' "$target" ;;
    esac
done
printf 'CONTROLLER_COUNT=%s\nCONTROLLER_PID=%s\nAPPLICATION_COUNT=%s\n' "$controller_count" "$controller_pid" "$application_count"
test "$controller_count" = 1
test "$(sha256sum "/proc/$controller_pid/exe" | cut -d' ' -f1)" = ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
ss -H -ltnp 'sport = :5000' | grep "pid=$controller_pid,"
test "$(readlink "$base/current")" = releases/08bc43a73a9e8e4826a559f501829355357a46a999799e8b662ebd1feae1a0ef
test "$application_count" = 0
printf 'READ_ONLY_PRECHECK=1\nPROGRAM_STARTED_OR_STOPPED=0\n'

find "$base/runtime/data" -maxdepth 1 -type f -iname '*.ini' -print0 | sort -z | xargs -0 -r sha256sum
