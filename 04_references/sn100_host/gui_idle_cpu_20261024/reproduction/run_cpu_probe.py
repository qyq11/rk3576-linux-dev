"""Uninstrumented actual six-store product, identical anonymous data/page/replay."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('name')
parser.add_argument('binary')
parser.add_argument('--existing', action='store_true', help='Summarize a completed observation without rerunning it')
args = parser.parse_args()
root = Path('/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux')
work = root / 'temp/sn100_host/o09_loopback'
out = work / 'cpu_comparison' / args.name
if not args.existing:
    assert not out.exists()
    out.mkdir(parents=True)
source = root / 'temp/sn100_host/status_replay_20261024/resource_agent/runtime_01'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
def inventory(directory):
    return {str(path.relative_to(directory)): sha(path) for path in directory.rglob('*') if path.is_file()}
before = inventory(source)
if not args.existing:
    for name in ('hospital', 'account', 'history', 'catalog'):
        shutil.copytree(source / name, out / 'runtime' / name)
    assert inventory(out / 'runtime') == before
binary = Path(args.binary)
binary_sha = sha(binary)
fixture = root / '06_programs/sn100_host/tests/fixtures/status_replay.json'
command = ['python3', str(root / '06_programs/sn100_host/tests/observe_linux_process.py'),
    '--output-dir', str(out / 'process'), '--stop-after', '20', '--timeout', '35', '--',
    str(binary), '--offline', '--accounts', '--history', '--catalog', '--samples',
    '--data-dir', str(out / 'runtime'), '--viewport', '1600x900', '--status-replay', str(fixture)]
if not args.existing:
    with (out / 'observer.log').open('w') as log:
        result = subprocess.run(command, env=dict(os.environ, QT_QPA_PLATFORM='xcb'), stdout=log, stderr=subprocess.STDOUT)
    assert result.returncode == 0
summary = json.loads((out / 'process/summary.json').read_text())
assert summary['exit_code'] == 0 and not summary['timed_out']
log = (out / 'process/process.log').read_text()
for marker in ('SHELL_READY', 'STATUS_REPLAY_STEP 14', 'replay.finished:', 'STATUS_REPLAY_STOPPED true', 'THREADS_STOPPED true', 'SAMPLE_STORE_CLOSED',
               'REAGENT_STORE_CLOSED', 'PROJECT_STORE_CLOSED', 'HOSPITAL_STORE_CLOSED',
               'ACCOUNT_STORE_CLOSED', 'HISTORY_STORE_CLOSED'):
    assert marker in log, marker
tail = [row for row in json.loads((out / 'process/samples.json').read_text()) if 6 <= row['elapsed_seconds'] <= 20]
span = tail[-1]['elapsed_seconds'] - tail[0]['elapsed_seconds']
summary.update(binary_sha256=binary_sha, fixture_sha256=sha(fixture),
    same_initial_data=before, page=7, state='recorded replay EOF; true instrument unknown',
    instrumentation=False, steady_observed_seconds=span,
    steady_cpu_percent_one_core=100*(tail[-1]['cpu_ticks']-tail[0]['cpu_ticks'])/os.sysconf('SC_CLK_TCK')/span)
assert sha(binary) == binary_sha and inventory(source) == before
(out / 'verification.json').write_text(json.dumps(summary, indent=2))
print(json.dumps({key: summary[key] for key in ('binary_sha256', 'exit_code', 'sample_count',
    'steady_observed_seconds', 'steady_cpu_percent_one_core', 'maximum_VmRSS', 'maximum_Pss')}))
