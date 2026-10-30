#!/usr/bin/env bash
# 一个驱动占一个编译作业；只生成ARM64文件，不在电脑运行，也不连接RK。
set -Eeuo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin
task=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$task/../../.." && pwd)
program=$root/06_programs/sn100_host
sysroot=$root/temp/sn100_host/original_db_20261027/integration/release_work_02/sysroot
qt=$(readlink -f "$sysroot/usr/include/aarch64-linux-gnu/qt5")
lib=$sysroot/usr/lib/aarch64-linux-gnu
[[ $EUID != 0 && $(uname -m) == x86_64 && -f $qt/QtSql/QtSql ]]
mode=${1:?指定qc_calibration或management}
case $mode in
 qc_calibration)
  target=original_qc_calibration_rk_driver
  project_file=$program/quality_control/tests/$target.pro
  sources=("$program/quality_control/tests/$target.cpp"
   "$program/quality_control/qc_store.cpp" "$program/quality_control/qc_original_store.cpp"
   "$program/quality_control/qc_sample_reference_guard.cpp" "$program/quality_control/qc_history_core.cpp"
   "$program/quality/statistics.cpp" "$program/samples/sample_qc_preset.cpp"
   "$program/calibration/calibration_store.cpp" "$program/calibration/calibration_original_store.cpp")
  headers=("$program/quality_control/qc_store.h" "$program/calibration/calibration_store.h")
  modules=(QtCore QtSql)
  libraries=(-lQt5Sql -lQt5Core)
  ;;
 management)
  target=original_management_rk_driver
  project_file=$program/tests/$target.pro
  sources=("$program/tests/$target.cpp" "$program/data/hospital_store.cpp" "$program/data/account_store.cpp"
   "$program/instrument/maintenance_store.cpp" "$program/alarm_history/alarm_history_store.cpp"
   "$program/alarm_history/original_alarm_events.cpp" "$program/original_files/original_ini.cpp"
   "$program/original_files/original_business_database.cpp" "$program/original_files/original_system_profile.cpp"
   "$program/protocol/read_only_messages.cpp" "$program/protocol/status_session.cpp" "$program/protocol/frame_codec.cpp")
  headers=("$program/data/hospital_store.h" "$program/data/account_store.h"
   "$program/instrument/maintenance_store.h" "$program/alarm_history/alarm_history_store.h")
  modules=(QtCore QtSql QtNetwork)
  libraries=(-lQt5Network -lQt5Sql -lQt5Core)
  ;;
 *) printf '不支持的独立驱动种类\n' >&2; exit 2 ;;
esac
build=$task/${mode}_build
mkdir -p "$build"
exec 9>"$task/core_build.lock"; flock -n 9
flags=(-std=c++14 -O2 -g -DQT_NO_DEBUG -DQT_CORE_LIB -DQT_SQL_LIB -fPIC -pthread -Wall -Wextra
 -iquote "$program" -iquote "$build" -isystem "$qt")
for module in "${modules[@]}"; do flags+=(-isystem "$qt/$module"); done
if [[ $mode == management ]]; then flags+=(-DQT_NETWORK_LIB); fi
printf '%s\n' "${sources[@]}" > "$build/sources.txt"
printf '%s\n' "${headers[@]}" > "$build/moc_headers.txt"
export CORE_BUILD_PROGRAM=$program CORE_BUILD_ROOT=$build CORE_PROJECT_FILE=$project_file
python3 - <<'PY'
import os,pathlib,re,hashlib
p=pathlib.Path(os.environ['CORE_BUILD_PROGRAM']); b=pathlib.Path(os.environ['CORE_BUILD_ROOT'])
pending=[pathlib.Path(n) for n in (b/'sources.txt').read_text().splitlines()]
pending.extend(pathlib.Path(n) for n in (b/'moc_headers.txt').read_text().splitlines())
pending.append(pathlib.Path(os.environ['CORE_PROJECT_FILE']))
seen=set()
while pending:
 f=pending.pop().resolve()
 if f in seen: continue
 seen.add(f)
 if f.suffix not in ['.h','.cpp','.moc']: continue
 for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', f.read_text(encoding='utf-8-sig'), re.M):
  found=next((candidate.resolve() for candidate in [f.parent/name,p/name,b/name] if candidate.is_file()),None)
  if found is None: raise RuntimeError('Missing quoted include: '+str(f)+' -> '+name)
  pending.append(found)
with (b/'inputs.SHA256SUMS').open('w') as out:
 for f in sorted(seen): out.write(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+str(f)+'\n')
print('FROZEN_INPUTS',len(seen))
PY
tool_inputs=("$(readlink -f "$(command -v aarch64-linux-gnu-g++)")" /usr/lib/qt5/bin/moc)
for module in "${modules[@]}"; do tool_inputs+=("$(readlink -f "$lib/lib${module/Qt/Qt5}.so")"); done
sha256sum "${tool_inputs[@]}" > "$build/toolchain.SHA256SUMS"
for header in "${headers[@]}"; do
 generated=$build/moc_$(basename "${header%.h}").cpp
 /usr/lib/qt5/bin/moc -I"$qt" -I"$program" "$header" -o "$generated"
 sources+=("$generated")
 sha256sum "$generated" >> "$build/inputs.SHA256SUMS"
done
sha256sum --quiet -c "$build/inputs.SHA256SUMS"
printf '%s\n' "${sources[@]}" > "$build/sources.txt"
printf '%s\n' "${flags[@]}" > "$build/compile_flags.txt"
objects=()
for index in "${!sources[@]}"; do
 object=$build/obj_$index.o
 printf 'COMPILE %s/%s %s\n' "$((index+1))" "${#sources[@]}" "$(basename "${sources[index]}")"
 aarch64-linux-gnu-g++ "${flags[@]}" -c "${sources[index]}" -o "$object"
 objects+=("$object")
done
sha256sum --quiet -c "$build/inputs.SHA256SUMS"
sha256sum --quiet -c "$build/toolchain.SHA256SUMS"
output=$build/$target
aarch64-linux-gnu-g++ "${objects[@]}" -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" \
 -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
 "${libraries[@]}" -lpthread -o "$output.new"
sha256sum --quiet -c "$build/inputs.SHA256SUMS"
sha256sum --quiet -c "$build/toolchain.SHA256SUMS"
readelf -h "$output.new" | tee "$build/elf.txt"
readelf -h "$output.new" | grep -q 'Machine:.*AArch64'
readelf -d "$output.new" > "$build/dynamic.txt"
chmod 700 "$output.new"; mv "$output.new" "$output"
sha256sum "$output" > "$build/binary.SHA256SUMS"
cat "$build/binary.SHA256SUMS"
