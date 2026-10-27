#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
src=$root/06_programs/sn100_host
work=$root/temp/sn100_host/report_remaining_20261025
SN100_REPORT_FORCE_REBUILD=1 bash "$src/history/report_remaining_wsl.sh" core-test > "$work/core_final_02.log" 2>&1
for directory in "$work/standalone" "$work/regression_history_page_guard_test"; do
  cd "$directory"
  make -B -j2 .obj/history_page.o
  make -j2
 done
QT_QPA_PLATFORM=offscreen "$work/regression_history_page_guard_test/history_page_guard_test" -o "$work/guard_final.txt",txt -o "$work/guard_final.xml",junitxml
set +e
QT_QPA_PLATFORM=offscreen "$work/standalone/sn100_history" --offline --data-dir "$work/standalone_smoke" --simulate-role 4 --exit-after-ms 1000 > "$work/standalone_smoke.log" 2>&1
result=$?
set -e
printf '{"exit_code":%s}\n' "$result" > "$work/standalone_smoke.exit.json"
[[ $result == 0 ]]
sha256sum "$work/standalone/sn100_history" "$work/standalone/sn100_report_renderer"