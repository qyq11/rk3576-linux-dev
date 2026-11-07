"""仅使用板A已核公开主机密钥，执行当次脚本或上传本轮文件。"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[3]
work = Path(__file__).resolve().parent
known = root / '04_references/sn100_host/board_a/20261104_formal_merge_identity/known_hosts'
options = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=6', '-o', 'ServerAliveInterval=5',
           '-o', 'ServerAliveCountMax=2', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'HostKeyAlgorithms=ssh-ed25519', '-o', 'HostKeyAlias=rk3576-board-a',
           '-o', f'UserKnownHostsFile={known}', '-o', 'UpdateHostKeys=no',
           '-i', 'C:/Users/52400/.ssh/id_ed25519']
ssh = ['C:/Program Files/Git/usr/bin/ssh.exe', *options, 'root@192.168.100.123']

def remote(name, script, timeout=120):
    guard = "set -Eeuo pipefail\n[[ $(tr -d '\\000' < /proc/device-tree/serial-number) == 3bf8222fe1417f6a ]]\n"
    result = subprocess.run(ssh + ['bash -s'], input=(guard + script).encode(), stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=timeout)
    (work / f'{name}.stdout').write_bytes(result.stdout)
    (work / f'{name}.stderr').write_bytes(result.stderr)
    (work / f'{name}.exit').write_text(str(result.returncode) + '\n')
    return result

def upload(source, target):
    assert target.startswith('/home/rkshare/programs/sn100_shell/') and '..' not in target
    subprocess.run(['C:/Program Files/Git/usr/bin/scp.exe', '-B', *options, str(source),
                    'root@192.168.100.123:' + target], check=True, timeout=180)

if __name__ == '__main__':
    result = remote(sys.argv[1], Path(sys.argv[2]).read_text(), 120)
    print(result.stdout.decode(errors='replace'))
    print(result.stderr.decode(errors='replace'), file=sys.stderr)
    sys.exit(result.returncode)
