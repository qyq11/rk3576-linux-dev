#!/usr/bin/env bash
# 按用户纠正后的文件替换，去掉微信文件名中的(1)，保留所有配套资料。
set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
shared=/userdata/shared
base=/home/rkshare/programs/aca_flow_test
run=$base/runs/correct_05
test -d "$shared" && test ! -L "$shared"
test ! -e "$run"
test ! -e "$shared/ACA_V1.0OffLineD-500"
test "$(sha256sum "$base/ACA_correct.upload" | cut -d' ' -f1)" = ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
test "$(sha256sum "$shared/ACA_V1.0D-500" | cut -d' ' -f1)" = 18d11230da40cea294b72be4e8454e54a3f6073dc3767e7bdfd10c2556d66332
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
    sha256sum "$shared/ACA_V1.0D-500" "$base/ACA_correct.upload"
} >"$run/before.log"
cd "$shared"
find . -type f ! -name 'ACA_V1.0D-500' -print0 | sort -z | xargs -0 -r sha256sum >"$run/support_files_before.sha256"
install -m 755 -o root -g root "$base/ACA_correct.upload" "$shared/ACA_V1.0OffLineD-500"
test "$(sha256sum "$shared/ACA_V1.0OffLineD-500" | cut -d' ' -f1)" = ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff
LD_LIBRARY_PATH="$shared" ldd "$shared/ACA_V1.0OffLineD-500" >"$run/dependencies.log" 2>&1
if grep -q 'not found' "$run/dependencies.log"; then exit 1; fi
rm -- "$shared/ACA_V1.0D-500" "$base/ACA_correct.upload"
sha256sum -c "$run/support_files_before.sha256" >"$run/support_files_after.log"
{
    date -Is
    sha256sum "$shared/ACA_V1.0OffLineD-500"
    stat -c '%n %U:%G %a %s' "$shared/ACA_V1.0OffLineD-500"
    printf 'WRONG_ACTIVE_PROGRAM_REMOVED=1\nSUPPORT_FILES_UNCHANGED=1\n'
} >"$run/result.txt"
cat "$run/result.txt"
