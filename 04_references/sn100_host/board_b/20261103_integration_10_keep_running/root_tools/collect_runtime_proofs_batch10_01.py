"""只读取板端本轮白名单凭据和已固定资源副本，不触碰运行程序或业务资料。"""
from pathlib import Path
import hashlib
import io
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent / 'runtime_batch10_01'
CALLS = ROOT / 'temp/sn100_host/root_manager_20261103'
assert (CALLS / 'post_live_batch10_01.exit').read_text().strip() == '0'
known = 'C:/Users/52400/.ssh/known_hosts_rk3576_board_b'
key = subprocess.run(['ssh-keygen', '-E', 'sha256', '-l', '-f', known], capture_output=True)
assert key.returncode == 0 and {x.split()[1] for x in key.stdout.decode('ascii').splitlines()} == {'SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg'}
options = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'HostKeyAlgorithms=ssh-ed25519', '-o', 'HostKeyAlias=rk3576-board-b', '-o', 'UserKnownHostsFile=' + known]
files = {'old_stop_receipt.txt', 'old_stop.actual_exit', 'waiter.actual_exit', 'launched_run.txt',
         'resources_snapshot_01.tsv', 'runtime_launch_intent.json', 'root_approval.md',
         'configuration.sha256', 'latest_run_before_start.txt'}
script = "set -Eeuo pipefail\n[[ $(tr -d '\\0\\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]\n"
script += '[[ $(cat /proc/sys/kernel/random/boot_id) == 0eb94389-f120-42ad-9042-9c21d96a1481 ]]\n'
script += 'tar -czf - -C /home/rkshare/programs/sn100_shell/verification/integration_next_02_runtime_01 -- ' + ' '.join(sorted(files)) + '\n'
result = subprocess.run(['ssh', *options, 'root@192.168.100.123', 'bash -s'], input=script.encode('ascii'), capture_output=True)
for name, raw in (('collect_proofs.command.sh', script.encode('ascii')), ('collect_proofs.stdout.tar.gz', result.stdout),
                  ('collect_proofs.stderr.log', result.stderr), ('collect_proofs.actual_exit', (str(result.returncode) + '\n').encode('ascii'))):
    with (OUT / name).open('xb') as stream:
        stream.write(raw)
assert result.returncode == 0
data = {}
with tarfile.open(fileobj=io.BytesIO(result.stdout), mode='r:gz') as bundle:
    for member in bundle:
        assert member.name in files and member.name not in data and member.isfile() and 0 < member.size < 1048576
        data[member.name] = bundle.extractfile(member).read()
assert set(data) == files
for name in ('runtime_launch_intent.json', 'root_approval.md', 'configuration.sha256'):
    assert data[name] == (OUT / name).read_bytes()
assert hashlib.sha256(data['old_stop_receipt.txt']).hexdigest() == '4619885a1c8f3d5b99b034c7168ce7021a7aaefb4a154fc9ad0475a4912ebcb0'
assert data['old_stop.actual_exit'].strip() == b'0' and data['waiter.actual_exit'].strip() == b'0'
folder = OUT / 'remote_proofs'
folder.mkdir()
for name, raw in data.items():
    with (folder / name).open('xb') as stream:
        stream.write(raw)
print('REMOTE_PROOFS_COLLECTED=9; PRODUCT_STARTED_OR_STOPPED=0')
