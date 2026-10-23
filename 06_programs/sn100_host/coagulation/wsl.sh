#!/usr/bin/env bash
# O06-2 independent native QtCore target. No GUI, database or instruments.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Run as ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
work=$root/temp/sn100_host/coagulation
mode=${1:-build}
[[ $# == 0 ]] || shift
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
qmake=/usr/lib/qt5/bin/qmake
[[ $(uname -m) == x86_64 && -x $qmake ]] || { echo 'WSL native Qt5 required.' >&2; exit 1; }
case $mode in
    build|run)
        mkdir -p "$work/wsl_release"
        (cd "$work/wsl_release" && "$qmake" "$module_dir/coagulation.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug && make -j"${SN100_JOBS:-4}")
        if [[ $mode == run ]]; then exec "$work/wsl_release/sn100_clot_endpoint" "$@"; fi
        file "$work/wsl_release/sn100_clot_endpoint"
        ;;
    test)
        mkdir -p "$work/wsl_tests"
        cd "$work/wsl_tests"
        "$qmake" "$module_dir/tests/endpoint_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
        make -j"${SN100_JOBS:-4}"
        exec ./sn100_clot_endpoint_test "$@"
        ;;
    *) echo 'Usage: bash coagulation/wsl.sh {build|run|test} [arguments]' >&2; exit 2 ;;
esac
