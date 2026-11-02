#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python3 "$here/check_stability.py"
bash "$here/deploy.sh" 2>&1 | tee "$here/deploy.log"
