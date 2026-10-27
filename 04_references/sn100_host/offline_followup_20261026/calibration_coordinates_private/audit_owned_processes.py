#!/usr/bin/env python3
"""Read-only end-of-slot check of the two recorded parents and six owned groups."""
import datetime
import json
import os
from pathlib import Path

here = Path(__file__).resolve().parent
checks = []
for attempt in ('core_01', 'api_01'):
    invocation = json.loads((here / attempt / 'invocation.json').read_text())
    subjects = [('parent', invocation['parent_pid'], False)]
    for name in ('compiler_version', 'compile', 'test'):
        receipt = json.loads((here / attempt / (name + '.exit.json')).read_text())
        subjects.append((name + '_group', receipt['pid'], True))
    for name, pid, group in subjects:
        try:
            (os.killpg if group else os.kill)(pid, 0)
            gone = False
        except ProcessLookupError:
            gone = True
        checks.append({'attempt': attempt, 'subject': name, 'pid_or_pgid': pid, 'gone': gone})
result = {'schema': 'sn100-coordinate-owned-process-audit-v1',
          'checked_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'checks': checks, 'all_owned_processes_and_groups_gone': all(row['gone'] for row in checks)}
(here / 'owned_processes_final.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
raise SystemExit(0 if result['all_owned_processes_and_groups_gone'] else 1)
