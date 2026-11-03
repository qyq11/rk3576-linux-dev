#!/usr/bin/env bash
# 复用已核对的ARM64运行组件，仅交叉编译；成功后检查组件并生成正式包。
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)
module=$root/06_programs/sn100_host/shell
work=$root/temp/sn100_host/rk_prepare
test -f "$work/sysroot/usr/lib/aarch64-linux-gnu/libQt5Core.so"
python3 "$module/package_arm64.py" snapshot --output "$work/source_snapshot.json"
SN100_ARM64_SYSROOT="$work/sysroot" SN100_ARM64_BUILD_DIR="$work/arm64_build" SN100_JOBS=2 \
    bash "$module/build.sh" "$work/build/sn100_shell.new"
python3 "$module/check_arm64.py" "$work/build/sn100_shell.new" --additional-binary "$work/build/sn100_report_renderer" \
    --sysroot "$work/sysroot" --output-dir "$work/dependencies"
mv -- "$work/build/sn100_shell.new" "$work/build/sn100_shell"
python3 "$module/package_arm64.py" package --binary "$work/build/sn100_shell" --sysroot "$work/sysroot" \
    --snapshot "$work/source_snapshot.json" --dependencies "$work/dependencies/dependency_report.json" --output "$work/packages"
printf 'ARM64_BUILD_AND_PACKAGE_PASSED=1\n'
