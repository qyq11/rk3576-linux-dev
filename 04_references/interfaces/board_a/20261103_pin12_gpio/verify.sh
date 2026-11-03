#!/usr/bin/env bash
# 软件核对只操作排针12脚；读回高低电平不替代万用表或真实485通信。
set -euo pipefail
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
exec 9>/run/lock/rk3576_hardware_configuration.lock
flock -n 9
cd /home/rkshare/programs/gpio_pin12_board_a_20261103
printf '[board_a_after_reboot]\n'
id
uname -r
test "$(cat /sys/kernel/realtime)" = 1
boot_id=$(cat /proc/sys/kernel/random/boot_id)
test "$boot_id" != 5d98c436-a8ae-4a42-a4af-7921c20f4182
printf 'BOOT_ID=%s\n' "$boot_id"
ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub
printf '\n[pwm_released]\n'
test "$(tr -d '\000' </sys/firmware/devicetree/base/pwm@2add1000/status)" = disabled
test ! -e /sys/bus/platform/devices/2add1000.pwm/driver
printf 'PWM_STATUS=disabled PWM_DRIVER_BOUND=no\n'
gpioinfo gpiochip0 | grep -E 'line +13:.*unused'
printf '\n[unchanged_configuration_files]\n'
printf '%s\n' \
 '4feffa5ae6ac7b4c4c1aa046a63038c893439619ca768e5525e2d184b8f185ed  /boot/overlays/overlays.ini' \
 '984e5d694ebf262282b66ad62a9c2001c94ea7f00a0483aa4986df9cc32c4697  /boot/overlays/.ubootenv' \
 '9e720d81dfa456d9824403cd6a02e8f6e9899a9eecf375e0d51226e129ea92a4  /boot/overlays/tspi-3m-uart-header-five.dtbo' \
 '92c1bdcfecb719bd1e9e04d75cee90dbb994a735fbf8e691ecc3259f4e1d7280  /boot/overlays/tspi-3m-can0-m2.dtbo' \
 '4479c74d7462b03da69eef3a57420e3ef1ddfa7eefd57d4e44c04827c550e319  /boot/overlays/tspi-3m-dsi-c41409463.dtbo' \
 '67dd1e85993831344c1cc0bd9e7a8c6e37aec6b1500bfcd08be87dc5c89be7a9  /boot/overlays/tspi-3m-dsi-c42388916.dtbo' | sha256sum -c -
printf '\n[gpio_output_low_high_low]\n'
fifo="$PWD/verification.fifo"
test ! -e "$fifo"
mkfifo -m 600 "$fifo"
exec 8<>"$fifo"
gpio_pid=
cleanup() {
    if test -n "$gpio_pid" && kill -0 "$gpio_pid" 2>/dev/null; then
        printf '0\nq\n' >&8 || true
        kill -TERM "$gpio_pid" 2>/dev/null || true
        wait "$gpio_pid" || true
    fi
    exec 8>&-
    rm -f -- "$fifo"
}
trap cleanup EXIT
./gpio_output 12 <&8 >gpio_output_run.log 2>&1 &
gpio_pid=$!
snapshot() {
    expected=$1
    sleep 0.3
    kill -0 "$gpio_pid"
    gpioinfo gpiochip0 | grep -E 'line +13:.*gpio-output-test.*output.*used'
    grep -E 'pin 13 ' /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinmux-pins
    pinconf=$(grep -E 'pin 13 ' /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinconf-pins)
    printf '%s\n' "$pinconf"
    printf '%s\n' "$pinconf" | grep -F "pin output ($expected level)"
    printf 'PIN12_SET=%s KERNEL_READBACK=%s\n' "$expected" "$expected"
}
snapshot 0
printf '1\n' >&8
snapshot 1
printf '0\n' >&8
snapshot 0
printf 'q\n' >&8
wait "$gpio_pid"
gpio_pid=
cat gpio_output_run.log
printf 'gpio_output_exit=0\n'
printf '\n[gpio_input_mode]\n'
input_value=$(gpioget gpiochip0 13)
printf 'PIN12_INPUT_READBACK=%s\n' "$input_value"
gpioinfo gpiochip0 | grep -E 'line +13:.*unused.*input'
grep -E 'pin 13 ' /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinconf-pins
printf '\n[existing_serial_and_can_interfaces]\n'
for port in 3 4 7 8 9; do test -c "/dev/ttyS$port"; done
ls -l /dev/ttyS3 /dev/ttyS4 /dev/ttyS7 /dev/ttyS8 /dev/ttyS9
grep -E 'pin (70|71|78|79|80|81|88|89|96|97|132|134) ' /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinmux-pins
ip -details link show can0
printf '\n[boot_configuration]\n'
ovtree status
printf '\nverification_exit=0\n'
