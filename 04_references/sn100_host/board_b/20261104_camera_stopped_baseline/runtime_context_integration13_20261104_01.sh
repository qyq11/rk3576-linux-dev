set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]
printf 'FORMAT=sn100-root-runtime-context-camera-stopped-v1\nBOARD=board_b\nSERIAL='
tr -d '\0\n' </proc/device-tree/serial-number
printf '\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
printf 'BOOT_ID='; cat /proc/sys/kernel/random/boot_id
sha256sum --quiet -c "$base/verification/integration_12_01_runtime_02/configuration.sha256"
printf 'CONFIGURATION_MANIFEST_SHA256=ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c\n'
for name in original_project_root instrument_configuration_file theme_configuration_file; do sha256sum "$base/$name"; done
printf 'CURRENT='; readlink "$base/current"
printf 'LATEST_RUN='; cat "$base/latest_run"
[[ $(readlink "$base/current") == releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e ]]
old=$base/runs/20261103_221819_662833
[[ ! -e $old/exit_code && ! -L $old/exit_code && ! -e $old/result.txt && ! -L $old/result.txt ]]
identity() {
    local prefix=$1 pid=$2 before rest
    local -a fields
    before=$(cat /proc/$pid/stat); rest=${before##*) }
    IFS=' ' read -r -a fields <<<"$rest"
    printf '%s_PID=%s\n%s_START_TICKS=%s\n%s_UID=' "$prefix" "$pid" "$prefix" "${fields[19]}" "$prefix"
    awk '/^Uid:/{print $3}' /proc/$pid/status
    printf '%s_EXE=%s\n%s_SHA256=' "$prefix" "$(readlink /proc/$pid/exe)" "$prefix"
    sha256sum /proc/$pid/exe | cut -d' ' -f1
}
identity MAIN 669382
identity CONTROLLER 5008
identity THREE_A 139148
printf 'CAMERA_SERVICE_BEGIN\n'
systemctl show opto-camera.service -p Id -p ActiveState -p SubState -p MainPID -p UnitFileState -p NRestarts
[[ $(systemctl show opto-camera.service -p MainPID --value) == 0 ]]
[[ $(systemctl show opto-camera.service -p ActiveState --value) == inactive ]]
[[ -z $(ss -H -lntp 'sport = :8080') ]]
printf 'CAMERA_8080_LISTENER_ABSENT=1\nCAMERA_SERVICE_END\n'
printf 'CONTROLLER_CONNECTIONS_BEGIN\n'
ss -H -lntp 'sport = :5000'
ss -H -ntp '( sport = :5000 or dport = :5000 )'
printf 'CONTROLLER_CONNECTIONS_END\nACTIVE_VT='; cat /sys/class/tty/tty0/active
printf 'OLD_APPLICATION_STATUS_BEGIN\n'
awk '/^State:|^VmRSS:|^Threads:/{print}' /proc/669382/status
printf 'OLD_APPLICATION_STATUS_END\nAPP_LOG_BYTES='; stat -c %s "$old/app.log"
df -Pk "$base"
printf 'READ_ONLY_CONTEXT=1\nPROGRAM_SIGNALLED=0\nCAMERA_STARTED_OR_STOPPED_BY_THIS_CALL=0\nCURRENT_CHANGED=0\n'
