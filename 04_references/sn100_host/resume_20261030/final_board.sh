#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ssh -o BatchMode=yes -o ConnectTimeout=8 lckfb@192.168.100.123 bash -s > "$here/final_board.txt" <<'REMOTE'
set -Eeuo pipefail
date -Is
base=/home/rkshare/programs/sn100_shell
release=$(readlink -f "$base/current")
test "${release##*/}" = a247d2f89c4914655730f61217eb98bbbcfedeb983a14e4c86b0daaec8d90f7e
source "$release/rk_board.sh"; verify_release "$release"
printf 'RELEASE_MANIFEST_OK=1\nCURRENT=%s\nPREVIOUS=' "$release"; cat "$base/previous_release"
sha256sum "$release/sn100_shell" "$release/sn100_report_renderer" "$base/sn100.sh"
test "$(sha256sum "$base/sn100.sh" | cut -d' ' -f1)" = 434c5ad2e71a0854192eba318df43bd4f340f990737a38ff14194a84ea7b2245
test -z "$(pgrep -x sn100_shell || true)"
test -z "$(pgrep -x driver || true)"
flock -n "$base/lifecycle.lock" -c true
owners=$(sudo -n fuser /dev/tty7 /dev/dri/card0 2>&1) && occupied=1 || occupied=0
[[ $occupied == 0 && -z $owners ]]
printf 'PRODUCT_PROCESSES=0\nTEST_DRIVERS=0\nLIFECYCLE_LOCK_FREE=1\nDISPLAY_FREE=1\nLAUNCHER_UNCHANGED=1\n'
REMOTE
cat "$here/final_board.txt"
