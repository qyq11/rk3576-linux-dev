#!/usr/bin/env bash
# 主控会改进程名，按/proc实际可执行文件核查，不靠显示名称判断。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
target=/userdata/shared/ACA_V1.0OffLineD-500
if [[ -f $target && ! -L $target ]]; then sha256sum -- "$target"; fi
matches=0
for process in /proc/[0-9]*; do
    executable=$(readlink "$process/exe" 2>/dev/null) || continue
    if [[ $executable == "$target" || $executable == "$target (deleted)" ]]; then
        matches=$((matches+1))
        printf 'MATCHED_PID=%s\nEXE=%s\n' "${process##*/}" "$executable"
        printf 'COMM='; cat "$process/comm"
        printf 'CWD='; readlink "$process/cwd" || true
    fi
done
printf 'MATCHED_EXE_PROCESSES=%s\n' "$matches"
ss -lntp '( sport = :5000 )'
printf 'PROGRAM_STARTED=0\nPROGRAM_STOPPED=0\n'
