#!/usr/bin/env bash
set -Eeuo pipefail
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$work/../../.." && pwd)
export SN100_ARM64_SYSROOT=$root/temp/sn100_host/resume_root_20261104_02/sysroot
export SN100_ARM64_BUILD_DIR=$work/generated
export SN100_CANDIDATE_FLAGS_OUTPUT=$work/compile_context.json
export RK_BUILD_TYPE=Release
set +e
bash "$work/prepare_headers.sh" "$work/unused_output/sn100_shell" > "$work/prepare_headers.log" 2>&1
result=$?
set -e
printf '%s\n' "$result" > "$work/prepare_headers.exit"
tail -n 2 "$work/prepare_headers.log"
exit "$result"
