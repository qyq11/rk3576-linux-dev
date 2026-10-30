#!/usr/bin/env bash
# 收尾本次私有失败目录并只读登记现场状态，不改部署或现场资料。
set -Eeuo pipefail
task=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
run=$(cat "$task/management_01/remote_temporary_path")
[[ $run =~ ^/tmp/sn100-original-data-test\.[a-zA-Z0-9]{6}$ ]]
host=lckfb@192.168.100.123; opts=(-o BatchMode=yes -o ConnectTimeout=8)
ssh "${opts[@]}" "$host" bash -s -- "$run" > "$task/management_02/final_board_context.txt" <<'REMOTE'
set -Eeuo pipefail
run=$1
test "$(readlink -f "$run")" = "$run"
case "$run" in /tmp/sn100-original-data-test.??????) ;; *) exit 2;; esac
test "$(sha256sum "$run/driver" | cut -d' ' -f1)" = 03365e6acd9931fc61140f368ec8ba88eb8dd18cdb4babce5e0e1b8811538e73
test "$(cat "$run/exit_code")" = 1
test "$(cat "$run/data/.sn100-original-work-v1")" = sn100-original-work-v1
if pgrep -f "^$run/driver([[:space:]]|$)" >/dev/null; then exit 2; fi
rm -rf --one-file-system -- "$run"
test ! -e "$run"
printf 'FAILED_PRIVATE_DATA_REMOVED=1\n'
base=/home/rkshare/programs/sn100_shell
printf 'BOARD_UTC=%s\nARCH=%s\nCURRENT_RELEASE=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$(uname -m)" "$(readlink -f "$base/current")"
sha256sum "$base/current/sn100_shell" "$base/current/sn100_report_renderer"
printf 'SHELL_PIDS=%s\n' "$(pgrep -u "$(id -u)" -x sn100_shell || true)"
printf 'DATA_DRIVER_PIDS=%s\n' "$(pgrep -u "$(id -u)" -f '/tmp/sn100-original-data-test\.[a-zA-Z0-9]+/driver' || true)"
if [[ -f $base/lifecycle.lock ]] && flock -n "$base/lifecycle.lock" true; then
 printf 'LIFECYCLE_LOCK_AVAILABLE=1\n'
else
 printf 'LIFECYCLE_LOCK_AVAILABLE=0\n'
fi
for device in /dev/tty7 /dev/dri/card0; do
 if [[ -e $device ]]; then
  printf 'OWN_USER_OPENERS_%s=' "$device"
  fuser "$device" 2>/dev/null || true
  printf '\n'
 fi
done
printf 'DISPLAY_NOT_ACQUIRED_BY_THIS_TEST=1\nFORMAL_RUNTIME_NOT_WRITTEN_BY_THIS_TEST=1\n'
REMOTE
ps -C cc1plus -o pid,args > "$task/management_02/local_compiler_processes.txt" || test $? = 1
printf 'HOSPITAL_FINISH_CONTEXT_RECORDED=1\n'
