#!/usr/bin/env bash
set -Eeuo pipefail
cd /
base=/home/rkshare/programs/sn100_shell
release=$base/releases/88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74
upload='/home/rkshare/programs/sn100_shell/incoming/formal_merge_88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74.tar.gz'
[[ $(tr -d '\000' < /proc/device-tree/serial-number) == 3bf8222fe1417f6a ]]
[[ $(readlink -f "$base/current") == $base/releases/0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743 ]]
[[ $(readlink /proc/4426/exe) == /userdata/shared/ACA_V1.0OffLineD-500 && $(awk '{print $22}' /proc/4426/stat) == 196590 ]]
[[ $(ss -H -ltnp 'sport = :5000') == *'pid=4426,'* ]]
[[ ! -L $base/releases && ! -e $release && ! -L $release ]]
[[ $(sha256sum "$upload" | cut -d' ' -f1) == '53c93457c0c022f0b6692157e554705cf471d854f560c9ab3847d04f3febce8a' ]]
[[ ! -L $base/lifecycle.lock ]]
exec 8>> "$base/lifecycle.lock"
flock -n 8
for entry in /proc/[0-9]*/exe; do
 exe=$(readlink "$entry" 2>/dev/null) || continue
 [[ ${exe##*/} != sn100_shell && ${exe##*/} != sample_add_review ]]
done
parent=$base/releases/.formal_merge_stage_20261104_01
[[ ! -e $parent && ! -L $parent ]]
install -d -m 700 "$parent/88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74"
tar --no-same-owner --no-same-permissions -xzf "$upload" -C "$parent/88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74"
source "$parent/88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74/rk_board.sh"
verify_release "$parent/88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74"
mv -T "$parent/88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74" "$release"
rmdir "$parent"
chown -R lckfb:lckfb "$release"
runuser -u lckfb -- bash -c 'source "$1"; preflight "$2" "$3"' board-a-preflight "$release/rk_board.sh" "$base" "$release"
run=$base/runs/$(date +%Y%m%d_%H%M%S)_$$
[[ ! -e $run && ! -L $run ]]
install -d -o lckfb -g lckfb -m 700 "$run"
printf 'RUN_DIRECTORY=%s\nREVIEW_RELEASE=%s\nCURRENT_POINTER_UNCHANGED=1\n' "$run" '88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74'
exec 8>&-
systemd-run --quiet --collect --unit=sn100-formal-merge-review-20261104 --property=Type=exec --property=Restart=no \
 /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u lckfb -- /bin/bash "$release/rk_board.sh" supervise "$base" "$release" "$run" 20 ''
for ((i=0;i<75;i++)); do
 if [[ -f $run/app.pid && -e /proc/$(cat "$run/app.pid")/exe ]]; then
  printf 'APP_PID=%s\n' "$(cat "$run/app.pid")"
  sha256sum /proc/$(cat "$run/app.pid")/exe
  exit 0
 fi
 if [[ -f $run/exit_code || -f $run/startup_failure.txt ]]; then
  tail -20 "$run/supervisor.log"
  exit 2
 fi
 sleep .2
done
exit 3
