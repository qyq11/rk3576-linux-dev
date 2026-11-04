"""板A固定公钥/硬件序列调用器；每步排他保存真实分离输出与SSH退出。"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
TRUST = HERE / 'known_hosts.board_a'
PIN = 'SHA256:xLQv5sqzTRvKbfD2fNQQLYjCbPN6WOXgkXEoUqV8Yy4'

def main():
    name = sys.argv[1]
    assert re.fullmatch('[a-z0-9_]+', name)
    outputs = {suffix: HERE / (name + suffix) for suffix in ('.command.json', '.stdout.log', '.stderr.log', '.exit')}
    assert all(not p.exists() and not p.is_symlink() for p in outputs.values())
    key = subprocess.run(['ssh-keygen', '-E', 'sha256', '-l', '-f', str(TRUST)], capture_output=True)
    assert key.returncode == 0 and key.stdout.decode('ascii').split()[1] == PIN
    payload = (HERE / (name + '.sh')).read_bytes().replace(b'\r\n', b'\n')
    payload = b"set -Eeuo pipefail\n[[ $(tr -d '\\0\\n' </proc/device-tree/serial-number) == 3bf8222fe1417f6a ]] || exit 96\n" + payload
    argv = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o', 'ConnectionAttempts=1',
            '-o', 'StrictHostKeyChecking=yes', '-o', 'HostKeyAlgorithms=ssh-ed25519', '-o', 'HostKeyAlias=rk3576-board-a',
            '-o', 'UserKnownHostsFile=' + str(TRUST), '-o', 'UpdateHostKeys=no', 'root@192.168.100.123', 'bash -s']
    with outputs['.command.json'].open('x',encoding='utf-8') as stream:
        json.dump({'argv': argv, 'board': 'board_a', 'serial': '3bf8222fe1417f6a', 'ssh_pin': PIN,
            'payload_sha256': hashlib.sha256(payload).hexdigest(), 'started_utc': datetime.now(timezone.utc).isoformat()},stream,indent=2)
    result = subprocess.run(argv,input=payload,capture_output=True)
    for suffix,raw in (('.stdout.log',result.stdout),('.stderr.log',result.stderr),('.exit',(str(result.returncode)+'\n').encode('ascii'))):
        with outputs[suffix].open('xb') as stream: stream.write(raw)
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    print('ACTUAL_BOARD_A_SSH_EXIT=' + str(result.returncode))
    raise SystemExit(result.returncode)

if __name__ == '__main__': main()
