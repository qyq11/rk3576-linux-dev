"""Root手动执行：只读回收第12批真实运行凭据，保持同一新版程序运行。"""
from pathlib import Path
import argparse
import gzip
import hashlib
import io
import json
import re
import shlex
import sys
import tarfile
import types

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
HELPER = HERE / 'collect_runtime_lis_batch12_01.py'
HELPER_SHA = '21805f6c036da483cba5a0787daf67a1001fff568a15321967a130e1af6a744c'
PHASES = ('metadata', 'prepare', 'preflight', 'stop', 'activate', 'launch', 'observe')
MAX_ARCHIVE = 32 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_guard():
    """复用已固定的新采集器定义；模块main守卫不成立，不会隐式执行网络或解析。"""
    raw = HELPER.read_bytes()
    require(len(raw) == 19245 and hashlib.sha256(raw).hexdigest() == HELPER_SHA, '只读公共定义不是固定21805原件。')
    module = types.ModuleType('_sn100_batch12_readonly_guard')
    module.__file__ = str(HELPER)
    exec(compile(raw, str(HELPER), 'exec'), module.__dict__)
    return module


def record(path, raw):
    return {'path': path.relative_to(ROOT).as_posix(), 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)}


def read_phases(g, originals, core, overrides):
    rows = {}
    for name in PHASES:
        receipt_raw, _ = originals.read(g.CAP / (name + '.receipt'))
        log_raw, _ = originals.read(g.CAP / (name + '.log'))
        exit_raw, _ = originals.read(g.CAP / (name + '.exit'))
        auth_path = overrides.get(name, HERE / ('authorization_' + name + '_01.json'))
        auth_raw, _ = originals.read(auth_path)
        auth = g.decode(auth_raw)
        facts = g.receipt_fields(receipt_raw)
        require(exit_raw == b'0\n', name + '整阶段没有真实LF0退出。')
        require(auth.get('format') == 'sn100-integration-12-01-runtime-phase-authorization-v1'
                and auth.get('frozen') is True and auth.get('allowed') is True and auth.get('phase') == name
                and auth.get('core_inputs') == record(g.CORE, (g.CORE).read_bytes()), '授权不属于同一冻结核心。')
        approval = originals.record(auth['root_approval'])
        require(bool(approval), 'Root批准原件为空。')
        require(facts.get('FORMAT') == 'sn100-integration-12-01-runtime-receipt-v1'
                and facts.get('PHASE') == name and facts.get('TRANSITION_ID') == core['transition_id']
                and facts.get('CORE_INPUTS_SHA256') == g.CORE_SHA and facts.get('AUTHORIZATION_SHA256') == g.digest(auth_raw)
                and facts.get('SERIAL') == g.SERIAL and facts.get('BOARD') == 'board_b'
                and facts.get('BOOT_ID') == core['fresh_baseline']['boot_id'] and facts.get('EXIT_CODE') == '0'
                and facts.get('ROOT_LAUNCH_INTENT_SHA256') == core['stable_target_intent']['sha256'], '回执阶段/核心/批准/身份不符。')
        current = core['fresh_baseline']['old_current'] if PHASES.index(name) <= PHASES.index('stop') else 'releases/' + g.RELEASE
        require(facts.get('CURRENT_AFTER') == current, '阶段current与该阶段真实前后关系不符。')
        if name == 'stop':
            require(all(facts.get(k) == v for k, v in {'APP_EXIT': '0', 'CLEAN_SHUTDOWN': '1',
                    'STOP_TOOL_ACTUAL_EXIT': '0', 'DISPLAY_RELEASED': '1'}.items()), 'stop没有真实干净退出及显示释放。')
        require(log_raw.endswith(('RUNTIME_RECEIPT_SHA256=' + g.digest(receipt_raw) + '\n').encode('ascii') + receipt_raw), '整阶段原log末尾不含准确回执。')
        lines = log_raw.decode('utf-8').splitlines()
        for key in ('PHASE', 'TRANSITION_ID', 'CORE_INPUTS_SHA256', 'AUTHORIZATION_SHA256'):
            require(lines.count(key + '=' + facts[key]) == 1, '日志缺失或重复关键字段。')
        require(lines.count('RUNTIME_RECEIPT_SHA256=' + g.digest(receipt_raw)) == 1, '日志重复或缺少回执摘要。')
        rows[name] = {'receipt': receipt_raw, 'log': log_raw, 'exit': exit_raw, 'auth': auth_raw,
                      'auth_path': g.regular(auth_path), 'approval': approval, 'facts': facts, 'value': auth}
    for i, name in enumerate(PHASES[1:], 1):
        previous = PHASES[i - 1]
        a, p = rows[name]['value'], rows[previous]
        for key, path, raw in [('previous_authorization', p['auth_path'], p['auth']),
                               ('previous_receipt', g.CAP / (previous + '.receipt'), p['receipt']),
                               ('previous_phase_log', g.CAP / (previous + '.log'), p['log']),
                               ('previous_phase_exit', g.CAP / (previous + '.exit'), p['exit'])]:
            require(a[key] == record(path, raw), '七阶段来源链没有连续承接：' + name + '/' + key)
    # 同一新进程和capture叶目录沿用另一工具已经收紧的实际launch/observe核对。
    core_record = {'path': str(g.CORE), 'sha256': g.CORE_SHA, 'bytes': g.CORE_BYTES}
    launch = g.phase(originals, 'launch', rows['launch']['auth_path'], core_record, core)[0]
    observe = g.phase(originals, 'observe', rows['observe']['auth_path'], core_record, core)[0]
    for key in ('NEW_RUN', 'NEW_PID', 'NEW_START_TICKS', 'OLD_STOP_RECEIPT_SHA256'):
        require(launch.get(key) and launch.get(key) == observe.get(key), 'launch/observe不属于同一新身份。')
    require(launch.get('NEW_EXE') == g.BASE + '/releases/' + g.RELEASE + '/sn100_shell'
            and launch.get('NEW_EXE_SHA256') == g.APP_SHA and launch.get('NEW_PROCESS_IDENTIFIED') == '1'
            and launch.get('UNIQUE_START_REQUEST_EXIT') == '0' and launch.get('SOFT_DEADLINE_SECONDS') == '0'
            and launch.get('APPLICATION_EXIT') == 'not_evaluated', 'launch不是同一新版本真实不限时启动。')
    require(observe.get('WAITER_ACTUAL_EXIT') == '0' and observe.get('SHELL_READY') == '1'
            and observe.get('SAME_NEW_APPLICATION_RUNNING') == '1' and observe.get('CLOSE_REQUESTED') == '0'
            and observe.get('CAPTURE_DIRECTORY') == core['observation']['capture_leaf'], 'observe没有真实封口成功。')
    require(re.fullmatch(r'[1-9][0-9]*', launch['NEW_PID']) and re.fullmatch(r'[1-9][0-9]*', launch['NEW_START_TICKS'])
            and re.fullmatch(re.escape(g.BASE) + r'/runs/[0-9_]+', launch['NEW_RUN']), '新身份格式错误。')
    return rows, launch


def file_scope(g, originals, core, rows, live):
    """白名单直接来自本轮固定目录；不枚举业务资料目录。"""
    meta = core['metadata_directory'][len(g.BASE) + 1:]
    audit = core['audit_directory'][len(g.BASE) + 1:]
    run = live['NEW_RUN'][len(g.BASE) + 1:]
    limits, expected = {}, {}

    def add(name, maximum=65536, raw=None):
        require(name not in limits and not name.startswith('/') and all(x not in ('', '.', '..') for x in name.split('/')), '白名单路径重复或不规范。')
        limits[name] = maximum
        if raw is not None:
            expected[name] = raw

    add(meta + '/core_inputs.json', raw=originals.read(g.CORE)[0])
    add(meta + '/runtime_launch_intent.json', raw=originals.record(core['stable_target_intent']))
    add(meta + '/configuration.sha256', raw=originals.record(core['fresh_baseline']['configuration_manifest']))
    for name in PHASES:
        add(meta + '/authorizations/' + name + '.json', raw=rows[name]['auth'])
        add(meta + '/authorizations/' + name + '.approval', raw=rows[name]['approval'])
        add(meta + '/receipts/' + name + '.receipt', raw=rows[name]['receipt'])
    for phase, names in {'stop': ('stdout.log', 'stderr.log', 'actual_exit'),
                         'launch': ('latest_run_before.txt', 'new_run.txt', 'systemd.stdout.log', 'systemd.stderr.log',
                                    'systemd.actual_exit', 'product_observation_environment.txt', 'service_properties.txt'),
                         'observe': ('waiter.stdout.log', 'waiter.stderr.log', 'waiter.actual_exit',
                                     'product_observation_environment.txt', 'post_live_services.txt')}.items():
        for name in names:
            add(meta + '/phase_logs/' + phase + '/' + name, 1048576)
    audit_values = {'current_before.txt': core['fresh_baseline']['old_current'], 'core_inputs.sha256': g.CORE_SHA,
                    'activation_authorization.sha256': g.digest(rows['activate']['auth']),
                    'stop_intent.sha256': core['stable_target_intent']['sha256'],
                    'old_stop_receipt.sha256': g.digest(rows['stop']['receipt']), 'current_after.txt': 'releases/' + g.RELEASE}
    for name, value in audit_values.items():
        add(audit + '/' + name, raw=(value + '\n').encode('ascii'))
    add(audit + '/latest_run_before.txt')
    for name, value in {'app.pid': live['NEW_PID'], 'app.start_ticks': live['NEW_START_TICKS'],
                        'app.exe': live['NEW_EXE'], 'boot_id': live['BOOT_ID']}.items():
        add(run + '/' + name, raw=(value + '\n').encode('ascii'))
    add(run + '/app.log', 8 * 1024 * 1024)
    add('latest_run', raw=(run.rsplit('/', 1)[-1] + '\n').encode('ascii'))
    return limits, expected


def script_text(g, core, live, limits):
    # 固定helper只返回文本；截取同一新PID核对函数，不执行它的六文件tar部分。
    source = g.remote_script(core, live).decode('ascii')
    marker = '\ncheck_live\n[[ -d $capture'
    require(source.count(marker) == 1, '固定新进程核对文本边界改变。')
    text = source.split(marker)[0] + '\n'
    text += 'meta=' + shlex.quote(core['metadata_directory']) + '\n'
    text += r'''
check_identity() {
    local identity_pid=$1 identity_ticks=$2 identity_uid=$3 identity_exe=$4 identity_sha=$5 raw rest later
    local -a fields=() after=()
    raw=$(cat "/proc/$identity_pid/stat"); rest=${raw##*) }; [[ $rest != "$raw" ]]
    IFS=' ' read -r -a fields <<<"$rest"
    [[ ${fields[19]-} == "$identity_ticks" && ${fields[0]-} != Z && ${fields[0]-} != X ]]
    [[ $(awk '/^Uid:/{print $3}' "/proc/$identity_pid/status") == "$identity_uid" ]]
    [[ $(readlink "/proc/$identity_pid/exe") == "$identity_exe" ]]
    [[ $(sha256sum "/proc/$identity_pid/exe" | cut -d' ' -f1) == "$identity_sha" ]]
    later=$(cat "/proc/$identity_pid/stat"); rest=${later##*) }; [[ $rest != "$later" ]]
    IFS=' ' read -r -a after <<<"$rest"
    [[ ${after[19]-} == "$identity_ticks" && ${after[0]-} != Z && ${after[0]-} != X ]]
}
check_context() {
    check_live
'''
    for identity in (core['fresh_baseline']['controller'], *core['fresh_baseline']['camera_processes']):
        text += 'check_identity ' + ' '.join(shlex.quote(identity[k]) for k in ('pid', 'start_ticks', 'uid', 'exe', 'sha256')) + '\n'
    for field, key in [('ActiveState', 'active_state'), ('SubState', 'sub_state'), ('MainPID', 'main_pid')]:
        service = core['fresh_baseline']['camera_service']
        text += '[[ $(systemctl show ' + shlex.quote(service['unit']) + ' -p ' + field + ' --value) == ' + shlex.quote(service[key]) + ' ]]\n'
    controller = core['fresh_baseline']['controller']
    text += '[[ $(ss -H -lntp "sport = :5000" | grep -F ' + shlex.quote('pid=' + controller['pid'] + ',') + " | awk '{print $4}' | sort | paste -sd, -) == " + shlex.quote(controller['listen_endpoints']) + ' ]]\n'
    text += '[[ $(sha256sum "$meta/configuration.sha256" | cut -d\' \' -f1) == ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c ]]\n'
    text += 'sha256sum --check --status "$meta/configuration.sha256"\n'
    for pointer in core['fresh_baseline']['pointers']:
        text += '[[ $(sha256sum ' + shlex.quote(pointer['path']) + ' | cut -d\' \' -f1) == ' + pointer['sha256'] + ' ]]\n'
    text += '}\ncheck_context\n'
    for name, maximum in limits.items():
        path = shlex.quote(g.BASE + '/' + name)
        text += '[[ -f ' + path + ' && ! -L ' + path + ' && $(readlink -f ' + path + ') == ' + path + ' && $(stat -c %h ' + path + ') == 1 ]]\n'
        text += '[[ $(stat -c %s ' + path + ') -le ' + str(maximum) + ' ]]\n'
    text += 'tar -czf - -C ' + shlex.quote(g.BASE) + ' -- ' + ' '.join(shlex.quote(n) for n in sorted(limits)) + '\ncheck_context\n'
    snapshot = {'FORMAT': 'sn100-runtime-proofs-12-01-live-v1', 'PHASE': 'post_observe', 'CORE_INPUTS_SHA256': g.CORE_SHA,
                'RELEASE_ID': g.RELEASE, 'APP_PID': live['NEW_PID'], 'APP_START_TICKS': live['NEW_START_TICKS'],
                'APP_EXE': live['NEW_EXE'], 'APP_SHA256': g.APP_SHA, 'BOOT_ID': live['BOOT_ID'], 'SERIAL': g.SERIAL,
                'APPLICATION_EXIT': 'null', 'PRODUCT_STARTED_OR_STOPPED': '0', 'SAME_APPLICATION_RUNNING': '1'}
    text += 'printf \'RUNTIME_PROOFS_SNAPSHOT_BEGIN\\n\' >&2\n'
    text += 'date -u \'UTC=%Y-%m-%dT%H:%M:%SZ\' >&2\n'
    for key, value in snapshot.items():
        text += 'printf \'%s\\n\' ' + shlex.quote(key + '=' + value) + ' >&2\n'
    text += 'printf \'CURRENT=%s\\n\' "$(readlink /home/rkshare/programs/sn100_shell/current)" >&2\n'
    text += 'printf \'CONTROLLER_PID=%s\\nCONTROLLER_START_TICKS=%s\\n\' ' + shlex.quote(controller['pid']) + ' ' + shlex.quote(controller['start_ticks']) + ' >&2\n'
    text += 'printf \'CONTROLLER_EXE=%s\\nCONTROLLER_SHA256=%s\\n\' "$(readlink /proc/' + controller['pid'] + '/exe)" "$(sha256sum /proc/' + controller['pid'] + '/exe | cut -d\' \' -f1)" >&2\n'
    for identity in core['fresh_baseline']['camera_processes']:
        text += 'printf \'CAMERA_IDENTITY=%s\\n\' ' + shlex.quote('|'.join(identity[k] for k in ('role', 'pid', 'start_ticks', 'uid', 'exe', 'sha256'))) + ' >&2\n'
    text += 'systemctl show ' + shlex.quote(core['fresh_baseline']['camera_service']['unit']) + ' -p Id -p ActiveState -p SubState -p MainPID >&2\n'
    text += 'while read -r expected_hash config_path; do sha256sum -- "$config_path" >&2; done < "$meta/configuration.sha256"\n'
    text += 'sha256sum -- ' + ' '.join(shlex.quote(p['path']) for p in core['fresh_baseline']['pointers']) + ' >&2\n'
    text += 'printf \'CONFIGURATION_MANIFEST_SHA256=ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c\\nRUNTIME_PROOFS_SNAPSHOT_END\\n\' >&2\n'
    return text.encode('ascii')


def read_bundle(path, limits):
    require(0 < path.stat().st_size <= MAX_ARCHIVE, '原压缩包超过32MiB；保留原件，不展开。')
    with gzip.open(path, 'rb') as stream:
        raw = stream.read(MAX_ARCHIVE + 1)
    require(len(raw) <= MAX_ARCHIVE, '解压内容超过32MiB。')
    values = {}
    with tarfile.open(fileobj=io.BytesIO(raw), mode='r:') as bundle:
        for member in bundle:
            require(member.name in limits and member.name not in values and len(values) < len(limits)
                    and member.type in (tarfile.REGTYPE, tarfile.AREGTYPE) and not member.pax_headers
                    and not member.issparse() and 0 <= member.size <= limits[member.name], '包内不是白名单有界普通文件。')
            source = bundle.extractfile(member)
            require(source is not None, '包内文件不可读。')
            content = source.read(limits[member.name] + 1)
            require(len(content) == member.size, '包内文件长度不符。')
            values[member.name] = content
    require(set(values) == set(limits), '包内白名单文件不完整。')
    return values


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--authorization', action='append', default=[], metavar='PHASE=PATH')
    ap.add_argument('--output', type=Path, default=HERE / 'runtime_proofs_01')
    args = ap.parse_args()
    overrides = {}
    for row in args.authorization:
        name, sep, path = row.partition('=')
        require(sep and name in PHASES and name not in overrides, '阶段授权覆盖参数不规范。')
        overrides[name] = Path(path)
    g = load_guard()
    originals = g.Originals()
    core_raw, _ = originals.read(g.CORE)
    require(len(core_raw) == g.CORE_BYTES and g.digest(core_raw) == g.CORE_SHA, '不是实际652e冻结核心。')
    core = g.decode(core_raw)
    rows, live = read_phases(g, originals, core, overrides)
    limits, expected = file_scope(g, originals, core, rows, live)
    script = script_text(g, core, live, limits)
    originals.read(g.KNOWN, inside=False)
    helper_raw, _ = originals.read(HELPER)
    require(g.digest(helper_raw) == HELPER_SHA, '实际复用定义在准备期间发生变化。')
    out = args.output.absolute()
    require(out.parent == HERE and re.fullmatch(r'[A-Za-z0-9_-]{1,64}', out.name)
            and not out.exists() and not out.is_symlink(), '要求同目录下全新输出目录。')
    originals.unchanged()
    out.mkdir(mode=0o750)
    key_code, key_output, _ = g.capture_process(['ssh-keygen', '-E', 'sha256', '-l', '-f', str(g.KNOWN)], out, 'known_host_pin')
    require(key_code == 0 and key_output.stat().st_size <= 16384, '本地SSH指纹核对失败。')
    pin_rows = key_output.read_text(encoding='ascii').splitlines()
    require(pin_rows and all(len(x.split()) >= 2 for x in pin_rows) and {x.split()[1] for x in pin_rows} == {g.PIN}, '不是固定板B SSH指纹。')
    originals.unchanged()
    g.exclusive(out / 'collect_proofs.command.sh', script)
    g.exclusive(out / 'input_records.json', (json.dumps(list(originals.seen.values()), ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    code, archive, stderr = g.capture_process(['ssh', *g.OPTIONS, 'root@192.168.100.123', 'bash -s'], out,
                                            'collect_proofs', input_raw=script, archive=True)
    if code != 0:
        return code
    values = read_bundle(archive, limits)
    for name, raw in expected.items():
        require(values[name] == raw, '远端原件不等于本轮已核来源：' + name)
    require(stderr.stat().st_size <= 1048576, '远端stderr超过上限。')
    text = stderr.read_bytes()
    begin, end = b'RUNTIME_PROOFS_SNAPSHOT_BEGIN\n', b'RUNTIME_PROOFS_SNAPSHOT_END\n'
    require(text.count(begin) == 1 and text.count(end) == 1 and text.endswith(end), '没有完整本次收证现场。')
    snapshot = text.split(begin, 1)[1].split(end, 1)[0]
    originals.unchanged()
    destination = out / 'remote_proofs'
    destination.mkdir(mode=0o750)
    manifest = []
    for name, raw in sorted(values.items()):
        target = destination.joinpath(*name.split('/'))
        target.parent.mkdir(parents=True, exist_ok=True)
        g.exclusive(target, raw)
        manifest.append({'name': name, 'bytes': len(raw), 'sha256': g.digest(raw)})
    g.exclusive(out / 'runtime_proofs_12_01.snapshot.txt', snapshot)
    g.exclusive(out / 'unpack_manifest.json', (json.dumps({'format': 'sn100-runtime-proofs-12-01-unpack-v1',
                'archive_sha256': g.digest(archive.read_bytes()), 'files': manifest, 'application_exit': None,
                'product_started_or_stopped': False}, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    print('RUNTIME_PROOFS_COLLECTED=' + str(len(values)) + '; APPLICATION_EXIT=null; PRODUCT_STARTED_OR_STOPPED=0')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, OSError, KeyError, TypeError, UnicodeError, tarfile.TarError, EOFError) as error:
        print('拒绝回收运行凭据：' + str(error), file=sys.stderr)
        sys.exit(2)
