#!/usr/bin/env bash
# 只读实际定标和默认试剂库，只输出字段及数量，不读取项目、批号或患者文字。
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
date -Is
command -v sqlite3
data_root=/home/rkshare/programs/sn100_shell/runtime/data
cal_file="$data_root/result/calibration.db"
reagent_file="$data_root/db/userSet/reagent_Information.db"
test -f "$cal_file" && test ! -L "$cal_file"
test -f "$reagent_file" && test ! -L "$reagent_file"
sqlite3 -readonly -batch "$cal_file" <<'SQL'
.bail on
.headers on
.mode list
.timeout 3000
PRAGMA query_only=ON;
ATTACH DATABASE 'file:/home/rkshare/programs/sn100_shell/runtime/data/db/userSet/reagent_Information.db?mode=ro' AS reagent;
BEGIN;
SELECT 'calibration_base' AS table_name,cid,name,type,"notnull",pk,hidden FROM pragma_table_xinfo('calibration_base');
SELECT 'calibration_data' AS table_name,cid,name,type,"notnull",pk,hidden FROM pragma_table_xinfo('calibration_data');
SELECT 'calibration_batch_group' AS table_name,cid,name,type,"notnull",pk,hidden FROM pragma_table_xinfo('calibration_batch_group');
SELECT 'calibraiton_total_count' AS table_name,cid,name,type,"notnull",pk,hidden FROM pragma_table_xinfo('calibraiton_total_count');
SELECT 'reagent_batch_group' AS table_name,cid,name,type,"notnull",pk,hidden FROM pragma_table_xinfo('reagent_batch_group','reagent');
SELECT 'counter_validation' AS check_name,count(*) AS total_rows,sum(CASE WHEN typeof(total_count)='integer' AND total_count>=0 THEN 1 ELSE 0 END) AS nonnegative_integer_rows FROM calibraiton_total_count;
SELECT 'counter_next_conflicts' AS check_name,
 (SELECT count(*) FROM calibration_base WHERE calibration_id IN (SELECT total_count+1 FROM calibraiton_total_count)) AS base_rows,
 (SELECT count(*) FROM calibration_data WHERE calibration_id IN (SELECT total_count+1 FROM calibraiton_total_count)) AS data_rows,
 (SELECT count(*) FROM calibration_batch_group WHERE calibration_id IN (SELECT total_count+1 FROM calibraiton_total_count)) AS binding_rows;
WITH matched AS (
 SELECT CASE WHEN b.calibration_id=0 THEN 'zero' WHEN b.calibration_id>0 THEN 'positive' WHEN b.calibration_id<0 THEN 'negative' ELSE 'null_or_other' END AS id_group,
 (SELECT count(*) FROM calibration_base AS c WHERE c.calibration_id=b.calibration_id AND c.project_name=b.project_name) AS base_matches
 FROM calibration_batch_group AS b
)
SELECT 'binding_base_matches' AS check_name,id_group,count(*) AS binding_count,sum(base_matches=0) AS no_base,sum(base_matches=1) AS one_base,sum(base_matches>1) AS multiple_bases FROM matched GROUP BY id_group;
SELECT 'zero_id_data' AS check_name,count(*) AS data_rows FROM calibration_data WHERE calibration_id=0;
SELECT 'default_reagent_bindings' AS check_name,count(*) AS reagent_groups,
 sum(NOT EXISTS(SELECT 1 FROM calibration_batch_group AS c WHERE c.project_name=r.project_name AND c.batch_group=r.batch_group)) AS groups_without_calibration_binding
 FROM reagent.reagent_batch_group AS r;
ROLLBACK;
SQL
printf 'SQLITE_READONLY=1\nCONFIG_CHANGED=0\nPROGRAM_STARTED=0\n'
