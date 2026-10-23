#!/usr/bin/env bash
# Integrated ARM64 target. Generate separately from O03, publish after success.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: shell/build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Build as the ordinary WSL user.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs/sn100_host/shell
program_dir=$(cd -- "$module_dir/.." && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
sysroot=${SN100_ARM64_SYSROOT:-$root/temp/sn100_host/arm64_sysroot}
[[ $sysroot == /* ]] || { echo 'SN100_ARM64_SYSROOT must be absolute.' >&2; exit 2; }
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]] || bash "$program_dir/prepare_arm64.sh"
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Charts.so.5.15.13 ]] || {
    echo 'Report ARM64 inputs missing; use shell/rk.sh prepare to restore the isolated sysroot.' >&2; exit 1;
}
include=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
build_dir=${SN100_ARM64_BUILD_DIR:-$root/temp/sn100_host/integration/arm64_build}
[[ $build_dir == /* ]] || { echo 'SN100_ARM64_BUILD_DIR must be absolute.' >&2; exit 2; }
mkdir -p "$build_dir" "$(dirname -- "$1")"
exec 9>"$build_dir/build.lock"
flock -n 9 || { echo 'This ARM64 build directory is already in use.' >&2; exit 1; }
case ${RK_BUILD_TYPE:-Release} in
    Release) optimization=(-O2 -g -DQT_NO_DEBUG) ;;
    Debug) optimization=(-O0 -g3 -fno-omit-frame-pointer) ;;
    *) echo 'RK_BUILD_TYPE must be Debug or Release.' >&2; exit 2 ;;
esac
qt_tools=/usr/lib/qt5/bin
vendor=$program_dir/thirdparty/qtrpt
include_flags=(-I"$module_dir" -I"$program_dir" -I"$program_dir/ui" -I"$program_dir/history" -I"$build_dir" -I"$module_dir/widgets" -I"$module_dir/widgets/material")
include_flags+=(-I"$vendor/QtRPT" -I"$vendor/CommonFiles")
modules=(QtCore QtGui QtWidgets QtSql QtXml QtPrintSupport QtQml QtNetwork QtCharts)
for module in "${modules[@]}"; do include_flags+=(-I"$include/$module"); done
sources=("$module_dir/main.cpp" "$module_dir/shutdown_request.cpp" "$module_dir/frmmain.cpp" "$module_dir/frmuserset.cpp" "$module_dir/history_shell.cpp"
         "$program_dir/data/hospital_store.cpp" "$program_dir/ui/frmhospinfosetting.cpp" "$program_dir/ui/mnemonic.cpp"
         "$program_dir/data/account_store.cpp" "$program_dir/ui/frmaccountsetting.cpp"
         "$program_dir/ui/cusmessagebox.cpp" "$program_dir/ui/cusdialogmask.cpp"
         "$program_dir/history/history_store.cpp" "$program_dir/history/history_page.cpp" "$program_dir/history/result_statistics.cpp"
         "$module_dir"/widgets/*.cpp "$module_dir"/widgets/material/*.cpp "$module_dir"/widgets/material/lib/*.cpp)
sources+=("$program_dir/history/report_controller.cpp" "$program_dir/history/report_document.cpp"
         "$program_dir/history/report_render.cpp" "$vendor/QtRPT/"*.cpp "$vendor/CommonFiles/"*.cpp)
forms=("$module_dir/frmmain.ui" "$module_dir/frmuserset.ui" "$module_dir/widgets/cusflageimage.ui" "$program_dir/ui/frmhospinfosetting.ui")
forms+=("$program_dir/ui/frmaccountsetting.ui" "$program_dir/ui/cusmessagebox.ui" "$program_dir/ui/cusdialogmask.ui")
forms+=("$program_dir/history/frm_report_material.ui" "$program_dir/history/mask_dlg_query_report.ui"
        "$program_dir/history/mask_dlg_report_detail.ui" "$program_dir/history/mask_dlg_export.ui")
for form in "${forms[@]}"; do
    name=$(basename -- "${form%.ui}")
    "$qt_tools/uic" "$form" -o "$build_dir/ui_$name.h"
done
headers=("$module_dir/frmmain.h" "$module_dir/frmuserset.h" "$program_dir/data/hospital_store.h" "$program_dir/ui/frmhospinfosetting.h"
         "$program_dir/data/account_store.h" "$program_dir/ui/frmaccountsetting.h" "$program_dir/ui/cusmessagebox.h" "$program_dir/ui/cusdialogmask.h"
         "$program_dir/history/history_store.h" "$program_dir/history/history_page.h"
         "$module_dir"/widgets/*.h "$module_dir"/widgets/material/*.h "$module_dir"/widgets/material/lib/*.h)
headers+=("$program_dir/history/report_controller.h" "$vendor/QtRPT/"*.h "$vendor/CommonFiles/"*.h)
for header in "${headers[@]}"; do
    if grep -q 'Q_OBJECT' "$header"; then
        name=$(basename -- "${header%.h}")
        generated=$build_dir/moc_$name.cpp
        "$qt_tools/moc" -DNO_BARCODE -DHAVE_QT5 "${include_flags[@]}" "$header" -o "$generated"
        sources+=("$generated")
    fi
done
# shell/resources.qrc already owns the anonymous history fixture; do not link
# history/resources.qrc again and register the same resource path twice.
"$qt_tools/rcc" -name shell_resources "$module_dir/resources.qrc" -o "$build_dir/qrc_shell_resources.cpp"
sources+=("$build_dir/qrc_shell_resources.cpp")
"$qt_tools/rcc" -name account_resources "$module_dir/account_resources.qrc" -o "$build_dir/qrc_account_resources.cpp"
sources+=("$build_dir/qrc_account_resources.cpp")
"$qt_tools/rcc" -name report_resources "$program_dir/history/report_resources.qrc" -o "$build_dir/qrc_report_resources.cpp"
"$qt_tools/rcc" -name imagesRpt "$vendor/QtRPT/imagesRpt.qrc" -o "$build_dir/qrc_imagesRpt.cpp"
sources+=("$build_dir/qrc_report_resources.cpp" "$build_dir/qrc_imagesRpt.cpp")
output=$(realpath -m -- "$1")
renderer_output=$(dirname -- "$output")/sn100_report_renderer
[[ $output != "$renderer_output" ]] || { echo 'The main output must not use the reserved helper name.' >&2; exit 2; }
temporary=$output.new
renderer_temporary=$renderer_output.new
trap 'rm -f -- "$temporary" "$renderer_temporary"' EXIT
jobs=${SN100_JOBS:-4}
[[ $jobs =~ ^[0-9]+$ && $jobs -ge 1 && $jobs -le 16 ]] || { echo 'SN100_JOBS must be 1..16.' >&2; exit 2; }
compile_flags=(-std=c++14 "${optimization[@]}" -fPIC -Wall -Wextra -DNO_BARCODE -DHAVE_QT5 "${include_flags[@]}"
               -isystem "$include" -isystem "$include/QtCore" -isystem "$include/QtGui"
               -isystem "$include/QtWidgets" -isystem "$include/QtSql")
objects=()
build_pids=()
wait_batch()
{
    local failed=0
    for task_pid in "${build_pids[@]}"; do
        wait "$task_pid" || failed=1
    done
    build_pids=()
    return "$failed"
}
# Recompile every object, with bounded parallelism. Never share an object or
# generated file with the native, Windows, O01 or O02 builds.
python3 "$module_dir/arm64_object_reuse.py" capture "/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/o03_4/arm_resume/compiled_objects_03.json" "/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/o03_4/arm_resume/source_snapshot_03.json" "$program_dir" "$build_dir" aarch64-linux-gnu-g++ "${RK_BUILD_TYPE:-Release}" --flags "${compile_flags[@]}" --sources "${sources[@]}"
exit 0
for source in "${sources[@]}"; do
    object=$build_dir/obj_${#objects[@]}.o
    objects+=("$object")
    aarch64-linux-gnu-g++ "${compile_flags[@]}" -c "$source" -o "$object" &
    build_pids+=("$!")
    if [[ ${#build_pids[@]} -ge $jobs ]]; then wait_batch; fi
done
wait_batch
link_flags=(
    -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" -Wl,-rpath-link,"$lib" \
    -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
    -lQt5Charts -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -lpthread)
aarch64-linux-gnu-g++ "${objects[@]}" "${link_flags[@]}" -o "$temporary"
# Reuse only report/store objects in the helper: no shell or GUI-page objects.
renderer_objects=()
for index in "${!sources[@]}"; do
    source=${sources[index]}
    case $source in
        "$vendor/"*|*/history_store.cpp|*/report_document.cpp|*/report_render.cpp|*/moc_history_store.cpp|*/qrc_report_resources.cpp|*/qrc_imagesRpt.cpp)
            renderer_objects+=("${objects[index]}") ;;
    esac
done
# Vendor QObject names are not a stable whitelist; include every generated
# vendor moc directly from its header name, keeping UI mocs out of this helper.
for header in "$vendor/QtRPT/"*.h "$vendor/CommonFiles/"*.h; do
    if ! grep -q 'Q_OBJECT' "$header"; then continue; fi
    generated=$build_dir/moc_$(basename -- "${header%.h}").cpp
    for index in "${!sources[@]}"; do
        [[ ${sources[index]} != "$generated" ]] || {
            candidate=${objects[index]}
            if [[ " ${renderer_objects[*]} " != *" $candidate "* ]]; then renderer_objects+=("$candidate"); fi
        }
    done
done
for source in "$program_dir/history/report_renderer.cpp" "$program_dir/history/report_pdf_mapping.cpp"; do
    object=$build_dir/helper_$(basename -- "${source%.cpp}").o
    aarch64-linux-gnu-g++ "${compile_flags[@]}" -c "$source" -o "$object"
    renderer_objects+=("$object")
done
aarch64-linux-gnu-g++ "${renderer_objects[@]}" "${link_flags[@]}" -o "$renderer_temporary"
for candidate in "$temporary" "$renderer_temporary"; do readelf -h "$candidate" | grep -q 'Machine:.*AArch64'; done
chmod +x "$temporary" "$renderer_temporary"
mv -f -- "$renderer_temporary" "$renderer_output"
mv -f -- "$temporary" "$output"
file "$output" "$renderer_output"
sha256sum "$output" "$renderer_output"
