#!/usr/bin/env python3
"""Create a new, explicitly named anonymous catalog for calibration-only tests.

Not a catalog migration/import tool. No production values, fitted coefficients,
instrument mapping or device settings are created.
"""
import argparse
from pathlib import Path
import sqlite3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="New absolute anonymous catalog.db path")
    args = parser.parse_args()
    path = args.output
    if not path.is_absolute() or path.exists() or "sn100_host_reference" in str(path).lower():
        parser.error("A new absolute file outside reference data is required")
    path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(path) as connection:
        connection.executescript("""
        CREATE TABLE catalog_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
        INSERT INTO catalog_metadata VALUES('schema','sn100-catalog-offline-v1');
        CREATE TABLE option_unit(unit TEXT PRIMARY KEY,ratio REAL NOT NULL);
        INSERT INTO option_unit VALUES('mg/dL',1),('Sec',1);
        CREATE TABLE project_base(id INTEGER PRIMARY KEY,project_id TEXT,project_en_name TEXT,
            project_cn_name TEXT,project_lis_code TEXT,is_used INTEGER,fixed INTEGER,change_time TEXT);
        INSERT INTO project_base VALUES(1,'offline-unmapped','LOCAL_CAL','匿名校准项目','',1,0,'2026-10-25T00:00:00Z');
        CREATE TABLE project_result(project_id INTEGER,result_type TEXT,result_name TEXT,decimal_point INTEGER,
            test_low_range REAL,test_up_range REAL,normal_low_range REAL,normal_up_range REAL,
            lis_code TEXT,unit_name TEXT,main_result INTEGER);
        INSERT INTO project_result VALUES(1,'Calibration','浓度',2,0,99999,0,99999,'','mg/dL',0);
        INSERT INTO project_result VALUES(1,'Original','原始值',2,0,99999,0,99999,'','Sec',1);
        CREATE TABLE reagent_batch_group(id INTEGER PRIMARY KEY,project_id INTEGER,batch_code TEXT,batch_group TEXT);
        INSERT INTO reagent_batch_group VALUES(7,1,'ANON-BATCH','ANON-R1|ANON-R2');
        """)
    print(f"anonymous catalog created: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
