#!/usr/bin/env bash
set -Eeuo pipefail
export LC_ALL=C
base=/home/rkshare/programs/rt_validation/tools/usr/bin
printf '[system]\n'
uname -r
cat /sys/kernel/realtime
printf '[tools]\n'
ls -l "$base/cyclictest" "$base/hackbench"
sha256sum "$base/cyclictest" "$base/hackbench"
ldd "$base/hackbench"
printf '[hackbench_help]\n'
set +e
"$base/hackbench" --help
help_status=$?
set -e
printf 'HACKBENCH_HELP_EXIT=%s\n' "$help_status"
printf '[pm_qos_device]\n'
ls -l /dev/cpu_dma_latency
printf '[existing_tests]\n'
pgrep -a -x cyclictest || true
pgrep -a -x hackbench || true
pgrep -a -x stress-ng || true
