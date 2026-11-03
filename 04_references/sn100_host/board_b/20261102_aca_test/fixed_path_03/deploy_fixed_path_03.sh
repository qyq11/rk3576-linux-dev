#!/usr/bin/env bash
# 将原配套包放到程序写定的实际目录，核对文件后限时启动；保留本轮目录供后续使用。
set -Eeuo pipefail
umask 027
test "$(id -u)" = 0
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/aca_flow_test
run=$base/runs/fixed_path_03
shared=/userdata/shared
test "$(readlink -f /userdata)" = /userdata
test ! -e "$shared" && test ! -L "$shared"
test ! -e "$run"
test "$(sha256sum "$base/SN500_run_input.tar.gz" | cut -d' ' -f1)" = a94b4cf674ebdd5b7738a2ed02255d8864da6a0389860feb73264b349f66d956
test "$(wc -l <"$base/SN500_expected_43.sha256")" = 43
install -d -m 750 -o lckfb -g lckfb "$run"
{
    printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
    date -Is
    id
    printf 'SHARED_PATH_ABSENT_BEFORE=1\n'
    ip -br a
    ss -lntp
    ps -eo pid,user,args | grep -E '[A]CA_V1|[s]n100_shell' || true
} >"$run/before.log"
install -d -m 750 -o lckfb -g lckfb "$shared"
runuser -u lckfb -- tar --no-same-owner --no-same-permissions -xzf "$base/SN500_run_input.tar.gz" -C "$shared"
chmod 750 "$shared/ACA_V1.0OffLineD-500"
cd "$shared"
sha256sum -c "$base/SN500_expected_43.sha256" >"$run/files_before.log"
test "$(find "$shared" -type f | wc -l)" = 43
test ! -L "$shared"
runuser -u lckfb -- test -r "$shared/config_linux.ini"
runuser -u lckfb -- test -r "$shared/reagent_Setting.db"
runuser -u lckfb -- test -d "$shared/public_operation500"
{
    printf 'SHARED_REAL_PATH='; readlink -f "$shared"
    printf 'SHARED_IS_SYMLINK=0\nEXPECTED_FILES_MATCHED=43\nCONFIG_READABLE_AS_LCKFB=1\n'
    stat -c '%n %F %U:%G %a %s' "$shared" "$shared/config_linux.ini" "$shared/ACA_V1.0OffLineD-500"
    printf 'BINARY_SHA256='; sha256sum "$shared/ACA_V1.0OffLineD-500"
    printf 'CONFIG_SHA256='; sha256sum "$shared/config_linux.ini"
    printf 'METHOD=direct_as_lckfb_actual_directory_10_second_limit\n'
} >"$run/path_check.log"
runuser -u lckfb -- env LD_LIBRARY_PATH="$shared" ldd "$shared/ACA_V1.0OffLineD-500" >"$run/dependencies.log" 2>&1
if grep -q 'not found' "$run/dependencies.log"; then
    printf 'DEPENDENCY_CHECK_FAILED=1\n' >"$run/result.txt"
    exit 1
fi
set +e
runuser -u lckfb -- env LD_LIBRARY_PATH="$shared" \
    strace -f -tt -e trace=execve,openat,exit_group -o "$run/syscalls.log" \
    timeout --signal=TERM --kill-after=2s 10s \
    /usr/bin/stdbuf -oL -eL ./ACA_V1.0OffLineD-500 >"$run/program.log" 2>&1
code=$?
set -e
printf 'PROGRAM_EXIT=%s\nSHARED_DIRECTORY_RETAINED=1\n' "$code" >"$run/result.txt"
if grep -E 'openat\(.*"/userdata/shared/config_linux.ini".* = [0-9]+' "$run/syscalls.log"; then
    printf 'PROGRAM_OPENED_CONFIG=1\n' >>"$run/result.txt"
else
    printf 'PROGRAM_OPENED_CONFIG=0\n' >>"$run/result.txt"
fi
ss -lntp >"$run/listeners_after.log"
if ss -H -lnt 'sport = :5000' | grep -q .; then
    printf 'LISTEN_5000_AFTER=1\n' >>"$run/result.txt"
else
    printf 'LISTEN_5000_AFTER=0\n' >>"$run/result.txt"
fi
set +e
sha256sum -c "$base/SN500_expected_43.sha256" >"$run/files_after.log" 2>&1
files_code=$?
set -e
printf 'ORIGINAL_FILE_CHECK_AFTER_EXIT=%s\n' "$files_code" >>"$run/result.txt"
find "$shared" -type f -printf '%P\n' | sort >"$run/files_after.txt"
ps -eo pid,user,args >"$run/processes_after.log"
cat "$run/path_check.log" "$run/result.txt"
tail -25 "$run/program.log"
