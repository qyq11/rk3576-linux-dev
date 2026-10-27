#!/usr/bin/env bash
set -Eeuo pipefail
local_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
binary=$local_dir/project_config_store_rk_driver
host=lckfb@192.168.100.123
remote=/home/rkshare/programs/sn100_shell/verification/project_config_store_04
sha=$(sha256sum "$binary" | cut -d' ' -f1)
opts=(-o BatchMode=yes -o ConnectTimeout=8)
ssh "${opts[@]}" "$host" "test ! -e $remote && mkdir -p $remote"
scp -B "${opts[@]}" "$binary" "$host:$remote/driver"
ssh "${opts[@]}" "$host" bash -s -- "$remote" "$sha" <<'REMOTE'
set -u
run=$1; expected=$2
cd "$run" || exit 2
test "$(sha256sum driver | cut -d' ' -f1)" = "$expected" || exit 3
chmod 700 driver
release=$(readlink -f /home/rkshare/programs/sn100_shell/current)
export LD_LIBRARY_PATH=$release/lib
export QT_PLUGIN_PATH=$release/plugins:/usr/lib/aarch64-linux-gnu/qt5/plugins
printf 'ELF_MACHINE=' > dependency.txt
od -An -tu2 -j18 -N2 driver >> dependency.txt
test "$(od -An -tu2 -j18 -N2 driver | tr -d ' ')" = 183 || exit 4
ldd driver >> dependency.txt
cat dependency.txt
if grep -q 'not found' dependency.txt; then exit 4; fi
printf 'APP_SHA256=%s\nOLD_RELEASE=%s\nDATA_ROOT=%s\n' "$expected" "$release" "$run/data" > runtime.txt
if [[ -x /usr/bin/time ]]; then
    /usr/bin/time -v -o resources.txt timeout 60 ./driver --data-dir "$run/data" > app.log 2>&1
else
    timeout 60 ./driver --data-dir "$run/data" > app.log 2>&1
fi
code=$?
printf '%s\n' "$code" > exit_code
cat app.log
printf 'ACTUAL_EXIT_CODE=%s\n' "$code"
exit 0
REMOTE
mkdir -p "$local_dir/store_rk_04"
for name in dependency.txt runtime.txt app.log exit_code; do
    scp -B "${opts[@]}" "$host:$remote/$name" "$local_dir/store_rk_04/$name" || true
done
test "$(cat "$local_dir/store_rk_04/exit_code")" = 0
