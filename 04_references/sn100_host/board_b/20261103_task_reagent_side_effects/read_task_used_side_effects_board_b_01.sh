#!/usr/bin/env bash
# 只核当前used字段、匿名数量及相关数据库规则，不读业务行或请求写入。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
base=/home/rkshare/programs/sn100_shell
test "$(cat "$base/original_project_root")" = "$base/runtime/data"
test "$(readlink "$base/current")" = releases/94d1b9794f2d8f1ff776b498e62d29ba2ebd82fef807d7cda70b05837c3eccb1
db="$base/runtime/data/db/userSet/reagent_Setting.db"
test -f "$db" && test ! -L "$db"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\nREAD_ONLY_SCHEMA_AND_AGGREGATES=1\n'
date -Is
printf 'DATABASE_SHA_BEFORE='
sha256sum "$db" | cut -d' ' -f1
sqlite3 -readonly -batch "$db" <<'SQL'
.headers on
.mode list
PRAGMA query_only=ON;
BEGIN;
SELECT name, type FROM pragma_table_info('reagent_setting_6600') WHERE name='used_ul';
SELECT typeof(used_ul) AS sqlite_storage_type, count(*) AS row_count FROM reagent_setting_6600 GROUP BY typeof(used_ul);
SELECT count(*) AS target_table_count FROM sqlite_schema WHERE type='table' AND name='reagent_setting_6600';
SELECT count(*) AS target_trigger_count FROM sqlite_schema WHERE type='trigger' AND tbl_name='reagent_setting_6600';
SELECT count(*) AS database_trigger_count FROM sqlite_schema WHERE type='trigger';
SELECT count(*) AS target_view_reference_count FROM sqlite_schema WHERE type='view' AND instr(lower(coalesce(sql,'')),'reagent_setting_6600')>0;
ROLLBACK;
SQL
printf 'DATABASE_SHA_AFTER='
sha256sum "$db" | cut -d' ' -f1
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
printf 'DATABASE_WRITES_REQUESTED=0\nBUSINESS_ROWS_EXPORTED=0\n'
