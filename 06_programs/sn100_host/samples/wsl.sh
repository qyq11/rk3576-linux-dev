#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
mode=${1:-build}
[[ $# == 0 ]] || shift
case $mode in
  build) project=samples.pro; output=wsl_release ;;
  test) project=tests/samples_test.pro; output=wsl_tests ;;
  run) project=samples.pro; output=wsl_release ;;
  *) echo 'Usage: samples/wsl.sh build|test|run [args]' >&2;exit 2 ;;
esac
directory=$root/temp/sn100_host/samples/$output
mkdir -p "$directory"
cd "$directory"
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
/usr/lib/qt5/bin/qmake "$module_dir/$project" -spec linux-g++ CONFIG+=release CONFIG-=debug
make -j2
if [[ $mode == test ]]; then exec env SN100_SAMPLE_TEST_ROOT="$root/temp/sn100_host/samples/test_runtime" QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen} ./samples_test "$@"; fi
if [[ $mode == run ]]; then exec ./sn100_samples "$@"; fi
file "$directory/sn100_samples"

