#!/usr/bin/env bash
set -Eeuo pipefail
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$work/release_work/sysroot
cache=/home/qyq/.cache/sn100_rk_board_20261027_01/sdk
[[ $EUID != 0 && $sdk == /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/rk_board_20261027_01/release_work/sysroot ]]
[[ -d $sdk && ! -L $sdk && ! -e $cache ]]
exec > >(tee "$work/sdk_cache.log") 2>&1
date -u +%FT%TZ
mkdir -p "$cache"
cp -a "$sdk/." "$cache/"
(cd "$sdk" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) > "$work/sdk_files.sha256"
(cd "$cache" && sha256sum --quiet -c "$work/sdk_files.sha256")
(cd "$sdk" && find . -type l -printf '%P %l\n' | LC_ALL=C sort) > "$work/sdk_links.original"
(cd "$cache" && find . -type l -printf '%P %l\n' | LC_ALL=C sort) > "$work/sdk_links.cache"
cmp "$work/sdk_links.original" "$work/sdk_links.cache"
printf 'SDK_CACHE_VERIFIED=1\n'
date -u +%FT%TZ
touch "$work/sdk_cache.verified"
