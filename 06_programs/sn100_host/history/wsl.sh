#!/usr/bin/env bash
# Independent O04 build and data paths allow other modules to develop in parallel.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Build/run as the ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
work=$root/temp/sn100_host/history
qmake=/usr/lib/qt5/bin/qmake
mode=${1:-run}
[[ $# == 0 ]] || shift
[[ $(uname -m) == x86_64 && -x $qmake ]] || { echo 'Native WSL Qt5 is required.' >&2; exit 1; }
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
build_dir=$work/wsl_release
renderer_build_dir=$work/wsl_renderer_release
stage_dir=$work/wsl_report_stage
report_test_dir=$work/wsl_report_tests
pdf_text_test_dir=$work/wsl_pdf_text_tests
mkdir -p "$build_dir" "$renderer_build_dir" "$stage_dir" "$work/logs"

build_project()
{
    local project=$1 directory=$2 destination=$3
    mkdir -p "$directory" "$destination"
    (
        cd "$directory"
        "$qmake" "$project" -spec linux-g++ CONFIG+=release CONFIG-=debug "DESTDIR=$destination"
        make -j"${SN100_JOBS:-4}"
    )
}

build_renderer()
{
    build_project "$module_dir/renderer.pro" "$renderer_build_dir" "$stage_dir"
    file "$stage_dir/sn100_report_renderer"
}

build()
{
    build_renderer
    build_project "$module_dir/history.pro" "$build_dir" "$stage_dir"
    # Publish the matching pair only after both builds succeed.
    mv -f -- "$stage_dir/sn100_report_renderer" "$build_dir/sn100_report_renderer"
    mv -f -- "$stage_dir/sn100_history" "$build_dir/sn100_history"
    file "$build_dir/sn100_history" "$build_dir/sn100_report_renderer"
    sha256sum "$build_dir/sn100_history" "$build_dir/sn100_report_renderer"
}

build_report_test()
{
    build_renderer
    build_project "$module_dir/../tests/history_report_test.pro" "$report_test_dir" "$report_test_dir"
    mv -f -- "$stage_dir/sn100_report_renderer" "$report_test_dir/sn100_report_renderer"
    file "$report_test_dir/history_report_test" "$report_test_dir/sn100_report_renderer"
}

build_pdf_text_test()
{
    build_renderer
    build_project "$module_dir/../tests/history_pdf_text_test.pro" "$pdf_text_test_dir" "$pdf_text_test_dir"
    mv -f -- "$stage_dir/sn100_report_renderer" "$pdf_text_test_dir/sn100_report_renderer"
}
case $mode in
    build) build ;;
    run)
        build
        cd "$build_dir"
        exec bash "$module_dir/../wsl_ime.sh" env QT_QPA_PLATFORM=xcb "$build_dir/sn100_history" --offline --data-dir "$work/runtime" "$@"
        ;;
    test)
        build_project "$module_dir/../tests/history_test.pro" "$work/wsl_tests" "$work/wsl_tests"
        cd "$work/wsl_tests"
        exec env QT_QPA_PLATFORM=xcb ./history_test "$@"
        ;;
    report-test-build) build_report_test ;;
    report-test)
        build_report_test
        cd "$report_test_dir"
        exec env QT_QPA_PLATFORM=xcb SN100_REPORT_RENDERER="$report_test_dir/sn100_report_renderer" ./history_report_test "$@"
        ;;
    pdf-text-test-build) build_pdf_text_test ;;
    pdf-text-test)
        build_pdf_text_test
        cd "$pdf_text_test_dir"
        exec env SN100_REPORT_RENDERER="$pdf_text_test_dir/sn100_report_renderer" \
            SN100_PDF_TEXT_VALIDATOR="$module_dir/../tests/verify_history_pdf_text.py" ./history_pdf_text_test "$@"
        ;;
    ide) exec bash "$module_dir/../wsl_ime.sh" env QT_QPA_PLATFORM=xcb qtcreator "$module_dir/history.pro" ;;
    *) echo 'Usage: bash history/wsl.sh {build|run|test|report-test-build|report-test|pdf-text-test-build|pdf-text-test|ide} [arguments]' >&2; exit 2 ;;
esac
