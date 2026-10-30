#!/usr/bin/env bash
# 只编译ARM64测试程序，不在电脑运行；使用整合窗口准备的匹配QtTest依赖。
set -Eeuo pipefail
task=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$task/../../../.." && pwd)
python3 - "$root/temp/sn100_host/original_db_20261027/integration/coordination.json" <<'PY'
import json,sys
status=json.load(open(sys.argv[1],encoding='utf-8-sig'))
assert status.get('otherThreadsMayCompile') is True, '整合窗口尚未释放编译预算。'
PY
program=$root/06_programs/sn100_host
sysroot=${SN100_TEST_SYSROOT:?请指定整合窗口已核对QtTest头文件和ARM64运行库的绝对根目录}
[[ $sysroot == /* ]] || exit 2
qt=$(readlink -f -- "$sysroot/usr/include/aarch64-linux-gnu/qt5")
lib=$sysroot/usr/lib/aarch64-linux-gnu
[[ -f $lib/libQt5Test.so ]] || { echo '缺少匹配的ARM64 QtTest链接库。' >&2; exit 2; }
out=$task/arm64_drivers
mkdir -p "$out"
flags=(-std=c++14 -O2 -g -fPIC -DQT_NO_DEBUG -DQT_CORE_LIB -DQT_TESTLIB_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -Wall -Wextra
       -iquote "$task" -iquote "$out" -iquote "$task/integrated" -iquote "$task/integrated/instrument" -iquote "$task/integrated/protocol"
       -iquote "$program" -iquote "$program/projects" -iquote "$program/samples" -iquote "$program/protocol" -iquote "$program/instrument" -isystem "$qt")
for module in QtCore QtGui QtWidgets QtSql QtNetwork QtTest; do flags+=(-isystem "$qt/$module"); done
link=(-L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu")
compile()
{
    local source=$1 object=$out/$(basename -- "${1%.cpp}").o
    aarch64-linux-gnu-g++ "${flags[@]}" -c "$source" -o "$object"
    objects+=("$object")
}
drivers=("$@")
if [[ ${#drivers[@]} == 0 ]]; then drivers=(original_task_codec_rk_test original_result_codec_rk_test original_detection_rk_test original_task_transport_rk_test); fi
for name in "${drivers[@]}"; do
    case $name in original_task_codec_rk_test|original_result_codec_rk_test|original_detection_rk_test|original_task_transport_rk_test|original_project_codec_rk_test) ;; *) echo '未知测试目标。' >&2; exit 2 ;; esac
    /usr/lib/qt5/bin/moc "$task/$name.cpp" -o "$out/$name.moc"
    objects=()
    compile "$task/$name.cpp"
    if [[ $name == original_task_codec_rk_test || $name == original_detection_rk_test ]]; then compile "$task/original_task_codec.cpp"; fi
    if [[ $name == original_result_codec_rk_test ]]; then compile "$task/original_result_codec.cpp"; fi
    if [[ $name == original_project_codec_rk_test ]]; then compile "$task/original_project_codec.cpp"; fi
    if [[ $name == original_detection_rk_test ]]; then
        for source in "$task/original_result_codec.cpp" "$task/original_business_database.cpp" "$task/original_sample_repository.cpp" \
                      "$task/original_reagent_repository.cpp" "$task/original_detection_session.cpp" \
                      "$program/original_files/original_ini.cpp" "$program/projects/original_project_repository.cpp" \
                      "$program/projects/project_advanced.cpp" "$program/projects/cuscheckbox.cpp" "$program/samples/sample_task_reset.cpp"; do compile "$source"; done
        for header in "$program/projects/project_advanced.h" "$program/projects/cuscheckbox.h"; do
            moc=$out/moc_$(basename -- "${header%.h}").cpp
            /usr/lib/qt5/bin/moc "$header" -o "$moc"
            compile "$moc"
        done
        libraries=(-lQt5Test -lQt5Widgets -lQt5Gui -lQt5Sql -lQt5Core -lpthread)
    elif [[ $name == original_task_transport_rk_test ]]; then
        for source in "$task/integrated/instrument/device_control.cpp" "$task/integrated/protocol/tcp_transport.cpp" \
                      "$program/instrument/original_device_commands.cpp" "$program/protocol/frame_codec.cpp" \
                      "$program/protocol/status_session.cpp" "$program/protocol/read_only_messages.cpp"; do compile "$source"; done
        for header in "$task/integrated/instrument/device_control.h" "$task/integrated/protocol/tcp_transport.h"; do
            moc=$out/moc_$(basename -- "${header%.h}").cpp
            /usr/lib/qt5/bin/moc "$header" -o "$moc"
            compile "$moc"
        done
        libraries=(-lQt5Test -lQt5Network -lQt5Core -lpthread)
    else libraries=(-lQt5Test -lQt5Sql -lQt5Core -lpthread); fi
    temporary=$out/$name.new
    aarch64-linux-gnu-g++ "${objects[@]}" "${link[@]}" "${libraries[@]}" -o "$temporary"
    readelf -h "$temporary" | grep -q 'Machine:.*AArch64'
    chmod +x "$temporary"
    mv -- "$temporary" "$out/$name"
    sha256sum "$out/$name"
done
