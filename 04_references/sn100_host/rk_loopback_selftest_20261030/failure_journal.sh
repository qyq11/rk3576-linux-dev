#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ssh -T -o BatchMode=yes -o ConnectTimeout=8 lckfb@192.168.100.123 bash -s > "$here/startup_failure_01/supervisor_journal.txt" <<'REMOTE'
journalctl --no-pager -u sn100-loopback-20261030 --since '2026-10-30 13:42:00' --until '2026-10-30 13:43:00' -o short-iso
REMOTE
