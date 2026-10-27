#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
work=$root/temp/sn100_host/projects_remaining_20261025/projects
evidence=$root/04_references/sn100_host/projects_remaining_20261025
mode=${1:-advanced}
mkdir -p "$work/receipts"
trap 'status=$?; printf "%s\n" "$status" > "$work/receipts/$mode.exit"' EXIT
case "$mode" in
    advanced) project=tests/project_advanced_test.pro; binary=project_advanced_test ;;
    baseline) project=tests/projects_test.pro; binary=projects_test ;;
    app) project=projects.pro; binary=sn100_projects ;;
    *) echo 'Use advanced|baseline|app' >&2; exit 2 ;;
esac
mkdir -p "$work/$mode" "$work/logs"
cd "$work/$mode"
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
/usr/lib/qt5/bin/qmake "$module_dir/$project" -spec linux-g++ CONFIG+=release CONFIG-=debug
make -j2
file "$binary"
if [[ $mode != app ]]; then
    env QT_QPA_PLATFORM=offscreen "./$binary" -o "$work/logs/$mode.xml,xml" -o -,txt
fi
