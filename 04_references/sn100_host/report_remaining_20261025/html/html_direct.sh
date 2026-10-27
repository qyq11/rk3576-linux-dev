#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work=$root/temp/sn100_host/report_remaining_20261025
set +e
"$work/html_renderer/sn100_report_renderer" --html "$work/html_run_03/rows_3.json" "$work/html_run_03/exports/direct_rows3.html" > "$work/html_logs/html_direct_3.log" 2>&1
result=$?
set -e
printf '{"exit_code":%s,"mode":"--html","snapshot":"html_run_03/rows_3.json","output":"html_run_03/exports/direct_rows3.html"}\n' "$result" > "$work/html_logs/html_direct_3.exit.json"
cat "$work/html_logs/html_direct_3.log"
exit "$result"