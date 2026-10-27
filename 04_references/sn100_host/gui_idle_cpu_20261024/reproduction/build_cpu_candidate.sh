#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work=$root/temp/sn100_host/o09_loopback
mkdir -p "$work/logs"
SN100_SHELL_WORK_DIR="$work/native" SN100_JOBS=2 bash "$root/06_programs/sn100_host/shell/wsl.sh" integrated-build > "$work/logs/cpu_native_build.log" 2>&1
bash "$work/build_material_test.sh" > "$work/logs/material_candidate_build.log" 2>&1
env QT_QPA_PLATFORM=offscreen SN100_MATERIAL_EVIDENCE_DIR="$work/material_offscreen" \
    "$work/material_test/material_idle_test" -o "$work/logs/material_candidate_offscreen.txt",txt
