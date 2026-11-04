"""总管理的板B调用器：固定主机指纹和序列，独立保存每次真实输出。"""
from pathlib import Path
import hashlib
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
KNOWN = 'C:/Users/52400/.ssh/known_hosts_rk3576_board_b'
PIN = 'SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg'
OPTIONS = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o',
           'StrictHostKeyChecking=yes', '-o', 'HostKeyAlgorithms=ssh-ed25519',
           '-o', 'HostKeyAlias=rk3576-board-b', '-o', 'UserKnownHostsFile=' + KNOWN]


def invoke(name):
    script = ROOT / (name + '.sh')
    for suffix in ('.log', '.exit', '.started'):
        if (ROOT / (name + suffix)).exists():
            raise RuntimeError('保留已有结果，拒绝覆盖：' + name)
    key = subprocess.run(['ssh-keygen', '-E', 'sha256', '-l', '-f', KNOWN], capture_output=True)
    assert key.returncode == 0 and {x.split()[1] for x in key.stdout.decode('ascii').splitlines()} == {PIN}
    payload = script.read_bytes().replace(b'\r\n', b'\n')
    (ROOT / (name + '.started')).write_text(hashlib.sha256(payload).hexdigest() + '\n', encoding='ascii')
    guard = b"set -Eeuo pipefail\n[[ $(tr -d '\\0\\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]\n"
    result = subprocess.run(['ssh', *OPTIONS, 'root@192.168.100.123', 'bash -s'],
                            input=guard + payload, capture_output=True)
    output = result.stdout + result.stderr
    (ROOT / (name + '.log')).write_bytes(output)
    (ROOT / (name + '.exit')).write_text(str(result.returncode) + '\n', encoding='ascii')
    sys.stdout.buffer.write(output)
    return result.returncode


if __name__ == '__main__':
    sys.exit(invoke(sys.argv[1]))
