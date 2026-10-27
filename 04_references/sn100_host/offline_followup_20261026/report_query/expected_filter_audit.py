"""Evaluate only the expected anonymous fixture filter, not a claimed observed query."""
import hashlib
import json
from pathlib import Path
import sqlite3

ROOT = Path(__file__).resolve().parents[4]
WORK = Path(__file__).resolve().parent
rows = []
for name in ('cumulative_report_v2', 'cumulative_report_v2_retry01'):
    path = ROOT / 'temp/sn100_host/catalog_integration' / name / 'legacy/history/history.sqlite'
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    connection = sqlite3.connect(path.resolve().as_uri() + '?mode=ro&immutable=1', uri=True)
    try:
        count = connection.execute('SELECT count(*) FROM report_data WHERE visible=1 AND data_time BETWEEN ? AND ?',
                                   ('20261023000000', '20261023235959')).fetchone()[0]
    finally:
        connection.close()
    assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
    rows.append({'scenario': name, 'sha256': digest, 'expected_filter_count': count})
result = {'expected_filter': {'start': '20261023000000', 'end': '20261023235959',
                             'sample_number': '', 'barcode': '', 'offset': 0, 'limit': 20},
          'actual_failed_ui_filter_observed': False, 'rows': rows}
with (WORK / 'expected_filter_read_only.json').open('x', encoding='utf-8') as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
    stream.write('\n')
print(json.dumps(result, ensure_ascii=False))
