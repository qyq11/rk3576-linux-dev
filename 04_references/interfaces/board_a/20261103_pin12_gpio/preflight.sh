#!/usr/bin/env bash
set -euo pipefail
serial=$(tr -d '\000' </proc/device-tree/serial-number)
test "$serial" = 3bf8222fe1417f6a
printf '[board_a_identity]\nSERIAL=%s\n' "$serial"
id
uname -a
cat /sys/kernel/realtime
cat /proc/sys/kernel/random/boot_id
ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub
printf '\n[boot_configuration]\n'
findmnt -T /boot/overlays
ls -la /boot/overlays
sha256sum /boot/overlays/overlays.ini /boot/overlays/.ubootenv
cat /boot/overlays/overlays.ini
printf '\n[tools]\n'
for tool in ovtree dtc fdtget fdtoverlay gpioset gpioget gpioinfo gpiodetect; do
    command -v "$tool" || true
done
gpioset --version
gpiodetect
printf '\n[gpio0_lines]\n'
gpioinfo gpiochip0
printf '\n[pin12_pwm_and_uart3_pinmux]\n'
grep -E 'pin (13|96|97) ' /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinmux-pins
grep -E 'pin 13 ' /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinconf-pins
symbol=/sys/firmware/devicetree/base/__symbols__/pwm1_6ch_1
if test -f "$symbol"; then
    pwm_path=$(tr -d '\000' <"$symbol")
    printf 'PWM_PATH=%s\nPWM_STATUS=' "$pwm_path"
    tr -d '\000' <"/sys/firmware/devicetree/base$pwm_path/status"
    printf '\n'
fi
printf '\n[pwm_state]\n'
cat /sys/kernel/debug/pwm
printf '\n[existing_interfaces]\n'
ls -l /dev/ttyS3 /dev/ttyS4 /dev/ttyS7 /dev/ttyS8 /dev/ttyS9
ip -details link show can0
printf '\n[running_application_names]\n'
ps -eo pid,user,comm | grep -E 'sn100|ACA|gpio|ovtree' || true
printf '\n[boot_payload_fingerprints]\n'
find /boot -maxdepth 2 -type f \( -name '*.dtbo' -o -name '*.dtb' -o -name 'Image*' -o -name 'extlinux.conf' \) -exec sha256sum '{}' +
printf '\npreflight_exit=0\n'
