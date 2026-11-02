#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
run=$(cat "$here/remote_path")
ssh -T -o BatchMode=yes -o ConnectTimeout=8 lckfb@192.168.100.123 bash -s -- "$run" > "$here/board_after.txt" <<'REMOTE'
set -Eeuo pipefail
run=$1;base=/home/rkshare/programs/sn100_shell
[[ $run =~ ^/tmp/sn100-loopback-20261030\.[a-zA-Z0-9]{6}$ ]]
date -Is
release=$(readlink -f "$base/current")
test "${release##*/}" = ef294a921e7eec2d293beb5aa0fbe5784d6b5056cef92648e799c3529b839a14
source "$release/rk_board.sh";verify_release "$release"
sha256sum "$release/sn100_shell" "$base/sn100.sh"
test -z "$(pgrep -x sn100_shell || true)"
flock -n "$base/lifecycle.lock" -c true
owners=$(sudo -n fuser /dev/tty7 /dev/dri/card0 2>&1) && occupied=1 || occupied=0
[[ $occupied == 0 && -z $owners ]]
test ! -e "$run/bootstrap_secret"
printf 'CURRENT_UNCHANGED=1\nPRODUCT_PROCESSES=0\nDISPLAY_FREE=1\nLIFECYCLE_FREE=1\nTEMP_SECRET_REMOVED=1\n'
REMOTE
cat "$here/board_after.txt"
