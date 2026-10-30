#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
work=$root/temp/sn100_host/all_buttons_audit_20261027
host=lckfb@192.168.100.123
batch=${1:?提供全新批次目录名}
[[ $batch =~ ^button_audit_[0-9][0-9]$ ]]
profile=${2:-generic}
[[ $profile == generic || $profile == original ]]
remote=/home/rkshare/programs/sn100_shell/verification/$batch
opts=(-o BatchMode=yes -o ConnectTimeout=6)
ssh "${opts[@]}" "$host" bash -s -- "$remote" <<'REMOTE'
set -Eeuo pipefail
remote=$1
base=/home/rkshare/programs/sn100_shell
expected=f8c6e73f59f458981e243af5dd00999c12a671f449f0daaf3f4ff70e82d8a11f
test "$(readlink -f "$base/current")" = "$base/releases/$expected"
test ! -e "$remote"
mkdir -m 700 "$remote"
readlink -f "$base/current" > "$remote/release_path"
REMOTE
scp "${opts[@]}" "$work/libbutton_audit.so" "$work/board_run.sh" "$host:$remote/"
printf '%s\n' "$profile" | ssh "${opts[@]}" "$host" "cat > $remote/profile"
if [[ $profile == original ]]; then
  scp -r "${opts[@]}" "$work/original_fixture" "$host:$remote/"
fi
local_sha=$(sha256sum "$work/libbutton_audit.so" | cut -d' ' -f1)
board_sha=$(ssh "${opts[@]}" "$host" "sha256sum $remote/libbutton_audit.so" | cut -d' ' -f1)
test "$local_sha" = "$board_sha"
printf 'UPLOADED_AUDIT_SHA256=%s\n' "$local_sha"
ssh "${opts[@]}" "$host" bash -s -- "$remote" "$batch" <<'REMOTE'
set -Eeuo pipefail
remote=$1
batch=$2
if sudo -n fuser /dev/tty7 /dev/dri/card0 2>/dev/null; then echo DISPLAY_IN_USE; exit 4; fi
sudo -n systemd-run --quiet --collect --unit="sn100-$batch" --property=Type=exec /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u lckfb -- /bin/bash "$remote/board_run.sh"
echo AUDIT_STARTED=1
REMOTE
