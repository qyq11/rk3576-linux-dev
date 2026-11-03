#!/usr/bin/env bash
set -Eeuo pipefail
date -Is
printf 'BOOT_ID='; cat /proc/sys/kernel/random/boot_id
uptime -s
systemctl show sn100-original-controller-board-b.service -p LoadState -p ActiveState -p SubState -p MainPID -p ExecMainStartTimestamp -p ExecMainExitTimestamp -p ExecMainCode -p ExecMainStatus -p Result -p Restart
printf 'CURRENT='; readlink /home/rkshare/programs/sn100_shell/current
sha256sum -c /home/rkshare/programs/sn100_shell/verification/status_login_08_20261103/runs/auto_01/ini_before.sha256
sha256sum -c /home/rkshare/programs/sn100_shell/verification/status_login_08_20261103/runs/auto_01/controller_ini_before.sha256
sha256sum -c /home/rkshare/programs/aca_flow_test/runs/controller_restore_20261102/original_launcher.sha256
ss -H -ltnp 'sport = :5000'
journalctl -u sn100-original-controller-board-b.service -n 12 --no-pager | sed -E '/(password|passwd|token|secret|授权|口令)/Id'
journalctl -k --since '2026-11-03 14:35:00' --no-pager | grep -Ei 'oom|killed process|out of memory|segfault' || true
