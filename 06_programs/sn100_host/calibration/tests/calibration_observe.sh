#!/usr/bin/env bash
# Reusable entry for the integration window, after competing builds have ended.
# This script does not start any device/network connection or create catalog data.
set -Eeuo pipefail
[[ $# == 3 && $1 == /* && $2 == /* && $3 == /* ]] || {
    echo 'Usage: calibration_observe.sh ABS_DATA_DIR ABS_ANON_CATALOG_DB NEW_ABS_OUTPUT_DIR' >&2; exit 2;
}
module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(cd -- "$module_dir/../../.." && pwd)
binary=$root/temp/sn100_host/calibration_local_20261025/wsl_release/sn100_calibration
[[ -x $binary && -f $2 ]] || { echo 'Build calibration first; provide explicit anonymous catalog snapshot.' >&2; exit 2; }
exec python3 "$module_dir/../tests/observe_linux_process.py" --output-dir "$3" --timeout 45 -- \
    "$binary" --data-dir "$1" --catalog-db "$2" --exit-after-ms 30000
