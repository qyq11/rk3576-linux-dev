#!/usr/bin/env bash
set -Eeuo pipefail
export LC_ALL=C
cd /home/rkshare/programs/rt_validation/tools/usr/bin
test "$(cat /sys/kernel/realtime)" = 1
test -z "$(pgrep -x cyclictest || true)"
test -z "$(pgrep -x hackbench || true)"
test -z "$(pgrep -x stress-ng || true)"
printf '[reference_hackbench_once]\n'
sudo -n timeout --signal=TERM --kill-after=3 15s ./hackbench -l 1 -g 15 -f 25 -p &
load_pid=$!
trap 'kill "$load_pid" 2>/dev/null || true' EXIT
printf '[five_thread_cyclictest_three_second_check]\n'
set +e
sudo -n timeout --signal=TERM --kill-after=3 10s ./cyclictest --policy=fifo -p80 -t5 -i1000 -d500 -D3s
cyclic_status=$?
wait "$load_pid"
load_status=$?
set -e
trap - EXIT
printf '[CYCLICTEST_EXIT]=%s\n[HACKBENCH_EXIT]=%s\n' "$cyclic_status" "$load_status"
test "$cyclic_status" = 0
test "$load_status" = 0
test -z "$(pgrep -x cyclictest || true)"
test -z "$(pgrep -x hackbench || true)"
printf '[NO_TEST_PROCESSES_REMAIN]=PASS\n'
