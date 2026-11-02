#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export SN100_RESUME_MAIN_WORK=$here SN100_RESUME_TEST_TARGET=original_main_task_chain_rk_test
bash "$here/build_driver.sh" > "$here/rebuild_driver_02.log" 2>&1
