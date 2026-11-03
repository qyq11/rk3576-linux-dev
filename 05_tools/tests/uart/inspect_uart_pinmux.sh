#!/bin/sh
# 只读核对开发图中的五组串口，不打开串口、不发送数据、不修改引脚或启动配置。
# 输出保存到电脑上的对应板卡日志目录；--sudo-login 只用于现场输入密码，读取受限的状态。
set -u

section() { printf '\n[%s]\n' "$1"; }
read_text() {
    if [ -r "$1" ]; then
        tr -d '\000' < "$1"
        printf '\n'
    else
        printf 'unavailable: %s\n' "$1"
    fi
}

if [ "${1:-}" = '--sudo-login' ]; then
    sudo -v || printf 'sudo unavailable; privileged observations may be missing\n'
fi

section identity
date -u '+%Y-%m-%dT%H:%M:%SZ'
uname -a
id
cat /etc/os-release
for property in model compatible serial-number; do
    printf '%s: ' "$property"
    read_text "/sys/firmware/devicetree/base/$property"
done
for interface in /sys/class/net/*; do
    printf '%s: ' "${interface##*/}"
    cat "$interface/address"
done
ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub 2>/dev/null || true

section console_and_serial_nodes
cat /proc/cmdline
cat /sys/class/tty/console/active
ls -l /dev/ttyS* /dev/ttyFIQ* 2>/dev/null || true
sudo -n cat /proc/tty/driver/serial 2>&1 || true
systemctl list-units 'serial-getty@*' --all --no-pager 2>&1 || true

section target_uart_bindings
dtroot=/sys/firmware/devicetree/base
for number in 3 4 7 8 9; do
    printf '\nUART%s\n' "$number"
    alias_path="$dtroot/aliases/serial$number"
    if [ -r "$alias_path" ]; then
        node_path=$(tr -d '\000' < "$alias_path")
        printf 'alias: %s\n' "$node_path"
        for property in status compatible pinctrl-names; do
            printf '%s: ' "$property"
            read_text "$dtroot$node_path/$property"
        done
        if [ -r "$dtroot$node_path/pinctrl-0" ]; then
            printf 'pinctrl-0 bytes (big endian): '
            od -An -tx1 "$dtroot$node_path/pinctrl-0"
        fi
    else
        printf 'serial alias unavailable\n'
    fi
    for link in device device/driver device/of_node; do
        printf 'ttyS%s/%s: ' "$number" "$link"
        readlink -f "/sys/class/tty/ttyS$number/$link" 2>/dev/null || true
    done
done

section serial_process_owners
if command -v fuser >/dev/null 2>&1; then
    sudo -n fuser -v /dev/ttyS3 /dev/ttyS4 /dev/ttyS7 /dev/ttyS8 /dev/ttyS9 2>&1 || true
fi

section runtime_pin_owners
# 在提权后的只读子命令中展开路径，以免普通用户无法遍历debugfs目录。
sudo -n sh -c '
for item in /sys/kernel/debug/pinctrl/*/pinmux-pins /sys/kernel/debug/pinctrl/*/pinconf-pins /sys/kernel/debug/gpio; do
    if [ -r "$item" ]; then
        printf "\nFILE %s\n" "$item"
        cat "$item"
    fi
done
' 2>&1 || true

section relevant_kernel_messages
sudo -n dmesg 2>&1 | grep -Ei 'ttyS[34789]|2ad[67abcd]0000|pinctrl.*(error|fail|conflict)|already requested|could not request pin|cannot claim' || true

section uart_kernel_options
if [ -r /proc/config.gz ]; then
    zcat /proc/config.gz | grep -E '^CONFIG_(SERIAL_8250|SERIAL_OF_PLATFORM|PINCTRL_ROCKCHIP|OF_OVERLAY|DEBUG_FS)' || true
fi

section boot_configuration
for config in /boot/extlinux/extlinux.conf /boot/armbianEnv.txt /boot/uEnv.txt; do
    if [ -r "$config" ]; then
        printf '\nFILE %s\n' "$config"
        cat "$config"
    fi
done
if command -v ovtree >/dev/null 2>&1; then
    ovtree --help 2>&1 | head -45
fi

section live_device_tree_begin
if command -v dtc >/dev/null 2>&1; then
    dtc -q -I fs -O dts "$dtroot" 2>&1
    printf 'dtc_exit=%s\n' "$?"
else
    printf 'dtc unavailable\n'
    for symbol in uart3m0_xfer uart4m0_xfer uart7m0_xfer uart8m1_xfer uart9m0_xfer i2c7m1_xfer i2c8m2_xfer spi1m1_csn1; do
        symbol_file="$dtroot/__symbols__/$symbol"
        if [ -r "$symbol_file" ]; then
            symbol_path=$(tr -d '\000' < "$symbol_file")
            printf '%s: %s\n' "$symbol" "$symbol_path"
            for property in phandle rockchip,pins; do
                if [ -r "$dtroot$symbol_path/$property" ]; then
                    printf '%s bytes: ' "$property"
                    od -An -tx1 "$dtroot$symbol_path/$property"
                fi
            done
        fi
    done
fi
section live_device_tree_end
printf '\naudit_exit=0\n'
