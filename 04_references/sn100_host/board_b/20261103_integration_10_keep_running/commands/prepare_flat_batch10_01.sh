set -- prepare-flat
#!/usr/bin/env bash
# 供总控另行冻结的板端模板；此原件不可执行，不包含停止/启动或超时TERM。
set -Eeuo pipefail
set -o noclobber
umask 077
template_frozen=true
[[ $template_frozen == true ]] || { echo '下一批切换模板未冻结；没有执行任何板端操作。' >&2; exit 78; }

# 这些值只由总控从新完整构建、真实配置和包后启动意图填写，再重新审查SHA。
allow_prepare_flat=true
allow_activate=true
release_id='c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e'
expected_current='releases/b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993'
expected_app_sha256='50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258'
expected_renderer_sha256='92d35399a3a96515b5cac8aed0d803f77c24a0891c307d9e4d026392572ef75b'
expected_board_script_sha256='e3587bad320003bf1a2ea7b3176bf14d23527ebaae65b60ffb4bd5388d3866d2'
expected_controller_sha256='ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff'
expected_root_decision_sha256='c27714ee5142d4125d40235da2f551977d5e9dc4580885e34ef8755f0242be3f'
expected_file_phase_config_sha256='50b94b7d026385816811ae720d1409b656518dde6929a7e481b4696944462d03'
expected_build_complete_sha256='22c82ee0b906eca1d36847212235b428cae9e6676ebfac638b785a91e9b589a9'
expected_package_sha256='0cfa398b008df64ebc92aae17e9837633b675adccee73c65083db08b255187ee'
configuration_manifest='/home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/configuration.sha256'
configuration_manifest_sha256='ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c'
old_stop_receipt='/home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/old_stop_receipt.txt'
old_stop_receipt_sha256='0000000000000000000000000000000000000000000000000000000000000000'
root_transition_approval='/home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/root_approval.md'
root_transition_approval_sha256='20747493a79ab553d1c22e6d2d0ac9019a7f9f759d1c98be64201ca835bf6eb2'
root_start_owner=root
observation_schema=sn100-lis-keep-running-observation-v1
root_launch_intent='/home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01/runtime_launch_intent.json'
root_launch_intent_sha256='e8810370280fe56587bf15aa10f0c3e1377ed31c30b5eb307ef0d44bcdda40eb'

base=/home/rkshare/programs/sn100_shell
action=${1:-}
[[ $# == 1 && ( $action == prepare-flat || $action == activate-only ) ]]
[[ $EUID == 0 && $(tr -d '\0\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
[[ $release_id =~ ^[a-f0-9]{64}$ && $expected_current =~ ^releases/[a-f0-9]{64}$ ]]
[[ releases/$release_id != "$expected_current" ]]
[[ $expected_app_sha256 =~ ^[a-f0-9]{64}$ && $expected_renderer_sha256 =~ ^[a-f0-9]{64}$ ]]
[[ $expected_board_script_sha256 =~ ^[a-f0-9]{64}$ ]]
[[ $expected_controller_sha256 =~ ^[a-f0-9]{64}$ ]]
for frozen_sha in "$expected_root_decision_sha256" "$expected_file_phase_config_sha256" "$expected_build_complete_sha256" "$expected_package_sha256"; do
    [[ $frozen_sha =~ ^[a-f0-9]{64}$ ]]
done
[[ $(readlink -f "$base") == "$base" && ! -L $base/releases ]]
[[ $(readlink -f "$base/releases") == "$base/releases" ]]
[[ -d $base/verification && ! -L $base/verification && $(readlink -f "$base/verification") == "$base/verification" ]]
[[ -L $base/current && $(readlink "$base/current") == "$expected_current" ]]
[[ ! -L $base/immutable_store.lock ]]
exec 8>>"$base/immutable_store.lock"
flock -n 8
[[ $(readlink "$base/current") == "$expected_current" ]]

nested=$base/releases/integration_next_02/$release_id
flat=$base/releases/$release_id
[[ ! -L $base/releases/integration_next_02 && $(readlink -f "$nested") == "$nested" ]]
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
        [[ ${later_fields[0]-} == "${fields[0]}" && ${later_fields[6]-} == "${fields[6]}" && ${later_fields[19]-} == "${fields[19]}" ]] || return 79
        return 1  # 僵尸/死亡或PF_KTHREAD内核线程没有存活用户exe。
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
checked_record "$root_transition_approval" "$root_transition_approval_sha256"
checked_record "$configuration_manifest" "$configuration_manifest_sha256"
checked_record "$root_launch_intent" "$root_launch_intent_sha256"
# 仅接受本合同固定顺序的平铺JSON：ASCII安全字符串、布尔及数值0，无转义/嵌套。
# 不eval或source数据；未知格式停止，不使用板端Python/jq或猜测字段。
intent_keys=(format frozen release_id application_exe application_sha256 application_sha_source
    observation_schema observation_dir observation_id observation_release root_decision_sha256 start_owner
    supervise_seconds allow_prepare_flat allow_activation allow_old_app_stop allow_unique_start leave_running_required
    file_phase_config_path file_phase_config_sha256 build_complete_path build_complete_sha256 package_path package_sha256)
mapfile -t intent_lines < "$root_launch_intent"
[[ ${#intent_lines[@]} == $((${#intent_keys[@]} + 2)) && ${intent_lines[0]} == '{' && ${intent_lines[-1]} == '}' ]]
declare -A intent=()
for index in "${!intent_keys[@]}"; do
    line=${intent_lines[index+1]}
    if ((index + 1 < ${#intent_keys[@]})); then [[ $line == *, ]]; line=${line%,}; else [[ $line != *, ]]; fi
    scalar_pattern='^  "([a-z_][a-z0-9_]*)": ("([A-Za-z0-9_./:-]*)"|true|false|0)$'
    [[ $line =~ $scalar_pattern ]]
    key=${BASH_REMATCH[1]}; scalar=${BASH_REMATCH[2]}; text=${BASH_REMATCH[3]-}
    [[ $key == "${intent_keys[index]}" && ! ${intent[$key]+exists} ]]
    case $key in
        frozen|allow_prepare_flat|allow_activation|allow_old_app_stop|allow_unique_start|leave_running_required)
            [[ $scalar == true || $scalar == false ]]; intent[$key]=$scalar ;;
        supervise_seconds) [[ $scalar == 0 ]]; intent[$key]=0 ;;
        *) [[ $scalar == \"*\" ]]; intent[$key]=$text ;;
    esac
done
[[ ${intent[format]} == sn100-root-launch-intent-v1 && ${intent[frozen]} == true ]]
[[ ${intent[release_id]} == "$release_id" && ${intent[observation_release]} == "$release_id" ]]
[[ ${intent[application_exe]} == "$flat/sn100_shell" && ${intent[application_sha256]} == "$expected_app_sha256" ]]
[[ ${intent[application_sha_source]} == external_collector && ${intent[observation_schema]} == "$observation_schema" ]]
[[ ${intent[root_decision_sha256]} == "$expected_root_decision_sha256" && ${intent[start_owner]} == root ]]
[[ ${intent[file_phase_config_sha256]} == "$expected_file_phase_config_sha256" ]]
[[ ${intent[build_complete_sha256]} == "$expected_build_complete_sha256" && ${intent[package_sha256]} == "$expected_package_sha256" ]]
[[ ${intent[supervise_seconds]} == 0 && ${intent[leave_running_required]} == true ]]
for name in file_phase_config_path build_complete_path package_path; do
    path=${intent[$name]}
    [[ -n $path && $path != /* && $path != *:* && $path != *'/../'* && $path != ../* && $path != *'/./'* && $path != ./* && $path != *'//'* ]]
done
[[ ${intent[package_path]##*/} == "sn100_shell_$release_id.tar.gz" ]]
[[ ${intent[file_phase_config_path]##*/} == integration_next_tools_config.json && ${intent[build_complete_path]##*/} == build_complete.json ]]
observation_dir=${intent[observation_dir]}
[[ $observation_dir == "$base/verification/"* && $observation_dir != *'/../'* && $observation_dir != *'/./'* && $observation_dir != *'//'* ]]
[[ -d $observation_dir && ! -L $observation_dir && $(readlink -f "$observation_dir") == "$observation_dir" ]]
[[ $(stat -c %u "$observation_dir") == "$(id -u lckfb)" && $(stat -c %a "$observation_dir") == 700 ]]
[[ ! -e $observation_dir/lis_observation.json && ! -L $observation_dir/lis_observation.json ]]
[[ ! -e $observation_dir/lis_observation.ready && ! -L $observation_dir/lis_observation.ready ]]
[[ ${intent[observation_id]} =~ ^[A-Za-z0-9_.-]+$ ]]
# 在source包内任何代码前，先用固定系统命令核Root批准的完整清单和helper实物。
[[ -f $nested/release_id && ! -L $nested/release_id && $(cat "$nested/release_id") == "$release_id" ]]
[[ -f $nested/SHA256SUMS && ! -L $nested/SHA256SUMS ]]
[[ $(sha256sum "$nested/SHA256SUMS" | cut -d' ' -f1) == "$release_id" ]]
[[ -z $(find "$nested" -type l -print -quit) ]]
[[ -z $(find "$nested" ! -type f ! -type d -print -quit) ]]
(cd "$nested" && sha256sum --quiet -c SHA256SUMS)
[[ -f $nested/rk_board.sh && ! -L $nested/rk_board.sh ]]
[[ $(sha256sum "$nested/rk_board.sh" | cut -d' ' -f1) == "$expected_board_script_sha256" ]]
source "$nested/rk_board.sh"
verify_release "$nested"
[[ $(sha256sum "$nested/sn100_shell" | cut -d' ' -f1) == "$expected_app_sha256" ]]
[[ $(sha256sum "$nested/sn100_report_renderer" | cut -d' ' -f1) == "$expected_renderer_sha256" ]]
sha256sum --quiet -c "$configuration_manifest"
check_controller
controller_guard_before=$controller_identity

if [[ $action == prepare-flat ]]; then
    [[ $allow_prepare_flat == true && ${intent[allow_prepare_flat]} == true && ! -e $flat && ! -L $flat ]]
    # 独立新目录复制，不使用生命周期锁，不改旧GUI/current；失败保留新现场。
    cp -a -- "$nested" "$flat"
    verify_release "$nested"
    verify_release "$flat"
    [[ $(sha256sum "$flat/SHA256SUMS" | cut -d' ' -f1) == "$release_id" ]]
    [[ $(readlink "$base/current") == "$expected_current" ]]
    sha256sum --quiet -c "$configuration_manifest"
    check_controller
    [[ $controller_identity == "$controller_guard_before" ]]
    printf 'FLAT_PREPARED=1\nCURRENT_CHANGED=0\nPROGRAM_STARTED=0\nCLOSE_REQUESTED=0\n'
    exit 0
fi

[[ $allow_activate == true && $root_start_owner == root && $observation_schema == sn100-lis-keep-running-observation-v1 ]]
[[ ${intent[allow_activation]} == true && ${intent[allow_old_app_stop]} == true && ${intent[allow_unique_start]} == true ]]
checked_record "$root_launch_intent" "$root_launch_intent_sha256"
checked_record "$old_stop_receipt" "$old_stop_receipt_sha256"
# 旧应用停止与唯一新启动只由总控安排。锁前、锁后、原子切前用同一组检查，不发信号。
transition_assert_quiet
[[ -f $base/lifecycle.lock && ! -L $base/lifecycle.lock ]]
exec 9<>"$base/lifecycle.lock"
flock -n 9
transition_assert_quiet
[[ -L $base/current && $(readlink "$base/current") == "$expected_current" ]]
verify_release "$flat"
verify_release "$nested"
checked_record "$old_stop_receipt" "$old_stop_receipt_sha256"
checked_record "$root_transition_approval" "$root_transition_approval_sha256"
checked_record "$root_launch_intent" "$root_launch_intent_sha256"
sha256sum --quiet -c "$configuration_manifest"
check_controller
[[ $controller_identity == "$controller_guard_before" ]]

audit=$base/verification/transition_next_02_$release_id
[[ ! -e $audit && ! -L $audit ]]
mkdir -m 750 -- "$audit"
printf '%s\n' "$expected_current" > "$audit/current_before.txt"
printf '%s\n' "$old_stop_receipt_sha256" > "$audit/old_stop_receipt.sha256"
printf '%s\n' "$root_transition_approval_sha256" > "$audit/root_approval.sha256"
printf '%s\n' "$root_launch_intent_sha256" > "$audit/root_launch_intent.sha256"
temporary=$base/.current-next-$$
[[ ! -e $temporary && ! -L $temporary ]]
ln -s "releases/$release_id" "$temporary"
chown -h 0:0 "$temporary"
[[ $(readlink "$base/current") == "$expected_current" ]]
transition_assert_quiet
mv -Tf -- "$temporary" "$base/current"
[[ $(current_release "$base") == "$flat" ]]
printf '%s\n' "$(readlink "$base/current")" > "$audit/current_after.txt"
# 必须先释放父级生命周期锁，再交总控独立启动；本脚本不启动程序或主控。
exec 9>&-
exec 8>&-
printf 'ACTIVATED=1\nPROGRAM_STARTED=0\nNESTED_PRESERVED=1\nCLOSE_REQUESTED=0\nLIVE_HANDOFF_PENDING=1\nSTART_OWNER=root\nROOT_LAUNCH_INTENT_SHA256=%s\n' "$root_launch_intent_sha256"
