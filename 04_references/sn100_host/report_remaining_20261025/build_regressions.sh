#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
src=$root/06_programs/sn100_host
work=$root/temp/sn100_host/report_remaining_20261025
for name in history_page_guard_test history_pdf_text_test history_report_test; do
  mkdir -p "$work/regression_$name"
  cd "$work/regression_$name"
  /usr/lib/qt5/bin/qmake "$src/tests/$name.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
  make -j2
 done