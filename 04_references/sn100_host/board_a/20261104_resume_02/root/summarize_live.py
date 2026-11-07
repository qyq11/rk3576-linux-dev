from pathlib import Path
from collections import Counter
import csv
import json
import re

work = Path(__file__).resolve().parent
folder = work / 'collected_final'
log = (folder / 'app.log').read_text(errors='replace')
tx, rx, first = Counter(), Counter(), {}
for line in log.splitlines():
    match = re.fullmatch(r'DEVICE_WIRE (TX queued|RX) ([0-9a-f ]+)', line)
    if not match: continue
    direction = 'TX' if match.group(1).startswith('TX') else 'RX'
    raw = bytes.fromhex(match.group(2))
    if len(raw) < 10: continue
    command = raw[8:10].decode('ascii', errors='replace')
    (tx if direction == 'TX' else rx)[command] += 1
    first.setdefault(direction + '_' + command, match.group(2))
rows = list(csv.DictReader((folder / 'resources.tsv').read_text().splitlines(), delimiter='\t'))
start, end = rows[0], rows[-1]
duration = float(end['uptime_s']) - float(start['uptime_s'])
state = (folder / 'current_state.txt').read_text()
hz = int(re.search(r'^CLOCK_TICKS_PER_SECOND=(\d+)$', state, re.MULTILINE).group(1))
ticks = lambda row: int(row['utime_ticks']) + int(row['stime_ticks'])
resources = {'samples': len(rows), 'elapsed_seconds': round(duration, 2),
             'cpu_one_core_100_percent_average': round(100 * (ticks(end) - ticks(start)) / hz / duration, 2),
             'rss_kib_min': min(int(row['rss_kib']) for row in rows),
             'rss_kib_max': max(int(row['rss_kib']) for row in rows),
             'pss_kib_max': max(int(row['pss_kib']) for row in rows if row['pss_kib'] != 'unavailable'),
             'threads_max': max(int(row['threads']) for row in rows),
             'fd_max': max(int(row['fd_count']) for row in rows),
             'scope': '仅启动及短时原主控连接，带只读观察器，未设置资源门槛，未测实体响应、事件循环延迟或长稳。'}
hashes = lambda text: dict((path, digest) for digest, path in re.findall(r'^([a-f0-9]{64})  (.+)$', text, re.MULTILINE))
before = hashes((work / 'identity_before.stdout').read_text())
after = hashes(state)
assert len(before) == len(after) == 7 and before == after
observations = [json.loads(line) for line in (folder / 'live_ui_observer.jsonl').read_text().splitlines()]
colours = [row for row in observations if row['kind'] == 'communication_timestamp']
assert {row['view'] for row in colours} == {'textBrowser_SendData', 'textBrowser_ReceiveData'}
assert all(row['timestamp_darkgreen'] and row['body_colour'] == '#000000' for row in colours)
summary = {'config_files_equal': 7, 'shell_ready': 'SHELL_READY' in log, 'tx_log_entries': dict(tx),
           'rx_log_entries': dict(rx), 'first_complete_frames': first, 'resources': resources,
           'communication_colour_observations': colours, 'lis_connection_refused': 'Connection refused' in log,
           'container_events_observed': sum(row['kind'] == 'container_event' for row in observations),
           'normal_exit_observed': (folder / 'result.txt').exists()}
(work / 'live_summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n')
print(json.dumps({key: value for key, value in summary.items() if key != 'first_complete_frames'}, ensure_ascii=False, indent=2))
