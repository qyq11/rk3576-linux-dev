#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
out=$root/temp/sn100_host/status_replay_20261024
trap 'printf "%s\n" "$?" > "$out/logs/native_final.exit"' EXIT
python3 "$root/06_programs/sn100_host/shell/package_arm64.py" snapshot --output "$out/native/final_inputs_before.json"
SN100_SHELL_WORK_DIR="$out/native" SN100_JOBS=4 bash "$root/06_programs/sn100_host/shell/wsl.sh" integrated-build > "$out/logs/native_build_final.log" 2>&1
python3 "$root/06_programs/sn100_host/shell/package_arm64.py" snapshot --output "$out/native/final_inputs_after.json"
cmp "$out/native/final_inputs_before.json" "$out/native/final_inputs_after.json"
sha256sum "$out/native/wsl_release/sn100_shell" "$out/native/wsl_release/sn100_report_renderer"
