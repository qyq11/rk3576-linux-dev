#!/usr/bin/env bash
# 仅读SQLite文件头18/19字节，不打开数据库或创建WAL/SHM，用于确认复制候选的适用范围。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
data_root=/home/rkshare/programs/sn100_shell/runtime/data
for file in "$data_root/result/calibration.db" "$data_root/db/reagent/reagent_Information.db" "$data_root/project/"*.db; do
    if [[ -f $file && ! -L $file ]]; then
        printf 'DATABASE=%s\nWRITE_READ_VERSION=' "$file"
        od -An -tu1 -j18 -N2 -- "$file"
    else
        printf 'REGULAR_FILE_NOT_FOUND=%s\n' "$file"
    fi
done
printf 'SQLITE_OPENED=0\nCONFIG_CHANGED=0\nPROGRAM_STARTED=0\n'
