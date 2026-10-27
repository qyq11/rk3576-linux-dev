#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work=$root/temp/sn100_host/report_remaining_20261025
run_name=${1:?new run name required}
[[ $run_name =~ ^html_run_[0-9]+$ ]]
cd "$work/html_tests"
set +e
QT_QPA_PLATFORM=offscreen SN100_REPORT_RENDERER="$work/html_renderer/sn100_report_renderer" SN100_REMAINING_HTML_DIR="$work/$run_name" ./report_remaining_html_test -o "$work/html_logs/$run_name.log",txt -o "$work/html_logs/$run_name.xml",junitxml
result=$?
set -e
printf '{"command":"QT_QPA_PLATFORM=offscreen SN100_REPORT_RENDERER=html_renderer/sn100_report_renderer SN100_REMAINING_HTML_DIR=%s report_remaining_html_test","exit_code":%s}\n' "$run_name" "$result" > "$work/html_logs/$run_name.exit.json"
cat "$work/html_logs/$run_name.log"
exit "$result"