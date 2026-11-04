#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
run=$base/runs/20261104_153722_5015
out=$base/reviews/sample_order_board_a_20261104_01
expected=$base/releases/0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743/sn100_shell
identity() {
    [[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
    [[ $(cat "$run/app.pid") == 5836 && $(cat "$run/app.start_ticks") == 225261 ]]
    [[ $(readlink /proc/5836/exe) == "$expected" && $(awk '{print $22}' /proc/5836/stat) == 225261 ]]
    [[ $(stat -c %u /proc/5836) == 1000 ]]
    [[ $(sha256sum /proc/5836/exe | cut -d' ' -f1) == fd966beae0563c4db798b6be6c0aaa591747f375e23a1259498ab9d362d9baef ]]
    [[ $(readlink -f "$base/current") == "${expected%/sn100_shell}" ]]
    [[ ! -e $run/exit_code && ! -e $run/result.txt ]]
    [[ $(readlink /proc/4426/exe) == /userdata/shared/ACA_V1.0OffLineD-500 && $(awk '{print $22}' /proc/4426/stat) == 196590 ]]
    printf 'BOOT=559493ce-5319-42b6-9383-bf7b93764a35\nAPP_PID=5836\nAPP_TICKS=225261\nAPP_UID=1000\nCONTROLLER_PID=4426\nCONTROLLER_TICKS=196590\nCURRENT=%s\n' "${expected%/sn100_shell}"
}
printf 'IDENTITY_BEFORE_BEGIN\n';identity;printf 'IDENTITY_BEFORE_END\n'
for file in result.json check_exit.txt first_new_empty_rack.png second_new_empty_rack.png; do
    path=$out/$file
    if [[ -f $path && ! -L $path ]]; then
        printf 'CAPTURE_FILE_BEGIN=%s\n' "$file"
        sha256sum "$path"
        printf 'BASE64=';base64 -w0 "$path";printf '\n'
        printf 'CAPTURE_FILE_END=%s\n' "$file"
    else printf 'CAPTURE_FILE_ABSENT=%s\n' "$file";fi
done
printf 'COMPLETION_LINE_BEGIN\n'
grep '^SAMPLE_ADD_REVIEW_COMPLETE ' "$run/app.log"
printf 'COMPLETION_LINE_END\n'
printf 'CONFIGURATION_FINAL_BEGIN\n'
sha256sum "$base/runtime/data/config.ini" "$base/runtime/data/config_Debug.ini" "$base/runtime/data/theme_config.ini" /userdata/shared/config_linux.ini /userdata/shared/consume.ini /userdata/shared/GL4A_config.cfg /userdata/shared/number.ini
sha256sum "$base/original_project_root" "$base/instrument_configuration_file" "$base/theme_configuration_file"
printf 'CONFIGURATION_FINAL_END\n'
printf 'LOADED_QTTEST_BEGIN\n';grep '/libQt5Test.so.5' /proc/5836/maps;printf 'LOADED_QTTEST_END\n'
printf 'PASSIVE_RESOURCE_SNAPSHOT_BEGIN\n'
awk '/^VmRSS:|^VmHWM:|^Threads:|^State:/{print}' /proc/5836/status
if [[ -f /proc/5836/smaps_rollup ]];then awk '/^Rss:|^Pss:/{print}' /proc/5836/smaps_rollup;fi
printf 'PASSIVE_RESOURCE_SNAPSHOT_END\n'
ss -H -tnp '( sport = :5000 or dport = :5000 )'
printf 'ACTIVE_VT=%s\n' "$(cat /sys/class/tty/tty0/active)"
printf 'IDENTITY_AFTER_BEGIN\n';identity;printf 'IDENTITY_AFTER_END\n'
printf 'APPLICATION_EXIT=null\nAPPLICATION_KEEP_RUNNING=1\nNO_STOP_OR_RETRY_REQUESTED=1\nFINAL_CAPTURE_COMPLETE=1\n'
