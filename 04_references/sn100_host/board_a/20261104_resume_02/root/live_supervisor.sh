#!/usr/bin/env bash
# 只给本轮新进程加只读观察器；正式release与默认current均不改写。
set -Eeuo pipefail
base=$1; release=$2; run=$3; verification=$4
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
