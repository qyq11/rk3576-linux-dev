"""Reparse the handwritten anonymous CSV fixture; never opens a production DB."""
import csv
import json
import pathlib
import sys

directory = pathlib.Path(sys.argv[1]).resolve()
before = directory / "csv/exports/before.csv"
after = directory / "csv/exports/after.csv"
assert before.read_bytes() == after.read_bytes(), "Display changes altered exported data"
with before.open(encoding="gbk", newline="") as stream:
    rows = list(csv.reader(stream, strict=True))
expected = [
    ["样本号", "样本条码", "报告日期", "姓名", "年龄", "性别", "PT s", "INR", "APTT s", "FIB g/L", "D-D ng/mL", "RAWX"],
    ["1", "ANON-001", "2026-10-22 23:59:59", "匿名一号", "20", "男", "11.3", "1.00", " ", " ", " ", " "],
    ["2", "ANON-002", "2026-10-23 00:00:00", "匿名二号", "30", "女", " ", " ", "0.00", " ", " ", " "],
    ["3", 'ANON,"003', "2026-10-23 12:34:56", '匿名,"三号"\n换行', "0", "", "12.3", "1.24", "35.68", "2.35", "123.456", '待确认,"raw"\nnext'],
    ["3", 'ANON,"003', "2026-10-23 12:34:56", '匿名,"三号"\n换行', "0", "", "13.5", "1.11", " ", " ", " ", " "],
    ["4", "ANON-004", "2026-10-23 23:59:59", "匿名四号", "40", "男", "****", "****", " ", " ", " ", " "],
    ["5", "ANON-005", "2026-10-24 00:00:00", "匿名五号", "50", "女", " ", " ", " ", "3.14", " ", " "],
]
assert rows == expected, "Decoded CSV fields differ from the explicit anonymous gold fixture"
print(json.dumps({"status": "passed", "reports": 5, "data_rows": 6, "columns": 12,
                  "exact_cells": 84, "encoding": "GBK", "byte_identical_after_display_save": True}, ensure_ascii=False))
