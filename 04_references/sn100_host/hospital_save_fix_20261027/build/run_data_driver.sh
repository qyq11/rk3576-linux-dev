#!/usr/bin/env bash
# 只在RK私有目录建立匿名原结构资料；不连接仪器、不改现场部署和原资料。
set -Eeuo pipefail
[[ $# == 4 && $1 =~ ^[a-z_0-9]+$ && $2 =~ ^(management|qc_calibration)$ && $3 == /* && $4 =~ ^[a-f0-9]{64}$ ]] || exit 2
task=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
name=$1; mode=$2; binary=$3; expected=$4
localrun=$task/$name; mkdir "$localrun"
test "$(sha256sum "$binary" | cut -d' ' -f1)" = "$expected"
cp "$binary" "$localrun/driver"
host=lckfb@192.168.100.123; opts=(-o BatchMode=yes -o ConnectTimeout=8)
run=$(ssh "${opts[@]}" "$host" 'umask 077; mktemp -d /tmp/sn100-original-data-test.XXXXXX')
[[ $run =~ ^/tmp/sn100-original-data-test\.[a-zA-Z0-9]{6}$ ]]
printf '%s\n' "$run" > "$localrun/remote_temporary_path"
scp -B "${opts[@]}" "$binary" "$host:$run/driver"
ssh "${opts[@]}" "$host" bash -s -- "$run" "$expected" "$mode" <<'REMOTE'
set -Eeuo pipefail
run=$1; expected=$2; mode=$3
test "$(readlink -f "$run")" = "$run"
case "$run" in /tmp/sn100-original-data-test.??????) ;; *) exit 2;; esac
test "$(sha256sum "$run/driver" | cut -d' ' -f1)" = "$expected"
test "$(od -An -tu2 -j18 -N2 "$run/driver" | tr -d ' ')" = 183
chmod 700 "$run/driver"
release=$(readlink -f /home/rkshare/programs/sn100_shell/current)
export LD_LIBRARY_PATH=$release/lib QT_PLUGIN_PATH=$release/plugins:/usr/lib/aarch64-linux-gnu/qt5/plugins
ldd "$run/driver" > "$run/dependencies.txt"
! grep -q 'not found' "$run/dependencies.txt"
cd "$run"
set +e
if [[ $mode == management ]]; then
 timeout 90 ./driver --data-dir "$run/data" --output-dir "$run/output" > app.log 2>&1
else
 mkdir output
 timeout 90 ./driver "$run/data" "$run/output/verification.json" > app.log 2>&1
fi
code=$?
set -e
printf '%s\n' "$code" > exit_code
printf 'BINARY_SHA256=%s\nACTUAL_EXIT_CODE=%s\nFORMAL_RELEASE=%s\nANONYMOUS_ONLY=1\nNO_GUI_OR_INSTRUMENT=1\n' "$expected" "$code" "${release##*/}" > runtime.txt
tail -7 app.log
printf 'ACTUAL_EXIT_CODE=%s\n' "$code"
REMOTE
for entry in app.log dependencies.txt exit_code runtime.txt; do scp -B "${opts[@]}" "$host:$run/$entry" "$localrun/$entry"; done
scp -B "${opts[@]}" "$host:$run/output/verification.json" "$localrun/verification.json"
test "$(cat "$localrun/exit_code")" = 0
python3 - "$localrun/verification.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['failed']==0 and r['passed']>0,r
print('RK_ANONYMOUS_CHECKS',r['passed'],r['failed'])
PY
ssh "${opts[@]}" "$host" bash -s -- "$run" <<'REMOTE'
set -Eeuo pipefail
run=$1
test "$(readlink -f "$run")" = "$run"
case "$run" in /tmp/sn100-original-data-test.??????) ;; *) exit 2;; esac
test "$(cat "$run/exit_code")" = 0
rm -rf --one-file-system -- "$run"
test ! -e "$run"
REMOTE
printf 'RK_DATA_DRIVER_PASSED=%s\nPRIVATE_TEMP_REMOVED=1\n' "$name"
