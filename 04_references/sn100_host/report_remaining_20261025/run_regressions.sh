#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
src=$root/06_programs/sn100_host
work=$root/temp/sn100_host/report_remaining_20261025
export QT_QPA_PLATFORM=offscreen
export SN100_REPORT_RENDERER=$work/html_renderer/sn100_report_renderer
export SN100_REPORT_PDF_PYTHON=/mnt/c/Users/52400/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe
export SN100_PDF_TEXT_VALIDATOR=$src/tests/verify_history_pdf_text.py
export SN100_PDF_TEXT_TEST_DIR=$work/pdf_text_run_01
export SN100_REPORT_EVIDENCE_DIR=$work/old_contract_run_01
run_case()
{
  local name=$1
  shift
  set +e
  "$@" -o "$work/$name.txt",txt -o "$work/$name.xml",junitxml
  result=$?
  set -e
  printf '{"exit_code":%s}\n' "$result" > "$work/$name.exit.json"
  cat "$work/$name.txt"
  [[ $result == 0 ]]
}
run_case guard_regression "$work/regression_history_page_guard_test/history_page_guard_test"
run_case old_report_contract "$work/regression_history_report_test/history_report_test" statisticsGolden statisticsBoundaries statisticsGrouping documentContract rendererOwnership
run_case pdf_text_regression "$work/regression_history_pdf_text_test/history_pdf_text_test"