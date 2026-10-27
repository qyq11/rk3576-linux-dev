#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
out="$root/temp/sn100_host/samples/visibility_formal"
evidence="$root/04_references/sn100_host/samples_20261024/rack_visibility_formal"
mkdir -p "$out" "$evidence"
cd "$root/temp/sn100_host/samples/wsl_tests"
make -f ../rack_visibility_formal_probe.mk -j2 ../visibility_formal/rack_visibility_probe > "$evidence/build.log" 2>&1
env QT_QPA_PLATFORM=offscreen SN100_SAMPLE_TEST_ROOT="$out/offscreen_runtime" "$out/rack_visibility_probe" > "$evidence/offscreen.log" 2>&1
env QT_QPA_PLATFORM=xcb SN100_SAMPLE_TEST_ROOT="$out/xcb_runtime" SN100_SAMPLE_CAPTURE_DIR="$evidence" "$out/rack_visibility_probe" > "$evidence/xcb.log" 2>&1
sha256sum "$root/06_programs/sn100_host/samples/sample_page.cpp" .obj/sample_page.o samples_test "$out/rack_visibility_probe" > "$evidence/input_artifact_sha256.txt"
