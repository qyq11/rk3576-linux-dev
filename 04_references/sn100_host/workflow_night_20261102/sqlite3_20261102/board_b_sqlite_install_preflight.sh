#!/usr/bin/env bash
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
cat /etc/os-release
dpkg-query -W -f='${Package}|${Version}|${Status}\n' libsqlite3-0
apt-cache policy sqlite3 libsqlite3-0
apt-get --simulate --no-install-recommends install sqlite3
