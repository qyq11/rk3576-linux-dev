#!/usr/bin/env bash
set -euo pipefail
task_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$task_dir/../../../.." && pwd)
cd "$task_dir"
build_part=${1:-all}
case "$build_part" in overlay|gpio|all) ;; *) exit 2 ;; esac
exec > >(tee "$task_dir/build_${build_part}.log") 2>&1
printf '[backup_fingerprints]\n'
printf '%s\n' \
  'fbda3263837bcfbe1c2b12e9111a100c47f373c6f2b33b61f0b2e89a40dcff79  overlays.ini.before' \
  '0a3a28657c9c3f01c60e37a0ee5dc07b09ac9abfea743dd30a2e314a684c0f44  ubootenv.before' \
  | (cd "$root/07_backups/interfaces/board_a_pin12_gpio_20261103" && sha256sum -c -)
if test "$build_part" != gpio; then
printf '\n[overlay_build_and_scope]\n'
dtc -@ -I dts -O dtb -o tspi-3m-uart3-485-gpio-pin12.dtbo \
  "$root/06_programs/rk3576_system/configs/uart3_485_gpio_pin12.dts"
fdtoverlay -i board_before.dtb -o board_candidate.dtb tspi-3m-uart3-485-gpio-pin12.dtbo
test "$(fdtget -t s board_before.dtb /pwm@2add1000 status)" = okay
test "$(fdtget -t s board_candidate.dtb /pwm@2add1000 status)" = disabled
dtc -q -s -I dtb -O dts -o board_before.dts board_before.dtb
dtc -q -s -I dtb -O dts -o board_candidate.dts board_candidate.dtb
set +e
diff -u board_before.dts board_candidate.dts >device_tree_changes.diff
diff_exit=$?
set -e
test "$diff_exit" -eq 1
cat device_tree_changes.diff
python3 - <<'PY'
from pathlib import Path
lines = Path('device_tree_changes.diff').read_text().splitlines()
changed = [line for line in lines if line.startswith(('+', '-')) and not line.startswith(('+++', '---'))]
assert [line.strip() for line in changed] == ['-\t\tstatus = "okay";', '+\t\tstatus = "disabled";'], changed
print('SCOPE=one_existing_PWM_status_property_only')
PY
fi
if test "$build_part" != overlay; then
printf '\n[arm64_gpio_tool_build]\n'
aarch64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Wpedantic \
  "$root/05_tools/tests/gpio/gpio_output.c" -Ideps -Ldeps -o gpio_output -lgpiod
file gpio_output
aarch64-linux-gnu-readelf -d gpio_output | grep NEEDED
fi
if test "$build_part" != overlay; then
sha256sum tspi-3m-uart3-485-gpio-pin12.dtbo gpio_output \
  "$root/06_programs/rk3576_system/configs/uart3_485_gpio_pin12.dts" \
  "$root/05_tools/tests/gpio/gpio_output.c" >deployment_inputs.sha256
cat deployment_inputs.sha256
fi
printf '\nbuild_exit=0\n'
