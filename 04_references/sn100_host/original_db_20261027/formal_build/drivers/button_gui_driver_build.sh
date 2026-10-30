#!/usr/bin/env bash
# 完整主程序成功后，用固定字节的业务对象构建两个RK按钮页面驱动。
# 本脚本不部署、不运行程序；不覆盖主程序对象或产物。
set -Eeuo pipefail
[[ $# == 2 && $2 == /* && $EUID != 0 ]] || {
    echo 'Usage: button_gui_driver_build.sh connection|shutdown ABSOLUTE_OUTPUT_FILE' >&2; exit 2;
}
case $1 in connection) target=instrument_connection_rk_test ;; shutdown) target=shutdown_button_rk_test ;; *) exit 2 ;; esac
export PATH=/usr/local/bin:/usr/bin:/bin
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../../.." && pwd)
program=$root/06_programs/sn100_host
work=${SN100_BUTTON_MAIN_WORK:-$here/release_work_02}
testroot=${SN100_BUTTON_TEST_SYSROOT:-$work/sysroot}
main_build=$work/arm64_build
sysroot=$work/sysroot
qt=$sysroot/usr/include/aarch64-linux-gnu/qt5
testqt=$testroot/usr/include/aarch64-linux-gnu/qt5
lib=$sysroot/usr/lib/aarch64-linux-gnu
testlib=$testroot/usr/lib/aarch64-linux-gnu
build=${SN100_BUTTON_GUI_BUILD_DIR:-$here/button_gui_build/$target}
[[ $work == /* && $testroot == /* && $build == /* ]] || exit 2
[[ ${RK_BUILD_TYPE:-Release} == Release ]] || { echo 'This reuse contract requires Release.' >&2; exit 2; }
test -f "$testqt/QtTest/QtTest"
test -f "$testlib/libQt5Test.so"
readelf -h "$testlib/libQt5Test.so" | grep -q 'Machine:.*AArch64'
test -f "$work/packages/latest_package"
test -f "$work/build/sn100_shell"
test -f "$work/source_snapshot.json"
# 锁定主构建目录，拒绝在主程序仍编译时读取未完成的对象。
exec 8>"$main_build/build.lock"
flock -n 8 || { echo 'Main ARM64 compilation is still running.' >&2; exit 1; }
mkdir -p "$build" "$(dirname -- "$2")"
exec 9>"$build/build.lock"
flock -n 9 || exit 1
SN100_ARM64_LIST_INPUTS=1 SN100_ARM64_SYSROOT="$sysroot" SN100_ARM64_BUILD_DIR="$main_build" \
    bash "$program/shell/build.sh" "$build/list_only_unused" > "$build/main_inputs.tsv"
export BUTTON_PROGRAM=$program BUTTON_WORK=$work BUTTON_BUILD=$build BUTTON_TARGET=$target
export BUTTON_MAIN_BUILD=$main_build BUTTON_TESTROOT=$testroot BUTTON_SYSROOT=$sysroot
python3 - <<'PY'
import hashlib,importlib.util,json,os,pathlib,subprocess,sys,tarfile
P=pathlib.Path
program=P(os.environ['BUTTON_PROGRAM']); work=P(os.environ['BUTTON_WORK']); build=P(os.environ['BUTTON_BUILD'])
main_build=P(os.environ['BUTTON_MAIN_BUILD']); sysroot=P(os.environ['BUTTON_SYSROOT']); testroot=P(os.environ['BUTTON_TESTROOT'])
target=os.environ['BUTTON_TARGET']
def sha(path): return hashlib.sha256(P(path).read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('sn100_package_readonly',program/'shell/package_arm64.py')
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
frozen=json.loads((work/'source_snapshot.json').read_text())
if frozen!=module.snapshot(program): raise RuntimeError('Source snapshot changed; no GUI object reuse')
reports=list((work/'logs').glob('*_prepare_*.log'))
if not any('READY_FOR_TRANSFER=1' in p.read_text(errors='replace') for p in reports):
    raise RuntimeError('No complete successful prepare marker; wait for full02')
package=P((work/'packages/latest_package').read_text().strip())
if not package.is_file(): raise RuntimeError('Package named by latest_package is missing')
main=work/'build/sn100_shell'; main_sha=sha(main)
dependency=json.loads((work/'dependencies/dependency_report.json').read_text())
if dependency.get('errors') or not dependency.get('offline_link_and_sql_closure_passed'):
    raise RuntimeError('Main dependency verification did not pass')
records=[x for x in dependency['files'] if 'main' in x['roles']]
if len(records)!=1 or records[0]['sha256']!=main_sha: raise RuntimeError('Main binary differs from dependency check')
with tarfile.open(package,'r:gz') as tar:
    members=[m for m in tar.getmembers() if P(m.name).name=='sn100_shell' and m.isfile()]
    if len(members)!=1 or hashlib.sha256(tar.extractfile(members[0]).read()).hexdigest()!=main_sha:
        raise RuntimeError('Successful release package differs from main binary')
listed={}
for line in (build/'main_inputs.tsv').read_text().splitlines():
    kind,value=line.split('\t',1); listed.setdefault(kind,[]).append(value)
sources=list(listed['source']); headers=listed['header']; resources=listed['resource']; includes=listed['include']
if sources[0]!=str(program/'shell/main.cpp'): raise RuntimeError('obj_0 is not shell/main.cpp')
for header in headers:
    if b'Q_OBJECT' in P(header).read_bytes(): sources.append(str(main_build/('moc_'+P(header).stem+'.cpp')))
names=['shell_resources','account_resources','report_resources','imagesRpt','projects_resources',
       'reagents_resources','samples_resources','instrument_resources','qc_history_resources',
       'report_dictionary_resources','alarm_history_resources']
if len(resources)!=len(names): raise RuntimeError('Resource count changed; review build source order')
for name in names: sources.append(str(main_build/('qrc_'+name+'.cpp')))
flags=['-std=c++14','-O2','-g','-DQT_NO_DEBUG','-fPIC','-Wall','-Wextra','-DNO_BARCODE','-DHAVE_QT5']+includes
qt=sysroot/'usr/include/aarch64-linux-gnu/qt5'
for suffix in ('','/QtCore','/QtGui','/QtWidgets','/QtSql'): flags+=['-isystem',str(qt)+suffix]
compiler=P(subprocess.check_output(['which','aarch64-linux-gnu-g++'],text=True).strip()).resolve()
objects=[]
for index,source in enumerate(sources):
    obj=main_build/f'obj_{index}.o'
    if not obj.is_file(): raise RuntimeError('Missing main object: '+str(obj))
    if 'AArch64' not in subprocess.check_output(['readelf','-h',str(obj)],text=True):
        raise RuntimeError('Wrong object architecture: '+str(obj))
    objects.append({'index':index,'source':source,'source_sha256':sha(source),'object':str(obj),'object_sha256':sha(obj)})
if (main_build/f'obj_{len(sources)}.o').exists(): raise RuntimeError('Unexpected extra indexed object; rebuild in clean directory')
# QtTest使用相同QtCore/Gui/Widgets/Network头与库，不能混入另一版本的基础Qt。
for mod in ('QtCore','QtGui','QtWidgets','QtNetwork'):
    left=sysroot/'usr/lib/aarch64-linux-gnu'/f'libQt5{mod[2:]}.so'
    right=testroot/'usr/lib/aarch64-linux-gnu'/f'libQt5{mod[2:]}.so'
    if sha(left)!=sha(right): raise RuntimeError('Test and main Qt library differ: '+mod)
    for header in (qt/mod).rglob('*'):
        if header.is_file() and sha(header)!=sha(testroot/'usr/include/aarch64-linux-gnu/qt5'/header.relative_to(qt)):
            raise RuntimeError('Test and main Qt header differ: '+str(header))
tracked={str(program/name):digest for name,digest in frozen.items()}
for obj in objects: tracked[obj['source']]=obj['source_sha256']; tracked[obj['object']]=obj['object_sha256']
for directory in (qt,testroot/'usr/include/aarch64-linux-gnu/qt5/QtTest'):
    for header in directory.rglob('*'):
        if header.is_file(): tracked[str(header)]=sha(header)
for header in main_build.glob('ui_*.h'): tracked[str(header)]=sha(header)
tracked[str(compiler)]=sha(compiler)
tracked[str(main)]=main_sha
tracked[str(work/'source_snapshot.json')]=sha(work/'source_snapshot.json')
tracked[str(package)]=sha(package)
tracked[str(testroot/'usr/lib/aarch64-linux-gnu/libQt5Test.so')]=sha(testroot/'usr/lib/aarch64-linux-gnu/libQt5Test.so')
for suffix in ('.cpp','.pro'): tracked[str(program/'tests'/(target+suffix))]=sha(program/'tests'/(target+suffix))
manifest={'schema':'sn100-button-gui-object-link-v1','frozen_source_snapshot_sha256':sha(work/'source_snapshot.json'),
 'main_binary_sha256':main_sha,'compiler':str(compiler),'compile_flags':flags,'objects':objects,'tracked':tracked}
path=build/'main_reuse_manifest.json'
if path.exists() and json.loads(path.read_text())!=manifest:
    raise RuntimeError('Existing GUI baseline changed; use a new reviewed build directory')
path.write_text(json.dumps(manifest,indent=2)+'\n')
(build/'main_objects.nul').write_bytes(b''.join(x['object'].encode()+b'\0' for x in objects))
(build/'main_flags.nul').write_bytes(b''.join(x.encode()+b'\0' for x in flags))
(build/'inputs.SHA256SUMS').write_text(''.join(digest+'  '+name+'\n' for name,digest in sorted(tracked.items())))
print('EXACT_MAIN_OBJECTS_VERIFIED='+str(len(objects)))
PY
mapfile -d '' -t objects < "$build/main_objects.nul"
mapfile -d '' -t flags < "$build/main_flags.nul"
link_flags=(-L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" -Wl,-rpath-link,"$lib"
    -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu"
    -lQt5Charts -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -lpthread)
# 与正式main相同对象、库次序重链接，须逐字节相同才准许替换入口。
aarch64-linux-gnu-g++ "${objects[@]}" -L"$lib" -lQt5Svg "${link_flags[@]}" -o "$build/main_relinked_proof"
cmp -- "$build/main_relinked_proof" "$work/build/sn100_shell"
sha256sum --quiet -c "$build/inputs.SHA256SUMS"
source=$program/tests/$target.cpp
test_flags=("${flags[@]}" -I"$build" -isystem "$testqt/QtTest"
    -DQT_TESTLIB_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CORE_LIB)
moc_flags=(-DNO_BARCODE -DHAVE_QT5 -DQT_NO_DEBUG -DQT_TESTLIB_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB
    -I"$program" -I"$qt" -I"$qt/QtCore" -I"$qt/QtGui" -I"$qt/QtWidgets" -I"$testqt/QtTest")
/usr/lib/qt5/bin/moc "${moc_flags[@]}" "$source" -o "$build/$target.moc"
sha256sum "$build/$target.moc" > "$build/test_moc.SHA256SUMS"
printf '%s\n' "${test_flags[@]}" > "$build/test_compile_flags.txt"
aarch64-linux-gnu-g++ "${test_flags[@]}" -c "$source" -o "$build/$target.o"
readelf -h "$build/$target.o" | grep -q 'Machine:.*AArch64'
sha256sum --quiet -c "$build/test_moc.SHA256SUMS"
sha256sum --quiet -c "$build/inputs.SHA256SUMS"
output=$2.new
trap 'rm -f -- "$output"' EXIT
# QtTest实体使用绝对路径；其余Qt库仍全部来自主程序sysroot。
aarch64-linux-gnu-g++ "$build/$target.o" "${objects[@]:1}" \
    "$testlib/libQt5Test.so" -L"$lib" -lQt5Svg "${link_flags[@]}" -o "$output"
sha256sum --quiet -c "$build/inputs.SHA256SUMS"
readelf -h "$output" | grep -q 'Machine:.*AArch64'
chmod 755 "$output"
mv -f -- "$output" "$2"
sha256sum "$build/$target.o" "$2" "$build/main_relinked_proof" > "$build/outputs.SHA256SUMS"
file "$2"
cat "$build/outputs.SHA256SUMS"
