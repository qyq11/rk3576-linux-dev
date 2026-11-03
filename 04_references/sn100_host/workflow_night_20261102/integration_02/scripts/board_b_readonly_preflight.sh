#!/usr/bin/env bash
# 只读取板卡身份、现有监听和版本，不启动或恢复任何仪器程序。
set -Eeuo pipefail
serial=$(tr -d '\0' </proc/device-tree/serial-number)
test "$serial" = 8314b1e8ffd0321f
printf 'BOARD=board_b\nSERIAL=%s\n' "$serial"
date -Is
uname -r
printf 'CURRENT_RELEASE='
readlink /home/rkshare/programs/sn100_shell/current
printf 'UPPER_PIDS='
pgrep -x sn100_shell || true
printf 'PORT5000_LISTENERS\n'
ss -H -lnt 'sport = :5000'
printf 'CONFIG_POINTERS\n'
cat /home/rkshare/programs/sn100_shell/original_project_root
cat /home/rkshare/programs/sn100_shell/instrument_configuration_file
cat /home/rkshare/programs/sn100_shell/theme_configuration_file
printf 'CONFIG_FINGERPRINTS\n'
sha256sum /home/rkshare/programs/sn100_shell/runtime/data/config.ini \
    /home/rkshare/programs/sn100_shell/runtime/data/config_Debug.ini \
    /home/rkshare/programs/sn100_shell/runtime/data/theme_config.ini
