#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
src=$root/06_programs/sn100_host
work=$root/temp/sn100_host/report_remaining_20261025
mkdir -p "$work/core_build" "$work/logs"
cd "$work/core_build"
/usr/lib/qt5/bin/qmake "$src/tests/report_remaining_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
make -j2