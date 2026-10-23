#!/usr/bin/env bash
# One absolute output path, isolated O05 ARM64 build, no device connections.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: protocol/build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Build as the ordinary WSL user.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
sysroot=$root/temp/sn100_host/arm64_sysroot
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Core.so.5.15.13 ]] || {
    echo 'Restore the existing Qt ARM64 inputs with prepare_arm64.sh first.' >&2; exit 1;
}
include=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
build_dir=$root/temp/sn100_host/protocol/arm64_build
mkdir -p "$build_dir" "$(dirname -- "$1")"
case ${RK_BUILD_TYPE:-Release} in
    Release) optimization=(-O2 -g -DQT_NO_DEBUG) ;;
    Debug) optimization=(-O0 -g3 -fno-omit-frame-pointer) ;;
    *) echo 'RK_BUILD_TYPE must be Debug or Release.' >&2; exit 2 ;;
esac
output=$(realpath -m -- "$1")
temporary=$build_dir/sn100_protocol_replay.new
trap 'rm -f -- "$temporary"' EXIT
aarch64-linux-gnu-g++ -std=c++14 "${optimization[@]}" -fPIC -Wall -Wextra \
    -I"$module_dir" -isystem "$include" -isystem "$include/QtCore" \
    "$module_dir/main.cpp" "$module_dir/frame_codec.cpp" "$module_dir/status_session.cpp" \
    -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
    -lQt5Core -lpthread -o "$temporary"
readelf -h "$temporary" | grep -q 'Machine:.*AArch64'
mv -f -- "$temporary" "$output"
file "$output"
sha256sum "$output"
