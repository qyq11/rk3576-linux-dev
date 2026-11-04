#!/usr/bin/env bash
# 下一批只交叉编译ARM64、核静态依赖和打包；不运行产品或连接板卡。
set -Eeuo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin

tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
task=$(cd -- "$tools/.." && pwd)
root=$(cd -- "$task/../../.." && pwd)
module=$root/06_programs/sn100_host/shell
work=$task/integration_next_02
sysroot=$root/temp/sn100_host/rk_prepare/sysroot
[[ $EUID != 0 ]] || { echo '请使用普通WSL用户构建。' >&2; exit 2; }
[[ ! -e $work && ! -L $work ]] || { echo '已有下一批构建目录，保留原件，不覆盖或自动重跑。' >&2; exit 2; }
# 模板或未完成晋级不能开始本批；这里不会创建work或恢复编译环境。
python3 "$tools/integration_next_tool_support.py" preflight
mkdir "$work"
completed=0
log_pid=
exec 3>&1 4>&2
finish() {
    code=$?
    trap - EXIT INT TERM HUP
    if [[ $code == 0 && $completed != 1 ]]; then code=125; fi
    # 先关闭日志管道再等tee排空，避免外部在日志未写完时开始归档。
    exec 1>&3 2>&4
    if [[ -n $log_pid ]]; then
        if ! wait "$log_pid"; then
            if [[ $code == 0 ]]; then code=125; fi
        fi
    fi
    if ! printf '%s\n' "$code" > "$work/build_exit.txt"; then code=125; fi
    exec 3>&- 4>&-
    exit "$code"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir "$work/build" "$work/packages" "$work/logs"
exec > >(tee "$work/logs/build.log") 2>&1
log_pid=$!

# 正式build.sh在缺QtSql时可能进入恢复分支。本批先核已有配套环境，缺失即停。
qt_modules=(Core Gui Widgets Sql Xml PrintSupport Qml Network Charts Svg)
for qt_module in "${qt_modules[@]}"; do
    [[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5${qt_module}.so ]] || {
        echo "已有sysroot缺少Qt5${qt_module}链接库，未恢复或重建环境。" >&2; exit 1;
    }
    [[ -d $sysroot/usr/include/aarch64-linux-gnu/qt5/Qt${qt_module} ]] || {
        echo "已有sysroot缺少Qt${qt_module}头文件目录，未恢复或重建环境。" >&2; exit 1;
    }
done
for input in usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 \
    usr/lib/aarch64-linux-gnu/libQt5Charts.so.5.15.13 \
    usr/include/aarch64-linux-gnu/qt5/QtSvg/QSvgRenderer; do
    [[ -f $sysroot/$input ]] || { echo "缺少固定交叉编译输入：$input；未恢复环境。" >&2; exit 1; }
done
for tool in python3 aarch64-linux-gnu-g++ readelf file flock; do
    command -v "$tool" >/dev/null || { echo "缺少已有构建工具：$tool；未安装或重建。" >&2; exit 1; }
done
for tool in /usr/lib/qt5/bin/uic /usr/lib/qt5/bin/moc /usr/lib/qt5/bin/rcc; do
    [[ -x $tool ]] || { echo "缺少已有Qt工具：$tool；未安装或重建。" >&2; exit 1; }
done
unset SN100_ARM64_OBJECT_REUSE_MANIFEST SN100_ARM64_FROZEN_SNAPSHOT
export RK_BUILD_TYPE=Release

python3 "$module/package_arm64.py" snapshot --output "$work/source_snapshot.json"
SN100_ARM64_LIST_INPUTS=1 SN100_ARM64_SYSROOT="$sysroot" SN100_ARM64_BUILD_DIR="$work/arm64_build" \
    bash "$module/build.sh" "$work/build/sn100_shell" > "$work/build_inputs.tsv"
python3 "$tools/verify_integration_next_inputs.py"
# 调用前再次拦住正式脚本唯一的环境恢复条件，不接受旧产物替代失败构建。
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]] || { echo 'QtSql输入消失，未开始编译。' >&2; exit 1; }
SN100_ARM64_LIST_INPUTS=0 SN100_ARM64_SYSROOT="$sysroot" SN100_ARM64_BUILD_DIR="$work/arm64_build" SN100_JOBS=2 \
    bash "$module/build.sh" "$work/build/sn100_shell"
python3 "$module/check_arm64.py" "$work/build/sn100_shell" \
    --additional-binary "$work/build/sn100_report_renderer" --sysroot "$sysroot" \
    --output-dir "$work/dependencies"
python3 "$module/package_arm64.py" package --binary "$work/build/sn100_shell" \
    --sysroot "$sysroot" --snapshot "$work/source_snapshot.json" \
    --dependencies "$work/dependencies/dependency_report.json" --output "$work/packages"
python3 "$tools/verify_integration_next_inputs.py" --after-package
printf 'ARM64_INTEGRATION_next_BUILD_AND_PACKAGE_PASSED=1\nPRODUCT_EXECUTED=0\nRK_RUNTIME_VERIFIED=0\n'
completed=1
