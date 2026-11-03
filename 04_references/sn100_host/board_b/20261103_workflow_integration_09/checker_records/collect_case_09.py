#!/usr/bin/env python3
"""只读取回已完成的09专项。先核远端元数据，只保存必要文件，禁止启停或写配置。"""
import argparse
import hashlib
import json
import re
import shlex
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit, urlunsplit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
RELEASE = None
APP = None
CHECKER = None
SERIAL = '8314b1e8ffd0321f'
PIN = 'SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg'
REMOTE = None
LOCAL = ROOT / 'temp/sn100_host/workflow_night_20261102/integration_09_live/auto_01'
KNOWN = Path('/mnt/c/Users/52400/.ssh/known_hosts_rk3576_board_b')
SSH = '/mnt/c/Windows/System32/OpenSSH/ssh.exe'
OPTIONS = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'HostKeyAlias=rk3576-board-b', '-o',
           'UserKnownHostsFile=C:/Users/52400/.ssh/known_hosts_rk3576_board_b']
TEXT_FILES = {'app.log', 'application_exit.txt', 'case_exit.txt', 'clock_ticks_per_second.txt',
              'controller_ini_before.sha256', 'controller_ini_check.txt', 'ended_at.txt', 'identity.txt',
              'ini_before.sha256', 'ini_check.txt', 'preflight.log', 'resources.tsv', 'started_at.txt',
              'wrapper_exit.txt', 'shutdown_contract.json', 'required_shutdown_markers.txt',
              'forbidden_shutdown_markers.txt', 'shutdown_contract_release_id', 'shutdown_contract_files.sha256',
              'runtime/kms.json', 'results/result.json', 'results/check_exit.txt', 'manifest_access.txt',
              'alarm_rules_before.sha256', 'alarm_rules_check.txt'}
IMAGE_FILES = {'results/ad_current_generation.png', 'results/connected_header.png',
               'results/disconnected_header.png', 'results/disconnected_lamp_first.png',
               'results/disconnected_lamp_second.png', 'results/disconnected_lamp.png',
               'results/instrument_actual_status.png'}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(data)


def save(name, value):
    write(LOCAL / name, (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8'))


def safe_name(name):
    name = name.removeprefix('./')
    p = PurePosixPath(name)
    require(name and re.fullmatch(r'[A-Za-z0-9_./-]+', name) and not p.is_absolute()
            and all(part not in ('', '.', '..') for part in name.split('/')), '远端文件名越界')
    return name


def scrub(data):
    """先在内存检查文本；认证值不落盘，不输出命中原文。协议十六进制保持原样。"""
    text = data.decode('utf-8')
    changes = []
    pattern = re.compile(r'(?i)([\"\']?(?:password|passwd|pwd|secret|token|api[_-]?key|authorization)[\"\']?\s*[:=]\s*)([\"\'][^\"\'\r\n]*[\"\']|[^\s,;}]+)')
    def field(match):
        if match[2].strip('\"\'') == '':
            return match[0]
        changes.append('credential_value')
        return match[1] + '"[REDACTED]"'
    text = pattern.sub(field, text)
    token = re.compile(r'\b(?:eyJ[A-Za-z0-9_-]{12,}\.[A-Za-z0-9_-]{12,}\.[A-Za-z0-9_-]{10,}|sk-[A-Za-z0-9_-]{20,}|gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|glpat-[A-Za-z0-9_-]{15,}|xox[baprs]-[A-Za-z0-9-]{15,})\b')
    def secret(match):
        changes.append('token_like_value')
        return '[REDACTED]'
    text = token.sub(secret, text)
    def url(match):
        value = match[0]
        parsed = urlsplit(value)
        if parsed.username is None and not parsed.query:
            return value
        changes.append('url_userinfo_or_query')
        host = parsed.netloc.rsplit('@', 1)[-1]
        return urlunsplit((parsed.scheme, host, parsed.path, '[REDACTED]' if parsed.query else '', parsed.fragment))
    text = re.sub(r'https?://[^\s\"\'<>]+', url, text)
    return text.encode('utf-8'), changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--remote-case')
    parser.add_argument('--expected-dispatch-exit', type=int, default=0)
    parser.add_argument('--expected-case-exit', type=int, default=0)
    args = parser.parse_args()
    global RELEASE, APP, CHECKER, REMOTE, LOCAL
    prepared, built = read(HERE / 'prepared_inputs.json'), read(HERE / 'execution_09_result.json')
    RELEASE = prepared['release_id']
    APP = prepared['formal_identity']['application_sha256']
    CHECKER = built['checker_sha256']
    require(prepared['schema'] == 'sn100-review-09-v1' and re.fullmatch(r'[0-9a-f]{64}', RELEASE), '尚未绑定09正式发布')
    require(prepared['release_id'] == built['release_id'] == RELEASE and built['checker_built']
            and sha(HERE / 'status_login_review_09') == CHECKER, '09本地发布绑定不符')
    handoff = read(HERE / 'final_09_handoff.json')
    require(handoff['schema'] == 'sn100-final-09-checker-handoff-v1'
            and handoff['release_id'] == RELEASE and handoff['checker_sha256'] == CHECKER
            and handoff['source_count'] == prepared['source_count'] == built['source_count']
            and handoff['object_count'] == built['object_count'], '本轮最终交接与实际构建绑定不符')
    contract = read(HERE / 'shutdown_contract.json')
    require(sha(HERE / 'shutdown_contract.json') == handoff['contract_sha256']
            and len(contract['required_markers']) == handoff['required_marker_count']
            and 'TASK_REAGENT_SHUTDOWN_READY' in contract['required_markers']
            and handoff['new_construction_proven'] is False and handoff['same_main_memory_bool_read'] is False,
            '实际09合同或急诊验证范围不符')
    for group in ('template_sha256', 'contract_sha256'):
        require(all(sha(HERE / n) == d for n, d in prepared[group].items()), '冻结工具或合同已变')
    if not args.execute:
        print('LOCAL_COLLECTOR_READY=1; NETWORK_ACCESSED=0')
        return 0
    require(args.remote_case is not None and re.fullmatch(r'/home/rkshare/programs/sn100_shell/verification/status_login_09_[0-9]{8}/runs/auto_[A-Za-z0-9_]+', args.remote_case), '须给本轮09独立已完成case路径')
    REMOTE = args.remote_case
    LOCAL = LOCAL.parent / PurePosixPath(REMOTE).name
    require(-255 <= args.expected_dispatch_exit <= 255 and -255 <= args.expected_case_exit <= 255, '实际退出码范围非法')
    require(not LOCAL.exists(), '采集目录已存在，不覆盖或重取')
    dispatch = LOCAL.parent / 'auto_dispatch.log'
    require('RUN_DIRECTORY=' + REMOTE in dispatch.read_text()
            and re.search(r'(?m)^WRAPPER_EXIT=[0-9]+$', dispatch.read_text())
            and (LOCAL.parent / 'auto_dispatch.exit').read_text().strip() == str(args.expected_dispatch_exit), '尚无本次真实结束记录')
    public_pin = subprocess.check_output(['/mnt/c/Windows/System32/OpenSSH/ssh-keygen.exe', '-lf',
        'C:/Users/52400/.ssh/known_hosts_rk3576_board_b'], text=True)
    require(PIN in public_pin, '固定公开主机指纹不符')
    known_sha = sha(KNOWN)
    LOCAL.mkdir(parents=True)
    save('collection_authorization.json', {'source_thread_id': '01a1a05a-73c4-7380-b761-c35a7fdbf76e',
         'case_finished': True, 'release_id': RELEASE, 'remote_run_directory': REMOTE,
         'scope': '总协调明确授权只读采集此已完成case及必要后验；禁止重跑、启停、切current或新控制动作'})
    steps = []
    prefix = ('set -Eeuo pipefail\n'
              'test "$(tr -d \'\\0\\n\' </proc/device-tree/serial-number)" = ' + SERIAL + '\n'
              'run=' + shlex.quote(REMOTE) + '\n'
              'test "$(readlink -f -- "$run")" = "$run"\n'
              'test -f "$run/ended_at.txt" && test "$(cat "$run/case_exit.txt")" = ' + str(args.expected_case_exit) + '\n'
              'grep -qx ' + shlex.quote('MODE=auto') + ' "$run/identity.txt"\n'
              'grep -qx ' + shlex.quote('RELEASE_ID=' + RELEASE) + ' "$run/identity.txt"\n'
              'grep -qx ' + shlex.quote('APP_SHA256=' + APP) + ' "$run/identity.txt"\n'
              'grep -qx ' + shlex.quote('PROBE_SHA256=' + CHECKER) + ' "$run/identity.txt"\n'
              'test -z "$(find "$run" -type l -print -quit)"\ncd -- "$run"\n')
    def remote(kind, body, name=None):
        require(sha(KNOWN) == known_sha, '固定公开主机文件已变')
        result = subprocess.run([SSH, *OPTIONS, 'root@192.168.100.123', 'bash -s'],
                                input=(prefix + body).encode('utf-8'), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        error, changes = scrub(result.stderr)
        steps.append({'step': kind, 'file': name, 'actual_exit': result.returncode,
                      'stderr': error.decode('utf-8'), 'stderr_redactions': changes})
        require(result.returncode == 0, '只读采集步骤失败，保留现场：' + kind)
        return result.stdout
    listing = 'find . -type f -printf \'%s\\t%P\\n\' | sort\nprintf \'HASHES_BEGIN\\n\'\nfind . -type f -print0 | sort -z | xargs -0 -r sha256sum\n'
    before = remote('manifest_before', listing)
    sizes_text, hashes_text = before.decode('utf-8').split('HASHES_BEGIN\n', 1)
    sizes = {}
    for line in sizes_text.splitlines():
        size, name = line.split('\t', 1)
        name = safe_name(name)
        require(size.isdigit() and name not in sizes, '远端大小清单异常')
        sizes[name] = int(size)
    entries = {}
    for line in hashes_text.splitlines():
        value, name = line.split('  ', 1)
        name = safe_name(name)
        require(re.fullmatch(r'[a-f0-9]{64}', value) and name not in entries, '远端SHA清单异常')
        entries[name] = value
    require(set(entries) == set(sizes) and len(entries) <= 512 and sum(sizes.values()) <= 134217728,
            '远端清单数量/大小不符')
    write(LOCAL / 'remote_hashes_before.txt', before)
    write(LOCAL / 'remote_capture.sha256', hashes_text.encode('utf-8'))
    selected, excluded = [], []
    for name in sorted(entries):
        basic = {'path': name, 'sha256': entries[name], 'size': sizes[name], 'source': 'remote'}
        if name not in TEXT_FILES | IMAGE_FILES:
            excluded.append(dict(basic, local_verified=False,
                reason='未取回：图形/字体缓存或非必要登录画面及非白名单文件；只保留远端元数据'))
            continue
        raw = remote('read_selected_file', 'cat -- ' + shlex.quote(name) + '\n', name)
        require(len(raw) == sizes[name] and digest(raw) == entries[name], '选定文件与远端SHA不符：' + name)
        stored, redactions = scrub(raw) if name in TEXT_FILES else (raw, [])
        if name in IMAGE_FILES:
            require(raw.startswith(b'\x89PNG\r\n\x1a\n'), '选定数值/状态图不是PNG')
        write(LOCAL / name, stored)
        selected.append(dict(basic, local_verified=True, raw_sha=entries[name], local_sha=digest(stored),
                             stored_exactly_as_remote=stored == raw, redactions=redactions))
    after = remote('manifest_after', listing)
    require(before == after, '只读采集期间远端文件发生变化')
    write(LOCAL / 'remote_hashes_after.txt', after)
    # 这一个非患者规则CSV在case目录之外，单独核来源及字节，不混入远端case分区计数。
    rule_path = '/home/rkshare/programs/sn100_shell/runtime/data/db/systemInfo/abnormal_Config.csv'
    rule_command = ('rule=' + shlex.quote(rule_path) + '\n'
                    'test -f "$rule" && test ! -L "$rule" && test "$(readlink -f -- "$rule")" = "$rule"\n'
                    'test "$(stat -c %s "$rule")" -le 4194304\n'
                    'stat -c %s "$rule"\nsha256sum "$rule"\n')
    rule_before = remote('alarm_rule_metadata_before', rule_command)
    rule_lines = rule_before.decode('ascii').splitlines()
    require(len(rule_lines) == 2 and rule_lines[0].isdigit(), '规则CSV元数据格式不符')
    rule_sha, rule_name = rule_lines[1].split('  ', 1)
    require(re.fullmatch(r'[a-f0-9]{64}', rule_sha) and rule_name == rule_path, '规则CSV路径或SHA不符')
    require((LOCAL / 'alarm_rules_before.sha256').read_text().strip() == rule_lines[1],
            '规则CSV与本次启动前摘要不一致，不据旧规则判告警通过')
    rule_raw = remote('read_alarm_rule_csv', 'cat -- ' + shlex.quote(rule_path) + '\n')
    require(len(rule_raw) == int(rule_lines[0]) and digest(rule_raw) == rule_sha, '规则CSV读取字节不符')
    try:
        rule_text = rule_raw.decode('utf-8-sig')
        rule_encoding = 'utf-8-sig' if rule_raw.startswith(b'\xef\xbb\xbf') else 'utf-8'
    except UnicodeDecodeError:
        rule_text = rule_raw.decode('gb18030')
        require(rule_text.encode('gb18030') == rule_raw, '规则CSV不能按原中文编码无损读取')
        rule_encoding = 'gb18030'
    require(rule_text.splitlines() and rule_text.splitlines()[0].strip().startswith('id,error_code,describe_cn,measure,'),
            '只允许原非患者告警规则表，不取其他CSV')
    checked_utf8, rule_redactions = scrub(rule_text.encode('utf-8'))
    rule_stored = rule_raw if not rule_redactions else checked_utf8
    write(LOCAL / 'rules/abnormal_Config.csv', rule_stored)
    rule_after = remote('alarm_rule_metadata_after', rule_command)
    require(rule_before == rule_after, '规则CSV在只读采集期间变化')
    alarm_rule_source = {'path': 'rules/abnormal_Config.csv', 'remote_path': rule_path,
        'raw_sha': rule_sha, 'local_sha': digest(rule_stored), 'raw_size': len(rule_raw),
        'startup_sha256': rule_sha, 'read_before_sha256': rule_sha, 'read_after_sha256': rule_sha,
        'local_verified': True, 'encoding': 'GB18030' if rule_encoding == 'gb18030' else 'UTF-8',
        'stored_exactly_as_remote': not rule_redactions, 'raw_encoding': rule_encoding,
        'stored_encoding': rule_encoding if not rule_redactions else 'utf-8', 'redactions': rule_redactions,
        'startup_sha256_matches': True, 'metadata_before_after_equal': True, 'patient_data_source': False}
    for name, value in prepared['contract_sha256'].items():
        require(sha(LOCAL / name) == value, '实际退出合同与冻结09不符')
    post = remote('postcheck', '''base=/home/rkshare/programs/sn100_shell
date -Is
printf 'BOARD_SERIAL=%s\\nCURRENT=%s\\n' "$(tr -d '\\0\\n' </proc/device-tree/serial-number)" "$(readlink -f "$base/current")"
apps=0; checkers=0; controllers=0
for exe in /proc/[0-9]*/exe; do
 path=$(readlink "$exe" 2>/dev/null || true)
 case "$path" in
 */sn100_shell|*/sn100_report_renderer) apps=$((apps+1));;
 */status_login*review*) checkers=$((checkers+1));;
 /userdata/shared/ACA_V1.0OffLineD-500)
  controllers=$((controllers+1)); pid=${exe#/proc/}; pid=${pid%/exe}
  printf 'CONTROLLER_PID=%s\\nCONTROLLER_EXE=%s\\nCONTROLLER_SHA256=%s\\n' "$pid" "$path" "$(sha256sum "$exe" | cut -d' ' -f1)";;
 esac
done
printf 'APPLICATION_COUNT=%s\\nCHECKER_COUNT=%s\\nCONTROLLER_COUNT=%s\\nPORT_5000_BEGIN\\n' "$apps" "$checkers" "$controllers"
ss -H -ltnp 'sport = :5000'
printf 'PORT_5000_END\\n'
set +e
sha256sum -c "$run/ini_before.sha256"; app_ini=$?
sha256sum -c "$run/controller_ini_before.sha256"; controller_ini=$?
printf 'POSTCHECK_INI_EXIT=%s\\nPOSTCHECK_CONTROLLER_INI_EXIT=%s\\n' "$app_ini" "$controller_ini"
''')
    post_stored, post_changes = scrub(post)
    write(LOCAL / 'postcheck.txt', post_stored)
    write(LOCAL / 'postcheck.exit', b'0\n')
    write(LOCAL / 'postcheck.stderr', b'')
    with (LOCAL / 'parser.log').open('xb') as output:
        parsed = subprocess.run([sys.executable, '-X', 'utf8', str(HERE / 'summarize_run.py'), str(LOCAL)],
                                stdout=output, stderr=subprocess.STDOUT)
    write(LOCAL / 'parser.exit', (str(parsed.returncode) + '\n').encode('ascii'))
    require((LOCAL / 'parsed_summary.json').is_file(), '解析器未生成汇总，原始选定证据已保存，不造结果')
    save('transport_steps.json', steps)
    save('collection.json', {'schema': 'sn100-status-login-09-collection-v1', 'case_name': LOCAL.name, 'mode': 'auto',
         'board': {'name': 'board_b', 'serial': SERIAL}, 'release_id': RELEASE,
         'application_sha256': APP, 'checker_sha256': CHECKER,
         'source_count': prepared['source_count'], 'object_count': built['object_count'],
         'required_marker_count': len(contract['required_markers']),
         'new_construction_proven': False, 'same_main_memory_bool_read': False,
         'task_business_verified': False, 'in_flight_task_single_outer_close_verified': False,
         'scope': '仅当前急诊初始视图与未变化INI的一致性；保留raw，不证明创建来源或main同一内存bool。',
         'local_run_directory': str(LOCAL), 'remote_run_directory': REMOTE,
         'collected_at_utc': datetime.now(timezone.utc).isoformat(), 'collection_complete': True, 'collector_exit_code': 0,
         'all_remote_files_downloaded_and_verified': False, 'all_selected_files_downloaded_and_verified': True,
         'remote_manifest': {'path': 'remote_capture.sha256', 'sha256': sha(LOCAL / 'remote_capture.sha256'), 'file_count': len(entries)},
         'files': selected, 'excluded': excluded,
         'alarm_rule_source': alarm_rule_source,
         'parser': {'path': 'parsed_summary.json', 'sha256': sha(LOCAL / 'parsed_summary.json'), 'exit_code': parsed.returncode, 'encoding': 'utf-8'},
         'postcheck': {'path': 'postcheck.txt', 'sha256': sha(LOCAL / 'postcheck.txt'), 'exit_code': 0, 'redactions': post_changes},
         'host': 'root@192.168.100.123', 'host_fingerprint': PIN, 'known_hosts_sha256': known_sha,
         'read_only_remote_commands': True, 'product_started_or_stopped': False, 'current_changed': False})
    print(json.dumps({'collection_complete': True, 'selected_files_verified': len(selected),
                      'excluded_metadata_only': len(excluded), 'redacted_selected_files': sum(bool(x['redactions']) for x in selected),
                      'parser_actual_exit': parsed.returncode}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())
