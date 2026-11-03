#!/usr/bin/env bash
set -euo pipefail
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
exec 9>/run/lock/rk3576_hardware_configuration.lock
flock -n 9
test "$(tr -d '\000' </sys/firmware/devicetree/base/pwm@2add1000/status)" = disabled
gpioinfo gpiochip0 | grep -E 'line +13:.*unused'
# 临时服务持续申请输出高电平，断开SSH后继续保持；不设置开机自启。
systemd-run --unit=gpio-pin12-output --collect --property=Type=exec \
  --description='板A排针12脚保持高电平' \
  /usr/bin/gpioset -m signal gpiochip0 13=1
systemctl is-active gpio-pin12-output.service
gpioinfo gpiochip0 | grep -E 'line +13:.*gpioset.*output.*used'
grep -E 'pin 13 ' /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinmux-pins
pinconf=$(grep -E 'pin 13 ' /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinconf-pins)
printf '%s\n' "$pinconf"
printf '%s\n' "$pinconf" | grep -F 'pin output (1 level)'
printf 'BOARD=board_a PIN=12 GPIO=GPIO0_B5 OUTPUT=HIGH KERNEL_READBACK=1\n'
printf 'set_high_exit=0\n'
