#!/usr/bin/env bash
# 仅交叉编译按钮后台驱动；生成后由主窗口上传RK执行。
# 不安装依赖、不访问网络、不在电脑运行产品。
set -Eeuo pipefail
[[ $# == 1 && $1 == /* && $EUID != 0 ]] || {
    echo 'Usage: button_driver_build.sh ABSOLUTE_OUTPUT_FILE (ordinary WSL user)' >&2; exit 2;
}
export PATH=/usr/local/bin:/usr/bin:/bin
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../../.." && pwd)
program=$root/06_programs/sn100_host
sysroot=${SN100_ARM64_SYSROOT:-$root/temp/sn100_host/rk_prepare/sysroot}
build=${SN100_BUTTON_DRIVER_BUILD_DIR:-$here/button_core_build}
[[ $sysroot == /* && $build == /* ]] || exit 2
qt=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
[[ -f $qt/QtTest/QtTest && -f $lib/libQt5Test.so ]] || {
    echo "ARM64_QTTEST_MISSING: require QtTest headers and resolved $lib/libQt5Test.so" >&2;
    echo 'No install/download attempted. A dangling .so link is insufficient.' >&2; exit 4;
}
readelf -h "$lib/libQt5Test.so" | grep -q 'Machine:.*AArch64' || {
    echo 'QtTest library must be AArch64 ELF.' >&2; exit 4;
}
mkdir -p "$build" "$(dirname -- "$1")"
exec 9>"$build/build.lock"
flock -n 9 || { echo 'This button driver build directory is in use.' >&2; exit 1; }
source=$program/tests/device_control_rk_test.cpp
sources=("$source" "$program/instrument/device_control.cpp" "$program/instrument/original_device_commands.cpp"
    "$program/protocol/tcp_transport.cpp" "$program/protocol/frame_codec.cpp"
    "$program/protocol/read_only_messages.cpp" "$program/protocol/status_session.cpp")
headers=("$program/instrument/device_control.h" "$program/instrument/original_device_commands.h"
    "$program/protocol/tcp_transport.h" "$program/protocol/frame_codec.h"
    "$program/protocol/read_only_messages.h" "$program/protocol/status_session.h")
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -fPIC -pthread -Wall -Wextra
    -DQT_TESTLIB_LIB -DQT_NETWORK_LIB -DQT_CORE_LIB -I"$program" -I"$build" -isystem "$qt")
for module in QtCore QtNetwork QtTest; do flags+=(-isystem "$qt/$module"); done
compiler=$(readlink -f -- "$(command -v aarch64-linux-gnu-g++)")
inputs=("${sources[@]}" "${headers[@]}" "$program/instrument/device_control.pri"
    "$program/tests/device_control_rk_test.pro" "$compiler" /usr/lib/qt5/bin/moc)
for candidate in "$lib/libQt5Core.so" "$lib/libQt5Network.so" "$lib/libQt5Test.so"; do
    [[ -f $candidate ]] || { echo "Required ARM64 library missing: $candidate" >&2; exit 4; }
    inputs+=("$(readlink -f -- "$candidate")")
done
# 固定实际使用的Qt头与业务输入，编译完成后逐字节复核，不采用文件时间判断。
while IFS= read -r -d '' candidate; do inputs+=("$candidate"); done < <(
    find "$qt/QtCore" "$qt/QtNetwork" "$qt/QtTest" -type f -print0 | sort -z
)
sha256sum "${inputs[@]}" > "$build/inputs.SHA256SUMS"
printf '%s\n' "${flags[@]}" > "$build/compile_flags.txt"
sha256sum "$build/compile_flags.txt" > "$build/compile_flags.SHA256SUMS"
moc_flags=(-DQT_NO_DEBUG -DQT_TESTLIB_LIB -DQT_NETWORK_LIB -DQT_CORE_LIB
    -I"$program" -I"$qt" -I"$qt/QtCore" -I"$qt/QtNetwork" -I"$qt/QtTest")
/usr/lib/qt5/bin/moc "${moc_flags[@]}" "$source" -o "$build/device_control_rk_test.moc"
for header in "${headers[@]}"; do
    if grep -q Q_OBJECT "$header"; then
        generated=$build/moc_$(basename -- "${header%.h}").cpp
        /usr/lib/qt5/bin/moc "${moc_flags[@]}" "$header" -o "$generated"
        sources+=("$generated")
    fi
done
sha256sum "${sources[@]}" "$build/device_control_rk_test.moc" > "$build/generated_sources.SHA256SUMS"
objects=()
: > "$build/object_manifest.tsv"
for source in "${sources[@]}"; do
    object=$build/obj_${#objects[@]}.o
    aarch64-linux-gnu-g++ "${flags[@]}" -c "$source" -o "$object"
    readelf -h "$object" | grep -q 'Machine:.*AArch64'
    objects+=("$object")
    printf '%s\t%s\t%s\t%s\n' "$source" "$(sha256sum "$source" | cut -d' ' -f1)" \
        "$object" "$(sha256sum "$object" | cut -d' ' -f1)" >> "$build/object_manifest.tsv"
done
sha256sum --quiet -c "$build/inputs.SHA256SUMS"
sha256sum --quiet -c "$build/generated_sources.SHA256SUMS"
sha256sum --quiet -c "$build/compile_flags.SHA256SUMS"
output=$1.new
trap 'rm -f -- "$output"' EXIT
aarch64-linux-gnu-g++ "${objects[@]}" -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
    -lQt5Test -lQt5Network -lQt5Core -lpthread -o "$output"
readelf -h "$output" | grep -q 'Machine:.*AArch64'
chmod 755 "$output"
mv -f -- "$output" "$1"
file "$1"
sha256sum "$1"
