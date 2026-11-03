#!/usr/bin/env bash
# 只替换主控可执行文件；先核新文件和板B身份，不覆盖配置、数据库及流程资料。
set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
shared=/userdata/shared
base=/home/rkshare/programs/aca_flow_test
run=$base/runs/replace_04
new_hash=18d11230da40cea294b72be4e8454e54a3f6073dc3767e7bdfd10c2556d66332
old_hash=bfb23e400584ee9f3b44eee2680fdd8eff85c51729963510f8612c71926ae33f
test -d "$shared" && test ! -L "$shared"
test ! -e "$run"
test ! -e "$shared/ACA_V1.0D-500"
test "$(sha256sum "$base/ACA_V1.0D-500.upload" | cut -d' ' -f1)" = "$new_hash"
test "$(sha256sum "$shared/ACA_V1.0OffLineD-500" | cut -d' ' -f1)" = "$old_hash"
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" || true)
    case "$path" in
        */ACA_V1.0OffLineD-500|*/ACA_V1.0D-500) printf '主控仍在运行，未替换：%s\n' "$path"; exit 1 ;;
    esac
done
install -d -m 750 "$run"
{
    date -Is
    printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
    id
    stat -c '%n %F %U:%G %a %s' "$shared" "$shared/ACA_V1.0OffLineD-500"
    sha256sum "$shared/ACA_V1.0OffLineD-500" "$base/ACA_V1.0D-500.upload"
} >"$run/before.log"
cd "$shared"
find . -type f ! -name 'ACA_V1.0OffLineD-500' ! -name 'ACA_V1.0D-500' -print0 |
    sort -z | xargs -0 -r sha256sum >"$run/support_files_before.sha256"
install -m 755 -o root -g root "$base/ACA_V1.0D-500.upload" "$shared/ACA_V1.0D-500"
test "$(sha256sum "$shared/ACA_V1.0D-500" | cut -d' ' -f1)" = "$new_hash"
LD_LIBRARY_PATH="$shared" ldd "$shared/ACA_V1.0D-500" >"$run/dependencies.log" 2>&1
if grep -q 'not found' "$run/dependencies.log"; then
    printf '新程序缺少运行组件，保留旧程序，未启动。\n'
    exit 1
fi
rm -- "$shared/ACA_V1.0OffLineD-500"
if test -f "$base/ACA_V1.0OffLineD-500"; then
    test "$(sha256sum "$base/ACA_V1.0OffLineD-500" | cut -d' ' -f1)" = "$old_hash"
    rm -- "$base/ACA_V1.0OffLineD-500"
fi
rm -- "$base/ACA_V1.0D-500.upload"
sha256sum -c "$run/support_files_before.sha256" >"$run/support_files_after.log"
{
    date -Is
    printf 'NEW_PROGRAM_SHA256='; sha256sum "$shared/ACA_V1.0D-500"
    stat -c '%n %F %U:%G %a %s' "$shared/ACA_V1.0D-500"
    printf 'OLD_ACTIVE_PROGRAM_REMOVED=1\nSUPPORT_FILES_UNCHANGED=1\nPROGRAM_STARTED_BY_REPLACEMENT=0\n'
} >"$run/result.txt"
cat "$run/result.txt"
