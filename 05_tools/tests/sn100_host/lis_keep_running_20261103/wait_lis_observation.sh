#!/usr/bin/env bash
# 只等待已获准进程的LIS封口快照。截止时只结束本工具，不向产品发送信号。
# Root须先核板卡和新包，提供真实身份；本脚本不启动程序，不切换current。
# 板端路径暂限不含空格的规范绝对路径；已有日志和资源采样不在采集范围。
set -euo pipefail
umask 077

result_directory= capture_directory= pid= start_ticks= release_id= executable=
executable_sha256= observation_id= timeout_seconds= boot_id= board_serial=
current_link= expected_current_target= release_id_file=
capture_created=0
capture_complete=0

fail()
{
    printf 'LIS采集未完成：%s\n' "$1" >&2
    exit "${2:-1}"
}

on_exit()
{
    local actual_code=$?
    if (( capture_created && !capture_complete )); then
        # 不写本工具尚未由外层取得的真实退出码，也不推断产品已退出。
        printf '%s\n' '{"schema":"sn100-lis-live-capture-status-v1","collection_complete":false,"application_exit":null,"application_exit_evaluated":false,"shutdown_evaluated":false,"reason":"incomplete_capture_see_stderr"}' \
            > "$capture_directory/capture_status.json" || true
    fi
    return "$actual_code"
}
trap on_exit EXIT

declare -A supplied=()
supplied_count=0
while (( $# )); do
    (( $# >= 2 )) || fail '每个参数都必须带值。' 2
    option=$1
    case "$option" in
        --result-directory|--capture-directory|--pid|--start-ticks|--release-id|--executable|--executable-sha256|--observation-id|--timeout-seconds|--boot-id|--board-serial|--current-link|--expected-current-target|--release-id-file) ;;
        *) fail '存在未支持的参数。' 2 ;;
    esac
    [[ -z ${supplied[$option]+x} ]] || fail '参数重复。' 2
    supplied[$option]=1
    supplied_count=$((supplied_count + 1))
    case "$option" in
        --result-directory) result_directory=$2 ;;
        --capture-directory) capture_directory=$2 ;;
        --pid) pid=$2 ;;
        --start-ticks) start_ticks=$2 ;;
        --release-id) release_id=$2 ;;
        --executable) executable=$2 ;;
        --executable-sha256) executable_sha256=$2 ;;
        --observation-id) observation_id=$2 ;;
        --timeout-seconds) timeout_seconds=$2 ;;
        --boot-id) boot_id=$2 ;;
        --board-serial) board_serial=$2 ;;
        --current-link) current_link=$2 ;;
        --expected-current-target) expected_current_target=$2 ;;
        --release-id-file) release_id_file=$2 ;;
    esac
    shift 2
done

[[ "$supplied_count" == 14 ]] || fail '必须显式提供全部14个参数。' 2
[[ "$pid" =~ ^[1-9][0-9]*$ && "$start_ticks" =~ ^[1-9][0-9]*$ ]] || fail 'PID或启动时刻格式错误。' 2
[[ "$release_id" =~ ^[0-9a-f]{64}$ && "$executable_sha256" =~ ^[0-9a-f]{64}$ ]] || fail '发布或程序指纹格式错误。' 2
[[ "$observation_id" =~ ^[A-Za-z0-9_-]{1,64}$ ]] || fail '观察ID格式错误。' 2
[[ "$boot_id" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]] || fail 'boot_id格式错误。' 2
[[ "$board_serial" == 8314b1e8ffd0321f ]] || fail '本模板只接受已确认的板B序列号。' 2
[[ "$timeout_seconds" =~ ^[1-9][0-9]{0,2}$ ]] || fail '等待上限须为1至600秒。' 2
(( 10#$timeout_seconds <= 600 )) || fail '等待上限须为1至600秒。' 2

safe_path()
{
    [[ "$1" =~ ^/[A-Za-z0-9_./-]+$ ]] || fail '路径须为无空格的安全绝对路径。' 2
    [[ "$1" != *'/../'* && "$1" != */.. && "$1" != *'/./'* && "$1" != */. && "$1" != *'//'* && "$1" != */ ]] \
        || fail '路径含非规范部分。' 2
}

canonical_directory()
{
    [[ -d "$1" && ! -L "$1" && "$(readlink -f -- "$1")" == "$1" ]] \
        || fail '目录不存在或经过符号链接重定向。' 2
}

for path in "$result_directory" "$capture_directory" "$executable" "$current_link" "$expected_current_target" "$release_id_file"; do
    safe_path "$path"
done
canonical_directory "$result_directory"
canonical_directory "$expected_current_target"
[[ -f "$executable" && ! -L "$executable" && "$(readlink -f -- "$executable")" == "$executable" ]] \
    || fail '程序须为实际包内的规范文件路径。' 2
[[ -f "$release_id_file" && ! -L "$release_id_file" && "$(readlink -f -- "$release_id_file")" == "$release_id_file" ]] \
    || fail 'release_id文件须为实际包内的规范文件路径。' 2
[[ "$executable" == "$expected_current_target/"* && "$release_id_file" == "$expected_current_target/"* ]] \
    || fail '程序和release_id文件须属于本次current目标包。' 2
current_parent=${current_link%/*}
canonical_directory "${current_parent:-/}"
[[ -L "$current_link" ]] || fail 'current-link必须是实际符号链接。' 2
capture_parent=${capture_directory%/*}
canonical_directory "${capture_parent:-/}"
[[ "$capture_directory" != "$result_directory" && "$capture_directory" != "$result_directory/"* ]] \
    || fail '采集目录不能等于结果目录或放在其中。' 2
[[ ! -e "$capture_directory" && ! -L "$capture_directory" ]] || fail '采集目录已存在，拒绝覆盖。' 2
mkdir -- "$capture_directory" || fail '无法独占创建新的采集目录。'
capture_created=1

read_process()
{
    local stat_text stat_tail
    [[ -r "/proc/$pid/stat" ]] || fail '受权进程已不存在或其身份不可读。'
    stat_text=$(<"/proc/$pid/stat")
    # comm字段可包含空格或括号；从最后一个右括号之后读取第3字段起的内容。
    stat_tail=${stat_text##*) }
    local -a fields
    read -r -a fields <<< "$stat_tail"
    (( ${#fields[@]} >= 20 )) || fail '进程stat字段不足。'
    [[ ${fields[0]} != Z && ${fields[0]} != X && ${fields[0]} != x ]] || fail '进程已为僵尸或退出状态。'
    [[ ${fields[19]} == "$start_ticks" ]] || fail 'PID已复用或启动时刻不符。'
    [[ "$(readlink -f -- "/proc/$pid/exe")" == "$executable" ]] || fail '活进程的实际程序路径不符。'
}

write_identity()
{
    local output=$1 actual_serial actual_boot actual_current actual_release file_size actual_sha
    [[ -r /proc/device-tree/serial-number ]] || fail '实际板序列号不可读。'
    actual_serial=$(tr -d '\000' < /proc/device-tree/serial-number)
    actual_boot=$(< /proc/sys/kernel/random/boot_id)
    [[ "$actual_serial" == "$board_serial" && "$actual_boot" == "$boot_id" ]] || fail '板卡或启动周期已经改变。'
    read_process
    [[ -L "$current_link" ]] || fail 'current不再是符号链接。'
    actual_current=$(readlink -f -- "$current_link")
    [[ "$actual_current" == "$expected_current_target" ]] || fail 'current目标已经改变。'
    [[ -f "$release_id_file" && ! -L "$release_id_file" ]] || fail '包内发布文件不可用。'
    file_size=$(stat -c %s -- "$release_id_file")
    [[ "$file_size" == 64 || "$file_size" == 65 ]] || fail '包内发布文件长度不符。'
    actual_release=$(< "$release_id_file")
    [[ "$actual_release" == "$release_id" ]] || fail '包内真实发布ID不符。'
    # 在外部工具核实际运行映像；GUI观察入口不读取大程序文件做SHA。
    actual_sha=$(sha256sum -- "/proc/$pid/exe")
    actual_sha=${actual_sha%% *}
    [[ "$actual_sha" == "$executable_sha256" ]] || fail '活进程实际程序SHA不符。'
    read_process
    [[ "$(< /proc/sys/kernel/random/boot_id)" == "$boot_id" ]] || fail '读取身份期间启动周期改变。'
    printf '{\n  "schema":"sn100-lis-live-capture-identity-v1",\n  "board_serial":"%s",\n  "boot_id":"%s",\n  "pid":"%s",\n  "start_ticks":"%s",\n  "release_id":"%s",\n  "executable":"%s",\n  "executable_sha256":"%s",\n  "current_link":"%s",\n  "current_target":"%s",\n  "release_id_file":"%s",\n  "release_file_value":"%s",\n  "observation_id":"%s"\n}\n' \
        "$actual_serial" "$actual_boot" "$pid" "$start_ticks" "$release_id" "$executable" "$actual_sha" \
        "$current_link" "$actual_current" "$release_id_file" "$actual_release" "$observation_id" > "$output"
}

write_identity "$capture_directory/capture_identity_before.json"
result_file=$result_directory/lis_observation.json
ready_file=$result_directory/lis_observation.ready
wait_started=$SECONDS
while [[ ! -e "$ready_file" && ! -L "$ready_file" ]]; do
    read_process
    (( SECONDS - wait_started < 10#$timeout_seconds )) || fail '等待封口标记已到截止时间；产品保持原状态。'
    sleep 0.2
done

read_sealed_pair()
{
    [[ -f "$result_file" && ! -L "$result_file" && -f "$ready_file" && ! -L "$ready_file" ]] \
        || fail '结果或标记缺失，或文件是符号链接。'
    local result_size ready_size expected_line
    result_size=$(stat -c %s -- "$result_file")
    ready_size=$(stat -c %s -- "$ready_file")
    (( result_size > 0 && result_size < 1048576 && ready_size > 0 && ready_size < 512 )) \
        || fail '结果或标记超过允许大小。'
    IFS= read -r sealed_line < "$ready_file" || fail '封口标记必须以换行结束。'
    local tag marker_id marker_release marker_pid marker_sha extra
    read -r tag marker_id marker_release marker_pid marker_sha extra <<< "$sealed_line"
    [[ "$marker_sha" =~ ^[0-9a-f]{64}$ ]] || fail '封口标记的SHA格式错误。'
    expected_line="SN100_LIS_OBSERVATION_READY_V1 $observation_id $release_id $pid $marker_sha"
    [[ "$sealed_line" == "$expected_line" && "$ready_size" == "$((${#sealed_line} + 1))" ]] \
        || fail '封口标记的身份或单行格式不符。'
    sealed_sha=$(sha256sum -- "$result_file")
    sealed_sha=${sealed_sha%% *}
    [[ "$sealed_sha" == "$marker_sha" ]] || fail '封口JSON的实际SHA与标记不符。'
}

sealed_line= sealed_sha=
read_sealed_pair
first_line=$sealed_line
first_sha=$sealed_sha
cp --no-clobber --no-dereference -- "$result_file" "$capture_directory/lis_observation.json"
cp --no-clobber --no-dereference -- "$ready_file" "$capture_directory/lis_observation.ready"
[[ -f "$capture_directory/lis_observation.json" && ! -L "$capture_directory/lis_observation.json" \
    && -f "$capture_directory/lis_observation.ready" && ! -L "$capture_directory/lis_observation.ready" ]] \
    || fail '采集副本不是普通文件。'
copied_sha=$(sha256sum -- "$capture_directory/lis_observation.json")
[[ ${copied_sha%% *} == "$first_sha" ]] || fail 'JSON在复制时改变。'
cmp -- "$ready_file" "$capture_directory/lis_observation.ready" || fail '标记在复制时改变。'
read_sealed_pair
[[ "$sealed_line" == "$first_line" && "$sealed_sha" == "$first_sha" ]] || fail '封口文件在读取期间改变。'
write_identity "$capture_directory/capture_identity_after.json"
cmp -- "$capture_directory/capture_identity_before.json" "$capture_directory/capture_identity_after.json" \
    || fail '采集前后身份不相同。'
read_sealed_pair
[[ "$sealed_line" == "$first_line" && "$sealed_sha" == "$first_sha" ]] || fail '复核身份后封口文件改变。'

printf '%s\n' '{"schema":"sn100-lis-live-capture-status-v1","collection_complete":true,"application_exit":null,"application_exit_evaluated":false,"shutdown_evaluated":false,"reason":"sealed_snapshot_copied_without_application_stop"}' \
    > "$capture_directory/capture_status.json"
(
    cd -- "$capture_directory"
    sha256sum -- capture_identity_after.json capture_identity_before.json capture_status.json lis_observation.json lis_observation.ready \
        > capture_manifest.sha256.tmp
    mv -- capture_manifest.sha256.tmp capture_manifest.sha256
)
capture_complete=1
printf 'LIS_CAPTURE_COMPLETE %s\n' "$capture_directory"
# 0仅表示这些文件已采集；软件结论由独立parser核对，应用退出仍未评估。
