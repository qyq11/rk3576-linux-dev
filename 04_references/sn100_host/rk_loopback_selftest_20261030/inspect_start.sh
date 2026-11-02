#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
run=$(cat "$here/remote_path")
ssh -T -o BatchMode=yes -o ConnectTimeout=8 lckfb@192.168.100.123 bash -s -- "$run" <<'REMOTE'
set -Eeuo pipefail
run=$1
cat "$run/app.log"
cat "$run/runtime.txt"
systemctl status sn100-loopback-20261030 --no-pager 2>&1 || true
ls -la "$run"
REMOTE
