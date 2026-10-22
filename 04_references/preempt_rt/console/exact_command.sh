#!/usr/bin/env bash
set -Eeuo pipefail
export LC_ALL=C
test "$(cat /sys/kernel/realtime)" = 1
test -z "$(pgrep -x cyclictest || true)"
test -z "$(pgrep -x stress-ng || true)"
cd /home/rkshare/programs/rt_validation
sudo -n timeout --signal=TERM --kill-after=3 30s ./tools/usr/bin/cyclictest --default-system -m --policy=fifo -p80 -t1 -a7 -i1000 -d0 -D20s
printf '[CYCLICTEST_EXIT]=0\n'
