#!/usr/bin/env bash
# Run on the RK board only if deliberately reverting this home-path change.
set -Eeuo pipefail
[[ $(id -u) == 0 ]] || { printf 'Run with sudo.\n' >&2; exit 1; }
test -d /home/rkshare/lckfb
test ! -L /home/rkshare/lckfb
current=$(getent passwd lckfb | cut -d: -f6)
[[ $current == /home/lckfb || $current == /home/rkshare/lckfb ]]
if [[ -e /home/lckfb || -L /home/lckfb ]]; then
    test -L /home/lckfb
    test "$(readlink -e /home/lckfb)" = /home/rkshare/lckfb
else
    ln -sT -- /home/rkshare/lckfb /home/lckfb
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bash "$script_dir/set_home.sh" /home/lckfb
getent passwd lckfb
readlink -- /home/lckfb
