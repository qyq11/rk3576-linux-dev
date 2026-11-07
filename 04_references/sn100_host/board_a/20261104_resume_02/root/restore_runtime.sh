#!/usr/bin/env bash
set -Eeuo pipefail
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$work/../../.." && pwd)
export SN100_RK_WORK_DIR=$work
set +e
bash "$root/06_programs/sn100_host/shell/prepare_runtime.sh" "$work/sysroot" > "$work/runtime_restore.log" 2>&1
result=$?
set -e
printf '%s\n' "$result" > "$work/runtime_restore.exit"
tail -n 5 "$work/runtime_restore.log"
exit "$result"
