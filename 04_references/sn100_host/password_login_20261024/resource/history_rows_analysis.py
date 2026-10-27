"""Post-exit schema/row-count supplement; never read row values or fingerprint DBs."""
import argparse
import json
from pathlib import Path
import sqlite3

ROOT = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
WORK = ROOT / 'temp/sn100_host/o02_login'
DATABASE = WORK / 'resource_runtime_01/history/history.sqlite'
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path)
args = parser.parse_args()

summary = json.loads((WORK / 'resource_01/summary.json').read_text())
verification = json.loads((WORK / 'resource_01/verification.json').read_text())
assert summary['exit_code'] == 0 and not summary['timed_out']
assert all(value is True for value in verification['checks'].values())
assert DATABASE.is_file() and not DATABASE.is_symlink()
tables = []
with sqlite3.connect(DATABASE.as_uri() + '?mode=ro', uri=True) as database:
    database.execute('PRAGMA query_only=ON')
    names = [row[0] for row in database.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name NOT GLOB 'sqlite_*' ORDER BY name")]
    for name in names:
        quoted = '"' + name.replace('"', '""') + '"'
        columns = [{'name': row[1], 'declared_type': row[2], 'primary_key_order': row[5]}
                   for row in database.execute('PRAGMA table_info(' + quoted + ')')]
        count = database.execute('SELECT count(*) FROM ' + quoted).fetchone()[0]
        tables.append({'table': name, 'columns': columns, 'row_count': count})
result = {
    'schema': 'sn100-post-exit-history-rows-v1',
    'source': str(DATABASE.relative_to(ROOT)),
    'phase': 'Post-exit read-only structure/row-count verification; supplemental analysis only',
    'reason': "Original run_resource.py rglob('*.db') omitted the closed history.sqlite extension",
    'connection': 'SQLite URI mode=ro; PRAGMA query_only=ON',
    'original_execution_exit_code': summary['exit_code'],
    'tables': tables,
    'row_values_read': False,
    'database_hashed_or_copied': False,
    'new_product_execution_or_growth_measurement': False,
    'original_raw_evidence_modified': False,
    'note': 'This supplements the initial eight-file archive with this JSON and its reproducer. The original five raw files/verification and initial archive_metadata remain unchanged; this is not a new run or a storage-growth measurement.'
}
text = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
if args.output:
    with args.output.open('x', encoding='utf-8') as output:
        output.write(text)
else:
    print(text, end='')
