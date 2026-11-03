#!/usr/bin/env bash
# 复用本轮已经交叉编译的业务对象，只替换检查入口；检查程序只在板B运行。
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)
program=$root/06_programs/sn100_host
work=$root/temp/sn100_host/rk_prepare
output=$root/temp/sn100_host/board_b/20261102_sn500_connection/check
mkdir -p "$output"
include=$(readlink -f "$work/sysroot/usr/include/aarch64-linux-gnu/qt5")
flags=(-std=c++14 -O2 -g -fPIC -I"$program" -I"$include")
for module in QtCore QtGui QtWidgets QtNetwork QtSql; do flags+=(-isystem "$include/$module"); done
aarch64-linux-gnu-g++ "${flags[@]}" -c "$program/tests/sn500_theme_connection_rk_check.cpp" -o "$output/check.o"
objects=()
for object in "$work/arm64_build"/obj_*.o; do
    name=${object##*/}
    [[ $name =~ ^obj_[0-9]+\.o$ && $name != obj_0.o ]] || continue
    objects+=("$object")
done
test "${#objects[@]}" -gt 200
lib=$work/sysroot/usr/lib/aarch64-linux-gnu
aarch64-linux-gnu-g++ "$output/check.o" "${objects[@]}" -L"$lib" -L"$work/sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$work/sysroot/lib/aarch64-linux-gnu" \
    -lQt5Svg -lQt5Charts -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -lpthread \
    -o "$output/sn500_theme_connection_rk_check"
printf 'CHECK_LINKED_OBJECTS=%s\n' "${#objects[@]}"
sha256sum "$output/sn500_theme_connection_rk_check"
