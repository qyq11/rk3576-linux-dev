#!/usr/bin/env bash
# 在板A以root恢复本次修改前的共享账号；不改系统密码和共享文件。
set -Eeuo pipefail
export LC_ALL=C
test "$(id -u)" = 0
test "$(tr -d '\000' </proc/device-tree/serial-number)" = 3bf8222fe1417f6a
archive_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test "$(sha256sum "$archive_dir/smb.conf.before" | cut -d' ' -f1)" = 012ae2e409e2f3f388e5e38d1c36bb3d28184c887a2056e628b7b1757f04bbe0
test "$(sha256sum /etc/samba/smb.conf | cut -d' ' -f1)" = bcbb1a8a51ccc3032d806981838e6e87ac2943aef9edc1b248fac154c76389b5
test -n "$(pdbedit -L | awk -F: '$1=="lckfb" {print $1}')"
testparm -s "$archive_dir/smb.conf.before" >/dev/null
candidate=$(mktemp /etc/samba/.smb.conf.restore.XXXXXX)
trap 'rm -f -- "$candidate"' EXIT
cat "$archive_dir/smb.conf.before" > "$candidate"
chmod --reference=/etc/samba/smb.conf "$candidate"
chown --reference=/etc/samba/smb.conf "$candidate"
mv -f -- "$candidate" /etc/samba/smb.conf
pdbedit -x -u root
systemctl restart smbd
systemctl is-active --quiet smbd
test "$(testparm -s --section-name=rkshare --parameter-name='valid users' 2>/dev/null)" = lckfb
printf 'BOARD_A_SAMBA_RESTORE=PASS\n'
