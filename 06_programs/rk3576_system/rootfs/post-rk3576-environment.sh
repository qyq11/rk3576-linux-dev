#!/bin/bash -e

source "${RK_POST_HELPER:-$(dirname "$(realpath "$0")")/post-helper}"
test "$POST_OS" = ubuntu || exit 0
test -f "$TARGET_DIR/usr/share/rk3576-image/environment.txt" || exit 0

# 官方99-owner只恢复/home下同名用户目录；rkshare是共享目录，须在其后恢复实际用户归属。
uid=$(awk -F: '$1 == "lckfb" { print $3 }' "$TARGET_DIR/etc/passwd")
gid=$(awk -F: '$1 == "lckfb" { print $4 }' "$TARGET_DIR/etc/passwd")
test -n "$uid"
test -n "$gid"
test -d "$TARGET_DIR/home/rkshare/lckfb"
chown -hR "$uid:$gid" "$TARGET_DIR/home/rkshare"
chmod 0700 "$TARGET_DIR/home/rkshare"
chmod 0750 "$TARGET_DIR/home/rkshare/lckfb" "$TARGET_DIR/home/rkshare/programs"
echo 'RK3576_POST_ROOTFS_OWNER_FIX=applied'
