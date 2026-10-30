#!/usr/bin/env bash
# 仅编译RK按钮检查工具，不在电脑运行程序，也不修改正式应用。
set -Eeuo pipefail
[[ $# == 1 && $1 == /* && $EUID != 0 ]] || exit 2
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$test_dir/../../.." && pwd)
sysroot=${SN100_ARM64_SYSROOT:-$root/temp/sn100_host/arm64_sysroot}
qt=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
mkdir -p -- "$(dirname -- "$1")"
sha256sum "$test_dir/button_audit_rk.cpp" > "$1.sources.sha256"
aarch64-linux-gnu-g++ -std=c++14 -O2 -g -fPIC -shared -DQT_NO_DEBUG \
  -isystem "$qt" -isystem "$qt/QtCore" -isystem "$qt/QtGui" -isystem "$qt/QtWidgets" \
  "$test_dir/button_audit_rk.cpp" -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" \
  -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
  -lQt5Widgets -lQt5Gui -lQt5Core -ldl -o "$1.new"
sha256sum --quiet -c "$1.sources.sha256"
readelf -h "$1.new" | grep -q 'Machine:.*AArch64'
mv -- "$1.new" "$1"
sha256sum "$1"
