#!/usr/bin/env bash
# Project convention: one absolute output path, producing an ARM64 executable.
set -Eeuo pipefail
[[ $# == 1 && $1 == /* ]] || { echo 'Usage: build.sh ABSOLUTE_OUTPUT_FILE' >&2; exit 2; }
[[ $EUID != 0 ]] || { echo 'Build as the ordinary WSL user.' >&2; exit 2; }
export PATH=/usr/local/bin:/usr/bin:/bin
program_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
sysroot=$root/temp/sn100_host/arm64_sysroot
[[ -f $sysroot/usr/lib/aarch64-linux-gnu/libQt5Sql.so.5.15.13 ]] || bash "$program_dir/prepare_arm64.sh"
include=$sysroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
build_dir=$root/temp/sn100_host/arm64_build
mkdir -p "$build_dir" "$(dirname -- "$1")"
case ${RK_BUILD_TYPE:-Release} in
    Release) optimization=(-O2 -g -DQT_NO_DEBUG) ;;
    Debug) optimization=(-O0 -g3 -fno-omit-frame-pointer) ;;
    *) echo 'RK_BUILD_TYPE must be Debug or Release.' >&2; exit 2 ;;
esac
qt_tools=/usr/lib/qt5/bin
"$qt_tools/uic" "$program_dir/ui/frmhospinfosetting.ui" -o "$build_dir/ui_frmhospinfosetting.h"
"$qt_tools/uic" "$program_dir/ui/frmaccountsetting.ui" -o "$build_dir/ui_frmaccountsetting.h"
"$qt_tools/uic" "$program_dir/ui/cusmessagebox.ui" -o "$build_dir/ui_cusmessagebox.h"
"$qt_tools/uic" "$program_dir/ui/cusdialogmask.ui" -o "$build_dir/ui_cusdialogmask.h"
"$qt_tools/rcc" "$program_dir/resources.qrc" -o "$build_dir/qrc_resources.cpp"
generated=()
for header in data/hospital_store ui/frmhospinfosetting data/account_store ui/frmaccountsetting ui/cusmessagebox ui/cusdialogmask; do
    target=$build_dir/moc_$(basename -- "$header").cpp
    "$qt_tools/moc" "$program_dir/$header.h" -o "$target"
    generated+=("$target")
done
output=$(realpath -m -- "$1")
temporary=$output.new
trap 'rm -f -- "$temporary"' EXIT
aarch64-linux-gnu-g++ -std=c++14 "${optimization[@]}" -fPIC -Wall -Wextra \
    -I"$program_dir" -I"$program_dir/ui" -I"$build_dir" \
    -isystem "$include" -isystem "$include/QtCore" -isystem "$include/QtGui" \
    -isystem "$include/QtWidgets" -isystem "$include/QtSql" \
    "$program_dir/main.cpp" "$program_dir/data/hospital_store.cpp" \
    "$program_dir/ui/frmhospinfosetting.cpp" "$program_dir/ui/mnemonic.cpp" "${generated[@]}" \
    "$program_dir/data/account_store.cpp" "$program_dir/ui/frmaccountsetting.cpp" "$build_dir/qrc_resources.cpp" \
    "$program_dir/ui/cusmessagebox.cpp" "$program_dir/ui/cusdialogmask.cpp" \
    -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" \
    -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
    -lQt5Widgets -lQt5Gui -lQt5Sql -lQt5Core -lpthread -o "$temporary"
readelf -h "$temporary" | grep -q 'Machine:.*AArch64'
mv -f -- "$temporary" "$output"
file "$output"
sha256sum "$output"
