import json
import os
from pathlib import Path

out = Path(__file__).resolve().parent / 'run_01'
invocation = json.loads((out / 'invocation.json').read_text())
subjects = [('controller_parent', invocation['parent_pid'], False)]
for path in out.glob('*.exit.json'):
    row = json.loads(path.read_text())
    if row['pid'] is not None:
        subjects.append((path.name, row['pid'], True))
checks = []
for name, pid, group in subjects:
    try:
        (os.killpg if group else os.kill)(pid, 0)
        gone = False
    except ProcessLookupError:
        gone = True
    checks.append({'subject': name, 'pid_or_pgid': pid, 'group': group, 'gone': gone})
result = {'all_gone': all(row['gone'] for row in checks), 'checks': checks,
          'child_exit_receipt_count': len(list(out.glob('*.exit.json')))}
(out / 'owned_processes_final.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
raise SystemExit(0 if result['all_gone'] else 1)
