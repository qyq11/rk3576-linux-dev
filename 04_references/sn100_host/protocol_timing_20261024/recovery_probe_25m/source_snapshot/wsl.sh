#!/usr/bin/env bash
# O05 has its own targets and data; never invokes another module's default build.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Build/run as the ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
work=$root/temp/sn100_host/protocol
mode=${1:-build}
[[ $# == 0 ]] || shift
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
qmake=/usr/lib/qt5/bin/qmake
[[ $(uname -m) == x86_64 && -x $qmake ]] || { echo 'WSL native Qt5 required.' >&2; exit 1; }
case $mode in
    build|run)
        mkdir -p "$work/wsl_release"
        (cd "$work/wsl_release" && "$qmake" "$module_dir/protocol.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug && make -j"${SN100_JOBS:-4}")
        file "$work/wsl_release/sn100_protocol_replay"
        if [[ $mode == run ]]; then exec "$work/wsl_release/sn100_protocol_replay" "$@"; fi
        ;;
    test)
        mkdir -p "$work/wsl_tests"
        cd "$work/wsl_tests"
        "$qmake" "$module_dir/../tests/protocol_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug
        make -j"${SN100_JOBS:-4}"
        exec ./sn100_protocol_test "$@"
        ;;
    *) echo 'Usage: bash protocol/wsl.sh {build|run|test} [arguments]' >&2; exit 2 ;;
esac
