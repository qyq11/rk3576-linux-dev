#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
mode=${1:-build}
[[ $# == 0 ]] || shift
case $mode in
    build) project=calibration.pro; output=wsl_release ;;
    store-test) project=tests/calibration_store_test.pro; output=wsl_store_tests ;;
    page-test) project=tests/calibration_page_test.pro; output=wsl_page_tests ;;
    *) echo 'Usage: calibration/wsl.sh build|store-test|page-test [QtTest args]' >&2; exit 2 ;;
esac
directory=$root/temp/sn100_host/calibration_local_20261025/$output
mkdir -p "$directory"
cd "$directory"
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
/usr/lib/qt5/bin/qmake "$module_dir/$project" -spec linux-g++ CONFIG+=release CONFIG-=debug
make -j2
if [[ $mode == store-test ]]; then exec env QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen} ./calibration_store_test "$@"; fi
if [[ $mode == page-test ]]; then exec env QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen} ./calibration_page_test "$@"; fi
file "$directory/sn100_calibration"
