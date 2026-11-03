#!/usr/bin/env bash
set -euo pipefail
cd /home/rkshare/programs/uart_setup
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
exec 9>/run/lock/rk3576_hardware_configuration.lock
flock -n 9
# 与正在进行的CAN配置共用其写入锁，避免同时改启动清单。
exec 8>/home/rkshare/programs/can_loopback/configuration.lock
flock -n 8
sha256sum --check <<'HASHES'
9e720d81dfa456d9824403cd6a02e8f6e9899a9eecf375e0d51226e129ea92a4  tspi-3m-uart-header-five.dtbo
cbc2fabe9c8ecb365d9514f4468b14dea6fc282ea14c3637b492e4490f587041  uart_access_check
975ed94b370b31d6666823aec3952616b5a486d9bb924f74064b0a78ef845cc3  overlays.ini.before
14f644f7c7507426e4b365c1de6a007f1096ea9c94e20c89e5524bf7cc1f3fdb  ubootenv.before
HASHES
cmp -s overlays.ini.before /boot/overlays/overlays.ini
cmp -s ubootenv.before /boot/overlays/.ubootenv
test ! -e /boot/overlays/tspi-3m-uart-header-five.dtbo
rollback() {
    local saved_exit=$?
    trap - ERR
    cp -- overlays.ini.before /boot/overlays/overlays.ini
    cp -- ubootenv.before /boot/overlays/.ubootenv
    rm -f -- /boot/overlays/tspi-3m-uart-header-five.dtbo
    sync
    printf 'configuration_rolled_back=%s\n' "$saved_exit"
    exit "$saved_exit"
}
trap rollback ERR
install -m 644 tspi-3m-uart-header-five.dtbo /boot/overlays/tspi-3m-uart-header-five.dtbo
chmod 755 uart_access_check
cat >>/boot/overlays/overlays.ini <<'CONFIG'

[tspi-3m-uart-header-five.dtbo]
enabled = false
name_zh = 40针排针五组串口
name_en = Five UARTs on 40-pin header
desc_zh = 启用UART3/4/7/8/9收发引脚；释放I2C7、I2C8和SPI1第二片选；外设参数及RS485方向另配
desc_en = Enable UART3/4/7/8/9 TX and RX; release I2C7, I2C8 and SPI1 CS1; no serial data sent
CONFIG
ovtree --lang zh enable tspi-3m-uart-header-five.dtbo
grep '^overlays=' /boot/overlays/.ubootenv | grep -F tspi-3m-can0-m2.dtbo | grep -F tspi-3m-uart-header-five.dtbo
sha256sum /boot/overlays/overlays.ini /boot/overlays/.ubootenv /boot/overlays/tspi-3m-uart-header-five.dtbo /boot/overlays/tspi-3m-can0-m2.dtbo
ovtree status
sync
trap - ERR
printf 'configuration_exit=0\n'
