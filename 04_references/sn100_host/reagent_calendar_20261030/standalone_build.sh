#!/usr/bin/env bash
# 独立试剂入口仅做ARM64编译检查，不运行电脑版本。
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../.." && pwd)
program=$(cat "$here/source_cache_path")
export SN100_ARM64_SYSROOT=$root/temp/sn100_host/original_db_20261027/integration/release_work_02/sysroot
export SN100_ARM64_BUILD_DIR=$here/standalone_arm64_build_02
set +e
bash "$program/reagents/build.sh" "$here/build/sn100_reagents" > "$here/standalone_build.log" 2>&1
code=$?
set -e
printf '%s\n' "$code" > "$here/standalone_exit_code"
exit "$code"
