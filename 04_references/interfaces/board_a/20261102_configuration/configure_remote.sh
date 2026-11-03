#!/usr/bin/env bash
# 仅适用于本次已核实的板A；先比较原配置，失败时恢复受影响的文件。
set -euo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
cd /home/rkshare/programs/interface_setup_board_a_20261102
exec 9>/run/lock/rk3576_hardware_configuration.lock
flock -n 9
sha256sum -c deployment_inputs.sha256
cmp -s board_before.dtb /sys/firmware/fdt
cmp -s overlays.ini.before /boot/overlays/overlays.ini
cmp -s ubootenv.before /boot/overlays/.ubootenv
for path in /usr/local/bin/ovtree /boot/overlays/tspi-3m-can0-m2.dtbo /boot/overlays/tspi-3m-uart-header-five.dtbo; do test ! -e "$path"; done
for pattern in '/sys/bus/i2c/devices/7-*' '/sys/bus/i2c/devices/8-*'; do
    if compgen -G "$pattern" >/dev/null; then printf '已登记外设，停止配置：%s\n' "$pattern"; exit 1; fi
done
if fuser /dev/ttyS4 /dev/i2c-7 /dev/i2c-8 /dev/spidev1.1; then printf '接口正在使用，停止配置\n'; exit 1; fi
rollback() {
    local saved_exit=$?
    trap - ERR
    cp -- overlays.ini.before /boot/overlays/overlays.ini
    cp -- ubootenv.before /boot/overlays/.ubootenv
    rm -f -- /boot/overlays/tspi-3m-can0-m2.dtbo /boot/overlays/tspi-3m-uart-header-five.dtbo /usr/local/bin/ovtree
    sync
    printf 'configuration_rolled_back=%s\n' "$saved_exit"
    exit "$saved_exit"
}
trap rollback ERR
install -m 755 ovtree /usr/local/bin/ovtree
install -m 644 tspi-3m-can0-m2.dtbo tspi-3m-uart-header-five.dtbo /boot/overlays/
chmod 755 uart_access_check
ldd ./uart_access_check | tee dependencies.log
if grep -q 'not found' dependencies.log; then exit 1; fi
cat >>/boot/overlays/overlays.ini <<'CONFIG'

[tspi-3m-can0-m2.dtbo]
enabled = false
name_zh = CAN0（40针7脚接收、11脚发送）
name_en = CAN0 M2 on header pins 7 and 11
desc_zh = 启用CAN0和7/11脚；通信速率另行设置，不自动收发
desc_en = Enable CAN0 M2 only; bitrate and link startup are configured separately

[tspi-3m-uart-header-five.dtbo]
enabled = false
name_zh = 40针排针五组串口
name_en = Five UARTs on 40-pin header
desc_zh = 启用UART3/4/7/8/9收发引脚；释放I2C7、I2C8和SPI1第二片选；外设参数及RS485方向另配
desc_en = Enable UART3/4/7/8/9 TX and RX; release I2C7, I2C8 and SPI1 CS1; no serial data sent
CONFIG
ovtree --lang zh enable tspi-3m-can0-m2.dtbo
ovtree --lang zh enable tspi-3m-uart-header-five.dtbo
grep '^overlays=' /boot/overlays/.ubootenv | grep -F tspi-3m-can0-m2.dtbo | grep -F tspi-3m-uart-header-five.dtbo
ovtree status
sha256sum /boot/overlays/overlays.ini /boot/overlays/.ubootenv /boot/overlays/*.dtbo /usr/local/bin/ovtree
sync
trap - ERR
printf 'configuration_exit=0\n'
