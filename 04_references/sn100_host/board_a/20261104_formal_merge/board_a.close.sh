#!/usr/bin/env bash
set -Eeuo pipefail
cd /
base=/home/rkshare/programs/sn100_shell
run='/home/rkshare/programs/sn100_shell/runs/20261104_171746_77036'
release=$base/releases/88ac35f775117aeb9658478ee900006e6aaad2259fa86656ede8462a516d7c74
[[ $(tr -d '\000' < /proc/device-tree/serial-number) == 3bf8222fe1417f6a ]]
pid=$(cat "$run/app.pid")
if [[ -e /proc/$pid ]]; then
 [[ $(readlink /proc/$pid/exe) == $release/sn100_shell ]]
 [[ $(sha256sum /proc/$pid/exe | cut -d' ' -f1) == fc82caaa5550df5594bb3cc359a6529a23bef47add739a0a46df3ab29476db55 ]]
 runuser -u lckfb -- bash -c 'source "$1"; stop_run "$2"' normal-close "$release/rk_board.sh" "$run"
fi
[[ -f $run/exit_code ]]
cat "$run/result.txt"
printf 'RUN_EXIT_CODE='; cat "$run/exit_code"
printf 'CONTROLLER='; readlink /proc/4426/exe
ss -H -ltnp 'sport = :5000'
