#!/usr/bin/env bash
set -Eeuo pipefail
task_root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
test_dir="$task_root/temp/sn100_host/coagulation/core_tests"
cd "$test_dir"
/usr/lib/qt5/bin/qmake "$task_root/06_programs/sn100_host/coagulation/tests/endpoint_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
make -j2
./sn100_clot_endpoint_test -o "$test_dir/results.txt",txt -o "$test_dir/results.xml",junitxml
