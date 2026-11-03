#!/usr/bin/env bash
# 仅在外部CAN硬件已断开时运行。500000只用于本机检查，不是轴卡正式速率。
# 默认核对发送确认；--no-local-echo关闭本机回送，另查接收通道。
# 本脚本和编译后的can_loopback放在板端同一目录，结束时停止接口并关闭回环。
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
finish() {
    local result=$?
    local cleanup_result=0
    trap - EXIT
    set +e
    ip -details -statistics link show can0
    sudo -n ip link set can0 down || cleanup_result=$?
    sudo -n ip link set can0 type can loopback off fd off || cleanup_result=$?
    printf '[final_interface]\n'
    ip -details -statistics link show can0
    printf '[loopback_process_exit] %s\n[cleanup_exit] %s\n' "$result" "$cleanup_result"
    if [[ $result == 0 ]]; then result=$cleanup_result; fi
    exit "$result"
}
trap finish EXIT
sudo -n ip link set can0 down
sudo -n ip link set can0 type can bitrate 500000 loopback on fd off
sudo -n ip link set can0 up
ip -details -statistics link show can0
ip -details link show can0 | grep -F 'can <LOOPBACK>'
timeout --signal=TERM --kill-after=2 10 ./can_loopback can0 "$@"
