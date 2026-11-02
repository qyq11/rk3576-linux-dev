#!/usr/bin/env bash
# 本轮从全新目录完整编译，任何失败都不使用旧产物。
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../.." && pwd)
python3 "$here/freeze_build.py"
program=$(cat "$here/source_cache_path")
export SN100_ARM64_SYSROOT=$root/temp/sn100_host/original_db_20261027/integration/release_work_02/sysroot
export SN100_ARM64_BUILD_DIR=$here/arm64_build SN100_JOBS=2
mkdir "$here/build"
set +e
bash "$program/shell/build.sh" "$here/build/sn100_shell" > "$here/build.log" 2>&1
code=$?
set -e
printf '%s\n' "$code" > "$here/build_exit_code"
test "$code" = 0
python3 "$program/shell/check_arm64.py" "$here/build/sn100_shell" --additional-binary "$here/build/sn100_report_renderer" --sysroot "$SN100_ARM64_SYSROOT" --output-dir "$here/dependencies"
python3 "$program/shell/package_arm64.py" package --binary "$here/build/sn100_shell" --sysroot "$SN100_ARM64_SYSROOT" --snapshot "$here/source_snapshot.json" --dependencies "$here/dependencies/dependency_report.json" --output "$here/packages"
for target in original_main_task_chain_rk_test; do
    SN100_RESUME_MAIN_WORK=$here SN100_RESUME_TEST_TARGET=$target bash "$here/build_driver.sh" > "$here/${target}_build.log" 2>&1
done
printf 'FULL_ARM64_AND_DRIVERS_OK=1\n'
