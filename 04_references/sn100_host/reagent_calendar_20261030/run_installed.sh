#!/usr/bin/env bash
set -Eeuo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
result=$here/installed_01; mkdir "$result"
host=lckfb@192.168.100.123;opts=(-o BatchMode=yes -o ConnectTimeout=8)
source_run=$(cat "$here/../resume_20261030/quality_02/remote_path")
fixture=$(ssh "${opts[@]}" "$host" "cat '$source_run/output/anonymous_fixture_root.txt'")
run=$(ssh "${opts[@]}" "$host" 'umask 077; mktemp -d /tmp/sn100-calendar-installed-20261030.XXXXXX')
[[ $run =~ ^/tmp/sn100-calendar-installed-20261030\.[a-zA-Z0-9]{6}$ ]]
printf '%s\n' "$run" > "$result/remote_path"
scp -B "${opts[@]}" "$here/installed_board.sh" "$host:$run/runner.sh"
sha=$(sha256sum "$here/build/sn100_shell" | cut -d' ' -f1)
runner=$(sha256sum "$here/installed_board.sh" | cut -d' ' -f1)
ssh "${opts[@]}" "$host" bash -s -- "$run" "$fixture" "$sha" "$runner" <<'REMOTE'
set -Eeuo pipefail
run=$1;fixture=$2;sha=$3
test "$(sha256sum "$run/runner.sh" | cut -d' ' -f1)" = "$4"
test -z "$(pgrep -x sn100_shell || true)"
flock -n /home/rkshare/programs/sn100_shell/lifecycle.lock -c true
owners=$(sudo -n fuser /dev/tty7 /dev/dri/card0 2>&1) && occupied=1 || occupied=0
[[ $occupied == 0 && -z $owners ]]
sudo -n systemd-run --quiet --collect --unit=sn100-calendar-installed-20261030 --property=Type=exec \
    /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u "$(id -un)" -- /bin/bash "$run/runner.sh" "$run" "$fixture" "$sha"
REMOTE
for attempt in $(seq 1 80); do
    if ssh "${opts[@]}" "$host" "test -f '$run/exit_code'"; then break; fi
    sleep 1
done
for file in app_1.log app_2.log exit_1 exit_2 exit_code runtime.txt dependencies.txt; do scp -B "${opts[@]}" "$host:$run/$file" "$result/$file"; done
test "$(cat "$result/exit_code")" = 0
ssh "${opts[@]}" "$host" 'pgrep -x sn100_shell || true; flock -n /home/rkshare/programs/sn100_shell/lifecycle.lock -c true; sudo -n fuser /dev/tty7 /dev/dri/card0 2>&1 || true' > "$result/after.txt"
printf 'INSTALLED_EGLFS_START_CLOSE_REOPEN_OK=1\n'
