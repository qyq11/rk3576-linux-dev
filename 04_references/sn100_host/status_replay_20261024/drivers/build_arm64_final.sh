#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
out=$root/temp/sn100_host/status_replay_20261024
trap 'printf "%s\n" "$?" > "$out/logs/arm64_final.exit"' EXIT
export SN100_ARM64_OBJECT_REUSE_MANIFEST=$out/arm_baseline_objects.json
export SN100_ARM64_FROZEN_SNAPSHOT=$out/arm64/source_snapshot.json
export SN100_ARM64_REUSE_ALLOWED_CPP=shell/device_status_shell.cpp
export SN100_ARM64_REUSE_UI_SNAPSHOT=$out/arm_baseline_ui_sha256.json
SN100_RK_WORK_DIR="$out/arm64" SN100_JOBS=2 bash "$root/06_programs/sn100_host/shell/rk.sh" prepare > "$out/logs/arm64_prepare_final.log" 2>&1
tail -n 8 "$out/logs/arm64_prepare_final.log"
