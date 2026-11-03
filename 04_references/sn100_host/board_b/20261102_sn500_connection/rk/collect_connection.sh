#!/usr/bin/env bash
# 只收存本轮程序日志、参数、指纹和截图，不把原业务库或认证配置放进普通证据。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
check=$base/verification/sn500_connection_20261102
failed=$base/runs/20261102_183413_52463
install -d -m 750 "$check/failed_first_start"
for name in app.log runtime.txt result.txt exit_code release_id; do cp "$failed/$name" "$check/failed_first_start/$name"; done
if [[ -f $check/first/run_path.txt ]]; then
    first=$(cat "$check/first/run_path.txt")
    [[ $first == "$base/runs/"* ]]
    for name in app.log result.txt exit_code; do cp "$first/$name" "$check/first/$name"; done
fi
if [[ -f $check/reopened/run_path.txt ]]; then
    reopened=$(cat "$check/reopened/run_path.txt")
    [[ $reopened == "$base/runs/"* ]]
    cp "$reopened/app.log" "$check/reopened/app.log"
fi
cp /home/rkshare/programs/aca_flow_test/runs/correct_05/live_20261102_173807/program.log "$check/main_control_live_snapshot.log"
{
    date -Is
    tr -d '\0' </proc/device-tree/serial-number; printf '\n'
    readlink "$base/current"
    sha256sum "$base/current/sn100_shell" /userdata/shared/ACA_V1.0OffLineD-500 "$base/runtime/data/theme_config.ini"
    stat -c '%a %U:%G %n' /userdata/shared/ACA_V1.0OffLineD-500
    ps -p 39408 -o pid,etime,args
    ss -ntp | grep ':5000'
} >"$check/final_state.txt"
tar --exclude=sn500_connection_20261102/sn500_theme_connection_rk_check \
    -czf "$base/verification/sn500_connection_20261102.tar.gz" -C "$base/verification" sn500_connection_20261102
sha256sum "$base/verification/sn500_connection_20261102.tar.gz"
