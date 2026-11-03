#!/usr/bin/env bash
set -euo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(cat /proc/sys/kernel/random/boot_id)" != 72b43693-95d9-4b7e-8620-63f56dfacbc4
printf '[identity_after_reboot]\n'
date -u '+%Y-%m-%dT%H:%M:%SZ'
id
uname -r
cat /proc/sys/kernel/random/boot_id
cat /sys/kernel/realtime
dtroot=/sys/firmware/devicetree/base
for specification in '3 2ad60000 uart3m0-xfer' '4 2ad70000 uart4m0-xfer' '7 2ada0000 uart7m0-xfer' '8 2adb0000 uart8m1-xfer' '9 2adc0000 uart9m0-xfer'; do
    read -r number address group <<<"$specification"
    test -c "/dev/ttyS$number"
    test "$(tr -d '\000' <"$dtroot/serial@$address/status")" = okay
    cmp "$dtroot/serial@$address/pinctrl-0" "$dtroot/pinctrl/uart$number/$group/phandle"
    printf 'UART%s STATUS=okay PIN_GROUP=%s DEVICE=/dev/ttyS%s\n' "$number" "$group" "$number"
done

printf '\n[actual_pin_owners]\n'
pinmux=/sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinmux-pins
test -r "$pinmux"
for specification in '96 97 uart3 uart3m0-xfer' '88 89 uart4 uart4m0-xfer' '78 79 uart7 uart7m0-xfer' '70 71 uart8 uart8m1-xfer' '80 81 uart9 uart9m0-xfer'; do
    read -r first second function group <<<"$specification"
    for pin in "$first" "$second"; do
        grep -E "^pin $pin .*function $function group $group$" "$pinmux"
    done
done

printf '\n[released_interfaces_and_preserved_spi]\n'
for address in 2aca0000 2acb0000; do
    test "$(tr -d '\000' <"$dtroot/i2c@$address/status")" = disabled
    printf 'i2c@%s STATUS=disabled\n' "$address"
done
test "$(tr -d '\000' <"$dtroot/spi@2ad00000/spidev@1/status")" = disabled
test -c /dev/spidev1.0
test ! -e /dev/spidev1.1
test ! -e /dev/i2c-7
test ! -e /dev/i2c-8
printf 'SPI1_CS0=preserved SPI1_CS1=released I2C7_and_I2C8=released\n'
ls -l /dev/ttyS* /dev/spidev* /dev/i2c-*

printf '\n[root_port_open_and_settings_query]\n'
cd /home/rkshare/programs/uart_setup
sha256sum --check <<'HASHES'
cbc2fabe9c8ecb365d9514f4468b14dea6fc282ea14c3637b492e4490f587041  uart_access_check
HASHES
timeout 10s ./uart_access_check

printf '\n[serial_driver_counters_after_query]\n'
cat /proc/tty/driver/serial
printf '\n[preserved_can_and_startup_configuration]\n'
test "$(tr -d '\000' <"$dtroot/can@2ac00000/status")" = okay
ip -details -statistics link show can0
cat /boot/overlays/.ubootenv
ovtree status
sha256sum /boot/overlays/overlays.ini /boot/overlays/.ubootenv /boot/overlays/tspi-3m-uart-header-five.dtbo /boot/overlays/tspi-3m-can0-m2.dtbo
printf '\n[relevant_kernel_messages]\n'
dmesg | grep -Ei 'ttyS[34789]|already requested|could not request pin|cannot claim|pinctrl.*(error|fail|conflict)' || true
printf '\nverify_exit=0\n'
