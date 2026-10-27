#!/usr/bin/env bash
# Standalone offline reagent page; same archived ARM Qt as the unified shell.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: reagents/build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
program_dir=$(cd -- "$module_dir/.." && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
sysroot=$root/temp/sn100_host/arm64_sysroot
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]] || {
    echo 'Existing archived ARM64 Qt5/SQL sysroot required.' >&2; exit 1;
}
build_dir=$root/temp/sn100_host/reagents/arm64_release
mkdir -p "$build_dir" "$(dirname -- "$1")"
exec 9>"$build_dir/build.lock"
flock -n 9 || { echo 'This reagent build is already running.' >&2; exit 1; }
qt_include=$sysroot/usr/include/aarch64-linux-gnu/qt5
qt_lib=$sysroot/usr/lib/aarch64-linux-gnu
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -fPIC -pthread -Wall -Wextra
    -I"$module_dir" -I"$program_dir" -I"$build_dir" -I"$program_dir/ui" -I"$program_dir/projects"
    -I"$program_dir/shell/widgets" -I"$program_dir/shell/widgets/material" -isystem "$qt_include")
for module in QtCore QtGui QtWidgets QtSql; do flags+=(-isystem "$qt_include/$module"); done
for form in "$module_dir/"*.ui "$program_dir/projects/frmsystemmaterial.ui" "$program_dir/ui/cusdialogmask.ui" "$program_dir/ui/cusmessagebox.ui"; do
    /usr/lib/qt5/bin/uic "$form" -o "$build_dir/ui_$(basename -- "${form%.ui}").h"
done
sources=("$module_dir/main.cpp" "$module_dir/reagent_store.cpp" "$module_dir/reagents_page.cpp"
    "$module_dir/wgtreagentpanel.cpp" "$module_dir/cusreagenthole.cpp"
    "$program_dir/projects/projects_store.cpp" "$program_dir/projects/project_page.cpp" "$program_dir/projects/cuscheckbox.cpp" "$program_dir/projects/project_advanced.cpp"
    "$program_dir/ui/cusdialogmask.cpp" "$program_dir/ui/cusmessagebox.cpp"
    "$program_dir/shell/widgets/cusview.cpp" "$program_dir/shell/widgets/material/"*.cpp "$program_dir/shell/widgets/material/lib/"*.cpp)
headers=("$module_dir/"*.h "$program_dir/projects/projects_store.h" "$program_dir/projects/project_page.h" "$program_dir/projects/cuscheckbox.h" "$program_dir/projects/project_advanced.h"
    "$program_dir/ui/cusdialogmask.h" "$program_dir/ui/cusmessagebox.h"
    "$program_dir/shell/widgets/cusview.h" "$program_dir/shell/widgets/material/"*.h "$program_dir/shell/widgets/material/lib/"*.h)
for header in "${headers[@]}"; do
    if grep -q Q_OBJECT "$header"; then
        generated=$build_dir/moc_$(basename -- "${header%.h}").cpp
        /usr/lib/qt5/bin/moc -I"$qt_include" -I"$module_dir" -I"$program_dir" "$header" -o "$generated"
        sources+=("$generated")
    fi
done
for resource in "$module_dir/reagents_resources.qrc" "$program_dir/projects/projects_resources.qrc" "$program_dir/shell/account_resources.qrc"; do
    name=$(basename -- "${resource%.qrc}")
    /usr/lib/qt5/bin/rcc -name "$name" "$resource" -o "$build_dir/qrc_$name.cpp"
    sources+=("$build_dir/qrc_$name.cpp")
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
