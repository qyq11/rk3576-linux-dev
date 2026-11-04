#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
run=$base/runs/20261104_153722_5015
release=$base/releases/0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
printf 'BEFORE_CURRENT=%s\n' "$(readlink -f "$base/current")"
sha256sum "$base/runtime/data/config.ini" "$base/runtime/data/config_Debug.ini" "$base/runtime/data/theme_config.ini"
if [[ ! -e /proc/5836 ]];then
    printf 'APP_ALREADY_ABSENT=1\n'
    [[ -f $run/exit_code ]];cat "$run/exit_code" "$run/result.txt";exit 0
fi
[[ $(readlink /proc/5836/exe) == "$release/sn100_shell" && $(awk '{print $22}' /proc/5836/stat) == 225261 ]]
[[ $(stat -c %u /proc/5836) == 1000 ]]
[[ $(sha256sum /proc/5836/exe | cut -d' ' -f1) == fd966beae0563c4db798b6be6c0aaa591747f375e23a1259498ab9d362d9baef ]]
printf 'USER_AUTHORIZED_APP_CLOSE=1\n'
cd /
set +e
runuser -u lckfb -- bash -c 'cd /;source "$1";stop_run "$2"' requested-normal-close "$release/rk_board.sh" "$run" > "$base/.board_a_requested_close_20261104_01.log" 2>&1
code=$?
set -e
printf 'GUARDED_CLOSE_ACTUAL_EXIT=%s\n' "$code"
grep -E '^APP_EXIT=|^CLEAN_SHUTDOWN=|^CLOSE_PENDING=|^ERROR:' "$base/.board_a_requested_close_20261104_01.log" || true
grep -E '^SHUTDOWN_|^THREADS_STOPPED|^.*STORE_CLOSED' "$run/app.log" | tail -12 || true
if [[ -e /proc/5836 ]];then printf 'APP_STILL_PRESENT=1\n';else printf 'APP_STILL_PRESENT=0\n';fi
if [[ -f $run/exit_code ]];then printf 'RUN_EXIT_CODE=';cat "$run/exit_code";cat "$run/result.txt";fi
printf 'CONTROLLER_EXE=%s\n' "$(readlink /proc/4426/exe)"
ss -H -ltnp 'sport = :5000'
exit "$code"
