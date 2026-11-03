#!/usr/bin/env bash
# 核对实际运行的新版、原资料参数和主控连接，不读取账户或患者内容。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$#" = 2
phase=$1; expected_release=$2
[[ $phase == first || $phase == reopened ]]
[[ $expected_release =~ ^[a-f0-9]{64}$ ]]
base=/home/rkshare/programs/sn100_shell
check=$base/verification/sn500_connection_20261102/$phase
test ! -e "$check"
install -d -m 750 -o lckfb -g lckfb "$check"
run=$base/runs/$(cat "$base/latest_run")
pid=$(cat "$run/app.pid")
[[ $pid =~ ^[0-9]+$ ]]
test "$(readlink -f "$base/current")" = "$base/releases/$expected_release"
test "$(readlink /proc/"$pid"/exe)" = "$base/releases/$expected_release/sn100_shell"
mapfile -d '' -t arguments </proc/"$pid"/cmdline
printf '%s\n' "${arguments[@]}" >"$check/arguments.txt"
grep -Fx -- --original-db-root "$check/arguments.txt"
grep -Fx -- "$base/runtime/data" "$check/arguments.txt"
grep -Fx -- --instrument-control "$check/arguments.txt"
grep -Fx -- --instrument-config "$check/arguments.txt"
grep -Fx -- "$base/runtime/data/config.ini" "$check/arguments.txt"
grep -Fx -- --theme-config "$check/arguments.txt"
grep -Fx -- "$base/runtime/data/theme_config.ini" "$check/arguments.txt"
! grep -E -- '--test-session|--seed-anonymous|--status-loopback' "$check/arguments.txt"
grep -qx SHELL_READY "$run/app.log"
printf '%s\n' "$run" >"$check/run_path.txt"
printf '%s\n' "$pid" >"$check/app.pid"
cp "$run/runtime.txt" "$check/runtime.txt"
cp "$run/app.log" "$check/app.log"
grep -E 'THEME_CONFIGURATION|ORIGINAL_INI_READ_OK|ORIGINAL_PROJECTS_READY|ORIGINAL_SETTINGS_READY|DEVICE_WIRE|DEVICE_ACTION_STATE|SHELL_READY|ERROR|FAILED|FAILURE' "$run/app.log" >"$check/connection_excerpt.log" || true
ss -ntp | grep ':5000' >"$check/socket.txt"
grep ESTAB "$check/socket.txt"
{
    cd "$base/runtime/data"
    sha256sum config.ini config_Debug.ini theme_config.ini calibrationFile/ReagentAmountCalibration.ini
} >"$check/configuration.sha256"
set +e
(cd "$base/runtime/data" && sha256sum --quiet -c "$base/verification/board_b_original_files_20261102/FILES.sha256") >"$check/original_files_check.log" 2>&1
files_exit=$?
set -e
printf '%s\n' "$files_exit" >"$check/original_files_check_exit.txt"
{
    date -Is
    ps -p "$pid",39408 -o pid,etime,pcpu,rss,nlwp,comm
    for item in "$pid" 39408; do
        printf 'PID=%s\n' "$item"
        grep -E 'VmRSS|VmHWM|Threads' /proc/"$item"/status
        printf 'FD_COUNT='; find /proc/"$item"/fd -maxdepth 1 -type l | wc -l
    done
    for sensor in /sys/class/thermal/thermal_zone*/temp; do printf '%s=' "$sensor"; cat "$sensor"; done
    du -sb "$base/runtime/data"
    stat -c '%s %n' "$run/app.log" /home/rkshare/programs/aca_flow_test/runs/correct_05/live_20261102_173807/program.log
} >"$check/resources_snapshot.txt"
head -22 "$check/connection_excerpt.log"
printf 'ORIGINAL_141_FILES_EXIT=%s\nRUN=%s\nUPPER_PID=%s\n' "$files_exit" "$run" "$pid"
