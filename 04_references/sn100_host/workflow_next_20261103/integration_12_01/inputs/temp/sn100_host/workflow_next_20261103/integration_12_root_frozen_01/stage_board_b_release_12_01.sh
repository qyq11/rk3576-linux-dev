#!/usr/bin/env bash
# 只保存新不可变包；活体快照只覆盖持存储锁的解包/提交，不是运行观察器。
set -Eeuo pipefail
shopt -s inherit_errexit

next_guard_fail() { printf 'LIVE_GUARD_FAILED: %s\n' "$*" >&2; exit 97; }
base=/home/rkshare/programs/sn100_shell
store_lock=$base/immutable_store.lock
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$#" = 8
archive=$1
expected=$2
release_id=$3
baseline=$4
controller_expected=$5
configuration_base64=$6
main_sha_expected=$7
controller_sha_expected=$8
[[ $main_sha_expected =~ ^[a-f0-9]{64}$ && $controller_sha_expected =~ ^[a-f0-9]{64}$ ]]
[[ ${#configuration_base64} -le 16384 && $configuration_base64 =~ ^[A-Za-z0-9+/]+={0,2}$ ]]
configuration_expected=$(printf '%s' "$configuration_base64" | base64 --decode) || next_guard_fail '冻结配置清单不能解码。'
[[ $(printf '%s\n' "$configuration_expected" | base64 -w0) == "$configuration_base64" ]] || next_guard_fail '冻结配置清单不是规范LF原文。'
configuration_paths=(
    /home/rkshare/programs/sn100_shell/runtime/data/config_Debug.ini
    /home/rkshare/programs/sn100_shell/runtime/data/config.ini
    /home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini
    /userdata/shared/config_linux.ini
    /userdata/shared/consume.ini
    /userdata/shared/GL4A_config.cfg
    /userdata/shared/number.ini
)
configuration_index=0
while IFS= read -r configuration_row; do
    [[ $configuration_row =~ ^([a-f0-9]{64})\ \ (/[A-Za-z0-9_./-]+)$ ]] || next_guard_fail '冻结配置清单行格式不明。'
    [[ ${configuration_paths[$configuration_index]-} == "${BASH_REMATCH[2]}" ]] || next_guard_fail '冻结配置清单不是指定的七路径和顺序。'
    configuration_index=$((configuration_index + 1))
done <<<"$configuration_expected"
[[ $configuration_index == ${#configuration_paths[@]} ]] || next_guard_fail '冻结配置清单缺行。'
[[ $baseline =~ ^releases/[a-f0-9]{64}$ ]]
[[ $archive =~ ^/home/rkshare/programs/sn100_shell/\.workflow_upload_12_01_[a-f0-9]{32}\.tar\.gz$ ]]
[[ $expected =~ ^[a-f0-9]{64}$ && $release_id =~ ^[a-f0-9]{64}$ ]]
[[ $controller_expected == /* && $controller_expected != *$'\n'* && $controller_expected != *$'\r'* && $controller_expected != *$'\t'* ]]
test -f "$controller_expected"
test ! -L "$controller_expected"
test "$(readlink -f -- "$controller_expected")" = "$controller_expected"
test "$(readlink -f -- "$base")" = "$base"
test -d "$base/releases"
test ! -L "$base/releases"
test -L "$base/current"
test "$(readlink -- "$base/current")" = "$baseline"
test ! -L "$store_lock"
if test -e "$store_lock"; then test -f "$store_lock"; fi
exec 9>>"$store_lock"
flock -n 9
# 这把锁只约束采用本合同的新工具，不阻止旧写者；总控仍须串行安排。
test ! -L "$store_lock"
test "$store_lock" -ef "/proc/$$/fd/9"
printf 'STORAGE_LOCK_PATH=%s\nSTORAGE_LOCK_KIND=exclusive_immutable_store_only\n' "$store_lock"
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(readlink -- "$base/current")" = "$baseline"
test -f "$archive"
test ! -L "$archive"
test "$(sha256sum -- "$archive" | cut -d' ' -f1)" = "$expected"

next_unique_pid() {
    local expected_exe=$1 label=$2 item actual wanted
    local -a found=()
    wanted=${expected_exe##*/}
    for item in /proc/[0-9]*/exe; do
        actual=$(readlink -- "$item" 2>/dev/null) || continue
        if [[ ${actual##*/} == "$wanted" || ${actual##*/} == "$wanted (deleted)" ]]; then
            actual=${item#/proc/}
            found+=("${actual%/exe}")
        fi
    done
    [[ ${#found[@]} == 1 ]] || next_guard_fail "$label 缺失或多份，未猜PID。"
    [[ $(readlink -- "/proc/${found[0]}/exe") == "$expected_exe" ]] || next_guard_fail "$label 执行文件位置已变。"
    printf '%s\n' "${found[0]}"
}

next_start_ticks() {
    local pid=$1 raw rest
    local -a fields=()
    raw=$(cat -- "/proc/$pid/stat") || next_guard_fail '不能读取进程身份。'
    rest=${raw##*) }
    [[ $rest != "$raw" ]] || next_guard_fail '进程stat格式不明。'
    IFS=' ' read -r -a fields <<<"$rest"
    [[ ${fields[19]-} =~ ^[0-9]+$ ]] || next_guard_fail '进程启动计数缺失。'
    printf '%s\n' "${fields[19]}"
}

next_proc_uid() {
    local value
    # 记录实际有效UID；主程序另与现场id -u lckfb核对，不沿用旧数字。
    value=$(awk '$1 == "Uid:" {if (NF != 5) exit 1; print $3; found++} END {if (found != 1) exit 1}' "/proc/$1/status") || next_guard_fail '进程UID读取失败。'
    [[ $value =~ ^[0-9]+$ ]] || next_guard_fail '进程UID格式不明。'
    printf '%s\n' "$value"
}

next_identity() {
    local pid=$1 expected_exe=$2 ticks exe digest again uid
    ticks=$(next_start_ticks "$pid")
    uid=$(next_proc_uid "$pid")
    exe=$(readlink -- "/proc/$pid/exe") || next_guard_fail '执行文件不可读。'
    [[ $exe == "$expected_exe" && $exe != *$'\n'* && $exe != *$'\r'* && $exe != *$'\t'* ]] || next_guard_fail '执行文件身份已变。'
    digest=$(sha256sum -- "/proc/$pid/exe" | cut -d' ' -f1) || next_guard_fail '执行文件指纹不可读。'
    [[ $digest =~ ^[a-f0-9]{64}$ ]] || next_guard_fail '执行文件指纹无效。'
    again=$(next_start_ticks "$pid")
    [[ $again == "$ticks" && $(readlink -- "/proc/$pid/exe") == "$exe" && $(next_proc_uid "$pid") == "$uid" ]] || next_guard_fail '读取期间进程身份变化。'
    printf '%s\t%s\t%s\t%s\t%s\n' "$pid" "$ticks" "$exe" "$digest" "$uid"
}

next_pointer_sha() {
    local path=$1 digest
    test -f "$path" && test ! -L "$path" || next_guard_fail '资料/配置指针不是普通原件。'
    [[ $(readlink -f -- "$path") == "$path" ]] || next_guard_fail '资料/配置指针经过链接。'
    digest=$(sha256sum -- "$path" | cut -d' ' -f1) || next_guard_fail '资料/配置指针指纹读取失败。'
    [[ $digest =~ ^[a-f0-9]{64}$ ]] || next_guard_fail '资料/配置指针指纹无效。'
    printf '%s\n' "$digest"
}

next_configuration_table() {
    local path digest
    for path in "${configuration_paths[@]}"; do
        test -f "$path" && test ! -L "$path" || next_guard_fail '受保护配置不是普通原件。'
        [[ $(readlink -f -- "$path") == "$path" ]] || next_guard_fail '受保护配置经过链接。'
        digest=$(sha256sum -- "$path" | cut -d' ' -f1) || next_guard_fail '受保护配置指纹读取失败。'
        [[ $digest =~ ^[a-f0-9]{64}$ ]] || next_guard_fail '受保护配置指纹格式不明。'
        printf '%s  %s\n' "$digest" "$path"
    done
}

next_live_guard() {
    local configuration_table=$1 configuration_sha
    local boot current main_expected main_pid main_ticks main_exe main_sha main_row main_uid expected_uid
    local controller_pid controller_ticks controller_exe controller_sha controller_row controller_uid
    local listening listener_pids listener_row endpoints original_sha instrument_sha theme_sha
    [[ $(next_configuration_table) == "$configuration_table" && $configuration_table == "$configuration_expected" ]] || next_guard_fail '运行快照开始时配置已变化。'
    configuration_sha=$(printf '%s\n' "$configuration_table" | sha256sum | cut -d' ' -f1)
    boot=$(cat /proc/sys/kernel/random/boot_id) || next_guard_fail 'boot_id不可读。'
    [[ $boot =~ ^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$ ]] || next_guard_fail 'boot_id格式不明。'
    current=$(readlink -- "$base/current") || next_guard_fail 'current不可读。'
    [[ $current == "$baseline" ]] || next_guard_fail 'current不是冻结原值。'
    main_expected=$base/$baseline/sn100_shell
    test -f "$main_expected" && test ! -L "$main_expected" || next_guard_fail '当前主程序原件不可读。'
    [[ $(readlink -f -- "$main_expected") == "$main_expected" ]] || next_guard_fail '当前主程序经过额外链接。'
    main_pid=$(next_unique_pid "$main_expected" '现有主程序')
    controller_pid=$(next_unique_pid "$controller_expected" '原主控')
    [[ $main_pid != "$controller_pid" ]] || next_guard_fail '主程序和主控不能使用同一进程身份。'
    main_row=$(next_identity "$main_pid" "$main_expected")
    IFS=$'\t' read -r main_pid main_ticks main_exe main_sha main_uid <<<"$main_row"
    expected_uid=$(id -u lckfb) || next_guard_fail '不能核对正式运行账户。'
    [[ $expected_uid =~ ^[0-9]+$ && $main_uid == "$expected_uid" ]] || next_guard_fail '现有主程序不是以lckfb运行。'
    controller_row=$(next_identity "$controller_pid" "$controller_expected")
    IFS=$'\t' read -r controller_pid controller_ticks controller_exe controller_sha controller_uid <<<"$controller_row"
    [[ $main_sha == "$main_sha_expected" && $controller_sha == "$controller_sha_expected" ]] || next_guard_fail '现有应用或原主控指纹不等于冻结原值。'
    listening=$(LC_ALL=C ss -H -ltnp 'sport = :5000') || next_guard_fail '5000监听读取失败。'
    [[ -n $listening ]] || next_guard_fail '原主控5000没有监听。'
    while IFS= read -r listener_row; do
        listener_pids=$(printf '%s\n' "$listener_row" | grep -oE 'pid=[0-9]+' | cut -d= -f2 | LC_ALL=C sort -u) || next_guard_fail '存在不能确认进程的5000监听行。'
        [[ $listener_pids == "$controller_pid" ]] || next_guard_fail '某条5000监听不是唯一原主控持有。'
    done <<<"$listening"
    listener_pids=$(printf '%s\n' "$listening" | grep -oE 'pid=[0-9]+' | cut -d= -f2 | LC_ALL=C sort -u) || next_guard_fail '5000监听者身份读取失败。'
    [[ $listener_pids == "$controller_pid" ]] || next_guard_fail '5000监听者不唯一或不是原主控。'
    endpoints=$(printf '%s\n' "$listening" | awk '{print $4}' | LC_ALL=C sort | paste -sd, -) || next_guard_fail '5000监听地址读取失败。'
    [[ -n $endpoints && $endpoints != *$'\n'* && $endpoints != *$'\t'* && $endpoints != *' '* ]] || next_guard_fail '5000监听地址格式不明。'
    original_sha=$(next_pointer_sha "$base/original_project_root")
    instrument_sha=$(next_pointer_sha "$base/instrument_configuration_file")
    theme_sha=$(next_pointer_sha "$base/theme_configuration_file")
    [[ $(next_unique_pid "$main_expected" '现有主程序') == "$main_pid" && $(next_start_ticks "$main_pid") == "$main_ticks" && $(next_proc_uid "$main_pid") == "$main_uid" ]] || next_guard_fail '快照末尾主程序身份变化。'
    [[ $(next_unique_pid "$controller_expected" '原主控') == "$controller_pid" && $(next_start_ticks "$controller_pid") == "$controller_ticks" && $(next_proc_uid "$controller_pid") == "$controller_uid" ]] || next_guard_fail '快照末尾原主控身份变化。'
    [[ $(readlink -- "$base/current") == "$current" && $(cat /proc/sys/kernel/random/boot_id) == "$boot" ]] || next_guard_fail '快照期间板卡或current变化。'
    [[ $(next_pointer_sha "$base/original_project_root") == "$original_sha" && $(next_pointer_sha "$base/instrument_configuration_file") == "$instrument_sha" && $(next_pointer_sha "$base/theme_configuration_file") == "$theme_sha" ]] || next_guard_fail '快照期间资料/配置指针变化。'
    [[ $(next_configuration_table) == "$configuration_table" ]] || next_guard_fail '运行快照结束时配置已变化。'
    printf 'FORMAT=sn100-integration-12-01-live-guard-v2\nBOARD=board_b\nSERIAL=8314b1e8ffd0321f\nBOOT_ID=%s\nCURRENT_RELEASE=%s\n' "$boot" "$current"
    printf 'MAIN_PID=%s\nMAIN_UID=%s\nMAIN_START_TICKS=%s\nMAIN_EXE=%s\nMAIN_EXE_SHA256=%s\n' "$main_pid" "$main_uid" "$main_ticks" "$main_exe" "$main_sha"
    printf 'CONTROLLER_PID=%s\nCONTROLLER_UID=%s\nCONTROLLER_START_TICKS=%s\nCONTROLLER_EXE=%s\nCONTROLLER_EXE_SHA256=%s\nCONTROLLER_LISTEN_ENDPOINTS=%s\n' "$controller_pid" "$controller_uid" "$controller_ticks" "$controller_exe" "$controller_sha" "$endpoints"
    printf 'POINTER_ORIGINAL_PROJECT_ROOT_PATH=%s\nPOINTER_ORIGINAL_PROJECT_ROOT_SHA256=%s\n' "$base/original_project_root" "$original_sha"
    printf 'POINTER_INSTRUMENT_CONFIGURATION_FILE_PATH=%s\nPOINTER_INSTRUMENT_CONFIGURATION_FILE_SHA256=%s\n' "$base/instrument_configuration_file" "$instrument_sha"
    printf 'POINTER_THEME_CONFIGURATION_FILE_PATH=%s\nPOINTER_THEME_CONFIGURATION_FILE_SHA256=%s\n' "$base/theme_configuration_file" "$theme_sha"
    printf 'CONFIGURATION_SHA256=%s\n' "$configuration_sha"
}

configuration_before=$(next_configuration_table)
configuration_before_sha=$(printf '%s\n' "$configuration_before" | sha256sum | cut -d' ' -f1)
printf 'CONFIG_GUARD_BEFORE_SHA256=%s\nCONFIG_GUARD_BEFORE_BASE64=%s\n' "$configuration_before_sha" "$(printf '%s\n' "$configuration_before" | base64 -w0)"
[[ $configuration_before == "$configuration_expected" ]] || next_guard_fail '解包前七份配置指纹不等于冻结原值。'
guard_before=$(next_live_guard "$configuration_before")
guard_before_sha=$(printf '%s\n' "$guard_before" | sha256sum | cut -d' ' -f1)
printf 'LIVE_GUARD_BEFORE_SHA256=%s\nLIVE_GUARD_BEFORE_BASE64=%s\n' "$guard_before_sha" "$(printf '%s\n' "$guard_before" | base64 -w0)"
namespace=$base/releases/integration_12_01
test "$(readlink -f -- "$base/releases")" = "$base/releases"
test ! -L "$namespace"
if test -e "$namespace"; then test -d "$namespace"; else mkdir -m 750 -- "$namespace"; chown lckfb:lckfb "$namespace"; fi
test "$(readlink -f -- "$namespace")" = "$namespace"
release=$namespace/$release_id
test ! -e "$release"
test ! -L "$release"
for previous_stage in "$namespace"/.workflow_stage_12_01.*; do
    if test -e "$previous_stage" || test -L "$previous_stage"; then
        printf '本批已有未结清暂存现场，未覆盖或重跑：%s\n' "$previous_stage" >&2
        exit 96
    fi
done
stage_parent=$(mktemp -d "$namespace/.workflow_stage_12_01.XXXXXXXX")
[[ $stage_parent == "$namespace/.workflow_stage_12_01."* ]]
test "$(readlink -f -- "$stage_parent")" = "$stage_parent"
stage=$stage_parent/$release_id
mkdir -m 750 "$stage"
tar --no-same-owner --no-same-permissions -xzf "$archive" -C "$stage"
test "$(cat "$stage/release_id")" = "$release_id"
test "$(sha256sum "$stage/SHA256SUMS" | cut -d' ' -f1)" = "$release_id"
(cd "$stage" && sha256sum --quiet -c SHA256SUMS)
source "$stage/rk_board.sh"
verify_release "$stage"
dependencies=$stage_parent/runtime_dependencies.txt
test ! -e "$dependencies"
for object in "$stage/sn100_shell" "$stage/sn100_report_renderer" \
    "$stage/plugins/sqldrivers/libqsqlite.so" "$stage/plugins/platforms/libqoffscreen.so"; do
    test -f "$object"
    LD_LIBRARY_PATH="$stage/lib" ldd "$object" >>"$dependencies"
done
if grep -q 'not found' "$dependencies"; then cat "$dependencies"; exit 1; fi
chown -R lckfb:lckfb "$stage"
test "$(readlink -- "$base/current")" = "$baseline"
test "$(sha256sum -- "$archive" | cut -d' ' -f1)" = "$expected"
test ! -L "$base/releases"
test "$(readlink -f -- "$base/releases")" = "$base/releases"
test ! -L "$namespace"
test "$(readlink -f -- "$namespace")" = "$namespace"
test ! -L "$stage_parent"
test "$(readlink -f -- "$stage_parent")" = "$stage_parent"
test ! -e "$release"
test ! -L "$release"
mv --no-clobber -T -- "$stage" "$release"
test ! -e "$stage"
test "$(readlink -- "$base/current")" = "$baseline"
test ! -L "$release"
test "$(readlink -f -- "$release")" = "$release"
(cd "$release" && sha256sum --quiet -c SHA256SUMS)
configuration_after=$(next_configuration_table)
configuration_after_sha=$(printf '%s\n' "$configuration_after" | sha256sum | cut -d' ' -f1)
printf 'CONFIG_GUARD_AFTER_SHA256=%s\nCONFIG_GUARD_AFTER_BASE64=%s\n' "$configuration_after_sha" "$(printf '%s\n' "$configuration_after" | base64 -w0)"
[[ $configuration_after == "$configuration_expected" && $configuration_after == "$configuration_before" && $configuration_after_sha == "$configuration_before_sha" ]] || next_guard_fail '提交后七份配置变化；现场保留。'
guard_after=$(next_live_guard "$configuration_after")
guard_after_sha=$(printf '%s\n' "$guard_after" | sha256sum | cut -d' ' -f1)
printf 'LIVE_GUARD_AFTER_SHA256=%s\nLIVE_GUARD_AFTER_BASE64=%s\n' "$guard_after_sha" "$(printf '%s\n' "$guard_after" | base64 -w0)"
[[ $guard_before == "$guard_after" && $guard_before_sha == "$guard_after_sha" ]] || next_guard_fail '持存储锁解包/提交期间运行身份或指针变化；现场保留。'
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
printf 'STAGED_RELEASE=%s\nCURRENT_RELEASE=%s\nREMOTE_RELEASE=%s\nACTIVATED=0\nPROGRAM_STARTED=0\n' "$release_id" "$baseline" "$release"
sha256sum "$release/sn100_shell" "$release/sn100_report_renderer"
printf 'PACKAGE_SHA256=%s\nDEPENDENCIES_BEGIN\n' "$expected"
cat "$dependencies"
printf 'DEPENDENCIES_END\n'
rm -f -- "$dependencies"
rmdir -- "$stage_parent"
rm -f -- "$archive"
printf 'STAGE_BATCH=integration_12_01\nSTAGE_SCRIPT_EXIT=0\n'
