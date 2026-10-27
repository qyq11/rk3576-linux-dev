#!/usr/bin/env bash
# Build only this isolated projects page, using the existing archived ARM Qt.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: projects/build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
program_dir=$(cd -- "$module_dir/.." && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
sysroot=$root/temp/sn100_host/arm64_sysroot
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]] || {
    echo 'Existing ARM64 Qt5/SQL sysroot required; restore using sn100_host/prepare_arm64.sh.' >&2; exit 1;
}
build_dir=$root/temp/sn100_host/projects/arm64_release
mkdir -p "$build_dir" "$(dirname -- "$1")"
exec 9>"$build_dir/build.lock"
flock -n 9 || { echo 'This projects build is already running.' >&2; exit 1; }
qt_include=$sysroot/usr/include/aarch64-linux-gnu/qt5
qt_lib=$sysroot/usr/lib/aarch64-linux-gnu
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -fPIC -pthread -Wall -Wextra
    -I"$module_dir" -I"$program_dir" -I"$build_dir"
    -I"$program_dir/shell/widgets" -I"$program_dir/shell/widgets/material"
    -isystem "$qt_include")
for module in QtCore QtGui QtWidgets QtSql; do flags+=(-isystem "$qt_include/$module"); done
/usr/lib/qt5/bin/uic "$module_dir/frmsystemmaterial.ui" -o "$build_dir/ui_frmsystemmaterial.h"
sources=("$module_dir/main.cpp" "$module_dir/projects_store.cpp" "$module_dir/project_page.cpp" "$module_dir/cuscheckbox.cpp" "$module_dir/project_advanced.cpp"
    "$program_dir/shell/widgets/cusview.cpp" "$program_dir/shell/widgets/material/"*.cpp "$program_dir/shell/widgets/material/lib/"*.cpp)
headers=("$module_dir/projects_store.h" "$module_dir/project_page.h" "$module_dir/cuscheckbox.h" "$module_dir/project_advanced.h"
    "$program_dir/shell/widgets/cusview.h" "$program_dir/shell/widgets/material/"*.h "$program_dir/shell/widgets/material/lib/"*.h)
# 原库适配与结果显示由同一存储入口引用，独立构建也必须带齐。
sources+=("$module_dir/projects_current_format.cpp" "$module_dir/original_project_repository.cpp")
headers+=("$module_dir/project_current_format.h" "$module_dir/original_project_repository.h")
sources+=("$module_dir/project_format_dialog.cpp")
headers+=("$module_dir/project_format_dialog.h")

for header in "${headers[@]}"; do
    if grep -q Q_OBJECT "$header"; then
        generated=$build_dir/moc_$(basename -- "${header%.h}").cpp
        /usr/lib/qt5/bin/moc -I"$qt_include" -I"$module_dir" -I"$program_dir" "$header" -o "$generated"
        sources+=("$generated")
    fi
done
for resource in projects_resources projects_standalone_resources; do
    /usr/lib/qt5/bin/rcc -name "$resource" "$module_dir/$resource.qrc" -o "$build_dir/qrc_$resource.cpp"
    sources+=("$build_dir/qrc_$resource.cpp")
done
objects=()
pids=()
wait_batch()
{
    local failed=0
    for task_pid in "${pids[@]}"; do wait "$task_pid" || failed=1; done
    pids=()
    return "$failed"
}
for source in "${sources[@]}"; do
    object=$build_dir/obj_${#objects[@]}.o
    objects+=("$object")
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
file "$temporary" | grep -q 'ARM aarch64'
chmod 755 "$temporary"
mv -f -- "$temporary" "$1"
sha256sum "$1"
