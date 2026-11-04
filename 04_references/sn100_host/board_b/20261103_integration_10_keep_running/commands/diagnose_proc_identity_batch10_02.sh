set -Eeuo pipefail
printf 'UTC='; date -u +%Y-%m-%dT%H:%M:%SZ
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
        [[ ${later_fields[0]-} == "${fields[0]}" && ${later_fields[6]-} == "${fields[6]}" && ${later_fields[19]-} == "${fields[19]}" ]] || { printf 'RECHECK node=%s state_before=%s state_after=%s flags_before=%s flags_after=%s ticks_before=%s ticks_after=%s\n' "$node" "${fields[0]-}" "${later_fields[0]-}" "${fields[6]-}" "${later_fields[6]-}" "${fields[19]-}" "${later_fields[19]-}" >&2; return 79; }
        return 1  # 僵尸/死亡或PF_KTHREAD内核线程没有存活用户exe。
    fi
    [[ ! -d $proc_dir ]] && return 1
    echo '存活用户进程exe不可访问；未切换版本。' >&2
    return 79
}

failures=0
for node in /proc/[0-9]*/exe; do
    if target=$(transition_process_exe "$node"); then
        case ${target##*/} in ACA_V1.0OffLineD-500*) printf 'CONTROLLER_NODE=%s TARGET=%s\n' "$node" "$target" ;; esac
    else
        code=$?
        if [[ $code != 1 ]]; then
            printf 'UNCLASSIFIED_NODE=%s STATUS=%s\n' "$node" "$code"
            dir=${node%/exe}
            printf 'STAT='; cat "$dir/stat" || true
            printf 'COMM='; cat "$dir/comm" || true
            failures=$((failures + 1))
        fi
    fi
done
printf 'UNCLASSIFIED_COUNT=%s\n' "$failures"
base=/home/rkshare/programs/sn100_shell
printf 'CURRENT='; readlink "$base/current"
printf 'APP_EXE='; readlink /proc/24414/exe
test ! -e "$base/releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e"
printf 'FLAT_NOT_CREATED=1\nREAD_ONLY_DIAGNOSTIC=1\n'
