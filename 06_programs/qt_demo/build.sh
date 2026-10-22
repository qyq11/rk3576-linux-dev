#!/usr/bin/env bash
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
sysroot=${QT_TARGET_ROOT:-$root/temp/qt_demo/sysroot}
include=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
[[ -f $include/QtWidgets/QApplication && -f $lib/libQt5Widgets.so.5.15.13 ]] || {
    echo 'Missing Qt target headers/libraries; run prepare.sh first.' >&2; exit 1;
}
command -v aarch64-linux-gnu-g++ >/dev/null
case ${RK_BUILD_TYPE:-Release} in
    Debug) optimization=(-O0 -g3 -fno-omit-frame-pointer) ;;
    Release) optimization=(-O2 -g) ;;
    *) echo 'RK_BUILD_TYPE must be Debug or Release.' >&2; exit 2 ;;
esac
mkdir -p -- "$(dirname -- "$1")"
aarch64-linux-gnu-g++ -std=c++17 "${optimization[@]}" -DRK_BOARD -fPIC -Wall -Wextra -Werror \
    -isystem "$include" -isystem "$include/QtCore" -isystem "$include/QtGui" -isystem "$include/QtWidgets" \
    "$program_dir/app_main.cpp" -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
    -lQt5Widgets -lQt5Gui -lQt5Core -o "$1"
