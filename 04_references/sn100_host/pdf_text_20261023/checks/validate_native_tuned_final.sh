#!/usr/bin/env bash
set -Eeuo pipefail
cd /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
root=$PWD
work=$root/temp/sn100_host/history/pdf_text_20261023
bash 06_programs/sn100_host/history/wsl.sh build > "$work/native_build_tuned_final.log" 2>&1
helper=$root/temp/sn100_host/history/wsl_release/sn100_report_renderer
mkdir -p "$work/reports_tuned_final"
"$helper" "$root/04_references/sn100_host/offline_report_20261023/reports/printable7_snapshot.json" "$work/reports_tuned_final/report_values.pdf" > "$work/reports_tuned_final/report_values_renderer.txt" 2>&1
"$helper" "$root/04_references/sn100_host/offline_report_20261023/reports/rows64_snapshot.json" "$work/reports_tuned_final/report_64_tasks.pdf" > "$work/reports_tuned_final/report_64_renderer.txt" 2>&1
SN100_PDF_TEXT_TEST_DIR="$work/exact_tuned_final" SN100_REPORT_RENDERER="$helper" SN100_REPORT_PDF_PYTHON=/mnt/c/Users/52400/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe SN100_PDF_TEXT_VALIDATOR="$root/06_programs/sn100_host/tests/verify_history_pdf_text.py" "$root/temp/sn100_host/history/wsl_pdf_text_tests/history_pdf_text_test" > "$work/exact_tuned_final.log" 2>&1
python3 "$root/temp/sn100_host/history/pdf_text_tests/measure_renderer.py" --renderer "$helper" --snapshot "$root/04_references/sn100_host/offline_report_20261023/reports/rows64_snapshot.json" --output "$work/new_measure_tuned" --repeat 3 > "$work/new_measure_tuned_summary.json"
