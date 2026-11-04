"""只读核板A；先核已知公钥，再在任何远端读取前核硬件序列号。"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess
import sys

HERE = Path(__file__).resolve().parent
SOURCE = Path('C:/Users/52400/.ssh/known_hosts')
PIN = 'SHA256:xLQv5sqzTRvKbfD2fNQQLYjCbPN6WOXgkXEoUqV8Yy4'


def save(path, raw):
    with path.open('xb') as stream:
        stream.write(raw)


def main():
    stem = HERE / 'identity_environment_01'
    assert all(not Path(str(stem) + suffix).exists() for suffix in ('.command.json', '.stdout.log', '.stderr.log', '.exit'))
    query = subprocess.run(['ssh-keygen', '-F', '192.168.100.123', '-f', str(SOURCE)], capture_output=True)
    assert query.returncode == 0
    rows = [line.split() for line in query.stdout.splitlines() if not line.startswith(b'#')]
    rows = [row for row in rows if len(row) == 3 and row[1] == b'ssh-ed25519']
    assert len(rows) == 1
    known = b'rk3576-board-a ssh-ed25519 ' + rows[0][2] + b'\n'
    pin = subprocess.run(['ssh-keygen', '-E', 'sha256', '-l', '-f', '-'], input=known, capture_output=True)
    assert pin.returncode == 0 and pin.stdout.decode('ascii').split()[1] == PIN
    trust = HERE / 'known_hosts.board_a'
    assert not trust.exists() and not trust.is_symlink()
    save(trust, known)
    payload = (HERE / 'identity_environment_01.sh').read_bytes().replace(b'\r\n', b'\n')
    guard = b"set -Eeuo pipefail\n[[ $(tr -d '\\0\\n' </proc/device-tree/serial-number) == 3bf8222fe1417f6a ]] || { printf 'BOARD_A_SERIAL_MISMATCH\\n' >&2; exit 96; }\n"
    argv = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o', 'ConnectionAttempts=1',
            '-o', 'StrictHostKeyChecking=yes', '-o', 'HostKeyAlgorithms=ssh-ed25519',
            '-o', 'HostKeyAlias=rk3576-board-a', '-o', 'UserKnownHostsFile=' + str(trust),
            '-o', 'UpdateHostKeys=no', 'root@192.168.100.123', 'bash -s']
    command = {'argv': argv, 'started_utc': datetime.now(timezone.utc).isoformat(), 'board': 'board_a',
               'serial': '3bf8222fe1417f6a', 'ssh_pin': PIN, 'trusted_public_key_sha256': hashlib.sha256(known).hexdigest(),
               'payload_sha256': hashlib.sha256(guard + payload).hexdigest(), 'scope': 'read_only_identity_environment_no_start_stop_config_or_device_action'}
    save(Path(str(stem) + '.command.json'), (json.dumps(command, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    actual = subprocess.run(argv, input=guard + payload, capture_output=True)
    for suffix, raw in (('.stdout.log', actual.stdout), ('.stderr.log', actual.stderr), ('.exit', (str(actual.returncode) + '\n').encode('ascii'))):
        save(Path(str(stem) + suffix), raw)
    sys.stdout.buffer.write(actual.stdout)
    sys.stderr.buffer.write(actual.stderr)
    print(json.dumps({'actual_ssh_exit': actual.returncode, 'board': 'board_a', 'read_only': True}))
    raise SystemExit(actual.returncode)


if __name__ == '__main__':
    main()
