#!/usr/bin/env bash
# 在当前终端显示主控原始输出，同时保存文件；Ctrl+C用于结束本次运行。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
cd /userdata/shared
test "$(sha256sum ACA_V1.0D-500 | cut -d' ' -f1)" = 18d11230da40cea294b72be4e8454e54a3f6073dc3767e7bdfd10c2556d66332
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    case "$path" in
        */ACA_V1.0D-500|*/ACA_V1.0OffLineD-500) printf '主控已经在运行：%s\n' "$path"; exit 1 ;;
    esac
done
run=/home/rkshare/programs/aca_flow_test/runs/replace_04
live=$run/live_$(date +%Y%m%d_%H%M%S)
mkdir -m 750 "$live"
printf '%s\n' "$live" >"$run/latest_live_path.txt"
printf '板B主控实时日志，按 Ctrl+C 结束。\n程序：/userdata/shared/ACA_V1.0D-500\n日志：%s/program.log\n\n' "$live"
date -Is >"$live/started_at.txt"
set +e
trap ':' INT TERM
LD_LIBRARY_PATH=/userdata/shared stdbuf -oL -eL ./ACA_V1.0D-500 2>&1 | tee "$live/program.log"
code=${PIPESTATUS[0]}
set -e
printf 'PROGRAM_EXIT=%s\n' "$code" | tee "$live/result.txt"
date -Is >"$live/ended_at.txt"
exit "$code"
