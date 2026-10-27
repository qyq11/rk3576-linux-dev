"""Read existing traces and anonymous SQLite; do not launch any Qt process."""
import collections
import hashlib
import json
from pathlib import Path
import re
import sqlite3

ROOT = Path(__file__).resolve().parents[4]
WORK = Path(__file__).resolve().parent
RUN = WORK / 'run_01'
WSL_ROOT = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def local(path):
    if not path.startswith(WSL_ROOT):
        raise RuntimeError('Trace path is outside the known repository')
    value = ROOT / path[len(WSL_ROOT):]
    if not value.resolve().is_relative_to(RUN.resolve()):
        raise RuntimeError('Anonymous read must remain in run_01')
    return value

summary = {'schema': 'sn100-report-query-run01-observation-v1', 'scenarios': [],
    'historical_root_cause_proven': False, 'production_defect_proven': False, 'test_defect_proven': False,
    'limits': ['queryReady timing is GUI delivery, not pure SQL execution duration',
               'SQL predicates are derived from actual request filters and current whereClause source; no live SQLite statement hook was installed',
               'Post-run read-only count is separately executed against this anonymous DB, not a replay of the old failed process',
               'File-append tracing adds scheduling overhead and can mask a race']}
for name in ('original_xcb', 'trace_xcb', 'trace_offscreen'):
    scenario = RUN / name
    text = (scenario / 'qt.txt').read_text(encoding='utf-8')
    match = re.search(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped.*?, (\d+)ms', text)
    receipt = json.loads((scenario / 'test.exit.json').read_text())
    row = {'name': name, 'actual_exit': receipt['exit_code'], 'timed_out': receipt['timed_out'],
           'qt_passed': int(match[1]), 'qt_failed': int(match[2]), 'qt_skipped': int(match[3]),
           'qt_elapsed_ms': int(match[4]), 'queries': []}
    if name == 'original_xcb':
        row.update(scope='historical executable only', stage_trace_available=False)
        summary['scenarios'].append(row)
        continue
    events = [json.loads(line) for line in (scenario / 'query_trace.jsonl').read_text(encoding='utf-8').splitlines()]
    row['scope'] = 'current explicit 432-source native binding'
    row['event_count'] = len(events)
    row['stage_counts'] = dict(collections.Counter(e['stage'] for e in events))
    row['store_error_events'] = [e for e in events if e['stage'] == 'store_error_gui_delivery']
    row['initial_query_results'] = [{k: e[k] for k in ('id', 'filter', 'rows', 'total', 'method')}
                                    for e in events if e['stage'] == 'query_ready_gui_delivery' and e['id'] == 1]
    starts = [i for i, event in enumerate(events) if event['stage'] == 'query_helper_begin']
    for start in starts:
        end = next(i for i in range(start + 1, len(events)) if events[i]['stage'] == 'query_helper_finished')
        block = events[start:end + 1]
        request = next(e for e in block if e['stage'] == 'request_query')
        response = next(e for e in block if e['stage'] == 'query_ready_gui_delivery')
        filtered = request['filter']
        if filtered['sample_number'] or filtered['barcode']:
            raise RuntimeError('Expected reviewed empty sample/barcode trace, preserve for separate analysis')
        where = 'visible=1 AND data_time BETWEEN ? AND ?'
        bindings = [filtered['sql_start'], filtered['sql_end']]
        database = local(events[start]['directory']) / 'history/history.sqlite'
        before = sha(database)
        connection = sqlite3.connect(database.resolve().as_uri() + '?mode=ro&immutable=1', uri=True)
        try:
            count = connection.execute('SELECT COUNT(*) FROM report_data WHERE ' + where, bindings).fetchone()[0]
        finally:
            connection.close()
        if sha(database) != before:
            raise RuntimeError('Anonymous DB changed during read-only analysis')
        row['queries'].append({'method': events[start]['method'], 'directory': events[start]['directory'],
            'legacy': events[start]['directory'].endswith('/legacy'),
            'serial_range': [events[start]['serial'], events[end]['serial']],
            'query_clicks': [{'name': e['button']['name'], 'clicked_count': e['clicked_count'],
                              'visible': e['button']['visible'], 'enabled': e['button']['enabled']}
                             for e in block if e['stage'] == 'click_after'],
            'calendar_clicked': [{'field': e['field'], 'date': e['date']} for e in block if e['stage'] == 'calendar_clicked'],
            'calendar_targets': [{k: e[k] for k in ('field', 'row', 'column', 'x', 'y', 'viewport_contains', 'visible_region_contains', 'shown_year', 'shown_month')}
                                 for e in block if e['stage'] == 'date_target_before_click'],
            'date_waits': [{k: e[k] for k in ('field', 'ok', 'actual_text')} for e in block if e['stage'] == 'date_wait_finished'],
            'request_id': request['id'], 'actual_request_filter': filtered,
            'reply_id': response['id'], 'reply_filter_matches_request': response['filter'] == filtered,
            'reply_rows': response['rows'], 'reply_total': response['total'],
            'sql_derived_not_live_observed': {'count': 'SELECT COUNT(*) FROM report_data WHERE ' + where,
                'bindings': bindings, 'page_order': 'ORDER BY id LIMIT ? OFFSET ?',
                'page_bindings_suffix': [filtered['limit'], filtered['offset']]},
            'postrun_read_only_count': count, 'postrun_db_sha256': before,
            'helper_ok': events[end]['ok'], 'busy_at_finish': events[end]['busy'],
            'helper_total_ms': (events[end]['monotonic_ns'] - events[start]['monotonic_ns']) / 1e6,
            'request_to_gui_reply_ms': (response['monotonic_ns'] - request['monotonic_ns']) / 1e6,
            'failed_wait_stage': None})
    summary['scenarios'].append(row)
with (RUN / 'observation_analysis.json').open('x', encoding='utf-8') as stream:
    json.dump(summary, stream, ensure_ascii=False, indent=2)
    stream.write('\n')
print(json.dumps({'totals': [{k: row[k] for k in ('name', 'actual_exit', 'qt_passed', 'qt_failed')} for row in summary['scenarios']],
    'legacy_queries': [{'scenario': row['name'], **query} for row in summary['scenarios'] for query in row['queries'] if query['legacy']]},
    ensure_ascii=True, indent=2))
