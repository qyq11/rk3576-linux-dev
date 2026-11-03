#!/usr/bin/env bash
# 仅供05全量构建/打包成功后执行；本准备阶段不运行此脚本。
set -Eeuo pipefail
[[ $# == 1 && $1 =~ ^[a-f0-9]{64}$ ]] || { echo 'Usage: build_review_05.sh RELEASE_ID' >&2; exit 2; }
release_id=$1
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../../../.." && pwd)
integration=$root/temp/sn100_host/workflow_night_20261102/integration_05
sysroot=$root/temp/sn100_host/rk_prepare/sysroot
frozen=$here/frozen_05/sn100_host
test "$release_id" != 7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9
test "$(cat "$integration/build_exit.txt")" = 0
exec 9>"$integration/arm64_build/build.lock"
flock -n 9 || { echo '05构建仍占用对象，停止而不是混用。' >&2; exit 1; }
test ! -e "$here/status_login_review_05"
python3 "$here/bind_build.py" "$release_id"
qt=$(readlink -f "$sysroot/usr/include/aarch64-linux-gnu/qt5")
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -DNO_BARCODE -DHAVE_QT5 -fPIC -I"$frozen" -isystem "$qt"
       "-DSN100_REVIEW_RELEASE=\"$release_id\"")
for module in QtCore QtGui QtWidgets QtNetwork QtSql QtXml QtPrintSupport QtQml QtCharts QtSvg; do
    flags+=(-isystem "$qt/$module")
done
aarch64-linux-gnu-g++ "${flags[@]}" -c "$here/status_login_review_05.cpp" -o "$here/review_05.o"
mapfile -t objects <"$here/link_objects.txt"
lib=$sysroot/usr/lib/aarch64-linux-gnu
aarch64-linux-gnu-g++ "$here/review_05.o" "${objects[@]}" -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
    -lQt5Svg -lQt5Charts -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -lpthread \
    -o "$here/status_login_review_05.new"
readelf -h "$here/status_login_review_05.new" | grep -q 'Machine:.*AArch64'
python3 "$here/bind_build.py" "$release_id" --verify-objects
mv -- "$here/status_login_review_05.new" "$here/status_login_review_05"
sha256sum "$here/status_login_review_05" >"$here/review_binary.sha256"
printf '%s\n' "$release_id" >"$here/review_release_id"
printf 'NEW_05_PROBE_LINKED=1\nRK_RUNTIME_VERIFIED=0\n'
