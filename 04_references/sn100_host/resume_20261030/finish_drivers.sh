#!/usr/bin/env bash
# 主程序已全量编译通过，测试入口修正后重新构建并核245对象身份，不改产品输入。
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python3 "$here/check_stability.py"
for target in original_quality_pages_rk_test original_main_task_chain_rk_test original_settings_pages_rk_test original_history_rk_test; do
    SN100_RESUME_MAIN_WORK=$here SN100_RESUME_TEST_TARGET=$target bash "$here/build_driver.sh" > "$here/${target}_build.log" 2>&1
done
printf 'FULL_ARM64_AND_DRIVERS_OK=1\n'
