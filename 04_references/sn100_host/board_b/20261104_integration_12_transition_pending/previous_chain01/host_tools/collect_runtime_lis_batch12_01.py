"""Root手动执行：只回收同一活进程的六份封口原件，再独立解析；不启停产品。"""
from pathlib import Path
import argparse
import gzip
import hashlib
import io
import json
import os
import re
import shlex
import stat
import subprocess
import sys
import tarfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
CAP = ROOT / 'temp/sn100_host/root_manager_20261103/runtime_transition_captures/integration_12_01_runtime_01'
CORE = HERE / 'core_inputs.frozen_01.json'
CORE_SHA = '3446ab38e1448c076b825efcdc2e6e2990600967d531fe00d24964a661d2bab7'
CORE_BYTES = 9604
BASE = '/home/rkshare/programs/sn100_shell'
RELEASE = '69220b2eb393408a0c612b43a0fc954e479e8c450a09a48755b3fd2d43b84f72'
APP_SHA = 'fb1d42f5a67b0f3663cb3098947ac69d480d4a6784d790bd055ee3dcd5d8ef91'
SERIAL = '8314b1e8ffd0321f'
OBSERVATION = 'integration_12_01_lis_01'
PARSER = ROOT / '05_tools/tests/sn100_host/lis_keep_running_20261103/review_lis_observation.py'
PARSER_SHA = 'a0ea76de798d3fee19e5bb0adf547d033d3619f5020e05a5f61ce6cbc1c46783'
KNOWN = Path('C:/Users/52400/.ssh/known_hosts_rk3576_board_b')
PIN = 'SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg'
OPTIONS = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'HostKeyAlgorithms=ssh-ed25519', '-o', 'HostKeyAlias=rk3576-board-b',
           '-o', 'UserKnownHostsFile=' + str(KNOWN)]
LIMITS = {'capture_manifest.sha256': 4096, 'capture_identity_before.json': 4096,
          'capture_identity_after.json': 4096, 'capture_status.json': 4096,
          'lis_observation.json': 1048576, 'lis_observation.ready': 511}
ARCHIVE_LIMIT = 2 * 1024 * 1024
TEXT_LIMIT = 1024 * 1024
SHA = re.compile(r'[a-f0-9]{64}\Z')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def unique(pairs):
    value = {}
    for key, item in pairs:
        require(key not in value, 'JSON字段重复：' + key)
        value[key] = item
    return value


def decode(raw):
    def invalid(value):
        raise ValueError('JSON含非法数值：' + value)
    value = json.loads(raw.decode('utf-8'), object_pairs_hook=unique, parse_constant=invalid)
    require(type(value) is dict, 'JSON原件须为对象。')
    return value


def regular(path, inside=True):
    path = Path(path).absolute()
    if inside:
        path.resolve().relative_to(ROOT)
    for item in (path, *path.parents):
        require(not item.is_symlink() and not (hasattr(item, 'is_junction') and item.is_junction()), '路径经过链接。')
        if item == ROOT or item == item.parent:
            break
    info = path.stat()
    require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, '原件不是单链接普通文件：' + str(path))
    return path


class Originals:
    """保存实读输入的摘要，连接前和解析前核对它们没有改变。"""
    def __init__(self):
        self.seen = {}

    def read(self, path, maximum=TEXT_LIMIT, inside=True):
        path = regular(path, inside)
        require(path.stat().st_size <= maximum, '原件超过读取上限：' + str(path))
        with path.open('rb') as stream:
            raw = stream.read(maximum + 1)
        require(len(raw) <= maximum, '读取时原件增大。')
        record = {'path': str(path), 'sha256': digest(raw), 'bytes': len(raw)}
        require(path not in self.seen or self.seen[path] == record, '读取期间输入改变。')
        self.seen[path] = record
        return raw, record

    def record(self, item):
        require(type(item) is dict and set(item) == {'path', 'sha256', 'bytes'}, '原件记录字段不符。')
        name = item['path']
        require(isinstance(name, str) and re.fullmatch(r'[A-Za-z0-9_./-]+', name)
                and not name.startswith('/') and all(p not in ('', '.', '..') for p in name.split('/')), '原件相对路径不规范。')
        require(type(item['bytes']) is int and 0 <= item['bytes'] <= TEXT_LIMIT
                and isinstance(item['sha256'], str) and SHA.fullmatch(item['sha256'])
                and item['sha256'] != '0' * 64, '原件记录未固定。')
        raw, actual = self.read(ROOT / name)
        require(actual['sha256'] == item['sha256'] and actual['bytes'] == item['bytes'], '原件摘要不符：' + name)
        return raw

    def unchanged(self):
        for path, expected in list(self.seen.items()):
            _, current = self.read(path, inside=path != KNOWN)
            require(current == expected, '实际输入已变化。')


def receipt_fields(raw):
    require(raw and len(raw) <= 65536 and raw.endswith(b'\n') and not raw.endswith(b'\n\n')
            and b'\r' not in raw, '阶段回执不是有界LF原文。')
    value = {}
    for line in raw.decode('ascii').split('\n')[:-1]:
        key, separator, item = line.partition('=')
        require(separator and re.fullmatch(r'[A-Z][A-Z0-9_]*', key) and key not in value
                and item and all(32 <= ord(c) < 127 for c in item), '阶段回执字段不规范。')
        value[key] = item
    return value


def phase(originals, name, authorization_path, core_record, core):
    raw, receipt_record = originals.read(CAP / (name + '.receipt'))
    values = receipt_fields(raw)
    log, log_record = originals.read(CAP / (name + '.log'))
    exit_raw, exit_record = originals.read(CAP / (name + '.exit'))
    require(exit_raw == b'0\n', name + '整阶段实际退出不是规范LF0。')
    auth_raw, auth_record = originals.read(authorization_path)
    auth = decode(auth_raw)
    expected_core = {'path': CORE.relative_to(ROOT).as_posix(), 'sha256': CORE_SHA, 'bytes': CORE_BYTES}
    require(auth.get('format') == 'sn100-integration-12-01-runtime-phase-authorization-v1'
            and auth.get('frozen') is True and auth.get('allowed') is True and auth.get('phase') == name
            and auth.get('core_inputs') == expected_core, name + '授权未真实固定到本轮核心。')
    require(bool(originals.record(auth['root_approval'])), '阶段Root批准原件为空。')
    expected = {'FORMAT': 'sn100-integration-12-01-runtime-receipt-v1', 'PHASE': name,
                'TRANSITION_ID': 'integration_12_01_runtime_01', 'BOARD': 'board_b', 'SERIAL': SERIAL,
                'BOOT_ID': core['fresh_baseline']['boot_id'], 'CORE_INPUTS_SHA256': core_record['sha256'],
                'AUTHORIZATION_SHA256': auth_record['sha256'], 'EXIT_CODE': '0',
                'ROOT_LAUNCH_INTENT_SHA256': core['stable_target_intent']['sha256'],
                'CONFIGURATION_MANIFEST_SHA256': 'ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c',
                'CURRENT_AFTER': 'releases/' + RELEASE}
    baseline = core['fresh_baseline']
    controller = baseline['controller']
    expected['CONTROLLER_IDENTITY'] = ':'.join((baseline['boot_id'], controller['pid'], controller['start_ticks'],
                                               controller['uid'], controller['exe'], controller['sha256'], controller['listen_endpoints']))
    camera_rows = ''.join('|'.join(row[k] for k in ('role', 'pid', 'start_ticks', 'uid', 'exe', 'sha256')) + '\n'
                          for row in baseline['camera_processes'])
    camera_rows += '|'.join(baseline['camera_service'][k] for k in ('unit', 'active_state', 'sub_state', 'main_pid')) + '\n'
    expected['CAMERA_IDENTITY_SHA256'] = digest(camera_rows.encode('ascii'))
    require(all(values.get(k) == v for k, v in expected.items()), name + '回执与实际授权/core/版本不符。')
    require(log.endswith(('RUNTIME_RECEIPT_SHA256=' + receipt_record['sha256'] + '\n').encode('ascii') + raw), '完整日志没有同一回执原文。')
    lines = log.decode('utf-8').splitlines()
    for key in ('PHASE', 'TRANSITION_ID', 'CORE_INPUTS_SHA256', 'AUTHORIZATION_SHA256'):
        require(lines.count(key + '=' + values[key]) == 1, '回执日志关键身份缺失或重复。')
    require(lines.count('RUNTIME_RECEIPT_SHA256=' + receipt_record['sha256']) == 1, '回执摘要缺失或重复。')
    return values, auth, receipt_record, auth_record, log_record, exit_record


def remote_script(core, live):
    capture = core['observation']['capture_leaf']
    flat = BASE + '/releases/' + RELEASE
    values = {'capture': capture, 'boot': live['BOOT_ID'], 'pid': live['NEW_PID'],
              'ticks': live['NEW_START_TICKS'], 'exe': flat + '/sn100_shell',
              'flat': flat, 'release': RELEASE, 'app_sha': APP_SHA}
    text = 'set -Eeuo pipefail\n' + ''.join(k + '=' + shlex.quote(v) + '\n' for k, v in values.items())
    text += r'''
check_live() {
    [[ $(tr -d '\0\n' </proc/device-tree/serial-number) == 8314b1e8ffd0321f ]]
    [[ $(cat /proc/sys/kernel/random/boot_id) == "$boot" ]]
    [[ $pid =~ ^[1-9][0-9]*$ && $ticks =~ ^[1-9][0-9]*$ ]]
    local before after rest later current_exe
    local -a fields=() later_fields=()
    before=$(cat "/proc/$pid/stat")
    rest=${before##*) }; [[ $rest != "$before" ]]
    IFS=' ' read -r -a fields <<<"$rest"
    [[ ${fields[19]-} == "$ticks" && ${fields[0]-} != Z && ${fields[0]-} != X ]]
    [[ $(awk '/^Uid:/{print $3}' "/proc/$pid/status") == "$(id -u lckfb)" ]]
    current_exe=$(readlink "/proc/$pid/exe"); [[ $current_exe == "$exe" ]]
    [[ $(sha256sum "/proc/$pid/exe" | cut -d' ' -f1) == "$app_sha" ]]
    [[ -L /home/rkshare/programs/sn100_shell/current ]]
    [[ $(readlink /home/rkshare/programs/sn100_shell/current) == "releases/$release" ]]
    [[ $(readlink -f /home/rkshare/programs/sn100_shell/current) == "$flat" ]]
    [[ -f $flat/release_id && ! -L $flat/release_id && $(stat -c %h "$flat/release_id") == 1 ]]
    [[ $(cat "$flat/release_id") == "$release" ]]
    after=$(cat "/proc/$pid/stat"); later=${after##*) }; [[ $later != "$after" ]]
    IFS=' ' read -r -a later_fields <<<"$later"
    [[ ${later_fields[19]-} == "$ticks" && ${later_fields[0]-} != Z && ${later_fields[0]-} != X ]]
}
check_live
[[ -d $capture && ! -L $capture && $(readlink -f "$capture") == "$capture" ]]
'''
    for name, maximum in LIMITS.items():
        target = shlex.quote(capture + '/' + name)
        text += '[[ -f ' + target + ' && ! -L ' + target + ' && $(stat -c %h ' + target + ') == 1 ]]\n'
        text += 'size=$(stat -c %s ' + target + '); [[ $size -gt 0 && $size -le ' + str(maximum) + ' ]]\n'
    text += 'tar -czf - -C "$capture" -- ' + ' '.join(shlex.quote(n) for n in sorted(LIMITS)) + '\n'
    text += 'check_live\nprintf "READ_ONLY_SAME_LIVE_PROCESS_AFTER_COLLECTION=1\\n" >&2\n'
    return text.encode('ascii')


def exclusive(path, raw):
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0)
    with os.fdopen(os.open(path, flags, 0o640), 'wb') as stream:
        stream.write(raw)


def capture_process(argv, out, prefix, input_raw=None, archive=False):
    """原输出直接落独占文件；读取压缩包前再检查大小，不把网络输出整包装入内存。"""
    stdout = out / (prefix + ('.stdout.tar.gz' if archive else '.stdout.log'))
    stderr = out / (prefix + '.stderr.log')
    exclusive(out / (prefix + '.command.json'), (json.dumps(argv, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    with stdout.open('xb') as so, stderr.open('xb') as se:
        result = subprocess.run(argv, input=input_raw, stdout=so, stderr=se, cwd=ROOT)
    exclusive(out / (prefix + '.actual_exit'), (str(result.returncode) + '\n').encode('ascii'))
    return result.returncode, stdout, stderr


def unpack_bounded(path):
    require(0 < path.stat().st_size <= ARCHIVE_LIMIT, '压缩输出超出2MiB上限；原件已保留。')
    with gzip.open(path, 'rb') as stream:
        raw = stream.read(ARCHIVE_LIMIT + 1)
    require(len(raw) <= ARCHIVE_LIMIT, '解压数据超出2MiB上限；不展开文件。')
    values = {}
    with tarfile.open(fileobj=io.BytesIO(raw), mode='r:') as bundle:
        for member in bundle:
            require(member.name in LIMITS and member.name not in values and len(values) < 6
                    and member.type in (tarfile.REGTYPE, tarfile.AREGTYPE)
                    and not member.pax_headers and not member.issparse()
                    and 0 < member.size <= LIMITS[member.name], 'tar成员不是约定的六份有界普通文件。')
            source = bundle.extractfile(member)
            require(source is not None, 'tar成员不能读取。')
            data = source.read(LIMITS[member.name] + 1)
            require(len(data) == member.size, 'tar成员字节数不符。')
            values[member.name] = data
    require(set(values) == set(LIMITS), '没有准确收到六份封口原件。')
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--launch-authorization', type=Path, default=HERE / 'authorization_launch_01.json')
    parser.add_argument('--observe-authorization', type=Path, default=HERE / 'authorization_observe_01.json')
    parser.add_argument('--output', type=Path, default=HERE / 'lis_collection_01')
    args = parser.parse_args()
    originals = Originals()
    core_raw, core_record = originals.read(CORE)
    require(len(core_raw) == CORE_BYTES and core_record['sha256'] == CORE_SHA, '冻结核心不是获审3446原件。')
    core = decode(core_raw)
    require(core.get('frozen') is True and core.get('base') == BASE and core.get('board_serial') == SERIAL
            and core['target']['release_id'] == RELEASE and core['target']['application_sha256'] == APP_SHA
            and core['observation']['id'] == OBSERVATION, '核心目标或观察ID不符。')
    launch, launch_auth, launch_rec, launch_auth_rec, launch_log, launch_exit = phase(originals, 'launch', args.launch_authorization, core_record, core)
    observe, observe_auth, _, _, _, _ = phase(originals, 'observe', args.observe_authorization, core_record, core)
    require(observe_auth['previous_receipt'] == {'path': (CAP / 'launch.receipt').relative_to(ROOT).as_posix(),
            'sha256': launch_rec['sha256'], 'bytes': launch_rec['bytes']}, 'observe未承接实际launch回执。')
    require(observe_auth['previous_authorization'] == {'path': regular(args.launch_authorization).relative_to(ROOT).as_posix(),
            'sha256': launch_auth_rec['sha256'], 'bytes': launch_auth_rec['bytes']}, 'observe未承接实际launch授权。')
    for key, item, name in (('previous_phase_exit', launch_exit, 'launch.exit'), ('previous_phase_log', launch_log, 'launch.log')):
        require(observe_auth[key] == {'path': (CAP / name).relative_to(ROOT).as_posix(),
                'sha256': item['sha256'], 'bytes': item['bytes']}, 'observe未承接同一launch日志/退出原件。')
    for key in ('NEW_RUN', 'NEW_PID', 'NEW_START_TICKS', 'OLD_STOP_RECEIPT_SHA256'):
        require(launch.get(key) == observe.get(key) and launch.get(key), '启动与观察没有绑定同一新身份：' + key)
    require(re.fullmatch(r'[1-9][0-9]*', launch['NEW_PID']) and re.fullmatch(r'[1-9][0-9]*', launch['NEW_START_TICKS'])
            and re.fullmatch(re.escape(BASE) + r'/runs/[0-9_]+', launch['NEW_RUN']), '新进程身份格式不符。')
    for key, expected in {'UNIQUE_START_REQUEST_EXIT': '0', 'NEW_PROCESS_IDENTIFIED': '1', 'SOFT_DEADLINE_SECONDS': '0',
                          'APPLICATION_EXIT': 'not_evaluated', 'NEW_EXE': BASE + '/releases/' + RELEASE + '/sn100_shell',
                          'NEW_EXE_SHA256': APP_SHA}.items():
        require(launch.get(key) == expected, 'launch缺真实启动事实：' + key)
    for key, expected in {'WAITER_ACTUAL_EXIT': '0', 'SHELL_READY': '1', 'SAME_NEW_APPLICATION_RUNNING': '1',
                          'APPLICATION_EXIT': 'not_evaluated', 'CLOSE_REQUESTED': '0',
                          'CAPTURE_DIRECTORY': core['observation']['capture_leaf']}.items():
        require(observe.get(key) == expected, 'observe缺真实封口事实：' + key)
    require(core['observation']['capture_leaf'] == core['observation']['capture_parent'] + '/lis_01'
            and core['observation']['capture_parent'] == core['metadata_directory'] + '/captures', '采集叶目录不属于冻结核心。')
    for auth in (launch_auth, observe_auth):
        require(auth['old_stop_receipt']['path'] == (CAP / 'stop.receipt').relative_to(ROOT).as_posix(), '旧停回执不是本轮固定路径。')
        stop_raw = originals.record(auth['old_stop_receipt'])
        require(digest(stop_raw) == launch['OLD_STOP_RECEIPT_SHA256'], '旧停回执来源不一致。')
    parser_raw, _ = originals.read(PARSER)
    require(digest(parser_raw) == PARSER_SHA, '独立解析器不是已审a0ea原件。')
    originals.read(KNOWN, inside=False)
    out = args.output.absolute()
    require(out.parent == HERE and re.fullmatch(r'[A-Za-z0-9_-]{1,64}', out.name)
            and not out.exists() and not out.is_symlink(), '输出必须是本目录下全新单层目录。')
    originals.unchanged()
    out.mkdir(mode=0o750)
    key_code, key_stdout, _ = capture_process(['ssh-keygen', '-E', 'sha256', '-l', '-f', str(KNOWN)], out, 'known_host_pin')
    require(key_code == 0 and key_stdout.stat().st_size <= 16384, '本地SSH指纹核对命令失败。')
    rows = key_stdout.read_text(encoding='ascii').splitlines()
    require(rows and all(len(row.split()) >= 2 for row in rows)
            and {row.split()[1] for row in rows} == {PIN}, '固定known_hosts不是已核板B SSH指纹。')
    originals.unchanged()
    script = remote_script(core, launch)
    exclusive(out / 'collect_sealed.command.sh', script)
    exclusive(out / 'input_records.json', (json.dumps(list(originals.seen.values()), ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    code, archive, _ = capture_process(['ssh', *OPTIONS, 'root@192.168.100.123', 'bash -s'], out,
                                     'collect_sealed', input_raw=script, archive=True)
    if code != 0:
        return code
    data = unpack_bounded(archive)
    local = out / 'capture_01'
    local.mkdir(mode=0o750)
    for name, raw in data.items():
        exclusive(local / name, raw)
    originals.unchanged()
    argv = [sys.executable, '-B', '-X', 'utf8', str(PARSER), '--capture-directory', str(local)]
    expected = {'board-serial': SERIAL, 'boot-id': launch['BOOT_ID'], 'pid': launch['NEW_PID'],
                'start-ticks': launch['NEW_START_TICKS'], 'release-id': RELEASE,
                'executable': launch['NEW_EXE'], 'executable-sha256': APP_SHA, 'current-link': BASE + '/current',
                'expected-current-target': BASE + '/releases/' + RELEASE,
                'release-id-file': BASE + '/releases/' + RELEASE + '/release_id', 'observation-id': OBSERVATION}
    for key, value in expected.items():
        argv.extend(['--' + key, value])
    review_code, _, _ = capture_process(argv, out, 'parser')
    print('PARSER_ACTUAL_EXIT=' + str(review_code), flush=True)
    return review_code


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, OSError, KeyError, TypeError, UnicodeError, tarfile.TarError, EOFError) as error:
        print('拒绝采集或解析：' + str(error), file=sys.stderr)
        sys.exit(2)
