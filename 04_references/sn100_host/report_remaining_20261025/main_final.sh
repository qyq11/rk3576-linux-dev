#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work=$root/temp/sn100_host/report_remaining_20261025
cd "$work/standalone"
make -B -j2 .obj/main.o
make -j2
QT_QPA_PLATFORM=offscreen ./sn100_history --offline --data-dir "$work/standalone_smoke_reopen" --simulate-role 4 --exit-after-ms 1000 > "$work/standalone_smoke_final.log" 2>&1
set +e
QT_QPA_PLATFORM=offscreen ./sn100_history --offline --data-dir "$work/invalid_cli_runtime" --simulate-role 6 --exit-after-ms 10 > "$work/invalid_cli_final.log" 2>&1
result=$?
set -e
printf '{"exit_code":%s}\n' "$result" > "$work/invalid_cli_final.exit.json"
[[ $result == 2 ]]
sha256sum ./sn100_history ./sn100_report_renderer