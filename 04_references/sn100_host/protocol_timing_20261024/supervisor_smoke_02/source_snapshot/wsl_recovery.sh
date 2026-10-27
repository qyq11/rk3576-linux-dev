#!/usr/bin/env bash
# O05-3 paths never reuse O05-2 build trees or another module's runtime.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
mode=${1:-build}
[[ $# == 0 ]] || shift
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
case $mode in
    build|run) target=$module_dir/soak.pro; leaf=wsl_soak; executable=sn100_protocol_soak ;;
    fault-test) target=$module_dir/../tests/tcp_fault_test.pro; leaf=fault_tests; executable=sn100_tcp_fault_test ;;
    silence-test) target=$module_dir/../tests/client_silence_test.pro; leaf=silence_tests; executable=sn100_client_silence_test ;;
    *) echo 'Usage: wsl_recovery.sh {build|run|fault-test|silence-test} [arguments]' >&2; exit 2 ;;
esac
build_dir=$root/temp/sn100_host/protocol/o05_3/$leaf
mkdir -p "$build_dir"
cd "$build_dir"
/usr/lib/qt5/bin/qmake "$target" -spec linux-g++ CONFIG+=release CONFIG-=debug
# DrvFs写入时间偶有毫秒级领先；不改系统时钟，等时间戳落后后再让make比较。
sleep 0.15
make -j"${SN100_JOBS:-4}"
file "$build_dir/$executable"
if [[ $mode != build ]]; then exec "$build_dir/$executable" "$@"; fi
