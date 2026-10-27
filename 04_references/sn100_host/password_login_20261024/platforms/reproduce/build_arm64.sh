#!/usr/bin/env bash
set -Eeuo pipefail
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
program=$root/06_programs/sn100_host
work=$root/temp/sn100_host/o02_login/platforms/arm64
native_freeze=$root/temp/sn100_host/o02_login/native/after.json
[[ -f $native_freeze && ! -e $work ]] || { echo 'Completed native freeze and new ARM work directory required.' >&2; exit 2; }
unset SN100_ARM64_OBJECT_REUSE_MANIFEST SN100_ARM64_FROZEN_SNAPSHOT SN100_ARM64_REUSE_CPP
# Reuse archived inputs only. Missing baseline manifests must never trigger a download.
for input in \
    qt_environment/sn100_rk_runtime/loader.SHA256SUMS \
    qt_environment/sn100_rk_runtime/sqlite.SHA256SUMS \
    qt_environment/qt5_arm64/SHA256SUMS \
    qt_environment/sn100_arm64/SHA256SUMS \
    qt_environment/sn100_report_arm64/SHA256SUMS \
    qt_environment/wsl_amd64/SHA256SUMS \
    qt_environment/sn100_report_runtime_arm64/SHA256SUMS \
    qt_environment/sn100_samples_arm64/SHA256SUMS; do
    [[ -f $root/07_backups/$input ]] || { echo "Required archived input missing: $input" >&2; exit 2; }
done
mkdir -p "$work"
python3 -B "$root/temp/sn100_host/o02_login/platforms/prepare_inventory.py" --phase arm-before
python3 "$program/shell/package_arm64.py" snapshot --output "$work/source_before.json"
cmp "$native_freeze" "$work/source_before.json"
SN100_ARM64_LIST_INPUTS=1 bash "$program/shell/build.sh" "$work/check_only" > "$work/actual_input_list.tsv"
cat > "$work/trace_build.bash" <<'TRACE'
if [[ $0 == */sn100_host/shell/build.sh ]]; then set -x; fi
TRACE
BASH_ENV="$work/trace_build.bash" SN100_RK_WORK_DIR="$work" SN100_JOBS=2 RK_BUILD_TYPE=Release \
    bash "$program/shell/rk.sh" prepare > "$work/prepare.log" 2>&1
python3 "$program/shell/package_arm64.py" snapshot --output "$work/source_after.json"
cmp "$work/source_before.json" "$work/source_after.json"
python3 "$program/shell/check_arm64.py" "$work/build/sn100_shell" \
    --additional-binary "$work/build/sn100_report_renderer" --sysroot "$work/sysroot" \
    --output-dir "$work/final_dependencies" > "$work/final_check.log" 2>&1
python3 -B "$root/temp/sn100_host/o02_login/platforms/prepare_inventory.py" --phase arm-after
sha256sum "$work/build/sn100_shell" "$work/build/sn100_report_renderer"
echo 'ARM_FULL_BUILD_AND_STATIC_CLOSURE_COMPLETE; BOARD_RUNTIME_VERIFIED=0'
