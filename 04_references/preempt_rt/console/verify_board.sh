#!/usr/bin/env bash
set -Eeuo pipefail
export LC_ALL=C
tool=/home/rkshare/programs/rt_validation/tools/usr/bin/cyclictest
printf '[system]\n'
uname -a
printf 'REALTIME='; cat /sys/kernel/realtime
printf 'ACTIVE_CONSOLE='; cat /sys/class/tty/console/active
ls -l /dev/ttyFIQ0
test -x "$tool"
printf '[tool_sha256]\n'
sha256sum "$tool"
printf '[dependencies]\n'
ldd "$tool"
test -z "$(pgrep -x cyclictest || true)"
test -z "$(pgrep -x stress-ng || true)"
printf '[five_second_live_stats]\n'
sudo -n timeout --signal=TERM --kill-after=3 12s "$tool" --default-system -m --policy=fifo -p80 -t1 -a7 -i1000 -D5s
printf '[LIVE_STATS_EXIT]=0\n'
printf '[five_second_scrolling_stats]\n'
sudo -n timeout --signal=TERM --kill-after=3 12s "$tool" --default-system -m --policy=fifo -p80 -t1 -a7 -i1000 -D5s -v -o1000 -u
printf '[SCROLLING_STATS_EXIT]=0\n'
