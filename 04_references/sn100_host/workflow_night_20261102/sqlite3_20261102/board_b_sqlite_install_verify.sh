#!/usr/bin/env bash
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
dpkg-query -W -f='${Package}|${Version}|${Status}\n' sqlite3 libsqlite3-0
sha256sum /usr/bin/sqlite3
ldd /usr/bin/sqlite3
apt-cache show sqlite3=3.45.1-1ubuntu2.8 | sed -n '/^Package:/p;/^Version:/p;/^Architecture:/p;/^Filename:/p;/^Size:/p;/^SHA256:/p'
package=/var/cache/apt/archives/sqlite3_3.45.1-1ubuntu2.8_arm64.deb
if test -f "$package"; then
    sha256sum "$package"
else
    printf 'APT_CACHE_PACKAGE_ABSENT=1\n'
fi
readlink -f /home/rkshare/programs/sn100_shell/current
printf 'SN100_PROGRAM_STARTED=0\nCONTROLLER_STARTED=0\n'
