#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
evidence="$root/04_references/sn100_host/samples_20261024"
env QT_QPA_PLATFORM=offscreen bash "$root/06_programs/sn100_host/samples/wsl.sh" test -o "$evidence/offscreen_final.log,txt" -o "$evidence/offscreen_final.xml,xml" > "$evidence/rack_visibility_formal_build.log" 2>&1
cd "$root/temp/sn100_host/samples/wsl_tests"
env QT_QPA_PLATFORM=xcb SN100_SAMPLE_TEST_ROOT="$root/temp/sn100_host/samples/test_runtime_xcb_formal" ./samples_test -o "$evidence/xcb_final.log,txt" -o "$evidence/xcb_final.xml,xml"
