#!/usr/bin/env bash
# Isolated QC page ARM build; uses only the already prepared offline Qt sysroot.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: quality_control/build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
program_dir=$(cd -- "$module_dir/.." && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
sysroot=${SN100_ARM64_SYSROOT:-$root/temp/sn100_host/arm64_sysroot}
[[ $sysroot == /* ]] || { echo 'SN100_ARM64_SYSROOT must be absolute.' >&2; exit 2; }
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]] || {
    echo 'Existing ARM64 Qt5/SQL sysroot required; restore through the existing project workflow.' >&2; exit 1;
}
build_dir=${SN100_ARM64_BUILD_DIR:-$root/temp/sn100_host/qc_local_20261025/page/arm64_release}
[[ $build_dir == /* ]] || { echo 'SN100_ARM64_BUILD_DIR must be absolute.' >&2; exit 2; }
mkdir -p "$build_dir" "$(dirname -- "$1")"
exec 9>"$build_dir/build.lock"
flock -n 9 || { echo 'This QC page build is already running.' >&2; exit 1; }
qt_include=$sysroot/usr/include/aarch64-linux-gnu/qt5
qt_lib=$sysroot/usr/lib/aarch64-linux-gnu
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -fPIC -pthread -Wall -Wextra
    -I"$module_dir" -I"$program_dir" -I"$build_dir" -I"$program_dir/projects" -I"$program_dir/ui"
    -I"$program_dir/samples/material" -I"$program_dir/shell/widgets" -I"$program_dir/shell/widgets/material"
    -isystem "$qt_include")
for module in QtCore QtGui QtWidgets QtSql; do flags+=(-isystem "$qt_include/$module"); done
for form in frm_quality mask_dlg_add_control mask_dlg_control_batch_setting quality_control_add_wgt quality_control_item_add_project_wgt cus_quality_chart; do
    /usr/lib/qt5/bin/uic "$module_dir/$form.ui" -o "$build_dir/ui_$form.h"
done
/usr/lib/qt5/bin/uic "$program_dir/ui/cusdialogmask.ui" -o "$build_dir/ui_cusdialogmask.h"
/usr/lib/qt5/bin/uic "$program_dir/ui/cusmessagebox.ui" -o "$build_dir/ui_cusmessagebox.h"
sources=("$module_dir/main.cpp" "$module_dir/qc_store.cpp" "$module_dir/qc_history_core.cpp"
    "$program_dir/quality/statistics.cpp" "$module_dir/quality_control_page.cpp"
    "$module_dir/qualitychart.cpp" "$module_dir/cus_quality_chart.cpp" "$module_dir/qc_history_presenter.cpp"
    "$program_dir/projects/projects_store.cpp" "$program_dir/projects/project_advanced.cpp" "$program_dir/projects/cuscheckbox.cpp"
    "$program_dir/ui/cusdialogmask.cpp" "$program_dir/ui/cusmessagebox.cpp" "$program_dir/samples/material/qtmaterialscrollbar.cpp" "$program_dir/samples/material/qtmaterialscrollbar_internal.cpp"
    "$program_dir/shell/widgets/cusview.cpp" "$program_dir/shell/widgets/material/"*.cpp "$program_dir/shell/widgets/material/lib/"*.cpp)
headers=("$module_dir/qc_store.h" "$module_dir/qc_history_core.h" "$module_dir/qc_history_reply.h"
    "$program_dir/quality/statistics.h" "$module_dir/quality_control_page.h"
    "$module_dir/qualitychart.h" "$module_dir/cus_quality_chart.h" "$module_dir/qc_history_presenter.h"
    "$program_dir/projects/projects_store.h" "$program_dir/projects/project_advanced.h" "$program_dir/projects/cuscheckbox.h"
    "$program_dir/ui/cusdialogmask.h" "$program_dir/ui/cusmessagebox.h" "$program_dir/samples/material/qtmaterialscrollbar.h" "$program_dir/samples/material/qtmaterialscrollbar_internal.h"
    "$program_dir/shell/widgets/cusview.h" "$program_dir/shell/widgets/material/"*.h "$program_dir/shell/widgets/material/lib/"*.h)
# 原库适配与结果显示由同一存储入口引用，独立构建也必须带齐。
sources+=("$program_dir/projects/projects_current_format.cpp" "$program_dir/projects/original_project_repository.cpp")
headers+=("$program_dir/projects/project_current_format.h" "$program_dir/projects/original_project_repository.h")

for header in "${headers[@]}"; do
    if grep -q Q_OBJECT "$header"; then
        generated=$build_dir/moc_$(basename -- "${header%.h}").cpp
        /usr/lib/qt5/bin/moc -I"$qt_include" -I"$module_dir" -I"$program_dir" "$header" -o "$generated"
        sources+=("$generated")
    fi
done
for resource in "$program_dir/projects/projects_resources.qrc" "$program_dir/shell/account_resources.qrc" "$module_dir/qc_history_resources.qrc"; do
    resource_name=$(basename -- "${resource%.qrc}")
    /usr/lib/qt5/bin/rcc -name "$resource_name" "$resource" -o "$build_dir/qrc_$resource_name.cpp"
    sources+=("$build_dir/qrc_$resource_name.cpp")
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
