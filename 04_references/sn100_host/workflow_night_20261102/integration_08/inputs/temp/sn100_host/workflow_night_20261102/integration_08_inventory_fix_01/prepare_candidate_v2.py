"""只在本目录准备构建清单修复，核普通补丁回演；不编译或运行产品。"""
from pathlib import Path
from datetime import datetime, timezone
from collections import Counter
import difflib
import hashlib
import json
import subprocess
import sys

HERE = Path(__file__).resolve().parent
TASK = HERE.parent
ROOT = TASK.parents[2]
PREFIX = '06_programs/sn100_host/'
PROGRAM = ROOT / PREFIX
BASE = HERE / 'base'
CANDIDATE = HERE / 'candidate'
REPLAY = HERE / 'ordinary_replay'
SNAPSHOT = TASK / 'integration_08_review/stage_02/source_manifest.json'
SNAPSHOT_SHA = '30cf6f075afaea1f5c6d5842759e63747abd8d7d0403f0e85f05ed019ae0346e'
ATTR_SHA = 'ac7fd8f4c639db19a3f6192a1cf4205bcc4ef06a573a29a2a98653fd19ae3bf8'
PATCH = HERE / 'build_inventory_fix.patch'
SYSROOT = ROOT / 'temp/sn100_host/rk_prepare/sysroot'
GUARDS = {}
STEPS = []


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    require(path.is_file() and not path.is_symlink(), '需要普通原件：' + str(path))
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def rel(path):
    return path.relative_to(ROOT).as_posix()


def linux(path):
    text = path.as_posix()
    require(text.startswith('C:/'), '只允许当前C盘项目路径。')
    return '/mnt/c/' + text[3:]


def write(path, data):
    with path.open('xb') as output:
        output.write(data if isinstance(data, bytes) else data.encode('utf-8'))


def new_json(path, value):
    write(path, json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + '\n')


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def source_map(container):
    program = container / PREFIX
    return {p.relative_to(program).as_posix(): sha(p) for p in sorted(program.rglob('*')) if p.is_file()}


def command(name, arguments):
    started = {'command': arguments, 'started_at_utc': datetime.now(timezone.utc).isoformat(), 'actual_exit_unknown': True}
    new_json(HERE / (name + '.inflight.json'), started)
    result = subprocess.run(arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    write(HERE / (name + '.stdout.log'), result.stdout)
    write(HERE / (name + '.stderr.log'), result.stderr)
    write(HERE / (name + '.exit'), str(result.returncode) + '\n')
    record = {'name': name, 'command': arguments, 'actual_exit': result.returncode,
              'stdout_sha256': sha(HERE / (name + '.stdout.log')), 'stderr_sha256': sha(HERE / (name + '.stderr.log'))}
    new_json(HERE / (name + '.result.json'), record)
    STEPS.append(record)
    print(name + '_ACTUAL_EXIT=' + str(result.returncode), flush=True)
    require(result.returncode == 0, '实际检查失败，保留现场：' + name)
    return result.stdout


def main():
    for path in (BASE, CANDIDATE, REPLAY, PATCH):
        require(not path.exists(), '拒绝覆盖或重跑：' + str(path))
    require(sha(SNAPSHOT) == SNAPSHOT_SHA, '正式08完整源表不是根给定的30cf。')
    before = load(SNAPSHOT)
    formal_all = source_map(ROOT)
    require(len(before) == 674 and all(formal_all.get(n) == h for n, h in before.items()), '正式674目标与完整表不等，尚未复制。')
    require(sha(ROOT / '.gitattributes') == ATTR_SHA, '当前根属性不是ac7。')
    for name, digest in formal_all.items():
        GUARDS[PREFIX + name] = digest
    for name in ('prepare_candidate.py', 'driver.command.json', 'driver.stdout.log', 'driver.stderr.log', 'driver.exit', 'preparation_stopped.json', 'ast.exit', 'preparation_attempt_01_failure.json'):
        GUARDS[rel(HERE / name)] = sha(HERE / name)
    for path in (SNAPSHOT, ROOT / '.gitattributes',
                 TASK / 'integration_08_tool_candidate/integration_08_tools_config.json',
                 TASK / 'integration_08_review/final_merge_review.json',
                 TASK / 'integration_08_review/promotion_progress.json'):
        GUARDS[rel(path)] = sha(path)
    # 原构建已停止；仅按原件读哈希，不修改或重跑旧目录中的任何内容。
    old = TASK / 'integration_08'
    for path in sorted(old.rglob('*')):
        if path.is_file():
            GUARDS[rel(path)] = sha(path)
    new_json(HERE / 'protected_inputs_before.json', GUARDS)
    for container in (BASE, CANDIDATE, REPLAY):
        container.mkdir()
        write(container / '.gitattributes', (ROOT / '.gitattributes').read_bytes())
        for name in before:
            target = container / PREFIX / name
            target.parent.mkdir(parents=True, exist_ok=True)
            write(target, (PROGRAM / name).read_bytes())
        require(source_map(container) == before, '自有副本与674底稿不同：' + str(container))
    target = CANDIDATE / PREFIX / 'shell/build.sh'
    original = target.read_bytes()
    require(b'\r' not in original and original.endswith(b'\n'), '原脚本不是已核LF行尾。')
    start = original.index('# 任务剩余量走专用重读和运行采用，不发送设备命令。\n'.encode('utf-8'))
    stop = original.index(b'for header in "${headers[@]}"; do\n', start)
    moved = original[start:stop]
    require(moved.count(b'sources+=(') == 2 and moved.count(b'.cpp"') == 7, '原4+3追加段不符。')
    require(original.count(moved) == 1, '原追加段不唯一。')
    headers = ('# 显式登记配套头文件，使只读清单与实际构建输入一致。\n'
               'headers+=("$program_dir/reagents/original_task_reagent_display.h"\n'
               '          "$program_dir/samples/original_task_reagent_receipt.h"\n'
               '          "$program_dir/samples/original_task_reagent_receipt_writer.h"\n'
               '          "$module_dir/task_reagent_context.h")\n').encode('utf-8')
    marker = b'for input in "${sources[@]}" "${forms[@]}" "${headers[@]}" "${resource_files[@]}" "${helper_sources[@]}"; do\n'
    removed = original.replace(moved, b'', 1)
    require(removed.count(marker) == 1, '输入核对入口不唯一。')
    changed = removed.replace(marker, moved + headers + marker, 1)
    require(changed.replace(moved + headers, b'', 1) == removed, '越出原段移动和四头登记。')
    target.write_bytes(changed)
    after = source_map(CANDIDATE)
    changed_files = sorted(name for name in before if before[name] != after[name])
    require(changed_files == ['shell/build.sh'] and set(after) == set(before), '改动越出单脚本。')
    new_json(HERE / 'source_manifest_before.json', before)
    new_json(HERE / 'source_manifest_after.json', after)
    write(HERE / 'source_table_before.sha256', ''.join(h + '  ' + n + '\n' for n, h in sorted(before.items())))
    write(HERE / 'source_table_after.sha256', ''.join(h + '  ' + n + '\n' for n, h in sorted(after.items())))
    patch = 'diff --git a/' + PREFIX + 'shell/build.sh b/' + PREFIX + 'shell/build.sh\n'
    patch += ''.join(difflib.unified_diff(original.decode('utf-8').splitlines(keepends=True),
                                        changed.decode('utf-8').splitlines(keepends=True),
                                        fromfile='a/' + PREFIX + 'shell/build.sh',
                                        tofile='b/' + PREFIX + 'shell/build.sh'))
    write(PATCH, patch)
    wsl = ['wsl.exe', '-d', 'Ubuntu-24.04', '-u', 'qyq', '--']
    command('01_bash_n', [*wsl, '/usr/bin/bash', '-n', linux(target)])
    build_dir = HERE / 'not_created_build_dir'
    output = HERE / 'not_created_output/sn100_shell'
    inventory = command('02_list_inputs', [*wsl, '/usr/bin/env', 'SN100_ARM64_LIST_INPUTS=1',
                        'SN100_ARM64_SYSROOT=' + linux(SYSROOT), 'SN100_ARM64_BUILD_DIR=' + linux(build_dir),
                        'RK_BUILD_TYPE=Release', '/usr/bin/bash', linux(target), linux(output)])
    require(not build_dir.exists() and not output.parent.exists(), '只列输入却创建了构建或产物目录。')
    write(HERE / 'build_inputs.tsv', inventory)
    counts = Counter()
    rows = {}
    candidate_program = linux(CANDIDATE / PREFIX) + '/'
    unique = set()
    for line in inventory.decode('utf-8').splitlines():
        kind, value = line.split('\t', 1)
        counts[kind] += 1
        if kind == 'include':
            continue
        require(value.startswith(candidate_program), '清单出现候选以外的源码：' + value)
        name = value[len(candidate_program):]
        require(name in after and sha(CANDIDATE / PREFIX / name) == after[name], '清单输入未被674完整源表覆盖。')
        rows.setdefault(kind, []).append(name)
        unique.add(name)
    seven = ['reagents/original_task_reagent_display.cpp', 'reagents/original_reagent_level_task.cpp',
             'reagents/original_task_reagent_readback.cpp', 'reagents/reagents_page_task.cpp',
             'shell/task_reagent_shell.cpp', 'shell/task_reagent_source.cpp', 'samples/original_task_reagent_receipt_writer.cpp']
    four = ['reagents/original_task_reagent_display.h', 'samples/original_task_reagent_receipt.h',
            'samples/original_task_reagent_receipt_writer.h', 'shell/task_reagent_context.h']
    require(all(rows['source'].count(n) == 1 for n in seven), '七源未各列一次。')
    require(all(rows['header'].count(n) == 1 for n in four), '四头未各列一次。')
    require(all(b'Q_OBJECT' not in (CANDIDATE / PREFIX / n).read_bytes() for n in four), '新增头意外产生moc。')
    inventory_result = {'kinds': dict(counts), 'unique_build_inputs': len(unique), 'seven_sources': seven,
                        'four_headers': four, 'seven_sources_and_four_headers_exactly_once': True,
                        'source_count': len(after), 'explicit_sysroot': linux(SYSROOT),
                        'build_or_output_directories_created': False, 'compiled': False}
    new_json(HERE / 'input_inventory_result.json', inventory_result)
    git = ['wsl.exe', '-d', 'Ubuntu-24.04', '-u', 'qyq', '--cd', linux(REPLAY), '--', '/usr/bin/git',
           '-c', 'core.autocrlf=false', '-c', 'core.safecrlf=false', '-c', 'core.whitespace=cr-at-eol']
    command('03_git_init', [*git, 'init', '-q'])
    command('04_git_attributes', [*git, 'check-attr', 'text', 'eol', '--', PREFIX + 'shell/build.sh'])
    command('05_plain_check', [*git, 'apply', '--check', linux(PATCH)])
    command('06_plain_apply', [*git, 'apply', linux(PATCH)])
    replay = source_map(REPLAY)
    require(replay == after, '实际普通回演完整674份不等于候选。')
    new_json(HERE / 'replay_source_manifest.json', replay)
    guarded_after = {name: sha(ROOT / name) for name in GUARDS}
    require(guarded_after == GUARDS and source_map(ROOT) == formal_all, '正式/旧构建/冻结输入发生变化。')
    new_json(HERE / 'protected_inputs_after.json', guarded_after)
    result = {'format': 'sn100-integration-08-inventory-fix-v1', 'complete': True,
              'baseline_source_manifest': {'path': rel(SNAPSHOT), 'sha256': SNAPSHOT_SHA},
              'baseline_files': len(before), 'final_files': len(after), 'changed_files': changed_files,
              'added_files': [], 'deleted_files': [], 'patch': {'path': rel(PATCH), 'sha256': sha(PATCH)},
              'before_build_sh_sha256': before['shell/build.sh'], 'after_build_sh_sha256': after['shell/build.sh'],
              'source_before_sha256': sha(HERE / 'source_manifest_before.json'),
              'source_after_sha256': sha(HERE / 'source_manifest_after.json'),
              'replay_source_sha256': sha(HERE / 'replay_source_manifest.json'),
              'ordinary_check_apply_actual_exits': [0, 0], 'full_674_replay_byte_equal': True,
              'moved_original_block_bytes': len(moved), 'moved_original_block_sha256': hashlib.sha256(moved).hexdigest(),
              'original_lf_preserved': True, 'steps': STEPS, 'input_inventory': inventory_result,
              'protected_input_count': len(GUARDS), 'formal_and_old_build_and_frozen_inputs_unchanged': True,
              'formal_modified': False, 'compiled': False, 'network_used': False, 'product_executed': False,
              'sql_executed': False, 'board_accessed': False, 'formal_existing_files_outside_674_snapshot_preserved': len(set(formal_all) - set(before)),
              'historical_failures_in_this_attempt': [{'actual_exit': 2, 'path': rel(HERE / 'preparation_attempt_01_failure.json'), 'sha256': sha(HERE / 'preparation_attempt_01_failure.json'), 'kind': 'tool_scope_error_before_source_copy'}]}
    new_json(HERE / 'preparation_result.json', result)
    print('PATCH_SHA256=' + sha(PATCH), flush=True)
    print('FINAL_SOURCE_MANIFEST_SHA256=' + result['source_after_sha256'], flush=True)
    print('INVENTORY=' + json.dumps(dict(counts)) + '\nUNIQUE_INPUTS=' + str(len(unique)), flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        if not (HERE / 'preparation_stopped.json').exists():
            new_json(HERE / 'preparation_stopped.json', {'reason': str(error), 'steps': STEPS,
                     'compiled': False, 'network_used': False, 'product_executed': False, 'sql_executed': False})
        print('STOP=' + str(error), file=sys.stderr)
        sys.exit(2)
