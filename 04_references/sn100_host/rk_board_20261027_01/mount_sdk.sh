#!/usr/bin/env bash
set -Eeuo pipefail
work=/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/rk_board_20261027_01
sdk=$work/release_work/sysroot
cache=/home/qyq/.cache/sn100_rk_board_20261027_01/sdk
[[ $EUID == 0 && -f $work/sdk_cache.verified && -d $cache && ! -L $cache ]]
[[ $(readlink -f "$sdk") == "$sdk" && $(stat -c %u "$cache") == 1000 ]]
case ${1:-} in
    mount)
        ! mountpoint -q "$sdk"
        mount --bind "$cache" "$sdk"
        mount -o remount,bind,ro "$sdk"
        findmnt -n -o TARGET,SOURCE,FSTYPE,OPTIONS -T "$sdk" > "$work/sdk_mount.log"
        cat "$work/sdk_mount.log"
        ;;
    unmount)
        mountpoint -q "$sdk"
        umount -- "$sdk"
        ! mountpoint -q "$sdk"
        printf 'SDK_CACHE_UNMOUNTED=1\n' | tee "$work/sdk_unmount.log"
        ;;
    *) exit 2 ;;
esac
