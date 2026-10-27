#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
kind=${1:-page}
case $kind in page|store) ;; *) exit 2 ;; esac
root=$(cd -- "$here/../../../.." && pwd)
program=$root/06_programs/sn100_host
qt=$root/temp/sn100_host/rk_prepare/sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$root/temp/sn100_host/rk_prepare/sysroot/usr/lib/aarch64-linux-gnu
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -fPIC -pthread -I"$program/projects" -I"$program" -isystem "$qt")
for module in QtCore QtGui QtWidgets QtSql; do flags+=(-isystem "$qt/$module"); done
aarch64-linux-gnu-g++ "${flags[@]}" -c "$here/${kind}_driver_candidate.cpp" -o "$here/${kind}_candidate.o"
if [[ ! -f $here/reagent_store.o ]]; then
    aarch64-linux-gnu-g++ "${flags[@]}" -c "$program/reagents/reagent_store.cpp" -o "$here/reagent_store.o"
    /usr/lib/qt5/bin/moc -I"$qt" -I"$program" "$program/reagents/reagent_store.h" -o "$here/moc_reagent_store.cpp"
    aarch64-linux-gnu-g++ "${flags[@]}" -c "$here/moc_reagent_store.cpp" -o "$here/moc_reagent_store.o"
fi
objects=("$here/${kind}_candidate.o" "$here/reagent_store.o" "$here/moc_reagent_store.o")
for object in "$here/page_build_01"/obj_*.o; do
    [[ ${object##*/} == obj_0.o ]] || objects+=("$object")
done
aarch64-linux-gnu-g++ "${objects[@]}" -L"$lib" -L"$root/temp/sn100_host/rk_prepare/sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$root/temp/sn100_host/rk_prepare/sysroot/lib/aarch64-linux-gnu" \
    -lQt5Widgets -lQt5Gui -lQt5Sql -lQt5Core -lpthread -o "$here/project_config_${kind}_rk_driver.new"
readelf -h "$here/project_config_${kind}_rk_driver.new" | grep -q 'Machine:.*AArch64'
mv "$here/project_config_${kind}_rk_driver.new" "$here/project_config_${kind}_rk_driver"
sha256sum "$here/project_config_${kind}_rk_driver" "$here/${kind}_driver_candidate.cpp"
