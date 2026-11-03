#!/usr/bin/env bash
# 最终08正式构建、归档和freeze成功后，另编一个专项观察文件，再链接原完整对象。
# 不构建正式程序、不部署、不运行、不连接板卡；失败证据保留且不覆盖重试。
set -Eeuo pipefail
set -o noclobber
umask 077
export PATH=/usr/local/bin:/usr/bin:/bin
export PYTHONDONTWRITEBYTECODE=1
die() { printf 'STOP: %s\n' "$*" >&2; exit 2; }
[[ $# -ge 1 && $# -le 2 && $1 =~ ^[a-f0-9]{64}$ ]] || die 'Usage: build_review_08.sh RELEASE_ID [ABSOLUTE_SYSROOT]'
[[ $EUID != 0 ]] || die '请用普通WSL用户构建。'
release_id=$1
[[ $release_id != 14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c && $release_id != 7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9 \
   && $release_id != e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270 ]] || die '拒绝06/05/7c发布及其对象。'
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../../../.." && pwd)
integration=$(python3 - "$here" "$release_id" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from bind_build_08 import integration_path, read_json, require, validate_release_id
release = validate_release_id(sys.argv[2])
prepared = read_json(Path(sys.argv[1]) / 'prepared_inputs.json')
require(prepared.get('release_id') == release, '未冻结这次最终08发布')
print(integration_path(prepared['integration_directory']))
PY
)
sysroot=${2:-$root/temp/sn100_host/rk_prepare/sysroot}
[[ $sysroot == /* && -d $sysroot && $sysroot != *$'\n'* ]] || die 'sysroot必须是已有的绝对目录；本工具不会安装或恢复依赖。'
sysroot=$(readlink -f -- "$sysroot")
frozen=$here/frozen_08/sn100_host
[[ -f $integration/arm64_build/build.lock && ! -L $integration/arm64_build/build.lock ]] || die '正式06构建锁不存在或被重定向。'
# <>保留正式锁文件；Python子工具确认继承的FD确为同一文件。
exec 9<>"$integration/arm64_build/build.lock"
flock -n 9 || die '正式06构建仍占用对象，停止而不混用。'
export SN100_08_LOCK_FD=9
for name in status_login_review_08 status_login_review_08.new review_08.o \
            review_release_id review_binary.sha256 review_elf_header.txt \
            review_build_provenance.json link_provenance.json link_objects.txt; do
    [[ ! -e $here/$name && ! -L $here/$name ]] || die "产物或证据已存在，不能覆盖：$name"
done
[[ $(cat "$integration/build_exit.txt") == 0 ]] || die '正式06全量构建没有exit0。'
python3 "$here/bind_build_08.py" "$release_id"
qt=$(readlink -f -- "$sysroot/usr/include/aarch64-linux-gnu/qt5")
lib=$sysroot/usr/lib/aarch64-linux-gnu
[[ -f $qt/QtCore/qglobal.h && -f $lib/libQt5Widgets.so && -f $lib/libQt5Svg.so ]] || die '缺少明确的ARM64 Qt依赖，不回退到电脑Qt。'
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -DNO_BARCODE -DHAVE_QT5 -fPIC -Wall -Wextra
       -I"$here" -I"$frozen" -I"$frozen/shell" -I"$integration/arm64_build" -isystem "$qt"
       "-DSN100_REVIEW_RELEASE=\"$release_id\"")
for module in QtCore QtGui QtWidgets QtNetwork QtSql QtXml QtPrintSupport QtQml QtCharts QtSvg; do
    flags+=(-isystem "$qt/$module")
done
mapfile -t objects <"$here/link_objects.txt"
[[ ${#objects[@]} -gt 0 ]] || die '正式对象清单为空。'
link_flags=(-L"$lib" -L"$sysroot/lib/aarch64-linux-gnu"
            -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu"
            -lQt5Svg -lQt5Charts -lQt5Qml -lQt5PrintSupport -lQt5Widgets -lQt5Gui
            -lQt5Network -lQt5Sql -lQt5Xml -lQt5Core -lpthread)
# 保存实际参数和编译器/链接库指纹。ad_wire_observer.h只由此cpp包含，不另编业务对象。
python3 - "$here" "$release_id" "$sysroot" "$qt" --flags "${flags[@]}" --link-flags "${link_flags[@]}" <<'PY'
import shutil
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from bind_build_08 import elf_arm64, new_json, read_json, require, sha
here, release, sysroot, qt = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3]), sys.argv[4]
split = sys.argv.index('--link-flags')
flags = sys.argv[sys.argv.index('--flags') + 1:split]
link_flags = sys.argv[split + 1:]
compiler_name = shutil.which('aarch64-linux-gnu-g++')
require(compiler_name is not None, '缺少ARM64交叉编译器')
compiler = Path(compiler_name).resolve(strict=True)
modules = ('Svg', 'Charts', 'Qml', 'PrintSupport', 'Widgets', 'Gui', 'Network', 'Sql', 'Xml', 'Core')
libraries = {}
for module in modules:
    library = (sysroot / ('usr/lib/aarch64-linux-gnu/libQt5' + module + '.so')).resolve(strict=True)
    elf_arm64(library)
    libraries[module] = {'path': str(library), 'sha256': sha(library)}
binding = read_json(here / 'link_provenance.json')
record = {
    'release_id': release, 'link_record_sha256': binding['record_sha256'],
    'compiler_path': str(compiler), 'compiler_sha256': sha(compiler),
    'compiler_version': subprocess.check_output([str(compiler), '--version'], text=True).splitlines()[0],
    'sysroot': str(sysroot), 'qt_include_directory': qt, 'compile_flags': flags, 'link_flags': link_flags,
    'compiled_sources': ['status_login_review_08.cpp'], 'header_inline_observer': 'ad_wire_observer.h',
    'formal_object_count': binding['object_count'], 'formal_main_object': binding['main_object'],
    'qt_libraries': libraries, 'runtime_verified': False,
}
new_json(here / 'review_build_provenance.json', record)
PY
aarch64-linux-gnu-g++ "${flags[@]}" -c "$here/status_login_review_08.cpp" -o "$here/review_08.o"
aarch64-linux-gnu-g++ "$here/review_08.o" "${objects[@]}" "${link_flags[@]}" -o "$here/status_login_review_08.new"
aarch64-linux-gnu-readelf -h "$here/status_login_review_08.new" >"$here/review_elf_header.txt"
python3 - "$here" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from bind_build_08 import elf_arm64, read_json, require, sha
here = Path(sys.argv[1])
elf_arm64(here / 'review_08.o', relocatable=True)
elf_arm64(here / 'status_login_review_08.new')
record = read_json(here / 'review_build_provenance.json')
require(sha(Path(record['compiler_path'])) == record['compiler_sha256'], '链接期间编译器变化')
for library in record['qt_libraries'].values():
    require(sha(Path(library['path'])) == library['sha256'], '链接期间Qt库变化')
PY
python3 "$here/bind_build_08.py" "$release_id" --verify-objects
# ln拒绝已有目标，不用可能覆盖已有文件的普通mv。
ln -- "$here/status_login_review_08.new" "$here/status_login_review_08"
rm -- "$here/status_login_review_08.new"
sha256sum "$here/status_login_review_08" >"$here/review_binary.sha256"
printf '%s\n' "$release_id" >"$here/review_release_id"
printf 'NEW_08_PROBE_LINKED=1\nRK_RUNTIME_VERIFIED=0\n'
