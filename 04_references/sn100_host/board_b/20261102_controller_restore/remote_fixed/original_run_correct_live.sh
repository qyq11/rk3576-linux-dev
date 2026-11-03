#!/usr/bin/env bash
# 使用用户最终确认的主控文件，实时显示并保存原始日志；终端Ctrl+C结束。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
cd /userdata/shared
test "$(sha256sum ACA_V1.0OffLineD-500 | cut -d' ' -f1)" = ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    case "$path" in
        */ACA_V1.0OffLineD-500|*/ACA_V1.0D-500) printf '主控已经在运行：%s\n' "$path"; exit 1 ;;
    esac
done
run=/home/rkshare/programs/aca_flow_test/runs/correct_05
live=$run/live_$(date +%Y%m%d_%H%M%S)
mkdir -m 750 "$live"
printf '%s\n' "$live" >"$run/latest_live_path.txt"
printf '板B主控实时日志：本次正确文件，已去掉(1)。按 Ctrl+C 结束。\n程序：/userdata/shared/ACA_V1.0OffLineD-500\n日志：%s/program.log\n\n' "$live"
date -Is >"$live/started_at.txt"
set +e
trap ':' INT TERM
LD_LIBRARY_PATH=/userdata/shared stdbuf -oL -eL ./ACA_V1.0OffLineD-500 2>&1 | tee "$live/program.log"
code=${PIPESTATUS[0]}
set -e
printf 'PROGRAM_EXIT=%s\n' "$code" | tee "$live/result.txt"
date -Is >"$live/ended_at.txt"
exit "$code"
