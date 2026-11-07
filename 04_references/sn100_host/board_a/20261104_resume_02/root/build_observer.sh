#!/usr/bin/env bash
set -Eeuo pipefail
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
qt=$(readlink -f "$work/sysroot/usr/include/aarch64-linux-gnu/qt5")
lib=$work/sysroot/usr/lib/aarch64-linux-gnu
aarch64-linux-gnu-g++ -std=c++14 -O2 -g -fPIC -shared -DQT_NO_DEBUG \
    -isystem "$qt" -isystem "$qt/QtCore" -isystem "$qt/QtGui" -isystem "$qt/QtWidgets" \
    "$work/live_ui_observer.cpp" -L"$lib" -L"$work/sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$work/sysroot/lib/aarch64-linux-gnu" \
    -lQt5Widgets -lQt5Gui -lQt5Core -ldl -o "$work/live_ui_observer.so"
readelf -h "$work/live_ui_observer.so" | grep 'Machine:'
sha256sum "$work/live_ui_observer.cpp" "$work/live_ui_observer.so" > "$work/observer.SHA256SUMS"
