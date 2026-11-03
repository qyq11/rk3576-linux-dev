#!/usr/bin/env bash
set -euo pipefail
task_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$task_dir/../../../.." && pwd)
cd "$task_dir"
dtc -@ -I dts -O dtb -o tspi-3m-can0-m2.dtbo "$root/06_programs/rk3576_system/configs/can0_m2.dts"
dtc -@ -I dts -O dtb -o tspi-3m-uart-header-five.dtbo "$root/06_programs/rk3576_system/configs/uart_header_five.dts"
fdtoverlay -i board_before.dtb -o board_candidate.dtb tspi-3m-can0-m2.dtbo tspi-3m-uart-header-five.dtbo
dtc -q -I dtb -O dts -o board_before.dts board_before.dtb
dtc -q -I dtb -O dts -o board_candidate.dts board_candidate.dtb
set +e
diff -u board_before.dts board_candidate.dts >device_tree_changes.diff
diff_exit=$?
set -e
test "$diff_exit" -eq 1
cp /home/qyq/TaishanPi-3-Linux/app/ovtree/ovtree ./ovtree
file ovtree
sha256sum tspi-3m-can0-m2.dtbo tspi-3m-uart-header-five.dtbo ovtree
cat device_tree_changes.diff
