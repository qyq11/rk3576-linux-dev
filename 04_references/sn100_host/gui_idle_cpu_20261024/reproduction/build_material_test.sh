#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work=$root/temp/sn100_host/o09_loopback
mkdir -p "$work/material_test" "$work/logs"
cd "$work/material_test"
/usr/lib/qt5/bin/qmake "$root/06_programs/sn100_host/tests/material_idle_test.pro" CONFIG+=release CONFIG-=debug
make -j2
