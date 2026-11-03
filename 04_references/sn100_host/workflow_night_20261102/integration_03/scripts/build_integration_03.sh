#!/usr/bin/env bash
# 只编译ARM64、核依赖并打包；不在电脑执行程序，也不连接板卡。
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
module=$root/06_programs/sn100_host/shell
work=$root/temp/sn100_host/workflow_night_20261102/integration_03
sysroot=$root/temp/sn100_host/rk_prepare/sysroot
mkdir -p "$work/build" "$work/packages" "$work/logs"
exec > >(tee -a "$work/logs/build.log") 2>&1
completed=0
trap 'code=$?; if [[ $code == 0 && $completed != 1 ]]; then code=125; fi; printf "%s\n" "$code" > "$work/build_exit.txt"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
test -f "$sysroot/usr/lib/aarch64-linux-gnu/libQt5Core.so"
python3 "$module/package_arm64.py" snapshot --output "$work/source_snapshot.json"
SN100_ARM64_LIST_INPUTS=1 SN100_ARM64_SYSROOT="$sysroot" \
    bash "$module/build.sh" "$work/build/sn100_shell" > "$work/build_inputs.tsv"
python3 "$(dirname -- "${BASH_SOURCE[0]}")/verify_integration_03_inputs.py"
SN100_ARM64_SYSROOT="$sysroot" SN100_ARM64_BUILD_DIR="$work/arm64_build" SN100_JOBS=2 \
    bash "$module/build.sh" "$work/build/sn100_shell"
python3 "$module/check_arm64.py" "$work/build/sn100_shell" \
    --additional-binary "$work/build/sn100_report_renderer" --sysroot "$sysroot" \
    --output-dir "$work/dependencies"
python3 "$module/package_arm64.py" package --binary "$work/build/sn100_shell" \
    --sysroot "$sysroot" --snapshot "$work/source_snapshot.json" \
    --dependencies "$work/dependencies/dependency_report.json" --output "$work/packages"
completed=1
printf 'ARM64_INTEGRATION_BUILD_AND_PACKAGE_PASSED=1\nRK_RUNTIME_VERIFIED=0\n'
