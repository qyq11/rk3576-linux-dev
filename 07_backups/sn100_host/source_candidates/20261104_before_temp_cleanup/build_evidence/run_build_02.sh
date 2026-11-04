#!/usr/bin/env bash
set -uo pipefail
root='/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
task_dir="$root/temp/sn100_host/formal_merge_20261104_01"
date -u +%FT%TZ > "$task_dir/start_02_utc.txt"
SN100_ARM64_SYSROOT="$task_dir/sysroot" SN100_ARM64_BUILD_DIR="$task_dir/arm64_build_02" SN100_JOBS=2 RK_BUILD_TYPE=Release bash "$root/06_programs/sn100_host/shell/build.sh" "$task_dir/bin_02/sn100_shell" > "$task_dir/build_02.stdout" 2> "$task_dir/build_02.stderr"
result=$?
printf '%s\n' "$result" > "$task_dir/build_02.exit"
date -u +%FT%TZ > "$task_dir/end_02_utc.txt"
exit "$result"
