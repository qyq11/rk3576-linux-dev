#!/usr/bin/env bash
# 只另编专项CPP并链接本次完整正式对象；不运行、不上传、不安装组件。
set -Eeuo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin
export PYTHONDONTWRITEBYTECODE=1
[[ $# == 3 && $1 == /* && $2 =~ ^[a-f0-9]{64}$ && $3 == /* && $EUID != 0 ]] || {
    echo 'Usage: bash build_sample_add_review.sh ABSOLUTE_COMPLETE_BUILD RELEASE_ID ABSOLUTE_SYSROOT (普通WSL用户)' >&2; exit 2;
}
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../../.." && pwd)
build=$1
release=$2
sysroot=$3
[[ -f $build/arm64_build/build.lock && ! -L $build/arm64_build/build.lock ]] || { echo '缺少本次正式构建锁' >&2; exit 2; }
exec 9<>"$build/arm64_build/build.lock"
flock -n 9 || { echo '正式构建仍在使用对象，未继续' >&2; exit 2; }
export SN100_SAMPLE_ADD_LOCK_FD=9
out=$here/build_01
[[ ! -e $out && ! -L $out ]] || { echo '保留已有专项构建目录，重试须另存修订' >&2; exit 2; }
mkdir "$out"
finish() { code=$?; trap - EXIT; printf '%s\n' "$code" >"$out/build_exit.txt"; exit "$code"; }
trap finish EXIT
python3 "$here/bind_sample_add_build.py" "$build" "$release"
qt=$(readlink -f -- "$sysroot/usr/include/aarch64-linux-gnu/qt5")
lib=$sysroot/usr/lib/aarch64-linux-gnu
[[ -f $qt/QtTest/QTest && -f $lib/libQt5Test.so.5.15.13 ]] || { echo '缺少匹配Qt5.15.13 Test组件；未安装或恢复' >&2; exit 2; }
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -DNO_BARCODE -DHAVE_QT5 -DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -DQT_TESTLIB_LIB -fPIC -Wall -Wextra
       -I"$root/06_programs/sn100_host" -I"$root/06_programs/sn100_host/shell" -I"$build/arm64_build"
       -I"$root/06_programs/sn100_host/samples" -I"$root/06_programs/sn100_host/samples/material"
       -I"$root/06_programs/sn100_host/shell/widgets/material" -I"$root/06_programs/sn100_host/shell/widgets/material/lib"
       "-DSN100_SAMPLE_ADD_RELEASE=\"$release\"" -isystem "$qt")
for module in QtCore QtGui QtWidgets QtNetwork QtSql QtXml QtPrintSupport QtQml QtCharts QtSvg QtTest; do
    flags+=(-isystem "$qt/$module")
done
mapfile -t objects <"$out/link_objects.txt"
[[ ${#objects[@]} -gt 0 ]] || { echo '正式对象清单为空' >&2; exit 2; }
link_flags=(-L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" -Wl,-rpath-link,"$lib"
            -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" -lQt5Test -lQt5Svg -lQt5Charts
            -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -lpthread)
printf '%q ' aarch64-linux-gnu-g++ "${flags[@]}" -c "$here/sample_add_review.cpp" -o "$out/sample_add_review.o" >"$out/compile_command.txt"
printf '\n' >>"$out/compile_command.txt"
aarch64-linux-gnu-g++ "${flags[@]}" -c "$here/sample_add_review.cpp" -o "$out/sample_add_review.o"
aarch64-linux-gnu-g++ "$out/sample_add_review.o" "${objects[@]}" "${link_flags[@]}" -o "$out/sample_add_review"
aarch64-linux-gnu-readelf -h "$out/sample_add_review" >"$out/elf_header.txt"
python3 "$here/bind_sample_add_build.py" "$build" "$release" --verify
# 检查器私有依赖，部署到检查器目录的lib/，不替换板端系统Qt。
mkdir "$out/lib"
cp -- "$lib/libQt5Test.so.5.15.13" "$out/lib/libQt5Test.so.5"
sha256sum "$out/sample_add_review" "$out/sample_add_review.o" "$out/lib/libQt5Test.so.5" >"$out/SHA256SUMS"
printf 'SAMPLE_ADD_REVIEW_ARM64_LINKED=1\nRK_RUNTIME_VERIFIED=0\n'
