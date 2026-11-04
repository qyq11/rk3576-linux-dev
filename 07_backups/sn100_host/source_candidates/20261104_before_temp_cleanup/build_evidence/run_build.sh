#!/usr/bin/env bash
set -uo pipefail
root='/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
task_dir="$root/temp/sn100_host/formal_merge_20261104_01"
sysroot="$task_dir/sysroot"
date -u +%FT%TZ > "$task_dir/start_utc.txt"
bash "$root/06_programs/sn100_host/shell/prepare_runtime.sh" "$sysroot" > "$task_dir/restore.stdout" 2> "$task_dir/restore.stderr"
restore_rc=$?
printf '%s\n' "$restore_rc" > "$task_dir/restore.exit"
if [[ $restore_rc != 0 ]]; then exit "$restore_rc"; fi
SN100_ARM64_SYSROOT="$sysroot" SN100_ARM64_BUILD_DIR="$task_dir/arm64_build" SN100_JOBS=2 RK_BUILD_TYPE=Release bash "$root/06_programs/sn100_host/shell/build.sh" "$task_dir/bin/sn100_shell" > "$task_dir/build.stdout" 2> "$task_dir/build.stderr"
build_rc=$?
printf '%s\n' "$build_rc" > "$task_dir/build.exit"
date -u +%FT%TZ > "$task_dir/end_utc.txt"
exit "$build_rc"
