#!/usr/bin/env bash
# 用普通用户只读核对板B资料，不查询患者或账户记录，不连接主控。
set -Eeuo pipefail
umask 077
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
expected=${1:?缺少检查程序指纹}
[[ $expected =~ ^[a-f0-9]{64}$ ]]
base=/home/rkshare/programs/sn100_shell
input=$base/verification/board_b_original_files_20261102
release=$(readlink -f "$base/current")
test "$(sha256sum "$input/original_files_check_arm64" | cut -d' ' -f1)" = "$expected"
test "$(od -An -tu2 -j18 -N2 "$input/original_files_check_arm64" | tr -d ' ')" = 183
chown -R lckfb:lckfb "$input"
chmod 700 "$input" "$input/original_files_check_arm64"
export LD_LIBRARY_PATH=$release/lib
ldd "$input/original_files_check_arm64" > "$input/check_dependencies.log"
! grep -q 'not found' "$input/check_dependencies.log"
set +e
runuser -u lckfb -- env LD_LIBRARY_PATH="$release/lib" QT_PLUGIN_PATH="$release/plugins:/usr/lib/aarch64-linux-gnu/qt5/plugins" \
  timeout 30 "$input/original_files_check_arm64" "$base/runtime/data" "$input/effective_check_manifest.json" "$input/files_check_result.json" > "$input/files_check.log" 2>&1
code=$?
set -e
printf '%s\n' "$code" > "$input/files_check_exit.txt"
tail -3 "$input/files_check.log"
printf 'CHECK_ACTUAL_EXIT=%s\n' "$code"
test "$code" = 0
(cd "$base/runtime/data" && sha256sum --quiet -c "$input/FILES.sha256")
printf 'ALL_ORIGINAL_WORK_FILES_UNCHANGED_AFTER_READONLY_CHECK=1\n'
