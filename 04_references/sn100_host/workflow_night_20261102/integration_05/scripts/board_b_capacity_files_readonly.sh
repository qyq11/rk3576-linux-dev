#!/usr/bin/env bash
# 只查当前原资料和已知主控目录中的容量文件名称、大小与指纹，不改参数。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
for directory in /home/rkshare/programs/sn100_shell/runtime/data/calibrationFile /userdata/shared/calibrationFile; do
    printf 'KNOWN_DIRECTORY=%s\n' "$directory"
    for name in ReagentAmount_600.json ReagentAmount_450.json 'ReagentAmount_450 - TJ.json'; do
        file=$directory/$name
        if [[ -f $file && ! -L $file ]]; then
            stat -c 'FILE=%n BYTES=%s' -- "$file"
            sha256sum -- "$file"
        else
            printf 'REGULAR_FILE_NOT_FOUND=%s\n' "$file"
        fi
    done
done
printf 'CONTROLLER_DIRECTORY_SEARCH_BEGIN\n'
if [[ -d /userdata/shared && ! -L /userdata/shared ]]; then
    find /userdata/shared -maxdepth 3 -type f -name ReagentAmount_600.json -print
fi
printf 'CONTROLLER_DIRECTORY_SEARCH_END\nCONFIG_CHANGED=0\nPROGRAM_STARTED=0\n'
