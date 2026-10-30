#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../../.." && pwd)
build=$here/button_core_build; sysroot=$here/release_work_02/sysroot
source=$root/06_programs/sn100_host/instrument/device_control.cpp
export CORE_FIX_BUILD=$build CORE_FIX_SOURCE=$source
python3 - <<'PY'
import os,pathlib,hashlib
b=pathlib.Path(os.environ['CORE_FIX_BUILD']);source=os.environ['CORE_FIX_SOURCE']
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
assert sha(source)=='552aa59babf06868be6b3582a7318563b596e9c3461bdd2bbcf72800f6e75030'
for line in (b/'inputs.SHA256SUMS').read_text().splitlines():
 digest,name=line.split('  ',1)
 if name!=source:assert sha(name)==digest,name
rows=[l.split('\t') for l in (b/'object_manifest.tsv').read_text().splitlines()]
for src,digest,obj,objdigest in rows:
 assert sha(obj)==objdigest,obj
 if src!=source:assert sha(src)==digest,src
assert rows[1][0]==source
(b/'fixed_objects.nul').write_bytes(b''.join(row[2].encode()+b'\0' for row in rows))
PY
mapfile -t flags < "$build/compile_flags.txt"
aarch64-linux-gnu-g++ "${flags[@]}" -c "$source" -o "$build/obj_1.o"
mapfile -d '' -t objects < "$build/fixed_objects.nul"
lib=$sysroot/usr/lib/aarch64-linux-gnu
aarch64-linux-gnu-g++ "${objects[@]}" -L"$lib" -L"$sysroot/lib/aarch64-linux-gnu" \
 -Wl,-rpath-link,"$lib" -Wl,-rpath-link,"$sysroot/lib/aarch64-linux-gnu" \
 -lQt5Test -lQt5Network -lQt5Core -lpthread -o "$here/device_control_rk_test.new"
mv "$here/device_control_rk_test.new" "$here/device_control_rk_test"
sha256sum "$here/device_control_rk_test"
