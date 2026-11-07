#!/usr/bin/env bash
# 仅给本轮新进程加入只读观察器，使用标准监督器要求的数字运行目录。
set -Eeuo pipefail
base=$1; release=$2; run=$3; verification=$4
[[ $run == "$base/runs/"* && ${run#"$base/runs/"} =~ ^[0-9_]+$ ]]
exec > "$run/supervisor.log" 2>&1
trap 'status=$?; if [[ ! -f $run/app.pid ]]; then printf "SUPERVISOR_START_FAILED=%s\n" "$status" > "$run/startup_failure.txt"; fi' EXIT
[[ $(tr -d '\000' < /proc/device-tree/serial-number) == 3bf8222fe1417f6a ]]
source "$release/rk_board.sh"
eval "$(declare -f runtime_environment | sed '1s/runtime_environment/runtime_environment_without_observer/')"
runtime_environment()
{
    runtime_environment_without_observer "$@"
    export LD_PRELOAD=$verification/live_ui_observer.so
    export SN100_LIVE_UI_OBSERVER=$verification/ui_$(basename "$run").jsonl
    printf '%s\n' "$SN100_LIVE_UI_OBSERVER" > "$run/observer_output"
}
supervise "$base" "$release" "$run" 0 ''
