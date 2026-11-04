#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
run=$base/runs/20261104_153722_5015
out=$base/reviews/sample_order_board_a_20261104_01
[[ $(cat /proc/sys/kernel/random/boot_id) == 559493ce-5319-42b6-9383-bf7b93764a35 ]]
printf 'CURRENT=%s\nACTIVE_VT=%s\n' "$(readlink -f "$base/current")" "$(cat /sys/class/tty/tty0/active)"
if [[ -e /proc/5836/exe ]]; then sha256sum /proc/5836/exe;cat /proc/5836/stat;fi
if [[ -f $out/check_exit.txt ]]; then printf 'CHECK_EXIT=';cat "$out/check_exit.txt";fi
if [[ -f $out/result.json ]]; then printf 'RESULT_JSON_BEGIN\n';cat "$out/result.json";printf 'RESULT_JSON_END\n';fi
printf 'STARTUP_REVIEW_MARKERS\n'
grep -E 'SHELL_READY|SAMPLE_ADD_REVIEW|ORIGINAL_AD_CHANNELS|CONFIGURATION_LOADED|(^| )ERROR|FAILED|failed|Error' "$run/app.log" | tail -18 || true
if [[ -f $run/exit_code ]]; then printf 'MAIN_EXIT=';cat "$run/exit_code";cat "$run/result.txt";else printf 'MAIN_EXIT=null\n';fi
ss -H -tnp '( sport = :5000 or dport = :5000 )'
printf 'REVIEW_STATUS_READONLY_COMPLETE=1\n'
