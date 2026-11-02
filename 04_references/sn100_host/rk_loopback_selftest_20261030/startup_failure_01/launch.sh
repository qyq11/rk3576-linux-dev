#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
host=lckfb@192.168.100.123;opts=(-o BatchMode=yes -o ConnectTimeout=8)
bash "$here/preflight.sh"
run=$(ssh -T "${opts[@]}" "$host" 'umask 077; mktemp -d /tmp/sn100-loopback-20261030.XXXXXX')
[[ $run =~ ^/tmp/sn100-loopback-20261030\.[a-zA-Z0-9]{6}$ ]]
printf '%s\n' "$run" > "$here/remote_path"
scp -B "${opts[@]}" "$here/board_run.sh" "$host:$run/runner.sh"
runner_sha=$(sha256sum "$here/board_run.sh" | cut -d' ' -f1)
ssh -T "${opts[@]}" "$host" bash -s -- "$run" "$runner_sha" <<'REMOTE'
set -Eeuo pipefail
run=$1
test "$(sha256sum "$run/runner.sh" | cut -d' ' -f1)" = "$2"
sudo -n systemd-run --quiet --collect --unit=sn100-loopback-20261030 --property=Type=exec \
    /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u "$(id -un)" -- /bin/bash "$run/runner.sh" "$run"
REMOTE
printf 'RUN_STARTED=%s\n' "$run"
