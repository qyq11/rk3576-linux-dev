set -Eeuo pipefail
set -o noclobber
umask 077
template_frozen=true
allow_prepare_flat=true
allow_activate=true
release_id=c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e
expected_current=releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993
expected_app_sha256=50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258
expected_renderer_sha256=92d35399a3a96515b5cac8aed0d803f77c24a0891c307d9e4d026392572ef75b
expected_board_script_sha256=e3587bad320003bf1a2ea7b3176bf14d23527ebaae65b60ffb4bd5388d3866d2
expected_controller_sha256=ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
expected_root_decision_sha256=c27714ee5142d4125d40235da2f551977d5e9dc4580885e34ef8755f0242be3f
expected_file_phase_config_sha256=50b94b7d026385816811ae720d1409b656518dde6929a7e481b4696944462d03
expected_build_complete_sha256=22c82ee0b906eca1d36847212235b428cae9e6676ebfac638b785a91e9b589a9
expected_package_sha256=0cfa398b008df64ebc92aae17e9837633b675adccee73c65083db08b255187ee
configuration_manifest=/home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/configuration.sha256
configuration_manifest_sha256=ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c
old_stop_receipt=/home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/old_stop_receipt.txt
old_stop_receipt_sha256=0000000000000000000000000000000000000000000000000000000000000000
root_transition_approval=/home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/root_approval.md
root_transition_approval_sha256=20747493a79ab553d1c22e6d2d0ac9019a7f9f759d1c98be64201ca835bf6eb2
root_launch_intent=/home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/runtime_launch_intent.json
root_launch_intent_sha256=e8810370280fe56587bf15aa10f0c3e1377ed31c30b5eb307ef0d44bcdda40eb
base=/home/rkshare/programs/sn100_shell
meta=/home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01
flat=/home/rkshare/programs/sn100_shell/releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e
oldrun=/home/rkshare/programs/sn100_shell/runs/20261103_180023_24243
old_pid=24414
old_ticks=555014
old_exe=/home/rkshare/programs/sn100_shell/releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993/sn100_shell
old_exe_sha=5b108ad35472709fccde35c4cc63d4be64637dcfc7e087839122a548c548d238
expected_boot=0eb94389-f120-42ad-9042-9c21d96a1481
controller_expected_identity=0eb94389-f120-42ad-9042-9c21d96a1481:5008:118180:0:/userdata/shared/ACA_V1.0OffLineD-500:ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff:0.0.0.0:5000
checked_record() {
    local path=$1 wanted=$2
    [[ $path == "$base/verification/"* && $path != *'/../'* && $path != *'/./'* && $path != *'//'* ]]
    [[ $wanted =~ ^[a-f0-9]{64}$ && -f $path && ! -L $path && $(stat -c %h "$path") == 1 ]]
    [[ $(readlink -f "$path") == "$path" && $(sha256sum "$path" | cut -d' ' -f1) == "$wanted" ]]
}
transition_process_exe() {
    local node=$1 target proc_dir raw rest later later_rest
    local -a fields=() later_fields=()
    proc_dir=${node%/exe}
    if target=$(readlink -- "$node" 2>/dev/null); then
        [[ -n $target ]] || return 79
        printf '%s\n' "$target"
        return 0
    fi
    # 已消失的进程不再持有exe；其余只能按内核状态明确分类，不能凭名称猜。
    [[ -d $proc_dir ]] || return 1
    if ! raw=$(cat -- "$proc_dir/stat" 2>/dev/null); then
        [[ ! -d $proc_dir ]] && return 1
        echo '存活进程身份不可访问；未切换版本。' >&2
        return 79
    fi
    rest=${raw##*) }
    [[ $rest != "$raw" ]] || return 79
    IFS=' ' read -r -a fields <<<"$rest"
    [[ ${fields[6]-} =~ ^[0-9]+$ && ${fields[19]-} =~ ^[0-9]+$ ]] || return 79
    if [[ ${fields[0]-} == Z || ${fields[0]-} == X ]] || (( (10#${fields[6]} & 0x00200000) != 0 )); then
        if ! later=$(cat -- "$proc_dir/stat" 2>/dev/null); then
            [[ ! -d $proc_dir ]] && return 1
            return 79
        fi
        later_rest=${later##*) }
        [[ $later_rest != "$later" ]] || return 79
        IFS=' ' read -r -a later_fields <<<"$later_rest"
        [[ ${later_fields[6]-} =~ ^[0-9]+$ && ${later_fields[19]-} == "${fields[19]}" ]] || return 79
        # 内核线程的S/R/D会随调度改变；两次同启动身份且PF_KTHREAD均置才跳过。
        if (( (10#${fields[6]} & 0x00200000) != 0 && (10#${later_fields[6]} & 0x00200000) != 0 )); then
            return 1
        fi
        # 死亡状态可在Z/X间变化；普通存活用户进程的exe不可读仍拒绝。
        if [[ ${fields[0]-} == Z || ${fields[0]-} == X ]] && [[ ${later_fields[0]-} == Z || ${later_fields[0]-} == X ]]; then
            return 1
        fi
        return 79
    fi
    [[ ! -d $proc_dir ]] && return 1
    echo '存活用户进程exe不可访问；未切换版本。' >&2
    return 79
}
transition_controller_pid() {
    local node target status pid name=ACA_V1.0OffLineD-500
    local -a matches=()
    for node in /proc/[0-9]*/exe; do
        if target=$(transition_process_exe "$node"); then
            case ${target##*/} in
                "$name"|"$name (deleted)") pid=${node#/proc/}; matches+=("${pid%/exe}") ;;
            esac
        else
            status=$?
            [[ $status == 1 ]] || return "$status"
        fi
    done
    [[ ${#matches[@]} == 1 ]] || { echo '同名主控缺失或多份；未切换版本。' >&2; return 79; }
    printf '%s\n' "${matches[0]}"
}
transition_start_ticks() {
    local raw rest
    local -a fields=()
    raw=$(cat -- "/proc/$1/stat") || return 79
    rest=${raw##*) }
    [[ $rest != "$raw" ]] || return 79
    IFS=' ' read -r -a fields <<<"$rest"
    [[ ${fields[19]-} =~ ^[1-9][0-9]*$ ]] || return 79
    printf '%s\n' "${fields[19]}"
}
transition_uid() {
    local value
    value=$(awk '$1=="Uid:" {print $3}' "/proc/$1/status") || return 79
    [[ $value =~ ^[0-9]+$ ]] || return 79
    printf '%s\n' "$value"
}
check_controller() {
    local pid ticks uid exe digest boot listening row owners endpoints
    pid=$(transition_controller_pid)
    ticks=$(transition_start_ticks "$pid")
    uid=$(transition_uid "$pid")
    exe=$(readlink -- "/proc/$pid/exe")
    [[ $exe == /userdata/shared/ACA_V1.0OffLineD-500 ]]
    digest=$(sha256sum -- "/proc/$pid/exe" | cut -d' ' -f1)
    [[ $digest == "$expected_controller_sha256" ]]
    boot=$(cat /proc/sys/kernel/random/boot_id)
    [[ $boot =~ ^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$ ]]
    listening=$(LC_ALL=C ss -H -ltnp 'sport = :5000')
    [[ -n $listening ]]
    while IFS= read -r row; do
        owners=$(printf '%s\n' "$row" | grep -oE 'pid=[0-9]+' | cut -d= -f2 | LC_ALL=C sort -u)
        [[ $owners == "$pid" ]]
    done <<<"$listening"
    endpoints=$(printf '%s\n' "$listening" | awk '{print $4}' | LC_ALL=C sort | paste -sd, -)
    [[ -n $endpoints && $endpoints != *$'\n'* && $endpoints != *$'\t'* && $endpoints != *' '* ]]
    [[ $(transition_controller_pid) == "$pid" && $(transition_start_ticks "$pid") == "$ticks" ]]
    [[ $(transition_uid "$pid") == "$uid" && $(readlink -- "/proc/$pid/exe") == "$exe" ]]
    [[ $(sha256sum -- "/proc/$pid/exe" | cut -d' ' -f1) == "$digest" && $(cat /proc/sys/kernel/random/boot_id) == "$boot" ]]
    controller_pid=$pid
    controller_identity="$boot:$pid:$ticks:$uid:$exe:$digest:$endpoints"
}
transition_assert_quiet() {
    local node target status service_state display_status=0 display_owners
    for node in /proc/[0-9]*/exe; do
        if target=$(transition_process_exe "$node"); then
            case ${target##*/} in
                sn100_shell|'sn100_shell (deleted)'|sn100_report_renderer|'sn100_report_renderer (deleted)'|status_login*review*)
                    echo '仍有GUI、报告或检查进程；未切换版本。' >&2; return 79 ;;
            esac
        else
            status=$?
            [[ $status == 1 ]] || return "$status"
        fi
    done
    service_state=$(systemctl show --property=ActiveState --value sn100-shell.service)
    case $service_state in
        inactive|failed) ;;
        *) echo '原图形服务仍活动或状态未确认；未切换版本。' >&2; return 79 ;;
    esac
    display_owners=$(fuser /dev/tty7 /dev/dri/card0 2>&1) || display_status=$?
    [[ $display_status == 1 && -z $display_owners ]] || { echo 'VT/DRM仍有持有者或读取未确认；未切换版本。' >&2; return 79; }
}

root_assert() {
    [[ $(tr -d '\0\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
    [[ $(cat /proc/sys/kernel/random/boot_id) == "$expected_boot" ]]
    [[ $(readlink "$base/current") == "$1" ]]
    checked_record "$root_transition_approval" "$root_transition_approval_sha256"
    checked_record "$root_launch_intent" "$root_launch_intent_sha256"
    checked_record "$configuration_manifest" "$configuration_manifest_sha256"
    sha256sum --quiet -c "$configuration_manifest"
    printf '%s  %s\n' 423baacf81fc1073b653eedd14651d701c3648b7e3324491d2a53f9d7af0f7a9 /home/rkshare/programs/sn100_shell/original_project_root | sha256sum --quiet -c -
    printf '%s  %s\n' acf0371f227fc6fbcad1cff6a355c956d34105cec33546d980abab9317416422 /home/rkshare/programs/sn100_shell/instrument_configuration_file | sha256sum --quiet -c -
    printf '%s  %s\n' b89f9a8af4a052739555b0df65127859c7060bffc3ca204de1238556f7d3bf5c /home/rkshare/programs/sn100_shell/theme_configuration_file | sha256sum --quiet -c -
    [[ $(sha256sum "$flat/rk_board.sh" | cut -d' ' -f1) == "$expected_board_script_sha256" ]]
    [[ $(sha256sum "$flat/SHA256SUMS" | cut -d' ' -f1) == "$release_id" ]]
    (cd "$flat" && sha256sum --quiet -c SHA256SUMS)
    check_controller
    [[ $controller_identity == "$controller_expected_identity" ]]
}

[[ ! -L $base/immutable_store.lock ]]
exec 8>>"$base/immutable_store.lock"
flock -n 8
root_assert "$expected_current"
[[ $(cat "$base/latest_run") == "${oldrun##*/}" && $(readlink -f "$oldrun") == "$oldrun" ]]
[[ $(cat "$oldrun/app.pid") == "$old_pid" && $(cat "$oldrun/app.start_ticks") == "$old_ticks" && ! -e $oldrun/exit_code ]]
[[ $(readlink /proc/$old_pid/exe) == "$old_exe" && $(transition_start_ticks "$old_pid") == "$old_ticks" ]]
[[ $(sha256sum /proc/$old_pid/exe | cut -d' ' -f1) == "$old_exe_sha" && $(transition_uid "$old_pid") == "$(id -u lckfb)" ]]
[[ ! -e $meta/old_stop.stdout.log && ! -e $meta/old_stop.stderr.log && ! -e $old_stop_receipt ]]
stop_code=0
# 只此一次正常关闭旧版；失败不强杀、不再发信号。
/usr/sbin/runuser -u lckfb -- /bin/bash "$base/$expected_current/rk_board.sh" stop "$base" >"$meta/old_stop.stdout.log" 2>"$meta/old_stop.stderr.log" || stop_code=$?
printf '%s\n' "$stop_code" > "$meta/old_stop.actual_exit"
printf 'OLD_STOP_ACTUAL_EXIT=%s\n' "$stop_code"
[[ $stop_code == 0 ]]
[[ -f $oldrun/exit_code && $(cat "$oldrun/exit_code") == 0 ]]
grep -qx 'APP_EXIT=0' "$oldrun/result.txt"
grep -qx 'CLEAN_SHUTDOWN=1' "$oldrun/result.txt"
# 只等旧服务自然退出及显示释放，不调用systemctl stop或额外信号。
quiet=0
for ((i=0;i<4;i++)); do
    if transition_assert_quiet; then quiet=1; break; fi
    sleep 0.1
done
[[ $quiet == 1 ]]
[[ -f $base/lifecycle.lock && ! -L $base/lifecycle.lock ]]
exec 9<>"$base/lifecycle.lock"
flock -n 9
root_assert "$expected_current"
transition_assert_quiet
{
    printf 'FORMAT=sn100-root-old-stop-receipt-v1\nBOARD=board_b\nSERIAL=8314b1e8ffd0321f\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
    printf 'BOOT_ID=%s\nOLD_RUN=%s\nOLD_PID=%s\nOLD_START_TICKS=%s\n' "$expected_boot" "$oldrun" "$old_pid" "$old_ticks"
    printf 'OLD_EXE=%s\nOLD_EXE_SHA256=%s\n' "$old_exe" "$old_exe_sha"
    cat "$oldrun/result.txt"
    printf 'STOP_TOOL_ACTUAL_EXIT=%s\nDISPLAY_RELEASED=1\nCURRENT_UNCHANGED=%s\nCONTROLLER_IDENTITY=%s\n' "$stop_code" "$expected_current" "$controller_identity"
    printf 'ROOT_LAUNCH_INTENT_SHA256=%s\nCONFIGURATION_MANIFEST_SHA256=%s\n' "$root_launch_intent_sha256" "$configuration_manifest_sha256"
} > "$old_stop_receipt"
printf 'OLD_STOP_RECEIPT_SHA256='; sha256sum "$old_stop_receipt" | cut -d' ' -f1
cat "$old_stop_receipt"
