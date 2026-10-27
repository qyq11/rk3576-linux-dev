#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
mode=${1:-build}
[[ $# == 0 ]] || shift
case $mode in
    build) project=projects.pro; output=wsl_release ;;
    test) project=tests/projects_test.pro; output=wsl_tests ;;
    *) echo 'Usage: projects/wsl.sh build|test [QtTest args]' >&2; exit 2 ;;
esac
directory=$root/temp/sn100_host/projects/$output
mkdir -p "$directory"
cd "$directory"
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
/usr/lib/qt5/bin/qmake "$module_dir/$project" -spec linux-g++ CONFIG+=release CONFIG-=debug
make -j2
if [[ $mode == test ]]; then exec env QT_QPA_PLATFORM=offscreen ./projects_test "$@"; fi
file "$directory/sn100_projects"
