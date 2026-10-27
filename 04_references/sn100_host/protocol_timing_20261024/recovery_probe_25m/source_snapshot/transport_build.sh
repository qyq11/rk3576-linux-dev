#!/usr/bin/env bash
# ARM64 O05-2 loopback target. Build only, never connect or deploy to a board.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: transport_build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
base=$root/temp/sn100_host/arm64_sysroot
network=$root/temp/sn100_host/protocol/o05_2/arm64_network_sysroot
[[ -f $base/usr/lib/aarch64-linux-gnu/libQt5Core.so.5.15.13 ]] || { echo 'Existing archived QtCore sysroot required.' >&2; exit 1; }
[[ -f $network/usr/lib/aarch64-linux-gnu/libQt5Network.so.5.15.13 ]] || bash "$module_dir/prepare_transport_arm64.sh"
include=$base/usr/include/aarch64-linux-gnu/qt5
lib=$base/usr/lib/aarch64-linux-gnu
netlib=$network/usr/lib/aarch64-linux-gnu
extra_sources=()
case ${SN100_PROTOCOL_TARGET:-loopback} in
    loopback) main_file=loopback_main.cpp; target=sn100_protocol_loopback; build_dir=$root/temp/sn100_host/protocol/o05_2/arm64_build ;;
    client) main_file=client_demo_main.cpp; target=sn100_read_only_client_demo; build_dir=$root/temp/sn100_host/protocol/o05_2/arm64_client_build ;;
    soak) main_file=soak_main.cpp; target=sn100_protocol_soak; build_dir=$root/temp/sn100_host/protocol/o05_3/arm64_soak_build ;;
    *) echo 'SN100_PROTOCOL_TARGET must be loopback, client or soak.' >&2; exit 2 ;;
esac
mkdir -p "$build_dir" "$(dirname -- "$1")"
case ${RK_BUILD_TYPE:-Release} in
    Release) optimization=(-O2 -g -DQT_NO_DEBUG) ;;
    Debug) optimization=(-O0 -g3 -fno-omit-frame-pointer) ;;
    *) echo 'RK_BUILD_TYPE must be Debug or Release.' >&2; exit 2 ;;
esac
/usr/lib/qt5/bin/moc "$module_dir/tcp_transport.h" -o "$build_dir/moc_tcp_transport.cpp"
if [[ ${SN100_PROTOCOL_TARGET:-loopback} != loopback ]]; then
    /usr/lib/qt5/bin/moc "$module_dir/read_only_client.h" -o "$build_dir/moc_read_only_client.cpp"
    extra_sources=("$module_dir/read_only_client.cpp" "$build_dir/moc_read_only_client.cpp")
fi
temporary=$build_dir/$target.new
trap 'rm -f -- "$temporary"' EXIT
aarch64-linux-gnu-g++ -std=c++14 "${optimization[@]}" -fPIC -Wall -Wextra \
    -I"$module_dir" -isystem "$include" -isystem "$include/QtCore" -isystem "$include/QtNetwork" \
    "$module_dir/$main_file" "$module_dir/tcp_transport.cpp" "$module_dir/read_only_messages.cpp" "${extra_sources[@]}" \
    "$module_dir/frame_codec.cpp" "$module_dir/status_session.cpp" "$build_dir/moc_tcp_transport.cpp" \
    -L"$netlib" -L"$lib" -L"$base/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$netlib" -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$base/lib/aarch64-linux-gnu" \
    -l:libQt5Network.so.5.15.13 -lQt5Core -lpthread -o "$temporary"
readelf -h "$temporary" | grep -q 'Machine:.*AArch64'
mv -f -- "$temporary" "$1"
file "$1"
sha256sum "$1"
