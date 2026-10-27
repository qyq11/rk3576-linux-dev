"""Read-only audit of the explicitly anonymous old failure and retry inputs."""
import hashlib
import json
from pathlib import Path
import sqlite3

ROOT = Path(__file__).resolve().parents[4]
WORK = Path(__file__).resolve().parent
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

names = [
    '04_references/sn100_host/catalog_integration_20261024/o08/logs/cumulative_v2_report.txt',
    '04_references/sn100_host/catalog_integration_20261024/o08/logs/cumulative_v2_report_retry01.txt',
    '04_references/sn100_host/followup_20261024/report_frozen/result.json',
    '04_references/sn100_host/followup_20261024/report_frozen/binary_SHA256SUMS',
    '04_references/sn100_host/integrated_report_20261023/mixed_load_offscreen_01/execution.json',
    'temp/sn100_host/catalog_integration/tests/integrated_report_test',
    'temp/sn100_host/catalog_integration/native/wsl_release/sn100_report_renderer',
    '06_programs/sn100_host/tests/integrated_report_test.cpp',
    '06_programs/sn100_host/history/history_page.cpp',
    '06_programs/sn100_host/history/history_store.cpp',
    '06_programs/sn100_host/history/history_store.h',
    '06_programs/sn100_host/history/report_controller.cpp',
    '06_programs/sn100_host/history/report_remaining_store.cpp',
    '06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC/frm_report/mask_dlg_report_detail.cpp',
    '06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC/thirdparty/QtRptProject/QtRPT/config.pri']
files = [{'path': name, 'sha256': sha(ROOT / name), 'bytes': (ROOT / name).stat().st_size} for name in names]
databases = []
for scenario in ('cumulative_report_v2', 'cumulative_report_v2_retry01'):
    path = ROOT / 'temp/sn100_host/catalog_integration' / scenario / 'legacy/history/history.sqlite'
    before = sha(path)
    uri = path.resolve().as_uri() + '?mode=ro&immutable=1'
    connection = sqlite3.connect(uri, uri=True)
    try:
        values = connection.execute('SELECT count(*),sum(visible),min(data_time),max(data_time) FROM report_data').fetchone()
        table_columns = [row[1] for row in connection.execute('PRAGMA table_info(report_data)')]
    finally:
        connection.close()
    after = sha(path)
    if before != after:
        raise RuntimeError('Old anonymous DB changed during read-only audit')
    databases.append({'scenario': scenario, 'path': str(path.relative_to(ROOT)), 'sha256': before,
        'unchanged_after_read': True, 'report_count': values[0], 'visible_count': values[1],
        'min_report_date': values[2], 'max_report_date': values[3], 'report_columns': table_columns})
result = {'schema': 'sn100-report-query-read-only-inputs-v1', 'files': files, 'anonymous_databases': databases,
    'gui_build_or_resource_measurement_executed': False,
    'limits': 'DB shape/rows cannot prove which filters the failed UI actually submitted; the original log has no stage/parameter trace'}
output = WORK / 'read_only_inputs.json'
with output.open('x', encoding='utf-8') as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
    stream.write('\n')
print(json.dumps({'output': str(output), 'anonymous_databases': databases}, ensure_ascii=False, indent=2))
