#!/usr/bin/env bash
# 新建板B的503无线连接，密码只从标准输入交给网络管理服务。
set -Eeuo pipefail
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
profile=board_b_wifi_503
test -z "$(nmcli -g UUID connection show "$profile" 2>/dev/null || true)"
nmcli radio wifi on
nmcli connection add type wifi ifname wlan0 con-name "$profile" ssid 503 \
    connection.autoconnect yes ipv4.method auto ipv6.method auto \
    wifi-sec.key-mgmt wpa-psk wifi-sec.psk-flags 0
nmcli --wait 35 connection up "$profile" ifname wlan0 passwd-file /dev/stdin
printf 'BOARD_B_WIFI_CONNECTED=1\n'
nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status
nmcli -f GENERAL.STATE,GENERAL.CONNECTION,IP4.ADDRESS,IP4.GATEWAY,IP4.DNS device show wlan0
printf 'WIRED_CONNECTION_PRESERVED\n'
ip -brief address show end0
