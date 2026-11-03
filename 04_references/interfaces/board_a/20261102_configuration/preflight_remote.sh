#!/usr/bin/env bash
set -euo pipefail
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
printf '[identity]\n'
date -u '+%Y-%m-%dT%H:%M:%SZ'
id
uname -a
cat /sys/kernel/realtime
cat /proc/sys/kernel/random/boot_id
ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub
printf '\n[boot_and_mounts]\n'
findmnt /boot || true
findmnt -T /home/rkshare/lckfb
ls -la /boot /boot/overlays /boot/extlinux 2>&1 || true
for path in /boot/extlinux/extlinux.conf /boot/overlays/overlays.ini /boot/overlays/.ubootenv /boot/uEnv.txt /boot/armbianEnv.txt; do
    if test -f "$path"; then printf '\nFILE %s\n' "$path"; sha256sum "$path"; cat "$path"; fi
done
printf '\n[tools]\n'
for name in ovtree dtc fdtget fdtoverlay; do command -v "$name" || true; done
printf '\n[devices_and_owners]\n'
ls -l /dev/ttyS* /dev/i2c-* /dev/spidev* 2>&1 || true
fuser -v /dev/ttyS3 /dev/ttyS4 /dev/ttyS7 /dev/ttyS8 /dev/ttyS9 /dev/i2c-7 /dev/i2c-8 /dev/spidev1.1 2>&1 || true
cat /proc/tty/driver/serial
ip -details -statistics link show
printf '\n[pinmux]\n'
cat /sys/kernel/debug/pinctrl/pinctrl-rockchip-pinctrl/pinmux-pins
printf '\n[target_tree_status]\n'
base=/sys/firmware/devicetree/base
for path in serial@2ad60000 serial@2ad70000 serial@2ada0000 serial@2adb0000 serial@2adc0000 can@2ac00000 i2c@2aca0000 i2c@2acb0000 spi@2ad00000; do
    printf '%s status=' "$path"
    if test -r "$base/$path/status"; then tr -d '\000' <"$base/$path/status"; fi
    printf '\n'
done
printf '\n[i2c_registered_devices]\n'
ls -la /sys/bus/i2c/devices/7-* /sys/bus/i2c/devices/8-* 2>&1 || true
printf '\n[active_programs]\n'
ps -eo pid,user,comm,args | grep -E 'sn100|can|uart|smb' || true
printf '\n[CAN_kernel_configuration]\n'
zcat /proc/config.gz | grep -E '^CONFIG_CAN(=|_|FD)' || true
printf '\npreflight_exit=0\n'
