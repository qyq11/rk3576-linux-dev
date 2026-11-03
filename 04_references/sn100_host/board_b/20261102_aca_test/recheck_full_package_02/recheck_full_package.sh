#!/usr/bin/env bash
# 用完整原包直接复测；仅使用本轮新目录，结束后移除本轮创建的路径映射。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(id -un)" = lckfb
base=/home/rkshare/programs/aca_flow_test
run=$base/runs/probe_02_full_package
test ! -e "$run"
test ! -e /userdata/shared && test ! -L /userdata/shared
test ! -e /etc/init.d/key.ini
mkdir -m 700 "$run"
mkdir -m 700 "$run/runtime"
tar --no-same-owner --no-same-permissions -xzf "$base/SN500_run_input.tar.gz" -C "$run/runtime"
binary=$run/runtime/ACA_V1.0OffLineD-500
test "$(sha256sum "$binary" | cut -d' ' -f1)" = bfb23e400584ee9f3b44eee2680fdd8eff85c51729963510f8612c71926ae33f
chmod 700 "$binary"
cleanup()
{
    if [[ -L /userdata/shared && $(readlink /userdata/shared) == "$run/runtime" ]]; then
        sudo -n unlink /userdata/shared
    fi
}
trap cleanup EXIT
sudo -n ln -s "$run/runtime" /userdata/shared
{
    printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
    date -Is
    id
    printf 'METHOD=direct_as_lckfb_full_package_no_systemd_sandbox\n'
    printf 'KEY_FILE_EXISTS=0\n'
    printf 'INPUT_TAR_SHA256='; sha256sum "$base/SN500_run_input.tar.gz"
    printf 'BINARY_SHA256='; sha256sum "$binary"
    printf 'INPUT_FILE_COUNT='; find "$run/runtime" -type f | wc -l
    ss -lntp
} > "$run/before.log"
cd /userdata/shared
set +e
LD_LIBRARY_PATH="$run/runtime" strace -f -e trace=execve,openat,exit_group \
    -o "$run/syscalls.log" timeout --signal=TERM --kill-after=2s 10s \
    /usr/bin/stdbuf -oL -eL ./ACA_V1.0OffLineD-500 > "$run/program.log" 2>&1
code=$?
set -e
printf 'DIRECT_RUN_EXIT=%s\n' "$code" > "$run/result.txt"
ss -lntp > "$run/listeners_after.log"
cleanup
trap - EXIT
printf 'MAPPING_REMOVED=%s\n' "$([[ ! -e /userdata/shared && ! -L /userdata/shared ]] && echo 1 || echo 0)" >> "$run/result.txt"
cat "$run/before.log" "$run/result.txt"
tail -35 "$run/program.log"
grep -E 'key.ini|config_linux.ini|exit_group' "$run/syscalls.log" | tail -12
