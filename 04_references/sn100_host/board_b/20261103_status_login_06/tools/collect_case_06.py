#!/usr/bin/env python3
"""取回已结束的06自动专项；默认只核本地输入，--execute才只读访问板B。

执行须提供总协调已通知实际退出及远端结果路径的授权记录。不启停、重跑、
部署或切换程序，不读取业务数据库和配置内容。完整原采集先核SHA，正式归档另做有限选择。
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tarfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
RELEASE = '14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c'
APP = 'f3e814b4110b14ddbaef8c4516ab8a5b1c43747c45dcd4fff54ad3af0806ec6c'
CHECKER = 'c920b7ca80ba0bd5f810ef6f57f4ff7e94f34abac32c14fe1820c449fe7c8021'
SERIAL = '8314b1e8ffd0321f'
FINGERPRINT = 'SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg'
HOST = 'root@192.168.100.123'
REMOTE_ROOT = '/home/rkshare/programs/sn100_shell/verification/status_login_06_20261103/runs/'
LOCAL_ROOT = ROOT / 'temp/sn100_host/workflow_night_20261102/integration_06_live'
OPENSSH = Path('/mnt/c/Windows/System32/OpenSSH')
KNOWN = Path('/mnt/c/Users/52400/.ssh/known_hosts_rk3576_board_b')
OPTIONS = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'HostKeyAlias=rk3576-board-b', '-o',
           'UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b']
ROOT_FILES = {'app.log', 'application_exit.txt', 'case_exit.txt', 'clock_ticks_per_second.txt',
              'controller_ini_before.sha256', 'controller_ini_check.txt', 'ended_at.txt', 'identity.txt',
              'ini_before.sha256', 'ini_check.txt', 'preflight.log', 'resources.tsv', 'started_at.txt',
              'wrapper_exit.txt', 'shutdown_contract.json', 'required_shutdown_markers.txt',
              'forbidden_shutdown_markers.txt', 'shutdown_contract_release_id', 'shutdown_contract_files.sha256'}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def new_text(path, value):
    with Path(path).open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(value)


def new_json(path, value):
    new_text(path, json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n')


def safe_name(name):
    name = name.removeprefix('./')
    path = PurePosixPath(name)
    require(name and re.fullmatch(r'[A-Za-z0-9_./-]+', name) is not None
            and not path.is_absolute() and all(p not in ('', '.', '..') for p in name.split('/')),
            '采集路径不安全：' + repr(name))
    allowed = (name in ROOT_FILES or name == 'runtime/kms.json'
               or name.startswith('runtime/cache/')
               or re.fullmatch(r'results/(?:[A-Za-z0-9_-]+\.png|result\.json|check_exit\.txt)', name))
    require(allowed and path.suffix.lower() not in ('.db', '.sqlite', '.sqlite3', '.ini', '.pem', '.key'),
            '出现本轮未授权的采集对象，先审查：' + name)
    return name


def local_check():
    prepared = read(HERE / 'prepared_inputs.json')
    built = read(HERE / 'execution_06_result.json')
    binding = read(HERE / 'link_provenance.json')
    require(prepared['release_id'] == built['release_id'] == binding['release_id'] == RELEASE,
            '本地输入不是最终06')
    require(built['checker_built'] and binding['source_count'] == 649 and binding['object_count'] == 267
            and binding['main_object_kept'] and built['checker_sha256'] == sha(HERE / 'status_login_review_06') == CHECKER,
            '649源码/267对象及检查器绑定不符')
    require(binding['record_sha256'] == built['link_record_sha256']
            and sha(HERE / 'link_provenance.json') == built['link_provenance_sha256']
            and sha(HERE / 'source_snapshot_06.json') == prepared['frozen_snapshot_sha256'],
            '实际源码/对象记录与本轮成功构建不符')
    require(prepared['formal_identity']['application_sha256'] == APP, '正式应用身份不符')
    for name, digest in prepared['template_sha256'].items():
        require(sha(HERE / name) == digest, '已绑定工具发生变化：' + name)
    for name, digest in prepared['contract_sha256'].items():
        require(sha(HERE / name) == digest, '已绑定合同发生变化：' + name)
    contract = read(HERE / 'shutdown_contract.json')
    require(len(contract['required_markers']) == 17
            and 'ORIGINAL_CHANNEL_WORKER_STOPPED true' in contract['required_markers'], '不是本轮完整退出条件')
    return prepared


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--authorization', type=Path, help='总协调已通知实际结束及路径的本地JSON记录')
    args = parser.parse_args()
    prepared = local_check()
    if not args.execute:
        print('LOCAL_COLLECTION_TOOLS_READY=1; NETWORK_ACCESSED=0; ACTUAL_CASE_NOT_COLLECTED=1')
        return 0
    require(args.authorization is not None and args.authorization.is_absolute(), '执行前须有实际结束通知记录')
    authorization = read(args.authorization)
    require(authorization['source_thread_id'] == '01a19c11-0cd5-7143-86a1-adaa47ced125'
            and authorization['release_id'] == RELEASE and authorization['case_finished'] is True
            and authorization['read_only_collection_authorized'] is True, '缺少本轮真实结束和只读采集授权')
    case = authorization['case_name']
    require(re.fullmatch(r'auto_[A-Za-z0-9_]{2,48}', case), '只能采集本轮已通知的自动专项')
    remote = REMOTE_ROOT + case
    require(authorization['remote_run_directory'] == remote and isinstance(authorization['actual_case_exit'], int),
            '远端路径/实际退出码未明确，不猜测')
    destination = LOCAL_ROOT / case
    require(not destination.exists() and not destination.is_symlink(), '本地采集目录已存在，不覆盖')
    require(KNOWN.is_file() and (OPENSSH / 'ssh.exe').is_file(), '缺少已固定的Windows SSH输入')
    fingerprints = subprocess.check_output([str(OPENSSH / 'ssh-keygen.exe'), '-lf',
                                            'C:/Users/52400/.ssh/known_hosts_rk3576_board_b'], text=True)
    require(FINGERPRINT in fingerprints, '本地固定主机指纹不符，未连接')
    known_sha = sha(KNOWN)
    destination.mkdir(parents=True)
    new_json(destination / 'collection_authorization.json', authorization)

    def remote_step(name, body, binary=False):
        require(sha(KNOWN) == known_sha, '固定主机文件变化，停止采集')
        script = ('set -Eeuo pipefail\n'
                  'test "$(tr -d \'\\0\\n\' </proc/device-tree/serial-number)" = ' + shlex.quote(SERIAL) + '\n'
                  'run=' + shlex.quote(remote) + '\n'
                  'test "$(readlink -f -- "$run")" = "$run"\n'
                  'test -f "$run/ended_at.txt" && test -f "$run/case_exit.txt"\n'
                  'test "$(cat "$run/case_exit.txt")" = ' + shlex.quote(str(authorization['actual_case_exit'])) + '\n'
                  'grep -qx ' + shlex.quote('MODE=auto') + ' "$run/identity.txt"\n'
                  'grep -qx ' + shlex.quote('RELEASE_ID=' + RELEASE) + ' "$run/identity.txt"\n'
                  'grep -qx ' + shlex.quote('APP_SHA256=' + APP) + ' "$run/identity.txt"\n'
                  'grep -qx ' + shlex.quote('PROBE_SHA256=' + CHECKER) + ' "$run/identity.txt"\n'
                  'test -z "$(find "$run" -type l -print -quit)"\n'
                  'test "$(du -sb -- "$run" | cut -f1)" -le 134217728\n'
                  'cd -- "$run"\n' + body)
        suffix = '.tar.gz' if binary else '.txt'
        output = destination / (name + suffix)
        with output.open('xb') as stream, (destination / (name + '.stderr')).open('xb') as error:
            result = subprocess.run([str(OPENSSH / 'ssh.exe'), *OPTIONS, HOST, 'bash -s'],
                                    input=script.encode('utf-8'), stdout=stream, stderr=error)
        new_text(destination / (name + '.exit'), str(result.returncode) + '\n')
        require(result.returncode == 0, '只读取回失败；保留本次结果，不能覆盖重试：' + name)
        return output

    hash_command = 'find . -type f -print0 | sort -z | xargs -0 -r sha256sum\n'
    before = remote_step('remote_hashes_before', hash_command)
    entries = {}
    for line in before.read_text(encoding='utf-8').splitlines():
        digest, name = line.split('  ', 1)
        name = safe_name(name)
        require(re.fullmatch(r'[a-f0-9]{64}', digest) and name not in entries, '远端SHA格式或重复路径异常')
        entries[name] = digest
    require(1 <= len(entries) <= 512, '采集文件数量异常')
    archive = remote_step('remote_raw', 'tar -czf - -- ' + ' '.join(shlex.quote(n) for n in sorted(entries)) + '\n', True)
    actual = set()
    with tarfile.open(archive, 'r:gz') as bundle:
        total = 0
        for member in bundle.getmembers():
            name = safe_name(member.name)
            require(member.isfile() and not member.issym() and not member.islnk()
                    and name in entries and name not in actual and 0 <= member.size <= 67108864, '取回包包含非预期条目')
            total += member.size
            require(total <= 134217728, '取回包展开大小异常')
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            require(target.parent.resolve().is_relative_to(destination), '取回包路径越界')
            with bundle.extractfile(member) as source, target.open('xb') as output:
                for block in iter(lambda: source.read(1048576), b''):
                    output.write(block)
            require(sha(target) == entries[name], '取回文件SHA不符：' + name)
            actual.add(name)
    require(actual == set(entries), '取回包缺少远端清单条目')
    after = remote_step('remote_hashes_after', hash_command)
    require(before.read_bytes() == after.read_bytes(), '只读取回期间结果发生变化，不能当固定采集')
    new_text(destination / 'remote_capture.sha256', before.read_text(encoding='utf-8'))
    postcheck = remote_step('postcheck', '''base=/home/rkshare/programs/sn100_shell
date -Is
printf 'BOARD_SERIAL=%s\\n' "$(tr -d '\\0\\n' </proc/device-tree/serial-number)"
printf 'CURRENT=%s\\n' "$(readlink -f -- "$base/current")"
apps=0; checkers=0; controllers=0
for exe in /proc/[0-9]*/exe; do
    path=$(readlink "$exe" 2>/dev/null || true)
    case "$path" in
        */sn100_shell|*/sn100_report_renderer|*/sn100_shell\\ \\(deleted\\)) apps=$((apps+1)) ;;
        */status_login*review*) checkers=$((checkers+1)) ;;
        /userdata/shared/ACA_V1.0OffLineD-500)
            controllers=$((controllers+1)); pid=${exe#/proc/}; pid=${pid%/exe}
            printf 'CONTROLLER_PID=%s\\nCONTROLLER_EXE=%s\\n' "$pid" "$path"
            printf 'CONTROLLER_SHA256=%s\\n' "$(sha256sum "$exe" | cut -d' ' -f1)" ;;
    esac
done
printf 'APPLICATION_COUNT=%s\\nCHECKER_COUNT=%s\\nCONTROLLER_COUNT=%s\\n' "$apps" "$checkers" "$controllers"
printf 'PORT_5000_BEGIN\\n'
ss -H -ltnp 'sport = :5000'
printf 'PORT_5000_END\\n'
set +e
sha256sum -c "$run/ini_before.sha256"; ini_code=$?
sha256sum -c "$run/controller_ini_before.sha256"; controller_ini_code=$?
set -e
printf 'POSTCHECK_INI_EXIT=%s\\nPOSTCHECK_CONTROLLER_INI_EXIT=%s\\n' "$ini_code" "$controller_ini_code"
''')
    for name in prepared['contract_sha256']:
        require(sha(destination / name) == prepared['contract_sha256'][name], '本次远端合同与最终06不符：' + name)
    with (destination / 'parser.log').open('xb') as stream:
        parsed = subprocess.run([sys.executable, '-X', 'utf8', str(HERE / 'summarize_run.py'), str(destination)],
                                stdout=stream, stderr=subprocess.STDOUT)
    new_text(destination / 'parser.exit', str(parsed.returncode) + '\n')
    summary = destination / 'parsed_summary.json'
    if not summary.exists():
        new_json(summary, {'schema': 'sn100-status-login-06-parser-unavailable-v1', 'release_id': RELEASE,
                           'software_checks_passed': False, 'actual_parser_exit': parsed.returncode,
                           'reason': '解析器未生成汇总；只保留实际失败和原采集，不能声明通过'})
    files, excluded = [], []
    for name, digest in sorted(entries.items()):
        item = {'path': name, 'sha256': digest, 'size': (destination / name).stat().st_size,
                'source': 'remote', 'local_verified': True}
        if name.startswith('runtime/cache/'):
            excluded.append(dict(item, reason='已取回临时目录并核SHA；仅从长期归档排除GPU/字体缓存'))
        else:
            files.append(item)
    new_json(destination / 'collection.json', {
        'schema': 'sn100-status-login-06-collection-v1', 'case_name': case, 'mode': 'auto',
        'board': {'name': 'board_b', 'serial': SERIAL}, 'release_id': RELEASE,
        'application_sha256': APP, 'checker_sha256': CHECKER, 'source_count': 649, 'object_count': 267,
        'local_run_directory': str(destination), 'remote_run_directory': remote,
        'collected_at_utc': datetime.now(timezone.utc).isoformat(), 'collection_complete': True,
        'collector_exit_code': 0, 'all_remote_files_downloaded_and_verified': True,
        'remote_manifest': {'path': 'remote_capture.sha256', 'sha256': sha(destination / 'remote_capture.sha256'),
                            'file_count': len(entries)}, 'files': files, 'excluded': excluded,
        'parser': {'path': 'parsed_summary.json', 'sha256': sha(summary), 'exit_code': parsed.returncode, 'encoding': 'utf-8'},
        'postcheck': {'path': postcheck.name, 'sha256': sha(postcheck), 'exit_code': 0,
                      'scope': '实际进程/主控/5000/current与原INI指纹，只读；命令成功不等于每项核对通过'},
        'host': HOST, 'host_fingerprint': FINGERPRINT, 'known_hosts_sha256': known_sha,
        'read_only_remote_commands': True, 'product_started_or_stopped': False, 'current_changed': False})
    print('COLLECTION_COMPLETE=1; FILES_VERIFIED=' + str(len(entries)) + '; PARSER_EXIT=' + str(parsed.returncode))
    print('COLLECTION_JSON=' + str(destination / 'collection.json'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
