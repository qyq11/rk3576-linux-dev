#!/usr/bin/env bash
# 仅使用本次完整ARM64构建对象；先逐字节重链接主程序，再换成页面检查入口。
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux
work=${SN100_RESUME_MAIN_WORK:-$here/resume_settings_01}
program=$(cat "$work/source_cache_path")
build=$work/arm64_build
sysroot=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/original_db_20261027/integration/release_work_02/sysroot
qt=$(readlink -f "$sysroot/usr/include/aarch64-linux-gnu/qt5")
lib=$sysroot/usr/lib/aarch64-linux-gnu
target=${SN100_RESUME_TEST_TARGET:-original_settings_pages_rk_test}
case "$target" in original_settings_pages_rk_test|original_main_task_chain_rk_test|original_history_rk_test|original_quality_pages_rk_test) ;; *) exit 2;; esac
testbuild=$work/${target}_build
mkdir -p "$testbuild"
test "$(cat "$work/build_exit_code")" = 0
exec 9>"$build/build.lock"; flock -n 9
SN100_ARM64_LIST_INPUTS=1 SN100_ARM64_SYSROOT=$sysroot SN100_ARM64_BUILD_DIR=$build bash "$program/shell/build.sh" "$testbuild/unused" > "$testbuild/inputs.tsv"
export SETTINGS_TEST_BUILD=$testbuild SETTINGS_PROGRAM=$program SETTINGS_BUILD=$build
python3 - <<'PY'
import os,pathlib,hashlib,json
b=pathlib.Path(os.environ['SETTINGS_BUILD']);t=pathlib.Path(os.environ['SETTINGS_TEST_BUILD']);p=pathlib.Path(os.environ['SETTINGS_PROGRAM'])
rows={}
for line in (t/'inputs.tsv').read_text().splitlines():
 k,v=line.split('\t',1); rows.setdefault(k,[]).append(v)
sources=rows['source'][:]
for h in rows['header']:
 if b'Q_OBJECT' in pathlib.Path(h).read_bytes(): sources.append(str(b/('moc_'+pathlib.Path(h).stem+'.cpp')))
names=['shell_resources','account_resources','report_resources','imagesRpt','projects_resources','reagents_resources','samples_resources','instrument_resources','qc_history_resources','report_dictionary_resources','alarm_history_resources']
sources.extend(str(b/('qrc_'+n+'.cpp')) for n in names)
objects=[b/('obj_'+str(i)+'.o') for i in range(len(sources))]
assert all(o.is_file() for o in objects)
tracked={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*objects,*map(pathlib.Path,sources)]}
(t/'objects.nul').write_bytes(b''.join(str(o).encode()+b'\0' for o in objects))
(t/'includes.nul').write_bytes(b''.join(i.encode()+b'\0' for i in rows['include']))
(t/'object_identity.json').write_text(json.dumps({'objects':list(zip(sources,map(str,objects))),'sha256':tracked},indent=2)+'\n')
(t/'objects.SHA256SUMS').write_text(''.join(d+'  '+f+'\n' for f,d in tracked.items()))
print('CURRENT_FULL_BUILD_OBJECTS='+str(len(objects)))
PY
mapfile -d '' -t objects < "$testbuild/objects.nul"
mapfile -d '' -t includes < "$testbuild/includes.nul"
link=(-L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" -lQt5Charts -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -lpthread)
aarch64-linux-gnu-g++ "${objects[@]}" -L"$lib" -lQt5Svg "${link[@]}" -o "$testbuild/main_identity_proof"
cmp "$testbuild/main_identity_proof" "$work/build/sn100_shell"
if [[ $target == original_settings_pages_rk_test || $target == original_quality_pages_rk_test ]]; then
 source=$program/tests/$target.cpp
 if [[ $target == original_quality_pages_rk_test ]]; then source=$work/test_inputs/$target.cpp; fi
elif [[ $target == original_history_rk_test ]]; then
 source=$program/history/tests/$target.cpp
else
 # 产品快照固定后允许补测试场景；测试入口另复制、取指纹，不混作产品已验源码。
 source=$work/test_inputs/$target.cpp
fi
cp "$source" "$testbuild/test_source.cpp"
sha256sum "$testbuild/test_source.cpp" > "$testbuild/test_source.SHA256SUMS"
/usr/lib/qt5/bin/moc -I"$qt" -I"$qt/QtTest" -I"$program" "$testbuild/test_source.cpp" -o "$testbuild/$target.moc"
flags=(-std=c++14 -O2 -g -fPIC -DQT_NO_DEBUG -DNO_BARCODE -DHAVE_QT5 -DQT_TESTLIB_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB "${includes[@]}" -I"$testbuild" -I"$program/samples/tests" -I"$program" -isystem "$qt" -isystem "$qt/QtTest")
aarch64-linux-gnu-g++ "${flags[@]}" -c "$testbuild/test_source.cpp" -o "$testbuild/driver.o"
aarch64-linux-gnu-g++ "$testbuild/driver.o" "${objects[@]:1}" "$lib/libQt5Test.so" -L"$lib" -lQt5Svg "${link[@]}" -o "$work/build/$target"
sha256sum --quiet -c "$testbuild/objects.SHA256SUMS"
sha256sum "$work/build/$target" "$work/build/sn100_shell" > "$testbuild/binaries.SHA256SUMS"
readelf -h "$work/build/$target" | grep 'Machine:'
cat "$testbuild/binaries.SHA256SUMS"
