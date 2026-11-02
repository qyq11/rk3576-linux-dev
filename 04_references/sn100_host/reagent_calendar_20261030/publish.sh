#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python3 "$here/prepare_deploy.py" > "$here/prepare_deploy.log" 2>&1
bash "$here/deploy.sh" > "$here/deploy.log" 2>&1
bash "$here/run_installed.sh" > "$here/installed_check.log" 2>&1
bash "$here/final_board.sh" > "$here/final_verify.log" 2>&1
printf 'PUBLISHED_AND_INSTALLED_CHECKED=1\n'
