"""只收同一活进程的封口证据并独立解析，失败也不停止或重启产品。"""
from pathlib import Path
import hashlib
import io
import json
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
CALLS = ROOT / 'temp/sn100_host/root_manager_20261103'
OUT = HERE / 'runtime_batch10_01'
KNOWN = 'C:/Users/52400/.ssh/known_hosts_rk3576_board_b'
PIN = 'SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg'
OPTIONS = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'HostKeyAlgorithms=ssh-ed25519', '-o', 'HostKeyAlias=rk3576-board-b',
           '-o', 'UserKnownHostsFile=' + KNOWN]
FILES = {'capture_manifest.sha256', 'capture_identity_before.json', 'capture_identity_after.json',
         'capture_status.json', 'lis_observation.json', 'lis_observation.ready'}


def save(name, raw):
    with (OUT / name).open('xb') as stream:
        stream.write(raw)


def main():
    assert (CALLS / 'launch_new_batch10_02.exit').read_text().strip() == '0'
    assert (CALLS / 'wait_lis_batch10_02.exit').read_text().strip() == '0'
    identity = {}
    for line in (CALLS / 'launch_new_batch10_02.log').read_text().splitlines():
        if line.startswith(('APP_PID=', 'START_TICKS=', 'BOOT_ID=')):
            key, value = line.split('=', 1)
            assert key not in identity
            identity[key] = value
    assert set(identity) == {'APP_PID', 'START_TICKS', 'BOOT_ID'}
    key = subprocess.run(['ssh-keygen', '-E', 'sha256', '-l', '-f', KNOWN], capture_output=True)
    assert key.returncode == 0 and {x.split()[1] for x in key.stdout.decode('ascii').splitlines()} == {PIN}
    intent = json.loads((OUT / 'runtime_launch_intent.json').read_text())
    capture = intent['observation_dir'].rsplit('/', 1)[0] + '/captures/capture_01'
    script = "set -Eeuo pipefail\n[[ $(tr -d '\\0\\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]\n"
    script += '[[ $(cat /proc/sys/kernel/random/boot_id) == ' + identity['BOOT_ID'] + ' ]]\n'
    script += 'test ! -L ' + capture + '\n'
    script += 'tar -czf - -C ' + capture + ' -- ' + ' '.join(sorted(FILES)) + '\n'
    save('collect_sealed.command.sh', script.encode('ascii'))
    result = subprocess.run(['ssh', *OPTIONS, 'root@192.168.100.123', 'bash -s'], input=script.encode('ascii'), capture_output=True)
    save('collect_sealed.stdout.tar.gz', result.stdout)
    save('collect_sealed.stderr.log', result.stderr)
    save('collect_sealed.actual_exit', (str(result.returncode) + '\n').encode('ascii'))
    if result.returncode:
        sys.stderr.buffer.write(result.stderr)
        return result.returncode
    data = {}
    with tarfile.open(fileobj=io.BytesIO(result.stdout), mode='r:gz') as bundle:
        for member in bundle:
            assert member.name in FILES and member.name not in data and member.isfile()
            assert 0 < member.size < 1048576
            data[member.name] = bundle.extractfile(member).read()
    assert set(data) == FILES
    local = OUT / 'capture_01'
    local.mkdir()
    for name, raw in data.items():
        with (local / name).open('xb') as stream:
            stream.write(raw)
    parser = ROOT / '05_tools/tests/sn100_host/lis_keep_running_20261103/review_lis_observation.py'
    assert hashlib.sha256(parser.read_bytes()).hexdigest() == 'a0ea76de798d3fee19e5bb0adf547d033d3619f5020e05a5f61ce6cbc1c46783'
    args = [sys.executable, '-X', 'utf8', str(parser), '--capture-directory', str(local)]
    expected = {'board-serial': '8314b1e8ffd0321f', 'boot-id': identity['BOOT_ID'],
                'pid': identity['APP_PID'], 'start-ticks': identity['START_TICKS'],
                'release-id': intent['release_id'], 'executable': intent['application_exe'],
                'executable-sha256': intent['application_sha256'],
                'current-link': '/home/rkshare/programs/sn100_shell/current',
                'expected-current-target': intent['application_exe'].rsplit('/', 1)[0],
                'release-id-file': intent['application_exe'].rsplit('/', 1)[0] + '/release_id',
                'observation-id': intent['observation_id']}
    for key, value in expected.items():
        args += ['--' + key, value]
    save('parser.command.json', (json.dumps(args, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    review = subprocess.run(args, cwd=ROOT, capture_output=True)
    save('parser.stdout.json', review.stdout)
    save('parser.stderr.log', review.stderr)
    save('parser.actual_exit', (str(review.returncode) + '\n').encode('ascii'))
    sys.stdout.buffer.write(review.stdout)
    sys.stderr.buffer.write(review.stderr)
    print('PARSER_ACTUAL_EXIT=' + str(review.returncode), flush=True)
    return review.returncode


if __name__ == '__main__':
    sys.exit(main())
