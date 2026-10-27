#!/usr/bin/env bash
# Standalone ARM64 build only. Integration owns final serial Windows/ARM builds.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: calibration/build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
program_dir=$(cd -- "$module_dir/.." && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
sysroot=$root/temp/sn100_host/arm64_sysroot
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]] || {
    echo 'Existing archived ARM64 Qt5/SQL sysroot required; no environment change attempted.' >&2; exit 1;
}
build_dir=$root/temp/sn100_host/calibration_local_20261025/arm64_release
mkdir -p "$build_dir" "$(dirname -- "$1")"
exec 9>"$build_dir/build.lock"
flock -n 9 || { echo 'This calibration build is already running.' >&2; exit 1; }
qt_include=$sysroot/usr/include/aarch64-linux-gnu/qt5
qt_lib=$sysroot/usr/lib/aarch64-linux-gnu
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -fPIC -pthread -Wall -Wextra
    -I"$module_dir" -I"$program_dir" -I"$build_dir"
    -I"$program_dir/shell/widgets" -I"$program_dir/shell/widgets/material" -isystem "$qt_include")
for module in QtCore QtGui QtWidgets QtSql; do flags+=(-isystem "$qt_include/$module"); done
/usr/lib/qt5/bin/uic "$module_dir/frmcalibrationmaterial.ui" -o "$build_dir/ui_frmcalibrationmaterial.h"
sources=("$module_dir/main.cpp" "$module_dir/calibration_store.cpp" "$module_dir/calibration_page.cpp"
    "$module_dir/calibrationchart.cpp" "$program_dir/shell/widgets/cusview.cpp"
    "$program_dir/shell/widgets/material/"*.cpp "$program_dir/shell/widgets/material/lib/"*.cpp)
headers=("$module_dir/calibration_store.h" "$module_dir/calibration_page.h" "$module_dir/calibrationchart.h"
    "$program_dir/shell/widgets/cusview.h" "$program_dir/shell/widgets/material/"*.h
    "$program_dir/shell/widgets/material/lib/"*.h)
for header in "${headers[@]}"; do
    if rg -q Q_OBJECT "$header"; then
        generated=$build_dir/moc_$(basename -- "${header%.h}").cpp
        /usr/lib/qt5/bin/moc -I"$qt_include" -I"$module_dir" -I"$program_dir" "$header" -o "$generated"
        sources+=("$generated")
    fi
done
objects=(); pids=()
wait_batch()
{
    local failed=0
    for task_pid in "${pids[@]}"; do wait "$task_pid" || failed=1; done
    pids=()
    return "$failed"
}
for source in "${sources[@]}"; do
    object=$build_dir/obj_${#objects[@]}.o; objects+=("$object")
    aarch64-linux-gnu-g++ "${flags[@]}" -c "$source" -o "$object" &
    pids+=("$!")
    if [[ ${#pids[@]} -ge 2 ]]; then wait_batch; fi
done
wait_batch
temporary=$(mktemp "$1.new.XXXXXX")
trap 'rm -f -- "$temporary"' EXIT
aarch64-linux-gnu-g++ "${objects[@]}" -L"$qt_lib" -L"$sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$qt_lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
    -lQt5Widgets -lQt5Gui -lQt5Sql -lQt5Core -lpthread -o "$temporary"
file "$temporary" | rg -q 'ARM aarch64'
chmod 755 "$temporary"
mv -f -- "$temporary" "$1"
sha256sum "$1"
