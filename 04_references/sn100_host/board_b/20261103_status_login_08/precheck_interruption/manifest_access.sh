#!/usr/bin/env bash
# 只读核对本轮可选观察为什么未取得启动指纹；不改权限、不重跑应用。
set -Eeuo pipefail
test "$(tr -d '\0\n' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
run=/home/rkshare/programs/sn100_shell/verification/status_login_08_20261103/runs/auto_01
date -Is
stat -c '%U:%G %a %n' "$run" "$run/results" "$run/ini_before.sha256"
sha256sum "$run/ini_before.sha256"
if runuser -u lckfb -- test -r "$run/ini_before.sha256"; then
    printf 'APPLICATION_USER_CAN_READ_STARTUP_MANIFEST=1\n'
else
    printf 'APPLICATION_USER_CAN_READ_STARTUP_MANIFEST=0\n'
fi
