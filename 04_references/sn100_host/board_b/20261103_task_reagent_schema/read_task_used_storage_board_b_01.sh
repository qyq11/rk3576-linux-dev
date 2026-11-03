#!/usr/bin/env bash
# 只核实际孔表used字段的声明/存储类型及匿名数量，不读出业务行内容。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
test "$(cat "$base/original_project_root")" = "$base/runtime/data"
db="$base/runtime/data/db/userSet/reagent_Setting.db"
test -f "$db" && test ! -L "$db"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nREAD_ONLY_SCHEMA_AND_AGGREGATES=1\n'
date -Is
sqlite3 -readonly -batch "$db" <<'SQL'
.headers on
.mode list
PRAGMA query_only=ON;
SELECT name, type FROM pragma_table_info('reagent_setting_6600') WHERE name='used_ul';
SELECT typeof(used_ul) AS sqlite_storage_type, count(*) AS row_count FROM reagent_setting_6600 GROUP BY typeof(used_ul);
SQL
printf 'DATABASE_WRITES_REQUESTED=0\n'
