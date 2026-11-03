#!/usr/bin/env bash
# 在已确认的板B读取实际配置，并连接正在运行的主控；不发检测或暂停命令。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
check=$base/verification/sn500_connection_20261102
release=$(readlink -f "$base/current")
test "$#" = 2
test "${release##*/}" = "$1"
test "$(sha256sum "$check/sn500_theme_connection_rk_check" | cut -d' ' -f1)" = "$2"
chmod 750 "$check/sn500_theme_connection_rk_check"
chown lckfb:lckfb "$check/sn500_theme_connection_rk_check"
install -d -m 700 -o lckfb -g lckfb "$check/check_runtime"
test ! -e "$check/check_result_final"
set +e
runuser -u lckfb -- env XDG_RUNTIME_DIR="$check/check_runtime" QT_QPA_PLATFORM=offscreen QT_PLUGIN_PATH="$release/plugins" LD_LIBRARY_PATH="$release/lib" \
    timeout 30s "$check/sn500_theme_connection_rk_check" "$base/runtime/data/theme_config.ini" "$check/check_result_final" >"$check/check_final.log" 2>&1
check_exit=$?
set -e
printf '%s\n' "$check_exit" >"$check/check_final_exit.txt"
cat "$check/check_final.log"
printf 'ACTUAL_EXIT=%s\n' "$check_exit"
exit "$check_exit"
