#!/usr/bin/env bash
set -euo pipefail
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
printf '[board_a_before_reboot]\n'
cat /proc/sys/kernel/random/boot_id
printf '%s\n' \
 '4feffa5ae6ac7b4c4c1aa046a63038c893439619ca768e5525e2d184b8f185ed  /boot/overlays/overlays.ini' \
 '984e5d694ebf262282b66ad62a9c2001c94ea7f00a0483aa4986df9cc32c4697  /boot/overlays/.ubootenv' | sha256sum -c -
if ps -eo comm | grep -Eq '^(sn100_shell|ACA_V)'; then
    printf '发现业务程序运行，停止本次自动重启。\n'
    exit 1
fi
sync
printf 'reboot_requested=1\n'
systemctl reboot --no-block
