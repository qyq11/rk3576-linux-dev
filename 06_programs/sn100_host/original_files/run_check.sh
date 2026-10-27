#!/usr/bin/env bash
# 在RK只读检查部署副本。输出只含文件指纹、表数量和格式状态。
set -Eeuo pipefail
[[ $# == 2 && $1 == /* && $2 == /* ]] || { echo 'Usage: run_check.sh ABS_LOCAL_BINARY ABS_LOCAL_RESULT_DIR' >&2; exit 2; }
binary=$1; output=$2
host=${RK_HOST:-lckfb@192.168.100.123}
base=${SN100_ORIGINAL_REMOTE_BASE:-/home/rkshare/programs/sn100_original/20261027_01}
[[ $host =~ ^[a-zA-Z0-9_.@-]+$ && $base =~ ^/home/rkshare/programs/sn100_original/[a-zA-Z0-9_-]+$ ]] || exit 2
opts=(-o BatchMode=yes -o ConnectTimeout=8)
sha=$(sha256sum "$binary" | cut -d' ' -f1)
scp -B "${opts[@]}" "$binary" "$host:$base/original_files_check"
ssh "${opts[@]}" "$host" bash -s -- "$base" "$sha" <<'REMOTE'
set -Eeuo pipefail
base=$1; expected=$2
test "$(sha256sum "$base/original_files_check" | cut -d' ' -f1)" = "$expected"
test "$(od -An -tu2 -j18 -N2 "$base/original_files_check" | tr -d ' ')" = 183
chmod 700 "$base/original_files_check"
release=$(readlink -f /home/rkshare/programs/sn100_shell/current)
export LD_LIBRARY_PATH=$release/lib QT_PLUGIN_PATH=$release/plugins:/usr/lib/aarch64-linux-gnu/qt5/plugins
ldd "$base/original_files_check" > "$base/check_dependencies.txt"
! grep -q 'not found' "$base/check_dependencies.txt"
for name in baseline working; do
    set +e
    "$base/original_files_check" "$base/$name" "$base/original_manifest.json" "$base/${name}_check.json" > "$base/${name}_check.log" 2>&1
    code=$?
    set -e
    printf '%s\n' "$code" > "$base/${name}_exit_code"
    tail -2 "$base/${name}_check.log"
    printf '%s_ACTUAL_EXIT_CODE=%s\n' "$name" "$code"
    test "$code" = 0
    (cd "$base/$name" && sha256sum --quiet -c "$base/original.SHA256SUMS")
done
printf 'ALL_ORIGINAL_BYTES_STILL_MATCH=1\n'
REMOTE
mkdir -p "$output"
for name in baseline_check.json working_check.json baseline_check.log working_check.log baseline_exit_code working_exit_code check_dependencies.txt; do
    scp -B "${opts[@]}" "$host:$base/$name" "$output/$name"
done
