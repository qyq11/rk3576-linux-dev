#!/usr/bin/env bash
# 仅在WSL交叉编译；检查程序上传RK运行，不执行电脑版本。
set -Eeuo pipefail
[[ $# == 1 && $1 == /* && $EUID != 0 ]] || { echo 'Usage: build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../.." && pwd)
sysroot=${SN100_ARM64_SYSROOT:-$root/temp/sn100_host/rk_prepare/sysroot}
include=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
mkdir -p "$(dirname -- "$1")"
output=$1.new
trap 'rm -f -- "$output"' EXIT
aarch64-linux-gnu-g++ -std=c++14 -O2 -g -DQT_NO_DEBUG -fPIC -pthread -Wall -Wextra \
    -isystem "$include" -isystem "$include/QtCore" -isystem "$include/QtSql" "$here/check.cpp" "$here/original_ini.cpp" \
    -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" -Wl,-rpath-link,"$lib" \
    -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" -lQt5Sql -lQt5Core -o "$output"
readelf -h "$output" | grep -q 'Machine:.*AArch64'
chmod 700 "$output"; mv "$output" "$1"
sha256sum "$1"
