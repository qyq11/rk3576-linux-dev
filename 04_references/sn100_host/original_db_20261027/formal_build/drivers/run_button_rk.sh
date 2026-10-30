#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../../.." && pwd)
mode=$1; name=$2
case $mode in core) binary=$here/device_control_rk_test ;; connection) binary=$here/instrument_connection_rk_test ;; shutdown) binary=$here/shutdown_button_rk_test ;; *) exit 2 ;; esac
[[ $name =~ ^page_[0-9]+$ ]]
run=/home/rkshare/programs/sn100_original/20261027_01/verification/$name
host=lckfb@192.168.100.123
opts=(-o BatchMode=yes -o ConnectTimeout=8)
testlib=$root/temp/sn100_host/rk_prepare/sysroot/usr/lib/aarch64-linux-gnu/libQt5Test.so.5.15.13
sha=$(sha256sum "$binary" | cut -d' ' -f1); libsha=$(sha256sum "$testlib" | cut -d' ' -f1)
runner_sha=$(sha256sum "$here/button_cases_board_runner.sh" | cut -d' ' -f1)
ssh "${opts[@]}" "$host" "umask 077; test ! -e $run && mkdir -p $run/data $run/lib"
scp -B "${opts[@]}" "$binary" "$host:$run/driver"
scp -B "${opts[@]}" "$testlib" "$host:$run/lib/libQt5Test.so.5"
scp -B "${opts[@]}" "$here/button_cases_board_runner.sh" "$host:$run/runner.sh"
ssh "${opts[@]}" "$host" bash -s -- "$run" "$sha" "$libsha" "$runner_sha" "$mode" <<'REMOTE'
set -Eeuo pipefail
run=$1; sha=$2; libsha=$3; runner_sha=$4; mode=$5
test "$(sha256sum "$run/driver" | cut -d' ' -f1)" = "$sha"
test "$(sha256sum "$run/lib/libQt5Test.so.5" | cut -d' ' -f1)" = "$libsha"
test "$(sha256sum "$run/runner.sh" | cut -d' ' -f1)" = "$runner_sha"
test "$(od -An -tu2 -j18 -N2 "$run/driver" | tr -d ' ')" = 183
chmod 700 "$run/driver"
release=$(readlink -f /home/rkshare/programs/sn100_shell/current)
export LD_LIBRARY_PATH=$run/lib:$release/lib
export QT_PLUGIN_PATH=$release/plugins:/usr/lib/aarch64-linux-gnu/qt5/plugins
ldd "$run/driver" > "$run/dependencies.txt"
! grep -q 'not found' "$run/dependencies.txt"
if [[ $mode == core ]]; then
    set +e
    cd "$run"
    timeout 90 ./driver -v1 > app.log 2>&1
    code=$?
    printf '%s\n' "$code" > exit_code
    tail -4 app.log
    printf 'ACTUAL_EXIT_CODE=%s\n' "$code"
    exit 0
fi
! systemctl is-active --quiet sn100-shell.service
owners=$(sudo -n fuser /dev/tty7 /dev/dri/card0 2>&1) && occupied=1 || occupied=0
[[ $occupied == 0 && -z $owners ]]
sudo -n systemd-run --quiet --collect --unit=sn100-original-page-test --property=Type=exec \
    /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u "$(id -un)" -- /bin/bash "$run/runner.sh" "$run" "$mode"
printf 'STARTED_BUTTON_TEST=%s\n' "$run"
REMOTE
