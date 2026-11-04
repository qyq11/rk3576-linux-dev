#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
[[ $(readlink /proc/5836/exe) == $base/releases/0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743/sn100_shell ]]
[[ $(awk '{print $22}' /proc/5836/stat) == 225261 && $(stat -c %u /proc/5836) == 1000 ]]
[[ $(readlink /proc/4426/exe) == /userdata/shared/ACA_V1.0OffLineD-500 && $(awk '{print $22}' /proc/4426/stat) == 196590 ]]
[[ ! -e $base/runs/20261104_153722_5015/exit_code ]]
[[ $(cat "$base/reviews/sample_order_board_a_20261104_01/check_exit.txt") == 1 ]]
date -Is
printf 'BOARD=board_a\nSERIAL=3bf8222fe1417f6a\nGUI_PID=5836\nGUI_START_TICKS=225261\nCONTROLLER_PID=4426\nCONTROLLER_START_TICKS=196590\nAPPLICATION_EXIT=null\n'
printf 'CURRENT=%s\nACTIVE_VT=%s\n' "$(readlink -f "$base/current")" "$(cat /sys/class/tty/tty0/active)"
sha256sum /proc/5836/exe /proc/4426/exe "$base/reviews/sample_order_board_a_20261104_01/result.json"
ss -H -tnp '( sport = :5000 or dport = :5000 )'
printf 'PAUSED_HANDOFF_PROGRAMS_KEPT_RUNNING=1\nNO_CLOSE_RETRY_OR_INPUT_SENT=1\n'
