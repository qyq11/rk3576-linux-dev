#!/usr/bin/env bash
# 只补装数据库命令行工具，沿用已经安装的SQLite运行库版本。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
sqlite_version=$(dpkg-query -W -f='${Version}' libsqlite3-0)
apt-get -o Acquire::Retries=0 -o Acquire::http::Timeout=15 -o Acquire::https::Timeout=15 update
apt-get --simulate --no-install-recommends install "sqlite3=$sqlite_version"
DEBIAN_FRONTEND=noninteractive apt-get -y --no-install-recommends install "sqlite3=$sqlite_version"
sqlite3 --version
dpkg-query -W -f='${Package}|${Version}|${Status}\n' sqlite3 libsqlite3-0
printf 'SN100_PROGRAM_STARTED=0\nCONTROLLER_STARTED=0\n'
