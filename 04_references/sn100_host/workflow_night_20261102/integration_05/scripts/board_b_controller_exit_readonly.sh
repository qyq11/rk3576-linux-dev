#!/usr/bin/env bash
# 只读核板B身份、主控当前状态和当时内核异常线索，不启动或停止任何程序。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
printf 'CONTROLLER_PROCESS_BEGIN\n'
pgrep -a -f '^(/userdata/shared/|\./)ACA_V1\.0OffLineD-500($| )' || true
printf 'CONTROLLER_PROCESS_END\nPORT_5000_BEGIN\n'
ss -lntp '( sport = :5000 )'
printf 'PORT_5000_END\nCURRENT_RELEASE='
readlink /home/rkshare/programs/sn100_shell/current
printf 'KERNEL_WINDOW_BEGIN\n'
if command -v journalctl >/dev/null 2>&1; then
    set +e
    journalctl -k --since '2026-11-02 19:45:00' --until '2026-11-02 20:10:00' --no-pager -o short-iso |
        awk '/Out of memory|oom-kill|Killed process|segfault|ACA_V1|sn100_shell/ { print; count++ } END { printf "MATCHED_KERNEL_LINES=%d\n", count }'
    codes=("${PIPESTATUS[@]}")
    set -e
    printf 'JOURNAL_EXIT=%s\nFILTER_EXIT=%s\n' "${codes[0]}" "${codes[1]}"
else
    printf 'KERNEL_JOURNAL_UNAVAILABLE=1\n'
fi
printf 'KERNEL_WINDOW_END\nPROGRAM_STARTED=0\nPROGRAM_STOPPED=0\n'
