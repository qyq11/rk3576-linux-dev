#!/usr/bin/env bash
# O05-2 independent targets; no shared module build/run paths.
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
work=$root/temp/sn100_host/protocol/o05_2
mode=${1:-build}
[[ $# == 0 ]] || shift
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
case $mode in
    build|run) target=$module_dir/loopback.pro; build_dir=$work/wsl_loopback; executable=sn100_protocol_loopback ;;
    messages-test) target=$module_dir/../tests/read_only_messages_test.pro; build_dir=$work/messages_tests; executable=sn100_read_only_messages_test ;;
    tcp-test) target=$module_dir/../tests/tcp_transport_test.pro; build_dir=$work/tcp_tests; executable=sn100_tcp_transport_test ;;
    client-test) target=$module_dir/../tests/read_only_client_test.pro; build_dir=$work/client_tests; executable=sn100_read_only_client_test ;;
    client-build|client-demo) target=$module_dir/client.pro; build_dir=$work/wsl_client; executable=sn100_read_only_client_demo ;;
    *) echo 'Usage: wsl_transport.sh {build|run|messages-test|tcp-test|client-test|client-build|client-demo} [arguments]' >&2; exit 2 ;;
esac
mkdir -p "$build_dir"
cd "$build_dir"
/usr/lib/qt5/bin/qmake "$target" -spec linux-g++ CONFIG+=release CONFIG-=debug
make -j"${SN100_JOBS:-4}"
file "$build_dir/$executable"
if [[ $mode != build && $mode != client-build ]]; then exec "$build_dir/$executable" "$@"; fi
