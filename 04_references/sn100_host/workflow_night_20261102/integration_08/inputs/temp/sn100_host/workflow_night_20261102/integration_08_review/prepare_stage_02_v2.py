#!/usr/bin/env python3
"""第二次隔离准备工具v2：严格核修后Root累积交付；默认只读，另获授权--execute才创建stage_02。"""
from pathlib import Path, PurePosixPath
from datetime import datetime, timezone
import argparse
import hashlib
import json
import re
import subprocess
import sys
import tarfile

SCRIPT = Path(__file__).resolve()
REVIEW = SCRIPT.parent
HERE = REVIEW / "stage_02"
ROOT = REVIEW.parents[3]
T = ROOT / 'temp/sn100_host/workflow_night_20261102'
SOURCE_ROOT = HERE / 'source'
PROGRAM = SOURCE_ROOT / '06_programs/sn100_host'
CLOSED = REVIEW / 'closed_component_inputs.json'
ATTRS = REVIEW / 'root_attributes.gitattributes'
ROOT_V2_PINS = {
    'delivery': ('temp/task_reagent_root_candidate/revision_02/delivery_manifest.json', 'a5aa6208b17b3689ca4a308799bce73874c8f71227424e2080075b177d01adc8'),
    'component': ('temp/task_reagent_root_candidate/revision_02/packaging/manifest.json', 'ead7054087684e9ffebc7b5abaab41fca51978047d524565510278471a969f19'),
    'source_map': ('temp/task_reagent_root_candidate/revision_02/source_snapshot.json', '957d7991629c4eff7103324255e9036252c008cc0f332e92d85daddaebffa906'),
    'cumulative_patch': ('temp/task_reagent_root_candidate/revision_02/packaging/task_reagent_root_on671_v2.patch', '3e29c16c008482cc51c51ee890d751fc6989ec8fac63fb2ae64e96f737450e82'),
}
ROOT_V2_INCREMENTAL_SHA = 'bc404a807268d4ad50d4768785b71fc97ac4028ff5c8ba30826cb4eba83fc83a'
ROOT_DELIVERY = ROOT_COMPONENT = ROOT_MAP = None
EXPECTED_ROOT_DELIVERY_SHA = EXPECTED_ROOT_COMPONENT_SHA = EXPECTED_ROOT_MAP_SHA = EXPECTED_ROOT_PATCH_SHA = None
BACKEND_COMPONENT = T / 'task_reagent_receipt_candidate/packaging_01/manifest.json'
BACKEND_COMPONENT_SHA = 'b885bad5093fe9101273003a652df5af691b81581c3d0f5d4a5692b508696f1d'
HISTORICAL_ATTR_SHA = '5de2ac8a8bf53f9c7775f129eac002896013a7d2415ae731a679616852a600a1'
PREVIOUS_FAILURE = REVIEW / 'stage_01/attempt_01_failure.json'
REJECTED_ROOT_PATCHES = {'5cf43ed58e3f7378cb80e886ab87d4d8afd3e923dee5f65a2c49ba24a4101c6f'}
HISTORICAL_BINDINGS = []
ATTEMPT_CREATED = False
MAP_671 = T / 'integration_08_base_review/source_snapshot_final.json'
DEFAULT_DIR = T / 'default_sample_container_candidate_20261103'
GIT = ['/usr/bin/git', '-c', 'core.autocrlf=false', '-c', 'core.safecrlf=false', '-c', 'core.whitespace=cr-at-eol']
ATTR_SHA = 'ac7fd8f4c639db19a3f6192a1cf4205bcc4ef06a573a29a2a98653fd19ae3bf8'
GUARDS = {}
STEPS = []
CHECKPOINTS = []
FAILURES = []


def require(ok, text):
    if not ok:
        raise RuntimeError(text)


def sha(path):
    require(path.is_file() and not path.is_symlink(), '需要普通文件：' + str(path))
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def write(path, value):
    require(not path.exists() and not path.is_symlink(), '拒绝覆盖：' + str(path))
    with path.open('xb') as stream:
        stream.write(value if isinstance(value, bytes) else value.encode('utf-8'))


def new_json(path, value):
    write(path, json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n')


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def relative(path):
    return path.relative_to(ROOT).as_posix()


def project_path(spelling):
    if spelling.startswith('/mnt/c/'):
        path = Path('C:/' + spelling[7:])
    elif re.match(r'^[A-Za-z]:[/\\]', spelling):
        path = Path(spelling)
    else:
        path = ROOT / spelling
    require(path.resolve().is_relative_to(ROOT.resolve()), '输入路径越出项目：' + spelling)
    return path


def bind(path, expected=None, bytes_expected=None):
    actual = sha(path)
    if expected is not None:
        require(actual == expected, '输入SHA不符：' + str(path))
    if bytes_expected is not None:
        require(path.stat().st_size == bytes_expected, '输入大小不符：' + str(path))
    key = relative(path)
    require(key not in GUARDS or GUARDS[key] == actual, '同一输入出现冲突：' + key)
    GUARDS[key] = actual


def bind_declared(node, pointer='', manifest=None):
    if isinstance(node, dict):
        if isinstance(node.get('path'), str) and re.fullmatch(r'[0-9a-f]{64}', str(node.get('sha256', ''))):
            # 只适配这份已固定组件的这一条历史引用，不能泛跳root属性。
            if manifest == BACKEND_COMPONENT and pointer == '/root_attributes':
                require(sha(BACKEND_COMPONENT) == BACKEND_COMPONENT_SHA, '后台组件JSON不是已固定版本。')
                require(node == {'path': '.gitattributes', 'sha256': HISTORICAL_ATTR_SHA, 'bytes': 3504},
                        '后台历史属性字段改变，停止。')
                component = load(BACKEND_COMPONENT)
                require(component['stage_directory'] == 'temp/sn100_host/workflow_night_20261102/task_reagent_receipt_candidate/packaging_01/stage_from_official06',
                        '后台历史基线归属改变，停止。')
                original = project_path(component['stage_directory']) / '.gitattributes'
                bind(original, HISTORICAL_ATTR_SHA, 3504)
                record = {'component_manifest': relative(BACKEND_COMPONENT), 'component_sha256': BACKEND_COMPONENT_SHA,
                          'json_pointer': pointer, 'historical_spelling': node['path'], 'historical_original': relative(original),
                          'sha256': HISTORICAL_ATTR_SHA, 'scope': '组件当时不可变基线的原属性字节；不代表本次根属性。'}
                if record not in HISTORICAL_BINDINGS: HISTORICAL_BINDINGS.append(record)
            else:
                bind(project_path(node['path']), node['sha256'], node.get('bytes'))
        for key, value in node.items():
            bind_declared(value, pointer + '/' + str(key), manifest)
    elif isinstance(node, list):
        for index, value in enumerate(node):
            bind_declared(value, pointer + '/' + str(index), manifest)


def background_check():
    bind(CLOSED, '3883e91eaa1d28c2a49c44641e0114724de1fed70ba130f874922dbb39e486da')
    closed = load(CLOSED)
    bind_declared(closed)
    require(sha(ATTRS) == sha(ROOT / '.gitattributes') == ATTR_SHA, '本次root/copy不再是ac7。')
    bind(BACKEND_COMPONENT, BACKEND_COMPONENT_SHA)
    bind_declared(load(BACKEND_COMPONENT), manifest=BACKEND_COMPONENT)
    bind(REVIEW / 'prepare_stage_02.py', '42c903d41bfbdb42e09f2af8b23381e10e402ff6b8ac20ca6c823217cad0965a')
    bind(PREVIOUS_FAILURE, 'ed0d9d42bd1e30dda954d4c3ed08334959d2988ece1db6828567647313e89441')
    previous = load(PREVIOUS_FAILURE)
    require(previous['actual_driver_exit'] == 2 and not previous['stage_source_created'], '首尝试历史不符。')
    require((REVIEW / 'stage_01/prepare_actual.exit').read_text().strip() == '2', '首尝试真实退出原件不是2。')
    for name in ('prepare_source_stage.py', 'prepare_actual.exit', 'prepare_actual.stdout.log',
                 'prepare_actual.stderr.log', 'stage_stopped.json', 'attempt_01_failure.json'):
        bind(REVIEW / 'stage_01' / name)
    failure = {'attempt': 1, 'actual_exit': 2, 'kind': 'historical_attribute_resolver_error_before_source_creation',
               'evidence': relative(PREVIOUS_FAILURE), 'evidence_sha256': sha(PREVIOUS_FAILURE)}
    if failure not in FAILURES: FAILURES.append(failure)
    print('CURRENT_ROOT_AND_COPY_AC7=1\nBACKEND_HISTORICAL_ATTRIBUTE_5DE_ORIGINAL_VERIFIED=1\nPREVIOUS_ACTUAL_EXIT_2_PRESERVED=1', flush=True)


def linux(path):
    text = path.as_posix()
    require(text.startswith('C:/'), '本工具只映射本项目C盘路径。')
    return '/mnt/c/' + text[3:]


def step(name, arguments):
    command = ['wsl.exe', '-d', 'Ubuntu-24.04', '-u', 'qyq', '--cd', linux(SOURCE_ROOT), '--', *GIT, *arguments]
    new_json(HERE / (name + '.inflight.json'), {'command': command, 'started_at': datetime.now(timezone.utc).isoformat(),
             'actual_exit_not_yet_known': True})
    p = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    write(HERE / (name + '.stdout.log'), p.stdout)
    write(HERE / (name + '.stderr.log'), p.stderr)
    write(HERE / (name + '.exit'), str(p.returncode) + '\n')
    item = {'name': name, 'command': command, 'actual_exit': p.returncode,
            'stdout_sha256': sha(HERE / (name + '.stdout.log')), 'stderr_sha256': sha(HERE / (name + '.stderr.log'))}
    new_json(HERE / (name + '.result.json'), item)
    STEPS.append(item)
    print(name + '_ACTUAL_EXIT=' + str(p.returncode), flush=True)
    if p.returncode:
        FAILURES.append({'name': name, 'actual_exit': p.returncode, 'result': relative(HERE / (name + '.result.json'))})
        raise RuntimeError(name + '实际失败，保留现场，不忽略空白、不修改补丁、不自动修复。')


def source_map():
    return {path.relative_to(PROGRAM).as_posix(): sha(path) for path in sorted(PROGRAM.rglob('*')) if path.is_file()}


def checkpoint(name, expected=None, expected_reference=None):
    actual = source_map()
    matched = expected is None or actual == expected
    target = HERE / ('source_map_' + name + '.json')
    new_json(target, actual)
    item = {'name': name, 'source_files': len(actual), 'source_map': relative(target), 'source_map_sha256': sha(target),
            'expected_reference': expected_reference, 'exact_map_match': matched}
    new_json(HERE / ('checkpoint_' + name + '.json'), item)
    CHECKPOINTS.append(item)
    require(matched, name + '完整源码表与指定输入不一致；停止，不改候选。')
    print(name + '_SOURCE_FILES=' + str(len(actual)), flush=True)
    return actual


def unchanged():
    after = {name: sha(ROOT / name) for name in GUARDS}
    require(after == GUARDS, '受保护的输入/原件发生变化，停止。')
    return after


def main(execute=False):
    global ATTEMPT_CREATED
    require(not SOURCE_ROOT.exists() and not SOURCE_ROOT.is_symlink(), '隔离源码已有内容，拒绝重跑。')
    bind(CLOSED, '3883e91eaa1d28c2a49c44641e0114724de1fed70ba130f874922dbb39e486da')
    closed = load(CLOSED)
    bind_declared(closed)
    require(sha(ATTRS) == sha(ROOT / '.gitattributes') == ATTR_SHA, '当前属性/固定副本不是本批ac7。')
    bind(ROOT_DELIVERY, EXPECTED_ROOT_DELIVERY_SHA)
    bind(ROOT_COMPONENT, EXPECTED_ROOT_COMPONENT_SHA)
    root_delivery = load(ROOT_DELIVERY); root_component = load(ROOT_COMPONENT)
    require(root_delivery['format'] == 'sn100-task-reagent-root-candidate-v2'
            and root_delivery['packaging_complete'] is True, '不是本次完整Root v2交付。')
    require(root_delivery['patches_are_alternatives'] is True and root_component['cumulative_base'] == 671,
            'Root两份补丁不是已确认的替代入口或累积底稿不符。')
    bind_declared(root_delivery)
    bind_declared(root_component, manifest=ROOT_COMPONENT)
    cumulative = root_component['cumulative_patch']
    require(cumulative == root_delivery['cumulative_on671']
            and project_path(cumulative['path']) == ROOT / ROOT_V2_PINS['cumulative_patch'][0]
            and cumulative['sha256'] == EXPECTED_ROOT_PATCH_SHA == ROOT_V2_PINS['cumulative_patch'][1]
            and EXPECTED_ROOT_PATCH_SHA not in REJECTED_ROOT_PATCHES, '未明确绑定修后Root的3e29累积补丁。')
    require(root_component['incremental_patch'] == root_delivery['incremental_on_previous674']
            and root_component['incremental_patch']['sha256'] == ROOT_V2_INCREMENTAL_SHA
            and root_component['incremental_patch'] != cumulative, '修复delta替代入口不符。')
    bind(ROOT_MAP, EXPECTED_ROOT_MAP_SHA)
    require(sha(ROOT_MAP) == root_component['source_snapshot_sha256'], 'Root v2完整源表没有绑定组件。')
    root_map = load(ROOT_MAP)
    require(type(root_delivery['source_count']) is int and type(root_component['final_source_count']) is int
            and len(root_map) == root_delivery['source_count'] == root_component['final_source_count'] == 674,
            'Root v2完整表/交付数量不一致。')
    bind(MAP_671, '188547eed49e592e67ed8ab4deb96f4c37c584402dd996f0321dd16c46d0a573')
    map671 = load(MAP_671)
    require(len(map671) == 671, '独立底稿不是671份。')
    require(set(map671).issubset(root_map), '修后Root意外删除了671底稿目标。')
    actual_root_changes = {name: {'before_sha256': map671.get(name), 'after_sha256': digest}
                           for name, digest in root_map.items() if map671.get(name) != digest}
    require(actual_root_changes == root_component['changed_on671'] and len(actual_root_changes) == 17,
            'Root v2的17个变更目标未与671/674完整表严格对应。')
    require(root_delivery['source_sha256'] == root_map['shell/task_reagent_shell.cpp'],
            'Root v2实际修复源码指纹未绑定完整表。')
    for name, digest in map671.items():
        bind(T / 'integration_08_base_review/derived_source/06_programs/sn100_host' / name, digest)
    default_delivery = load(DEFAULT_DIR / 'delivery_manifest.json')
    for name, digest in default_delivery['artifact_sha256'].items():
        bind(DEFAULT_DIR / name, digest)
    default_component = load(DEFAULT_DIR / default_delivery['component_manifest']['path'])
    require(len(default_component['owned_sources']) == 7, '默认容器不是七个自有目标。')
    for name, item in default_component['owned_sources'].items():
        bind(DEFAULT_DIR / 'base/06_programs/sn100_host' / name, item['base_sha256'])
        bind(DEFAULT_DIR / 'candidate/06_programs/sn100_host' / name, item['candidate_sha256'])
        require(root_map.get(name) == item['base_sha256'], 'Root v2完整表与默认容器组件七文件前像不一致：' + name)
    for key in ('backend_delivery', 'store_delivery'):
        delivery = load(project_path(closed['inputs'][key]['path']))
        bind_declared(delivery)
        component = project_path(delivery['component_manifest']['path'])
        bind_declared(load(component), manifest=component)
    page_component = load(project_path(closed['inputs']['page_consumer_manifest']['path']))
    for name, digest in page_component['changed'].items():
        bind(ROOT / 'temp/task_reagent_display_candidate/candidate/06_programs/sn100_host' / name, digest)
    archive = project_path(closed['inputs']['baseline_source_archive']['path'])
    snapshot_file = project_path(closed['inputs']['baseline_source_snapshot']['path'])
    baseline = load(snapshot_file)
    require(len(baseline) == 663, '正式07基线不是663份。')
    for name, digest in baseline.items():
        bind(ROOT / '06_programs/sn100_host' / name, digest)
    bind(Path(__file__).resolve())
    unchanged()
    if not execute:
        print('LOCAL_INPUTS=READY\nROOT_V2_SCHEMA_EXACT=1\nROOT_CUMULATIVE_PATCH_3E29_ONLY=1\nROOT_MAP_674_AND_COMPONENT_17_TARGETS_MATCH=1\nDEFAULT_SEVEN_EXPECTED_PREIMAGES_MATCH_ROOT_MAP=1\nPROTECTED_INPUTS=' + str(len(GUARDS)) + '\nSTAGE_SOURCE_CREATED=0\nGIT_STEPS_EXECUTED=0\nNEXT=等待根实读命令后另行安排--execute', flush=True)
        return 0
    require(not HERE.exists() and not HERE.is_symlink(), 'stage_02记录已存在，拒绝覆盖或重跑。')
    HERE.mkdir()
    ATTEMPT_CREATED = True
    new_json(HERE / 'historical_attribute_bindings.json', HISTORICAL_BINDINGS)
    new_json(HERE / 'protected_inputs_before.json', GUARDS)
    inherited_failures = []
    for name, digest in sorted(GUARDS.items()):
        path = ROOT / name
        if path.suffix == '.exit':
            text = path.read_text(encoding='utf-8').strip()
            if re.fullmatch(r'-?\d+', text) and int(text) != 0:
                inherited_failures.append({'path': name, 'sha256': digest, 'actual_exit': int(text), 'scope': '组件交付已列的历史退出原件，不计作本次失败。'})
        elif path.name == 'preparation_attempt_01.json' and DEFAULT_DIR in path.parents:
            old = load(path)
            if old.get('actual_exit'):
                inherited_failures.append({'path': name, 'sha256': digest, 'actual_exit': old['actual_exit'],
                                           'scope': '默认容器既有复制前置失败，沿原交付保留。'})
    new_json(HERE / 'component_historical_failures.json', inherited_failures)
    patches = [
        ('01_backend', project_path(closed['inputs']['backend_patch']['path'])),
        ('02_store', project_path(closed['inputs']['store_patch']['path'])),
        ('03_page_consumer', project_path(closed['inputs']['page_consumer_patch']['path'])),
        ('04_final_root', project_path(root_component['cumulative_patch']['path'])),
        ('05_default_container', project_path(closed['inputs']['default_container_patch']['path'])),
    ]
    require(sha(patches[3][1]) == EXPECTED_ROOT_PATCH_SHA and EXPECTED_ROOT_PATCH_SHA not in REJECTED_ROOT_PATCHES, '不是修后最终Root补丁。')
    new_json(HERE / 'resolved_inputs.json', {'original_closed_input_sha256': sha(CLOSED), 'root_delivery_sha256': sha(ROOT_DELIVERY),
             'root_component_sha256': sha(ROOT_COMPONENT), 'attributes_sha256': ATTR_SHA,
             'patches': [{'name': name, 'path': relative(path), 'sha256': sha(path)} for name, path in patches],
             'approval_scope': '只作隔离整合，未批准晋级、编译、联网、SQL或产品运行。'})
    SOURCE_ROOT.mkdir()
    write(SOURCE_ROOT / '.gitattributes', ATTRS.read_bytes())
    seen = set()
    with tarfile.open(archive, 'r:gz') as package:
        members = package.getmembers()
        require(len(members) == 663, '官方07包成员数不符。')
        for member in members:
            name = member.name
            require(member.isfile() and name.startswith('06_programs/sn100_host/') and not name.startswith('/')
                    and '..' not in PurePosixPath(name).parts, '源码包成员越界或非普通文件。')
            rel = name.removeprefix('06_programs/sn100_host/')
            require(rel in baseline and rel not in seen, '源码包成员不在唯一清单。')
            seen.add(rel)
            path = SOURCE_ROOT / name
            path.parent.mkdir(parents=True, exist_ok=True)
            write(path, package.extractfile(member).read())
            require(sha(path) == baseline[rel], '解包字节不符：' + rel)
    checkpoint('00_official07', baseline, relative(snapshot_file))
    step('00_git_init', ['init', '-q'])
    step('00_git_attributes', ['check-attr', 'text', 'eol', '--', '06_programs/sn100_host/shell/main.cpp',
                              '06_programs/sn100_host/shell/build.sh', '06_programs/sn100_host/samples/sample_store.h'])
    for name, patch in patches:
        require(sha(patch) == GUARDS[relative(patch)], '应用前补丁已变。')
        if name == '05_default_container':
            preimages = [{'path': target, 'expected_before_sha256': item['base_sha256'],
                          'actual_before_sha256': sha(PROGRAM / target), 'match': sha(PROGRAM / target) == item['base_sha256']}
                         for target, item in sorted(default_component['owned_sources'].items())]
            matched = all(item['match'] for item in preimages)
            new_json(HERE / 'default_container_full_preimages.json', {'all_seven_match': matched, 'files': preimages,
                     'default_component_sha256': sha(DEFAULT_DIR / 'component_manifest.json')})
            if not matched:
                FAILURES.append({'name': '05_default_container_full_preimages', 'status': 'source_comparison_failed',
                                 'git_check_or_apply_executed': False})
                raise RuntimeError('默认容器七文件完整前像不等，未执行第五片Git检查/应用。')
        step(name + '_check', ['apply', '--check', linux(patch)])
        step(name + '_apply', ['apply', linux(patch)])
        expected = map671 if name == '03_page_consumer' else root_map if name == '04_final_root' else None
        ref = relative(MAP_671) if name == '03_page_consumer' else relative(ROOT_MAP) if name == '04_final_root' else None
        actual = checkpoint(name, expected, ref)
        if name == '05_default_container':
            require(all(actual[target] == item['candidate_sha256'] for target, item in default_component['owned_sources'].items()),
                    '默认容器应用后七文件未等精确候选。')
            expected_final = dict(root_map)
            expected_final.update({target: item['candidate_sha256'] for target, item in default_component['owned_sources'].items()})
            require(actual == expected_final, '第五片改动越出七个目标或未保Root完整源。')
    final = source_map()
    new_json(HERE / 'source_manifest.json', final)
    write(HERE / 'source_manifest.sha256', ''.join(digest + '  ' + name + '\n' for name, digest in sorted(final.items())))
    after = unchanged()
    new_json(HERE / 'protected_inputs_after.json', after)
    changes = {'added': sorted(set(final) - set(baseline)), 'deleted': sorted(set(baseline) - set(final)),
               'changed': sorted(name for name in set(baseline) & set(final) if baseline[name] != final[name])}
    new_json(HERE / 'source_changes_from_official07.json', changes)
    # 全路径检查点供后续晋级检查读取；这是同一次实际应用的完整表，不冒充第二次独立回演。
    all_targets = sorted(set(baseline) | set(final))
    prefix = '06_programs/sn100_host/'
    full_checkpoints = []
    for index, item in enumerate(CHECKPOINTS):
        existing = load(ROOT / item['source_map'])
        require(set(existing).issubset(all_targets), '中间检查点出现最终表以外的源码目标。')
        full_map = {prefix + name: existing.get(name) for name in all_targets}
        target = HERE / ('checkpoint_full_' + str(index).zfill(2) + '.json')
        new_json(target, full_map)
        full_checkpoints.append({'index': index, 'path': relative(target), 'sha256': sha(target)})
    require(len(full_checkpoints) == 6, '实际五片应用没有六份完整检查点。')
    patch_rows = [{'index': index, 'label': name, 'patch': relative(path), 'sha256': sha(path),
                   'before_checkpoint': full_checkpoints[index - 1], 'after_checkpoint': full_checkpoints[index]}
                  for index, (name, path) in enumerate(patches, 1)]
    single_application = {'format': 'sn100-integration-08-single-isolated-application-v1',
                          'complete': True, 'five_standard_checks_and_applies_passed': True,
                          'actual_git_steps': STEPS, 'final_source_map_sha256': sha(HERE / 'source_manifest.json'),
                          'root674_map_and_default_seven_after_bytes_equal': True,
                          'independent_second_replay_executed': False,
                          'scope': '本次独立目录中从官方07依次普通应用五片；再次独立回演由根的晋级前检查实际执行。'}
    new_json(HERE / 'single_stage_application_result.json', single_application)
    metadata = {'format': 'sn100-integration-08-stage-02-metadata-candidate-v2', 'isolation_complete': True,
                'stage_directory': relative(SOURCE_ROOT), 'patch_count': 5, 'base_count': len(baseline), 'source_count': len(final),
                'source07_archive': {'path': relative(archive), 'sha256': sha(archive)},
                'source07_snapshot': {'path': relative(snapshot_file), 'sha256': sha(snapshot_file)},
                'root_attributes': {'path': '.gitattributes', 'sha256': ATTR_SHA},
                'base_source_files': baseline, 'final_source_files': final,
                'final_source_snapshot': {'path': relative(HERE / 'source_manifest.json'), 'sha256': sha(HERE / 'source_manifest.json')},
                'root_delivery': {'path': relative(ROOT_DELIVERY), 'sha256': sha(ROOT_DELIVERY)},
                'root_component': {'path': relative(ROOT_COMPONENT), 'sha256': sha(ROOT_COMPONENT)},
                'root_source_map': {'path': relative(ROOT_MAP), 'sha256': sha(ROOT_MAP)},
                'git_apply_config': {'implementation': '/usr/bin/git', 'wsl_distribution': 'Ubuntu-24.04', 'user': 'qyq',
                                     'flags': GIT[1:], 'core_eol': 'unset'},
                'single_stage_application': {'path': relative(HERE / 'single_stage_application_result.json'),
                                             'sha256': sha(HERE / 'single_stage_application_result.json')},
                'independent_second_replay_executed': False,
                'independent_second_replay_status': '待根晋级脚本preflight实际执行；本任务未复制表冒称第二次回演。',
                'protected_inputs_before': {'path': relative(HERE / 'protected_inputs_before.json'), 'sha256': sha(HERE / 'protected_inputs_before.json')},
                'protected_inputs_after': {'path': relative(HERE / 'protected_inputs_after.json'), 'sha256': sha(HERE / 'protected_inputs_after.json')},
                'changed_files': sorted(prefix + name for name in all_targets if baseline.get(name) != final.get(name)),
                'added_files': [prefix + name for name in changes['added']],
                'deleted_files': [prefix + name for name in changes['deleted']],
                'review_approved': False, 'promotion_executed': False, 'formal_sources_changed_by_this_task': False,
                'source_directory_windows': str(PROGRAM), 'source_directory_wsl': linux(PROGRAM),
                'baseline_files': len(baseline), 'actual_final_files': len(final), 'patches': patch_rows,
                'source_manifest_sha256': sha(HERE / 'source_manifest.json'),
                'source_table_sha256': sha(HERE / 'source_manifest.sha256'), 'checkpoints': full_checkpoints, 'stage_checkpoints': CHECKPOINTS,
                'default_all_seven_full_preimages_match': True, 'default_seven_final_bytes_match': True,
                'all_protected_inputs_and_formal663_unchanged': True, 'protected_input_files': len(GUARDS),
                'attributes_sha256': ATTR_SHA, 'historical_attribute_bindings': HISTORICAL_BINDINGS, 'steps': STEPS, 'historical_failures': FAILURES,
                'component_historical_failures_reference': relative(HERE / 'component_historical_failures.json'),
                'component_historical_failures_sha256': sha(HERE / 'component_historical_failures.json'),
                'source_changes': changes, 'compiled': False, 'network_used': False, 'sql_executed': False,
                'tests_or_products_executed': False, 'board_accessed': False}
    new_json(HERE / 'integration_metadata_candidate.json', metadata)
    print('ISOLATED_SOURCE_FILES=' + str(len(final)) + '\nALL_FIVE_STANDARD_CHECK_APPLY_PASSED=1\nDEFAULT_FULL_PREIMAGES=7_MATCH\nFORMAL663_AND_INPUTS_UNCHANGED=1', flush=True)
    print('FINAL_SOURCE_MAP_SHA256=' + metadata['source_manifest_sha256'] + '\nFINAL_SOURCE_TABLE_SHA256=' + metadata['source_table_sha256'], flush=True)
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for item in ('root-delivery', 'root-delivery-sha', 'root-component', 'root-component-sha',
                 'root-source-map', 'root-source-map-sha', 'root-patch-sha'):
        parser.add_argument('--' + item)
    parser.add_argument('--execute', action='store_true', help='仅根实读v2完整参数命令后另行安排；不编译或运行。')
    args = parser.parse_args()
    try:
        background_check()
        values = (args.root_delivery, args.root_delivery_sha, args.root_component, args.root_component_sha,
                  args.root_source_map, args.root_source_map_sha, args.root_patch_sha)
        if not all(values):
            print('STATUS=WAITING_INPUTS\nREASON=等待根提供修后Root的delivery/component/完整表确名与SHA及patch SHA\nSTAGE_SOURCE_CREATED=0\nGIT_STEPS_EXECUTED=0', flush=True)
            sys.exit(75)
        for digest in (args.root_delivery_sha, args.root_component_sha, args.root_source_map_sha, args.root_patch_sha):
            require(re.fullmatch(r'[a-f0-9]{64}', digest) is not None, '修后Root SHA格式不符。')
        require(args.root_patch_sha not in REJECTED_ROOT_PATCHES, '旧5cf候选已被终审发现缺口，不得继续本次整合。')
        ROOT_DELIVERY, ROOT_COMPONENT, ROOT_MAP = map(project_path, (args.root_delivery, args.root_component, args.root_source_map))
        EXPECTED_ROOT_DELIVERY_SHA, EXPECTED_ROOT_COMPONENT_SHA = args.root_delivery_sha, args.root_component_sha
        EXPECTED_ROOT_MAP_SHA, EXPECTED_ROOT_PATCH_SHA = args.root_source_map_sha, args.root_patch_sha
        for key, path, digest in (('delivery', ROOT_DELIVERY, EXPECTED_ROOT_DELIVERY_SHA),
                                  ('component', ROOT_COMPONENT, EXPECTED_ROOT_COMPONENT_SHA),
                                  ('source_map', ROOT_MAP, EXPECTED_ROOT_MAP_SHA)):
            require(path == ROOT / ROOT_V2_PINS[key][0] and digest == ROOT_V2_PINS[key][1],
                    '参数不是根明确给定的修后Root v2确名/SHA：' + key)
        require(EXPECTED_ROOT_PATCH_SHA == ROOT_V2_PINS['cumulative_patch'][1],
                '只能选择根给定的3e29累积补丁，不能混用bc404 delta或旧5cf。')
        sys.exit(main(execute=args.execute))
    except Exception as error:
        if args.execute and ATTEMPT_CREATED:
            new_json(HERE / 'stage_stopped.json', {'reason': str(error), 'actual_steps': STEPS,
                     'checkpoints': CHECKPOINTS, 'historical_failures': FAILURES, 'automatic_repair_or_whitespace_ignore': False,
                     'compiled': False, 'network_used': False, 'sql_executed': False, 'tests_or_products_executed': False})
        print('STOP=' + str(error), file=sys.stderr)
        sys.exit(2)
