#!/usr/bin/env bash
# 使用落地脚本保存退出码，避免Windows命令行再次解释美元符号。
set -uo pipefail
task=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mode=${1:?指定qc_calibration或management}
bash "$task/build_original_core_driver.sh" "$mode" 2>&1 | tee "$task/${mode}_compile.log"
status=${PIPESTATUS[0]}
printf 'BUILD_EXIT=%s\n' "$status" | tee -a "$task/${mode}_compile.log"
exit "$status"
