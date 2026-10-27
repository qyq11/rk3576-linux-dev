#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work=$root/temp/sn100_host/report_remaining_20261025
mkdir -p "$work/html_renderer" "$work/html_tests" "$work/html_logs"
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd "$work/html_renderer"
/usr/lib/qt5/bin/qmake "$root/06_programs/sn100_host/history/renderer.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug > "$work/html_logs/renderer_qmake.log" 2>&1
make -j2 > "$work/html_logs/renderer_make.log" 2>&1
cd "$work/html_tests"
/usr/lib/qt5/bin/qmake "$root/06_programs/sn100_host/tests/report_remaining_html_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug > "$work/html_logs/test_qmake.log" 2>&1
make -j2 > "$work/html_logs/test_make.log" 2>&1