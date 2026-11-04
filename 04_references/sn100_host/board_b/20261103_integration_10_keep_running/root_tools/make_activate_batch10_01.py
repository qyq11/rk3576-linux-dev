"""仅在真实旧停成功后，绑定实际凭据SHA派生activate-only；不联板。"""
from pathlib import Path
import difflib
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[3]
CALLS = ROOT / 'temp/sn100_host/root_manager_20261103'
OUT = Path(__file__).resolve().parent / 'runtime_batch10_01'
assert (CALLS / 'stop_old_batch10_02.exit').read_text().strip() == '0'
log = (CALLS / 'stop_old_batch10_02.log').read_text()
matches = re.findall(r'^OLD_STOP_RECEIPT_SHA256=([a-f0-9]{64})$', log, re.M)
assert len(matches) == 1
for line in ('STOP_TOOL_ACTUAL_EXIT=0', 'APP_EXIT=0', 'CLEAN_SHUTDOWN=1', 'DISPLAY_RELEASED=1'):
    assert line in log.splitlines()
before = (CALLS / 'prepare_flat_batch10_02.sh').read_text()
assert before.count('set -- prepare-flat\n') == 1
placeholder = "old_stop_receipt_sha256='" + '0' * 64 + "'"
assert before.count(placeholder) == 1
after = before.replace('set -- prepare-flat\n', 'set -- activate-only\n', 1)
after = after.replace(placeholder, "old_stop_receipt_sha256='" + matches[0] + "'", 1)
path = CALLS / 'activate_new_batch10_01.sh'
with path.open('x', encoding='utf-8', newline='\n') as stream:
    stream.write(after)
with (OUT / 'activate_delta.diff').open('x', encoding='utf-8', newline='\n') as stream:
    stream.write(''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                             fromfile='prepare_flat_batch10_02.sh', tofile=path.name)))
with (OUT / 'activate_derivation.json').open('x', encoding='utf-8', newline='\n') as stream:
    json.dump({'old_stop_receipt_sha256': matches[0],
               'actual_stop_log_sha256': hashlib.sha256((CALLS / 'stop_old_batch10_02.log').read_bytes()).hexdigest(),
               'script_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
               'board_executed_by_generator': False}, stream, indent=2)
    stream.write('\n')
print('ACTIVATE_SCRIPT_DERIVED_FROM_ACTUAL_STOP=1; BOARD_EXECUTED=0')
