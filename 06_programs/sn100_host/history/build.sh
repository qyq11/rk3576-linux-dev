#!/usr/bin/env bash
# Build the ARM64 UI and its matching isolated PDF helper; never deploy stale files.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: history/build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Build as the ordinary WSL user.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
program_dir=$(cd -- "$module_dir/.." && pwd)
vendor=$program_dir/thirdparty/qtrpt
sysroot=$root/temp/sn100_host/arm64_sysroot
if [[ ! -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]]; then
    # The shared restorer has a download branch; this offline build only permits
    # its existing archived-input branch and otherwise stops before any network.
    [[ -f $root/07_backups/qt_environment/qt5_arm64/SHA256SUMS
       && -f $root/07_backups/qt_environment/sn100_arm64/SHA256SUMS ]] \
        || { echo 'Archived base/SQL ARM64 inputs are required for offline restoration.' >&2; exit 1; }
    bash "$program_dir/prepare_arm64.sh"
fi
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Qml.so.5.15.13 && -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Charts.so.5.15.13 ]] || bash "$module_dir/prepare_report_arm64.sh"
include=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
case ${RK_BUILD_TYPE:-Release} in
    Release) optimization=(-O2 -g -DQT_NO_DEBUG) ;;
    Debug) optimization=(-O0 -g3 -fno-omit-frame-pointer) ;;
    *) echo 'RK_BUILD_TYPE must be Debug or Release.' >&2; exit 2 ;;
esac
build_dir=$root/temp/sn100_host/history/arm64_build/${RK_BUILD_TYPE:-Release}
mkdir -p "$build_dir" "$(dirname -- "$1")"
output=$(realpath -m -- "$1")
renderer_output=$(dirname -- "$output")/sn100_report_renderer
[[ $output != "$renderer_output" ]] || { echo 'The main output must not use the reserved helper name.' >&2; exit 2; }
temporary=$(mktemp "$output.new.XXXXXX")
renderer_temporary=$(mktemp "$renderer_output.new.XXXXXX")
trap 'rm -f -- "$temporary" "$renderer_temporary"' EXIT
qt_tools=/usr/lib/qt5/bin
modules=(QtCore QtGui QtWidgets QtSql QtXml QtPrintSupport QtQml QtNetwork QtCharts)
compile_flags=(-std=c++14 "${optimization[@]}" -fPIC -pthread -Wall -Wextra -DNO_BARCODE -DHAVE_QT5
    -I"$module_dir" -I"$build_dir" -I"$vendor/QtRPT" -I"$vendor/CommonFiles" -isystem "$include")
moc_flags=(-DNO_BARCODE -DHAVE_QT5 -I"$module_dir" -I"$vendor/QtRPT" -I"$vendor/CommonFiles" -I"$include")
for module in "${modules[@]}"; do
    compile_flags+=(-isystem "$include/$module")
    moc_flags+=(-I"$include/$module")
done
for form in frm_report_material mask_dlg_query_report mask_dlg_report_detail mask_dlg_export; do
    "$qt_tools/uic" "$module_dir/$form.ui" -o "$build_dir/ui_$form.h"
done
vendor_sources=("$vendor/QtRPT/"*.cpp "$vendor/CommonFiles/"*.cpp)
common_sources=("$module_dir/history_store.cpp" "$module_dir/report_document.cpp" "$module_dir/report_render.cpp" "${vendor_sources[@]}")
main_sources=("$module_dir/result_statistics.cpp" "$module_dir/history_page.cpp" "$module_dir/report_controller.cpp" "$module_dir/main.cpp")
renderer_sources=("$module_dir/report_renderer.cpp" "$module_dir/report_pdf_mapping.cpp")
for header in "$module_dir/"*.h "$vendor/QtRPT/"*.h "$vendor/CommonFiles/"*.h; do
    if ! grep -q 'Q_OBJECT' "$header"; then continue; fi
    name=$(basename -- "${header%.h}")
    generated=$build_dir/moc_$name.cpp
    "$qt_tools/moc" "${moc_flags[@]}" "$header" -o "$generated"
    case $name in
        history_page|report_controller) main_sources+=("$generated") ;;
        *) common_sources+=("$generated") ;;
    esac
done
"$qt_tools/rcc" -name report_resources "$module_dir/report_resources.qrc" -o "$build_dir/qrc_report_resources.cpp"
"$qt_tools/rcc" -name imagesRpt "$vendor/QtRPT/imagesRpt.qrc" -o "$build_dir/qrc_imagesRpt.cpp"
"$qt_tools/rcc" -name history_resources "$module_dir/resources.qrc" -o "$build_dir/qrc_history_resources.cpp"
common_sources+=("$build_dir/qrc_report_resources.cpp" "$build_dir/qrc_imagesRpt.cpp")
main_sources+=("$build_dir/qrc_history_resources.cpp")
all_sources=("${common_sources[@]}" "${main_sources[@]}" "${renderer_sources[@]}")
all_objects=()
for source in "${all_sources[@]}"; do
    [[ -f $source ]] || { echo "Required source missing: $source" >&2; exit 1; }
    all_objects+=("$build_dir/$(basename -- "${source%.cpp}").o")
done
makefile=$build_dir/Makefile.report
{
    printf 'SHELL := /bin/bash\n.PHONY: all\nall:'
    printf ' %q' "${all_objects[@]}"
    printf '\n'
    for index in "${!all_sources[@]}"; do
        printf '%q: %q\n\t' "${all_objects[index]}" "${all_sources[index]}"
        printf '%q ' aarch64-linux-gnu-g++ "${compile_flags[@]}" -MMD -MP -c "${all_sources[index]}" -o "${all_objects[index]}"
        printf '\n'
    done
    printf -- '-include'
    for object in "${all_objects[@]}"; do printf ' %q' "${object%.o}.d"; done
    printf '\n'
} > "$makefile"
make -f "$makefile" -j"${SN100_JOBS:-4}"
common_objects=()
main_objects=()
renderer_objects=()
for source in "${common_sources[@]}"; do common_objects+=("$build_dir/$(basename -- "${source%.cpp}").o"); done
for source in "${main_sources[@]}"; do main_objects+=("$build_dir/$(basename -- "${source%.cpp}").o"); done
for source in "${renderer_sources[@]}"; do renderer_objects+=("$build_dir/$(basename -- "${source%.cpp}").o"); done
link_flags=(-L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu"
    -lQt5Charts -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -pthread)
aarch64-linux-gnu-g++ "${renderer_objects[@]}" "${common_objects[@]}" "${link_flags[@]}" -o "$renderer_temporary"
aarch64-linux-gnu-g++ "${main_objects[@]}" "${common_objects[@]}" "${link_flags[@]}" -o "$temporary"
for candidate in "$temporary" "$renderer_temporary"; do
    readelf -h "$candidate" | grep -q 'Machine:.*AArch64'
done
chmod +x "$temporary" "$renderer_temporary"
# Both candidates are valid before either published executable is replaced.
mv -f -- "$renderer_temporary" "$renderer_output"
mv -f -- "$temporary" "$output"
file "$output" "$renderer_output"
sha256sum "$output" "$renderer_output"
