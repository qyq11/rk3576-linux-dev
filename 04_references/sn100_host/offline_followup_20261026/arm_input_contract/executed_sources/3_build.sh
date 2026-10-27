#!/usr/bin/env bash
# Integrated ARM64 target. Generate separately from O03, publish after success.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: shell/build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Build as the ordinary WSL user.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
program_dir=$(cd -- "$module_dir/.." && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
# Read-only input inventory for the freeze/contract test. This mode must not
# restore a sysroot, generate files, compile, or publish an executable.
list_inputs=${SN100_ARM64_LIST_INPUTS:-0}
[[ $list_inputs == 0 || $list_inputs == 1 ]] || { echo 'SN100_ARM64_LIST_INPUTS must be 0 or 1.' >&2; exit 2; }
sysroot=${SN100_ARM64_SYSROOT:-$root/temp/sn100_host/arm64_sysroot}
[[ $sysroot == /* ]] || { echo 'SN100_ARM64_SYSROOT must be absolute.' >&2; exit 2; }
if [[ $list_inputs == 0 ]]; then
    [[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]] || bash "$program_dir/prepare_arm64.sh"
    [[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Charts.so.5.15.13 ]] || {
        echo 'Report ARM64 inputs missing; use shell/rk.sh prepare to restore the isolated sysroot.' >&2; exit 1;
    }
    [[ -f $sysroot/usr/include/aarch64-linux-gnu/qt5/QtSvg/QSvgRenderer && -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Svg.so ]] || {
        echo 'Sample UI ARM64 SVG inputs missing; restore matching archived Qt5.15.13 SVG headers/runtime before building.' >&2; exit 1;
    }
fi
include=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
build_dir=${SN100_ARM64_BUILD_DIR:-$root/temp/sn100_host/integration/arm64_build}
[[ $build_dir == /* ]] || { echo 'SN100_ARM64_BUILD_DIR must be absolute.' >&2; exit 2; }
if [[ $list_inputs == 0 ]]; then
    mkdir -p "$build_dir" "$(dirname -- "$1")"
    exec 9>"$build_dir/build.lock"
    flock -n 9 || { echo 'This ARM64 build directory is already in use.' >&2; exit 1; }
fi
case ${RK_BUILD_TYPE:-Release} in
    Release) optimization=(-O2 -g -DQT_NO_DEBUG) ;;
    Debug) optimization=(-O0 -g3 -fno-omit-frame-pointer) ;;
    *) echo 'RK_BUILD_TYPE must be Debug or Release.' >&2; exit 2 ;;
esac
qt_tools=/usr/lib/qt5/bin
vendor=$program_dir/thirdparty/qtrpt
include_flags=(-I"$module_dir" -I"$program_dir" -I"$program_dir/ui" -I"$program_dir/history" -I"$build_dir" -I"$module_dir/widgets" -I"$module_dir/widgets/material")
include_flags+=(-I"$vendor/QtRPT" -I"$vendor/CommonFiles")
include_flags+=(-I"$program_dir/projects" -I"$program_dir/reagents" -I"$program_dir/samples" -I"$program_dir/samples/material")
include_flags+=(-I"$program_dir/instrument")
include_flags+=(-I"$program_dir/quality_control")
include_flags+=(-I"$program_dir/calibration")
modules=(QtCore QtGui QtWidgets QtSql QtXml QtPrintSupport QtQml QtNetwork QtCharts QtSvg)
for module in "${modules[@]}"; do include_flags+=(-I"$include/$module"); done
sources=("$module_dir/main.cpp" "$module_dir/shutdown_request.cpp" "$module_dir/frmmain.cpp" "$module_dir/frmuserset.cpp" "$module_dir/history_shell.cpp"
         "$program_dir/data/hospital_store.cpp" "$program_dir/ui/frmhospinfosetting.cpp" "$program_dir/ui/mnemonic.cpp"
         "$program_dir/data/account_store.cpp" "$program_dir/ui/frmaccountsetting.cpp"
         "$program_dir/ui/cusmessagebox.cpp" "$program_dir/ui/cusdialogmask.cpp"
         "$program_dir/history/history_store.cpp" "$program_dir/history/history_page.cpp" "$program_dir/history/result_statistics.cpp"
         "$module_dir"/widgets/*.cpp "$module_dir"/widgets/material/*.cpp "$module_dir"/widgets/material/lib/*.cpp)
sources+=("$program_dir/history/report_controller.cpp" "$program_dir/history/report_document.cpp"
         "$program_dir/history/report_render.cpp" "$vendor/QtRPT/"*.cpp "$vendor/CommonFiles/"*.cpp)
# Match projects_core.pri and reagents.pri explicitly. Their standalone main.cpp,
# tests and alternate widget bundles do not belong to the unified executable.
sources+=("$module_dir/catalog_shell.cpp"
         "$program_dir/projects/projects_store.cpp" "$program_dir/projects/project_page.cpp" "$program_dir/projects/cuscheckbox.cpp"
         "$program_dir/projects/project_advanced.cpp"
         "$program_dir/reagents/reagent_store.cpp" "$program_dir/reagents/reagents_page.cpp"
         "$program_dir/reagents/wgtreagentpanel.cpp" "$program_dir/reagents/cusreagenthole.cpp")
# samples_core.pri contains the local preset/task page and only the additional
# Material controls it needs; the shell already owns the common Material base.
sources+=("$program_dir/samples/sample_store.cpp" "$program_dir/samples/sample_page.cpp"
         "$program_dir/samples/sample_task_reset.cpp"
         "$program_dir/samples/sample_widgets.cpp" "$program_dir/samples/cusracklistwidget.cpp"
         "$program_dir/samples/cuscheckheader.cpp" "$program_dir/samples/cuscombox_base.cpp"
         "$program_dir/samples/material/qtmaterialradiobutton.cpp"
         "$program_dir/samples/material/qtmaterialscrollbar.cpp" "$program_dir/samples/material/qtmaterialscrollbar_internal.cpp"
         "$program_dir/samples/material/lib/qtmaterialcheckable.cpp" "$program_dir/samples/material/lib/qtmaterialcheckable_internal.cpp")
# 文件回放及受限自建回环只读闭包；独立长测入口不进入统一程序。
sources+=("$module_dir/device_status_shell.cpp" "$program_dir/protocol/status_replay.cpp"
         "$program_dir/protocol/status_session.cpp" "$program_dir/protocol/frame_codec.cpp")
sources+=("$program_dir/protocol/status_loopback.cpp" "$program_dir/protocol/read_only_client.cpp"
          "$program_dir/protocol/tcp_transport.cpp" "$program_dir/protocol/read_only_messages.cpp")
sources+=("$module_dir/instrument_shell.cpp" "$program_dir/instrument/instrument_page.cpp"
         "$program_dir/instrument/maintenance_store.cpp")
sources+=("$module_dir/login_shell.cpp" "$program_dir/ui/maskdlglogin.cpp")
sources+=("$module_dir/printer_shell.cpp" "$program_dir/ui/frmprintersetting.cpp" "$program_dir/data/printer_settings_store.cpp")
sources+=("$module_dir/lis_shell.cpp" "$program_dir/ui/frmlissetting.cpp" "$program_dir/data/lis_settings_store.cpp" "$program_dir/data/serial_port_catalog.cpp")
sources+=("$program_dir/history/report_remaining_store.cpp" "$program_dir/history/report_remaining_display_dialog.cpp")
sources+=("$module_dir/quality_shell.cpp" "$program_dir/quality_control/qc_store.cpp"
          "$program_dir/quality_control/qc_history_core.cpp" "$program_dir/quality/statistics.cpp"
          "$program_dir/quality_control/quality_control_page.cpp"
          "$program_dir/quality_control/cus_quality_chart.cpp" "$program_dir/quality_control/qualitychart.cpp"
          "$program_dir/quality_control/qc_history_presenter.cpp")
sources+=("$module_dir/calibration_shell.cpp" "$program_dir/calibration/calibration_store.cpp"
          "$program_dir/calibration/calibration_page.cpp" "$program_dir/calibration/calibrationchart.cpp")
forms=("$module_dir/frmmain.ui" "$module_dir/frmuserset.ui" "$module_dir/widgets/cusflageimage.ui" "$program_dir/ui/frmhospinfosetting.ui")
forms+=("$program_dir/ui/frmaccountsetting.ui" "$program_dir/ui/cusmessagebox.ui" "$program_dir/ui/cusdialogmask.ui")
forms+=("$program_dir/history/frm_report_material.ui" "$program_dir/history/mask_dlg_query_report.ui"
        "$program_dir/history/mask_dlg_report_detail.ui" "$program_dir/history/mask_dlg_export.ui")
forms+=("$program_dir/projects/frmsystemmaterial.ui"
        "$program_dir/reagents/frmreagentmaterial.ui" "$program_dir/reagents/wgtreagentpanel.ui"
        "$program_dir/reagents/maskdlgreagentlistsetting.ui" "$program_dir/reagents/maskdlgreagentsetting.ui"
        "$program_dir/reagents/maskdlggroupsetting.ui" "$program_dir/reagents/maskdlgreagentcode.ui"
        "$program_dir/reagents/addbatchgroupdialog.ui")
forms+=("$program_dir/samples/frmsamplematerial.ui" "$program_dir/samples/frmsamplesettingmaterial.ui"
        "$program_dir/samples/cussamplerack.ui" "$program_dir/samples/maskdlgsampledetail.ui"
        "$program_dir/samples/maskdlgrepeatsetting.ui" "$program_dir/samples/maskdlgcombasetting.ui"
        "$program_dir/samples/maskdlgracknosetting.ui")
headers=("$module_dir/frmmain.h" "$module_dir/frmuserset.h" "$program_dir/data/hospital_store.h" "$program_dir/ui/frmhospinfosetting.h"
         "$program_dir/data/account_store.h" "$program_dir/ui/frmaccountsetting.h" "$program_dir/ui/cusmessagebox.h" "$program_dir/ui/cusdialogmask.h"
         "$program_dir/history/history_store.h" "$program_dir/history/history_page.h"
         "$module_dir"/widgets/*.h "$module_dir"/widgets/material/*.h "$module_dir"/widgets/material/lib/*.h)
headers+=("$program_dir/history/report_controller.h" "$vendor/QtRPT/"*.h "$vendor/CommonFiles/"*.h)
headers+=("$program_dir/projects/projects_store.h" "$program_dir/projects/project_page.h" "$program_dir/projects/cuscheckbox.h"
          "$program_dir/projects/project_advanced.h"
          "$program_dir/reagents/reagent_types.h" "$program_dir/reagents/reagent_store.h" "$program_dir/reagents/reagents_page.h"
          "$program_dir/reagents/wgtreagentpanel.h" "$program_dir/reagents/cusreagenthole.h")
headers+=("$program_dir/samples/sample_types.h" "$program_dir/samples/sample_store.h" "$program_dir/samples/sample_page.h"
          "$program_dir/samples/sample_task_reset.h"
          "$program_dir/samples/sample_widgets.h" "$program_dir/samples/cusracklistwidget.h"
          "$program_dir/samples/cuscheckheader.h" "$program_dir/samples/cuscombox_base.h"
          "$program_dir/samples/material/qtmaterialradiobutton.h" "$program_dir/samples/material/qtmaterialradiobutton_p.h"
          "$program_dir/samples/material/qtmaterialscrollbar.h" "$program_dir/samples/material/qtmaterialscrollbar_p.h"
          "$program_dir/samples/material/qtmaterialscrollbar_internal.h"
          "$program_dir/samples/material/lib/qtmaterialcheckable.h" "$program_dir/samples/material/lib/qtmaterialcheckable_p.h"
          "$program_dir/samples/material/lib/qtmaterialcheckable_internal.h")
headers+=("$program_dir/protocol/status_replay.h" "$program_dir/protocol/status_session.h" "$program_dir/protocol/frame_codec.h")
headers+=("$program_dir/protocol/status_loopback.h" "$program_dir/protocol/read_only_client.h"
          "$program_dir/protocol/tcp_transport.h" "$program_dir/protocol/read_only_messages.h")
forms+=("$program_dir/instrument/frmdriverset.ui")
forms+=("$program_dir/ui/maskdlglogin.ui")
forms+=("$program_dir/ui/frmprintersetting.ui" "$program_dir/ui/frmlissetting.ui")
forms+=("$program_dir/history/mask_dlg_display_setting.ui")
forms+=("$program_dir/calibration/frmcalibrationmaterial.ui")
forms+=("$program_dir/quality_control/frm_quality.ui" "$program_dir/quality_control/mask_dlg_add_control.ui"
         "$program_dir/quality_control/mask_dlg_control_batch_setting.ui"
         "$program_dir/quality_control/quality_control_add_wgt.ui"
         "$program_dir/quality_control/quality_control_item_add_project_wgt.ui"
         "$program_dir/quality_control/cus_quality_chart.ui")
headers+=("$program_dir/instrument/instrument_page.h" "$program_dir/instrument/maintenance_store.h")
headers+=("$program_dir/ui/maskdlglogin.h")
headers+=("$program_dir/ui/frmprintersetting.h" "$program_dir/data/printer_settings_store.h"
           "$program_dir/ui/frmlissetting.h" "$program_dir/data/lis_settings_store.h" "$program_dir/data/serial_port_catalog.h")
headers+=("$program_dir/quality_control/qc_store.h" "$program_dir/quality_control/quality_control_page.h"
          "$program_dir/quality_control/qc_history_core.h" "$program_dir/quality_control/qc_history_reply.h"
          "$program_dir/quality_control/cus_quality_chart.h" "$program_dir/quality_control/qualitychart.h"
          "$program_dir/quality_control/qc_history_presenter.h" "$program_dir/quality/statistics.h")
headers+=("$program_dir/history/report_remaining_view.h" "$program_dir/history/report_remaining_display_dialog.h"
           "$program_dir/history/report_remaining_html.h")
headers+=("$program_dir/calibration/calibration_types.h" "$program_dir/calibration/calibration_store.h"
           "$program_dir/calibration/calibration_page.h" "$program_dir/calibration/calibrationchart.h")
# One stable name for each QRC prevents basename collisions with other modules.
resource_names=(shell_resources account_resources report_resources imagesRpt projects_resources reagents_resources samples_resources instrument_resources qc_history_resources)
resource_files=("$module_dir/resources.qrc" "$module_dir/account_resources.qrc"
                "$program_dir/history/report_resources.qrc" "$vendor/QtRPT/imagesRpt.qrc"
                "$program_dir/projects/projects_resources.qrc" "$program_dir/reagents/reagents_resources.qrc"
                "$program_dir/samples/samples_resources.qrc")
resource_files+=("$program_dir/instrument/instrument_resources.qrc")
resource_files+=("$program_dir/quality_control/qc_history_resources.qrc")
helper_sources=("$program_dir/history/report_renderer.cpp" "$program_dir/history/report_pdf_mapping.cpp")
is_report_helper_source()
{
    # Shared report engine/store only; catalog pages and stores stay in main.
    case $1 in
        "$vendor/"*|*/history_store.cpp|*/report_remaining_store.cpp|*/report_document.cpp|*/report_render.cpp|*/moc_history_store.cpp|*/qrc_report_resources.cpp|*/qrc_imagesRpt.cpp)
            return 0 ;;
    esac
    return 1
}
for input in "${sources[@]}" "${forms[@]}" "${headers[@]}" "${resource_files[@]}" "${helper_sources[@]}"; do
    [[ -f $input ]] || { echo "Required ARM64 build input missing: $input" >&2; exit 1; }
done
if [[ $list_inputs == 1 ]]; then
    printf 'source\t%s\n' "${sources[@]}"
    printf 'form\t%s\n' "${forms[@]}"
    printf 'header\t%s\n' "${headers[@]}"
    printf 'resource\t%s\n' "${resource_files[@]}"
    printf 'include\t%s\n' "${include_flags[@]}"
    for source in "${sources[@]}"; do
        if is_report_helper_source "$source"; then printf 'helper_source\t%s\n' "$source"; fi
    done
    printf 'helper_source\t%s\n' "${helper_sources[@]}"
    exit 0
fi
for form in "${forms[@]}"; do
    name=$(basename -- "${form%.ui}")
    "$qt_tools/uic" "$form" -o "$build_dir/ui_$name.h"
done
for header in "${headers[@]}"; do
    if grep -q 'Q_OBJECT' "$header"; then
        name=$(basename -- "${header%.h}")
        generated=$build_dir/moc_$name.cpp
        "$qt_tools/moc" -DNO_BARCODE -DHAVE_QT5 -I"$include" "${include_flags[@]}" "$header" -o "$generated"
        sources+=("$generated")
    fi
done
# shell/resources.qrc already owns the anonymous history fixture; do not link
# history/resources.qrc again and register the same resource path twice.
for index in "${!resource_files[@]}"; do
    name=${resource_names[index]}
    "$qt_tools/rcc" -name "$name" "${resource_files[index]}" -o "$build_dir/qrc_$name.cpp"
    sources+=("$build_dir/qrc_$name.cpp")
done
output=$(realpath -m -- "$1")
renderer_output=$(dirname -- "$output")/sn100_report_renderer
[[ $output != "$renderer_output" ]] || { echo 'The main output must not use the reserved helper name.' >&2; exit 2; }
temporary=$output.new
renderer_temporary=$renderer_output.new
trap 'rm -f -- "$temporary" "$renderer_temporary"' EXIT
jobs=${SN100_JOBS:-2}
[[ $jobs =~ ^[0-9]+$ && $jobs -ge 1 && $jobs -le 2 ]] || { echo 'SN100_JOBS must be 1 or 2 for the current WSL build budget.' >&2; exit 2; }
compile_flags=(-std=c++14 "${optimization[@]}" -fPIC -Wall -Wextra -DNO_BARCODE -DHAVE_QT5 "${include_flags[@]}"
               -isystem "$include" -isystem "$include/QtCore" -isystem "$include/QtGui"
               -isystem "$include/QtWidgets" -isystem "$include/QtSql")
objects=()
build_pids=()
declare -A reusable=()
if [[ -n ${SN100_ARM64_OBJECT_REUSE_MANIFEST:-} ]]; then
    [[ $SN100_ARM64_OBJECT_REUSE_MANIFEST == /* && -n ${SN100_ARM64_FROZEN_SNAPSHOT:-} ]] || {
        echo 'Explicit object reuse requires absolute manifest and frozen snapshot paths.' >&2; exit 2;
    }
    python3 "$module_dir/arm64_object_reuse.py" reusable "$SN100_ARM64_OBJECT_REUSE_MANIFEST" \
        "$SN100_ARM64_FROZEN_SNAPSHOT" "$program_dir" "$build_dir" aarch64-linux-gnu-g++ "${RK_BUILD_TYPE:-Release}" \
        --flags "${compile_flags[@]}" --sources "${sources[@]}" > "$build_dir/reusable_indices.txt"
    while read -r index; do reusable[$index]=1; done < "$build_dir/reusable_indices.txt"
    printf 'VERIFIED_OBJECT_REUSE=%s\n' "${#reusable[@]}"
fi
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
for source in "${sources[@]}"; do
    index=${#objects[@]}
    object=$build_dir/obj_$index.o
    objects+=("$object")
    if [[ ${reusable[$index]+exists} ]]; then continue; fi
    aarch64-linux-gnu-g++ "${compile_flags[@]}" -c "$source" -o "$object" &
    build_pids+=("$!")
    if [[ ${#build_pids[@]} -ge $jobs ]]; then wait_batch; fi
done
wait_batch
link_flags=(
    -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" -Wl,-rpath-link,"$lib" \
    -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
    -lQt5Charts -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -lpthread)
# QSvgRenderer belongs to the sample combo control, so only main gains QtSvg.
# Keep the report helper's link libraries and source closure unchanged.
aarch64-linux-gnu-g++ "${objects[@]}" -L"$lib" -lQt5Svg "${link_flags[@]}" -o "$temporary"
# Reuse only report/store objects in the helper: no shell or GUI-page objects.
renderer_objects=()
for index in "${!sources[@]}"; do
    source=${sources[index]}
    if is_report_helper_source "$source"; then renderer_objects+=("${objects[index]}"); fi
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
for source in "${helper_sources[@]}"; do
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
