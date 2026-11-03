#!/usr/bin/env bash
# 只在已核对身份的板A释放排针12脚；失败时恢复本次受影响的两个启动设置。
set -euo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
exec 9>/run/lock/rk3576_hardware_configuration.lock
flock -n 9
cd /home/rkshare/programs/gpio_pin12_board_a_20261103
sha256sum -c deployment_board.sha256
cmp -s board_before.dtb /sys/firmware/fdt
cmp -s overlays.ini.before /boot/overlays/overlays.ini
cmp -s ubootenv.before /boot/overlays/.ubootenv
test ! -e /boot/overlays/tspi-3m-uart3-485-gpio-pin12.dtbo
gpioinfo gpiochip0 | grep -E 'line +13:.*unused'
test "$(cat /sys/kernel/debug/pwm | sed -n '/platform\/2add1000.pwm/,/^$/p' | grep -c requested || true)" = 0
chmod 755 gpio_output
ldd ./gpio_output | tee dependencies.log
if grep -q 'not found' dependencies.log; then exit 1; fi
./gpio_output --help
rollback() {
    saved_exit=$?
    trap - ERR
    cp -- overlays.ini.before /boot/overlays/overlays.ini
    cp -- ubootenv.before /boot/overlays/.ubootenv
    if test -f /boot/overlays/tspi-3m-uart3-485-gpio-pin12.dtbo; then
        mv -- /boot/overlays/tspi-3m-uart3-485-gpio-pin12.dtbo ./failed-overlay.dtbo
    fi
    sync
    printf 'configuration_rolled_back=%s\n' "$saved_exit"
    exit "$saved_exit"
}
trap rollback ERR
install -m 644 tspi-3m-uart3-485-gpio-pin12.dtbo /boot/overlays/
cat >>/boot/overlays/overlays.ini <<'CONFIG'

[tspi-3m-uart3-485-gpio-pin12.dtbo]
enabled = false
name_zh = 排针12脚普通GPIO（预留485方向控制）
name_en = Header pin 12 GPIO for RS485 direction
desc_zh = 关闭GPIO0_B5的PWM通道，供程序申请输入或输出；不自动切换485方向，不发送串口数据
desc_en = Disable PWM on GPIO0_B5; direction is controlled by the application
CONFIG
ovtree --lang zh enable tspi-3m-uart3-485-gpio-pin12.dtbo
grep '^overlays=' /boot/overlays/.ubootenv | grep -F tspi-3m-uart3-485-gpio-pin12.dtbo | grep -F tspi-3m-can0-m2.dtbo | grep -F tspi-3m-uart-header-five.dtbo
ovtree status
sha256sum /boot/overlays/overlays.ini /boot/overlays/.ubootenv /boot/overlays/*.dtbo
cp /boot/overlays/overlays.ini ./overlays.ini.after
cp /boot/overlays/.ubootenv ./ubootenv.after
sync
trap - ERR
printf 'configuration_exit=0\n'
