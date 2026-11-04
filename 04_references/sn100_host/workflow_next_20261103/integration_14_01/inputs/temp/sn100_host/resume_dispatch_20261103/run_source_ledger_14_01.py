"""Root运行只读来源聚合器，独立保留真实命令、输出与退出。"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'source_ledger_inputs_01/source_ledger_candidate_v2.py'
SPEC = ROOT / 'temp/sn100_host/workflow_next_20261103/source_ledger_inputs_14_01/root_spec_14.frozen_01.json'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == '307674360d5c24c3fe17eae04e5c7a869f7f35021fdaabecb9fd0afedbc165cd'
assert hashlib.sha256(SPEC.read_bytes()).hexdigest() == 'f84e04e189c1f8a20fa84e6c6cca5a5c900488e6efd0691f145279b014ed6643'
stem = 'source_ledger_run_14_01'
for suffix in ('.command.json', '.stdout.log', '.stderr.log', '.exit'):
    assert not (HERE / (stem + suffix)).exists()
argv = [sys.executable, '-B', '-X', 'utf8', str(SOURCE), '--root-spec', str(SPEC)]
with (HERE / (stem + '.command.json')).open('xb') as stream:
    stream.write((json.dumps({'argv': argv, 'cwd': str(ROOT)}, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
result = subprocess.run(argv, cwd=ROOT, capture_output=True)
for suffix, raw in (('.stdout.log', result.stdout), ('.stderr.log', result.stderr),
                    ('.exit', (str(result.returncode) + '\n').encode('ascii'))):
    with (HERE / (stem + suffix)).open('xb') as stream:
        stream.write(raw)
sys.stdout.buffer.write(result.stdout)
sys.stderr.buffer.write(result.stderr)
sys.exit(result.returncode)
