set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
current=$(readlink "$base/current")
[[ $current == releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e ]]
[[ -f $base/latest_run && ! -L $base/latest_run ]]
run_name=$(cat "$base/latest_run")
[[ $run_name =~ ^[0-9]{8}_[0-9]{6}_[0-9]+$ ]]
old_run=$base/runs/$run_name
[[ -d $old_run && ! -L $old_run && $(readlink -f "$old_run") == "$old_run" && ! -e $old_run/exit_code ]]
app_pid=$(cat "$old_run/app.pid")
[[ $app_pid == 669382 ]]
identity() {
    local prefix=$1 pid=$2 raw
    local -a fields
    [[ $pid =~ ^[1-9][0-9]*$ ]]
    raw=$(cat /proc/$pid/stat)
    read -r -a fields <<<"${raw##*) }"
    printf '%s_PID=%s\n%s_START_TICKS=%s\n%s_UID=' "$prefix" "$pid" "$prefix" "${fields[19]}" "$prefix"
    awk '/^Uid:/{print $3}' /proc/$pid/status
    printf '%s_EXE=%s\n%s_SHA256=' "$prefix" "$(readlink /proc/$pid/exe)" "$prefix"
    sha256sum /proc/$pid/exe | cut -d' ' -f1
}
printf 'FORMAT=sn100-root-runtime-context-v2\nBOARD=board_b\nSERIAL='
tr -d '\0\n' </proc/device-tree/serial-number
printf '\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
printf 'BOOT_ID='; cat /proc/sys/kernel/random/boot_id
printf 'CURRENT=%s\nOLD_RUN_NAME=%s\nOLD_RUN=%s\n' "$current" "$run_name" "$old_run"
[[ $(cat "$old_run/app.start_ticks") == 2102645 ]]
[[ $(readlink /proc/$app_pid/exe) == "$base/$current/sn100_shell" ]]
[[ $(sha256sum /proc/$app_pid/exe | cut -d' ' -f1) == 50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258 ]]
identity MAIN "$app_pid"
identity CONTROLLER 5008
[[ $(readlink /proc/5008/exe) == /userdata/shared/ACA_V1.0OffLineD-500 ]]
[[ $(ss -H -lntp 'sport = :5000' | wc -l) == 1 ]]
printf 'CONTROLLER_LISTEN_ENDPOINTS='
ss -H -lntp 'sport = :5000' | awk '{print $4}'
camera_pid=$(systemctl show opto-camera.service -p MainPID --value)
identity CAMERA "$camera_pid"
three_pid=
for node in /proc/[0-9]*/exe; do
    value=$(readlink "$node" 2>/dev/null || true)
    if [[ ${value##*/} == rkaiq_3A_server ]]; then
        [[ -z $three_pid ]]
        three_pid=${node#/proc/}; three_pid=${three_pid%/exe}
    fi
done
[[ -n $three_pid ]]
identity THREE_A "$three_pid"
printf 'CAMERA_SERVICE_UNIT=opto-camera.service\nCAMERA_SERVICE_ACTIVE='
systemctl show opto-camera.service -p ActiveState --value
printf 'CAMERA_SERVICE_SUB='
systemctl show opto-camera.service -p SubState --value
printf 'CAMERA_SERVICE_MAIN_PID=%s\n' "$camera_pid"
for pointer in original_project_root instrument_configuration_file theme_configuration_file; do
    [[ -f $base/$pointer && ! -L $base/$pointer ]]
    printf 'POINTER_%s_PATH=%s\nPOINTER_%s_SHA256=' "$pointer" "$base/$pointer" "$pointer"
    sha256sum "$base/$pointer" | cut -d' ' -f1
done
manifest=$base/verification/integration_next_02_runtime_01/configuration.sha256
[[ $(sha256sum "$manifest" | cut -d' ' -f1) == ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c ]]
sha256sum --quiet -c "$manifest"
printf 'CONFIGURATION_MANIFEST_SHA256=ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c\n'
printf 'LATEST_RUN_FILE_SHA256='; sha256sum "$base/latest_run" | cut -d' ' -f1
printf 'OLD_HELPER_SHA256='; sha256sum "$base/$current/rk_board.sh" | cut -d' ' -f1
printf 'ACTIVE_VT='; cat /sys/class/tty/tty0/active
printf 'READ_ONLY_CONTEXT=1\nPROGRAM_SIGNALLED=0\nCURRENT_CHANGED=0\n'
