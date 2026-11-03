#!/usr/bin/env bash
# 仅在板B停机后切回本次保留的旧测试资料，原资料就地保留，不删除。
set -Eeuo pipefail
umask 077
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(id -u)" = 0
base=/home/rkshare/programs/sn100_shell
previous=$base/runtime/previous_test_data_20261102
retained=$base/runtime/original_data_retained_on_restore_20261102
input=$base/verification/board_b_original_files_20261102
exec 9>"$base/lifecycle.lock"; flock -n 9
! pgrep -x sn100_shell >/dev/null
! systemctl is-active --quiet sn100-shell.service
test -d "$previous" && test ! -L "$previous"
test ! -e "$retained"
# 有后续连接/主题指针时先由维护者核对，不连带撤销其他聊天的更新。
test ! -e "$base/instrument_configuration_file"
test ! -e "$base/theme_configuration_file"
test "$(cat "$base/original_project_root")" = "$base/runtime/data"
mv "$base/runtime/data" "$retained"
mv "$previous" "$base/runtime/data"
mv "$base/original_project_root" "$input/original_project_root.retained"
install -m 700 -o lckfb -g lckfb "$input/sn100.sh.before" "$base/sn100.sh"
printf 'BOARD_B_PREVIOUS_TEST_DATA_RESTORED=1\nORIGINAL_DATA_RETAINED=%s\nAPPLICATION_STARTED=0\n' "$retained"
