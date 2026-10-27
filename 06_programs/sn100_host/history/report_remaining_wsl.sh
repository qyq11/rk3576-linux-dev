#!/usr/bin/env bash
# Dedicated repeatable report-slice build/test/run. Never connects to hardware.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
module=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module/../../.." && pwd)
work=$root/temp/sn100_host/report_remaining_20261025
qmake=/usr/lib/qt5/bin/qmake
[[ $(uname -m) == x86_64 && -x $qmake ]] || exit 2
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
mkdir -p "$work/logs"
mode=${1:-core-test}
[[ $# == 0 ]] || shift
build_project()
{
    local project=$1 directory=$2
    mkdir -p "$directory"
    (
        cd "$directory"
        "$qmake" "$project" -spec linux-g++ CONFIG+=release CONFIG-=debug
        # At most two compiler jobs per window. Force mode resolves the known
        # Windows/WSL clock-skew warning without trusting timestamp freshness.
        if [[ ${SN100_REPORT_FORCE_REBUILD:-0} == 1 ]]; then make -B -j2; else make -j2; fi
    )
}
case "$mode" in
    core-test)
        build_project "$module/../tests/report_remaining_test.pro" "$work/core_build"
        run=$work/core_$(date -u +%Y%m%dT%H%M%S)_$$
        export SN100_REPORT_REMAINING_DIR=$run
        QT_QPA_PLATFORM=offscreen "$work/core_build/report_remaining_test" "$@"
        # Separate OS process proves persisted values survived shutdown.
        QT_QPA_PLATFORM=offscreen "$work/core_build/report_remaining_test" persistenceRead
        python3 "$module/../tests/report_remaining_verify_csv.py" "$run"
        ;;
    build)
        build_project "$module/renderer.pro" "$work/standalone_renderer"
        build_project "$module/history.pro" "$work/standalone"
        cp -- "$work/standalone_renderer/sn100_report_renderer" "$work/standalone/sn100_report_renderer"
        sha256sum "$work/standalone/sn100_history" "$work/standalone/sn100_report_renderer"
        ;;
    run)
        [[ -x $work/standalone/sn100_history && -x $work/standalone/sn100_report_renderer ]] || {
            echo 'Run report_remaining_wsl.sh build first.' >&2; exit 2;
        }
        exec env QT_QPA_PLATFORM=xcb "$work/standalone/sn100_history" --offline \
            --data-dir "$work/standalone_runtime" --simulate-role 4 "$@"
        ;;
    *) echo 'Usage: report_remaining_wsl.sh {core-test|build|run} [arguments]' >&2; exit 2 ;;
esac
