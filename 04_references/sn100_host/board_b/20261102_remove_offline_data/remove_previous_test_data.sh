#!/usr/bin/env bash
# 按用户要求，仅删除板B已被原资料替换的旧脱机测试目录。
# 先核对板卡、真实路径、挂载和进程占用，再比较当前资料及程序指纹。
set -Eeuo pipefail
umask 077
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
target=/home/rkshare/programs/sn100_shell/runtime/previous_test_data_20261102
active=/home/rkshare/programs/sn100_shell/runtime/data
out=$base/verification/board_b_remove_offline_data_20261102
test "$(realpath -e -- "$base")" = "$base"
test "$(realpath -e -- "$target")" = "$target"
test "$(realpath -e -- "$active")" = "$active"
test -d "$target" && test ! -L "$target"
test "$target" = "$base/runtime/previous_test_data_20261102"
test "$(cat "$base/original_project_root")" = "$active"
test -f "$active/.sn100-original-work-v1"
test -z "$(find -P "$target" -type l -print -quit)"
if awk -v p="$target" '$5 == p || index($5, p "/") == 1 { found=1 } END { exit !found }' /proc/self/mountinfo; then
    printf '停止：目标目录内有挂载点。\n' >&2
    exit 2
fi
exec 9>"$base/lifecycle.lock"
flock -n 9
references=$(find /proc/[0-9]*/fd /proc/[0-9]*/cwd /proc/[0-9]*/root /proc/[0-9]*/exe -maxdepth 1 -type l -lname "$target*" -print 2>/dev/null || true)
mapped=$(grep -lF -- "$target" /proc/[0-9]*/maps 2>/dev/null || true)
test -z "$references" && test -z "$mapped"
release=$(realpath -e -- "$base/current")
for file in "$base/sn100.sh" "$release/rk_board.sh" "$base/original_project_root" "$base/instrument_configuration_file" "$base/theme_configuration_file" /etc/systemd/system/sn100-shell.service; do
    test -f "$file" || continue
    if grep -Fq -- "$target" "$file"; then
        printf '停止：当前启动配置引用旧目录：%s\n' "$file" >&2
        exit 2
    fi
done
test ! -e "$out"
mkdir -m 700 -- "$out"
find -P "$target" -type f -printf '%P\t%s\n' | LC_ALL=C sort > "$out/deleted_files.tsv"
files=$(find -P "$target" -type f -printf '.\n' | wc -l)
bytes=$(find -P "$target" -type f -printf '%s\n' | awk '{sum += $1} END {printf "%.0f", sum}')
snapshot()
{
    (
        cd -- "$active"
        find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum
    )
    for file in "$base/sn100.sh" "$base/original_project_root" "$base/instrument_configuration_file" "$base/theme_configuration_file" "$release/sn100_shell" "$release/sn100_report_renderer" "$release/rk_board.sh" /userdata/shared/ACA_V1.0OffLineD-500 /userdata/shared/config_linux.ini; do
        test ! -f "$file" || sha256sum -- "$file"
    done
}
snapshot > "$out/preserved_before.sha256"
{
    printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
    date -Is
    printf 'TARGET=%s\nACTIVE_DATA=%s\n' "$target" "$active"
    printf 'FILES=%s\nFILE_BYTES=%s\n' "$files" "$bytes"
    printf 'PATH_CHECK=passed\nNO_SYMLINKS=1\nNO_MOUNTS=1\nNO_PROCESS_REFERENCES=1\nNO_ACTIVE_LAUNCH_REFERENCES=1\n'
    printf 'CURRENT_RELEASE=%s\n' "$release"
    printf 'PROCESSES_BEFORE\n'
    ps -eo pid=,comm= | awk '$2 == "sn100_shell" || $2 ~ /^ACA_V/ { print }'
} > "$out/result.txt"
# 目标是上文核对过的固定旧目录，不触及runtime/data、程序或主控目录。
rm -rf --one-file-system -- "$target"
test ! -e "$target" && test ! -L "$target"
snapshot > "$out/preserved_after.sha256"
cmp -- "$out/preserved_before.sha256" "$out/preserved_after.sha256"
{
    printf 'DELETED=1\nPRESERVED_FILES_UNCHANGED=1\nORIGINAL_DATA_FILES=%s\n' "$(find "$active" -type f -printf '.\n' | wc -l)"
    printf 'PROCESSES_AFTER\n'
    ps -eo pid=,comm= | awk '$2 == "sn100_shell" || $2 ~ /^ACA_V/ { print }'
    printf 'APPLICATION_START_STOP_PERFORMED=0\nCONTROLLER_COMMANDS_SENT=0\n'
} >> "$out/result.txt"
cat "$out/result.txt"
