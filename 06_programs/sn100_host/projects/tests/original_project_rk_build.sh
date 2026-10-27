#!/usr/bin/env bash
# 仅交叉编译。生成的驱动须上传RK执行，不能在电脑运行。
set -Eeuo pipefail
[[ $# == 1 && $1 == /* && $EUID != 0 ]] || { echo 'Usage: original_project_rk_build.sh ABSOLUTE_OUTPUT_FILE (ordinary WSL user)' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$test_dir/.." && pwd)
program_dir=$(cd -- "$project_dir/.." && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
sysroot=${SN100_ARM64_SYSROOT:-$root/temp/sn100_host/rk_prepare/sysroot}
build=${SN100_ORIGINAL_RK_BUILD_DIR:-$root/temp/sn100_host/original_db_20261027/integration/driver_build}
[[ $build == /* && $sysroot == /* ]] || exit 2
mkdir -p "$build" "$(dirname -- "$1")"
exec 9>"$build/build.lock"; flock -n 9 || exit 1
qt=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -fPIC -pthread -Wall -Wextra
    -I"$project_dir" -I"$program_dir" -I"$build" -I"$program_dir/shell/widgets" -I"$program_dir/shell/widgets/material" -isystem "$qt")
for module in QtCore QtGui QtWidgets QtSql; do flags+=(-isystem "$qt/$module"); done
stage=${SN100_ORIGINAL_RK_STAGE:-store}
case $stage in store|page) ;; *) echo 'SN100_ORIGINAL_RK_STAGE must be store or page.' >&2; exit 2 ;; esac
sources=("$test_dir/original_project_${stage}_rk_driver.cpp" "$project_dir/projects_store.cpp" "$project_dir/projects_current_format.cpp"
    "$project_dir/project_advanced.cpp" "$project_dir/cuscheckbox.cpp" "$project_dir/original_project_repository.cpp")
headers=("$project_dir/projects_store.h" "$project_dir/project_advanced.h" "$project_dir/cuscheckbox.h" "$project_dir/original_project_repository.h")
if [[ $stage == page ]]; then
    sources+=("$project_dir/project_page.cpp" "$project_dir/project_format_dialog.cpp" "$program_dir/shell/widgets/cusview.cpp"
        "$program_dir/shell/widgets/material/"*.cpp "$program_dir/shell/widgets/material/lib/"*.cpp "$program_dir/reagents/reagent_store.cpp")
    headers+=("$project_dir/project_page.h" "$project_dir/project_format_dialog.h" "$program_dir/shell/widgets/cusview.h"
        "$program_dir/shell/widgets/material/"*.h "$program_dir/shell/widgets/material/lib/"*.h "$program_dir/reagents/reagent_store.h")
    /usr/lib/qt5/bin/uic "$project_dir/frmsystemmaterial.ui" -o "$build/ui_frmsystemmaterial.h"
fi
for header in "${headers[@]}"; do
    if grep -q Q_OBJECT "$header"; then
        generated=$build/moc_$(basename -- "${header%.h}").cpp
        /usr/lib/qt5/bin/moc -I"$qt" -I"$project_dir" -I"$program_dir" "$header" -o "$generated"
        sources+=("$generated")
    fi
done
if [[ $stage == page ]]; then
    for name in projects_resources projects_standalone_resources; do
        /usr/lib/qt5/bin/rcc -name "$name" "$project_dir/$name.qrc" -o "$build/qrc_$name.cpp"
        sources+=("$build/qrc_$name.cpp")
    done
fi
# 固定本次实际输入字节，构建后再核对，拒绝混入其它窗口的中途修改。
inputs=("${sources[@]}" "${headers[@]}" "$program_dir/data/account_store.h" "$project_dir/project_current_format.h")
if [[ $stage == page ]]; then
    inputs+=("$project_dir/frmsystemmaterial.ui" "$program_dir/reagents/reagent_types.h"
        "$project_dir/projects_resources.qrc" "$project_dir/projects_standalone_resources.qrc"
        "$project_dir/resources/ic_check_box_24px.svg" "$project_dir/resources/ic_check_box_outline_blank_24px.svg"
        "$program_dir/resources/arrow_drop_down.svg")
fi
sha256sum "${inputs[@]}" > "$build/source_sha256.txt"
pids=(); objects=()
wait_batch() { local failed=0; for pid in "${pids[@]}"; do wait "$pid" || failed=1; done; pids=(); return "$failed"; }
for source in "${sources[@]}"; do
    object=$build/obj_${#objects[@]}.o; objects+=("$object")
    aarch64-linux-gnu-g++ "${flags[@]}" -c "$source" -o "$object" & pids+=("$!")
    if [[ ${#pids[@]} -ge 2 ]]; then wait_batch; fi
done
wait_batch
sha256sum --quiet -c "$build/source_sha256.txt"
output=$1.new
trap 'rm -f -- "$output"' EXIT
aarch64-linux-gnu-g++ "${objects[@]}" -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
    -lQt5Widgets -lQt5Gui -lQt5Sql -lQt5Core -lpthread -o "$output"
readelf -h "$output" | grep -q 'Machine:.*AArch64'
chmod 755 "$output"; mv -f -- "$output" "$1"
file "$1"; sha256sum "$1"
