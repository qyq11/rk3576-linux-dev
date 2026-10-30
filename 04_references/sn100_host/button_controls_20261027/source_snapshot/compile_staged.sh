#!/usr/bin/env bash
# 只交叉编译暂存副本；须由整合窗口释放一个编译作业后运行，不在电脑启动产品。
set -Eeuo pipefail
task=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$task/../../../.." && pwd)
coordination=$root/temp/sn100_host/original_db_20261027/integration/coordination.json
python3 - "$coordination" <<'PY'
import json,sys
status=json.load(open(sys.argv[1],encoding='utf-8-sig'))
assert status.get('otherThreadsMayCompile') is True, '整合窗口尚未释放编译预算，未启动编译。'
PY
program=$root/06_programs/sn100_host
staged=$task/integrated
out=$task/arm64_objects
sysroot=${SN100_TEST_SYSROOT:?请指定整合窗口已核对的ARM64 Qt依赖根目录}
[[ $sysroot == /* ]] || exit 2
qt=$(readlink -f -- "$sysroot/usr/include/aarch64-linux-gnu/qt5")
mkdir -p "$out"
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB -DQT_NETWORK_LIB -fPIC -Wall -Wextra
       -iquote "$out" -iquote "$staged" -iquote "$staged/samples" -iquote "$staged/reagents" -iquote "$staged/instrument" -iquote "$staged/shell"
       -iquote "$program" -iquote "$program/samples" -iquote "$program/samples/material" -iquote "$program/projects" -iquote "$program/ui" -iquote "$program/protocol" -iquote "$program/instrument"
       -iquote "$program/shell" -iquote "$program/shell/widgets" -iquote "$program/shell/widgets/material" -isystem "$qt")
for module in QtCore QtGui QtWidgets QtSql QtXml QtPrintSupport QtQml QtNetwork QtCharts QtSvg QtTest; do flags+=(-isystem "$qt/$module"); done
for source in "$program/samples/"*.ui "$program/shell/frmmain.ui"; do
    /usr/lib/qt5/bin/uic "$source" -o "$out/ui_$(basename -- "${source%.ui}").h"
done
for header in "$staged/samples/sample_store.h" "$staged/samples/sample_page.h" "$staged/shell/frmmain.h" "$staged/instrument/device_control.h"; do
    /usr/lib/qt5/bin/moc "$header" -o "$out/moc_$(basename -- "${header%.h}").cpp"
done
for source in "$staged/original_files/original_business_database.cpp" "$staged/samples/"*.cpp "$staged/reagents/original_reagent_repository.cpp" \
              "$staged/shell/catalog_shell.cpp" "$staged/shell/device_control_shell.cpp" "$staged/instrument/device_control.cpp" \
              "$staged/protocol/tcp_transport.cpp" "$out/"moc_*.cpp; do
    object=$out/$(basename -- "${source%.cpp}").o
    aarch64-linux-gnu-g++ "${flags[@]}" -c "$source" -o "$object"
    readelf -h "$object" | grep 'Machine:'
    sha256sum "$source" "$object"
done
