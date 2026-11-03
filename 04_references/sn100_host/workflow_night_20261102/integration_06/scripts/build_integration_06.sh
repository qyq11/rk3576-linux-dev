#!/usr/bin/env bash
# 第06批：只做ARM64交叉编译、静态依赖核查和打包，不运行产品或连接板卡。
set -Eeuo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin
task=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$task/../../.." && pwd)
module=$root/06_programs/sn100_host/shell
work=$task/integration_06
sysroot=$root/temp/sn100_host/rk_prepare/sysroot
[[ ! -e $work && ! -L $work ]] || { echo '已有第06批构建目录，请人工核查；不覆盖或自动重跑。' >&2; exit 2; }
mkdir "$work"
completed=0
finish() {
    code=$?
    trap - EXIT
    if [[ $code == 0 && $completed != 1 ]]; then code=125; fi
    printf '%s\n' "$code" > "$work/build_exit.txt" || code=125
    exit "$code"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir "$work/build" "$work/packages" "$work/logs"
exec > >(tee "$work/logs/build.log") 2>&1
# 先确认已有配套环境，缺失即停，不让正式build.sh进入环境恢复分支。
for input in usr/lib/aarch64-linux-gnu/libQt5Core.so usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 usr/lib/aarch64-linux-gnu/libQt5Charts.so.5.15.13 usr/lib/aarch64-linux-gnu/libQt5Svg.so usr/include/aarch64-linux-gnu/qt5/QtSvg/QSvgRenderer; do
    [[ -f $sysroot/$input ]] || { echo "缺少同批交叉编译输入：$input" >&2; exit 1; }
done
python3 "$module/package_arm64.py" snapshot --output "$work/source_snapshot.json"
SN100_ARM64_LIST_INPUTS=1 SN100_ARM64_SYSROOT="$sysroot" \
    bash "$module/build.sh" "$work/build/sn100_shell" > "$work/build_inputs.tsv"
python3 "$task/verify_integration_06_inputs.py"
SN100_ARM64_SYSROOT="$sysroot" SN100_ARM64_BUILD_DIR="$work/arm64_build" SN100_JOBS=2 \
    bash "$module/build.sh" "$work/build/sn100_shell"
python3 "$module/check_arm64.py" "$work/build/sn100_shell" \
    --additional-binary "$work/build/sn100_report_renderer" --sysroot "$sysroot" \
    --output-dir "$work/dependencies"
python3 "$module/package_arm64.py" package --binary "$work/build/sn100_shell" \
    --sysroot "$sysroot" --snapshot "$work/source_snapshot.json" \
    --dependencies "$work/dependencies/dependency_report.json" --output "$work/packages"
python3 "$task/verify_integration_06_inputs.py" --after-package
printf 'ARM64_INTEGRATION_06_BUILD_AND_PACKAGE_PASSED=1\nRK_RUNTIME_VERIFIED=0\n'
completed=1
