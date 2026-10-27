#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work=$root/temp/sn100_host/report_remaining_20261025
for rows in 3 64; do
  python3 "$root/06_programs/sn100_host/tests/report_remaining_html_verify.py" --html "$work/html_run_03/exports/rows_$rows.html" --snapshot "$work/html_run_03/rows_$rows.json" --output "$work/html_run_03/rows_${rows}_python_reparse.json"
done