#!/usr/bin/env python3
"""09正式构建与官方源码包到齐后，串行冻结、构建同版检查器、检查ARM依赖。

默认仅打印准备状态。--execute时必须同时给出正式构建绝对目录、64位发布号、
官方源码包绝对路径及其SHA-256。不构建正式程序、不运行产品、不上传或联网。
数量取实际清单；原输入与已有证据不覆盖，静态scope不回填发布号。
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
EXPECTED_WORK = ROOT / 'temp/sn100_host/workflow_night_20261102/integration_09_revision_05'
STEP_NAMES = ('freeze', 'build_review', 'checker_dependency_check')
UPLOAD_NAMES = ('status_login_review_09', 'review_release_id', 'run_case_on_board.sh',
                'capture_application_exit.sh', 'shutdown_contract.json', 'required_shutdown_markers.txt',
                'forbidden_shutdown_markers.txt', 'shutdown_contract_release_id')
SCOPE_FALSE = ('n_function_verified', 'in_flight_n_close_verified', 'task_business_verified',
               'in_flight_task_single_outer_close_verified', 'new_construction_proven',
               'same_main_memory_bool_read')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(value, name):
    require(isinstance(value, str) and re.fullmatch(r'[a-f0-9]{64}', value), name + '须是完整小写SHA-256')
    return value


def file_record(path, sha, allow_link=False):
    path = Path(path)
    require(path.is_absolute() and path.is_file(), '实际输入文件缺失：' + str(path))
    resolved = path.resolve(strict=True)
    require(allow_link or (not path.is_symlink() and resolved == path), '正式输入被重定向：' + str(path))
    stat = resolved.stat()
    return {'sha256': sha(resolved), 'bytes': stat.st_size, 'mtime_ns': stat.st_mtime_ns,
            'resolved': str(resolved), 'link_target': os.readlink(path) if path.is_symlink() else None}


def audit_inputs(integration, release, source_archive, source_sha, api):
    """只读核对正式输入；每次读取均在正式构建锁内进行。"""
    sha, read = api['sha'], api['read_json']
    identity, snapshot, rows, source_program = api['verify_stage'](integration, release)
    approved = read(HERE / 'approved_final_merge_review.json')
    require(snapshot == read(HERE / 'staged_source_manifest_09.json') == approved.get('final_source_files'),
            '正式源码不是已晋级并批准的09后像')
    promoted = read(HERE / 'root_promoted_inputs_09.json')
    require(release != promoted.get('expected_current', '').removeprefix('releases/'), '拒绝08发布及其对象')
    for name, expected in snapshot.items():
        require(sha(source_program / name) == expected, '实际正式源码已变：' + name)
    complete = read(integration / 'build_complete.json')
    require((integration / 'build_exit.txt').read_text().strip() == '0'
            and complete.get('arm64_build_and_package_passed') is True and complete.get('release_id') == release,
            '正式09完整构建没有实际成功记录')
    # Root09正式完成记录不把include搜索目录计为源码输入；列出总行数仍另存。
    file_rows = [(kind, value) for kind, value in rows if kind != 'include']
    kinds = dict(Counter(kind for kind, _ in file_rows))
    unique_inputs = len({value for _, value in file_rows})
    require(complete.get('source_count') == len(snapshot) and complete.get('unique_build_inputs') == unique_inputs
            and complete.get('kinds') == kinds, '正式构建记录与实际源码/列出输入/唯一输入不一致')
    require(sha(integration / 'source_snapshot.json') == complete.get('source_snapshot_sha256'),
            '正式构建清单字节不等于完成记录')

    app_sha = digest(complete.get('binaries', {}).get('sn100_shell'), '正式应用')
    renderer_sha = digest(complete.get('binaries', {}).get('sn100_report_renderer'), '正式报告程序')
    stage = Path(identity['stage_directory'])
    require(identity['application_sha256'] == app_sha == sha(integration / 'build/sn100_shell')
            and sha(integration / 'build/sn100_report_renderer') == sha(stage / 'sn100_report_renderer') == renderer_sha,
            '正式应用、报告程序与发布暂存内容不一致')
    package_name = complete.get('package')
    require(package_name == 'sn100_shell_' + release + '.tar.gz', '正式程序包名称不属于本批09')
    package_sha = digest(complete.get('package_sha256'), '正式程序包')
    archive_path, actual_source_sha = api['official_archive'](source_archive, release, identity, snapshot)
    require(actual_source_sha == source_sha, '官方源码包不等于调用方给出的实际SHA')
    official = archive_path.parent
    require(official == ROOT / '07_backups/sn100_host/board_b/workflow_integration_20261102/integration_09',
            '不是板B正式integration_09归档目录')
    manifest = {}
    for line in (official / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([a-f0-9]{64})  ([A-Za-z0-9_.-]+)', line)
        require(match is not None and match[2] not in manifest, '官方归档清单格式错误或重复')
        require(sha(official / match[2]) == match[1], '官方归档文件校验失败：' + match[2])
        manifest[match[2]] = match[1]
    require(manifest.get(archive_path.name) == source_sha and manifest.get(package_name) == package_sha
            and sha(integration / 'packages' / package_name) == package_sha
            and manifest.get('build_complete.json') == sha(integration / 'build_complete.json'),
            '官方源码、程序包或构建完成记录不是同一批输入')
    formal_binding_path = integration / 'object_binding.json'
    formal_binding = read(formal_binding_path)
    require(complete.get('object_binding', {}).get('path') == formal_binding_path.relative_to(ROOT).as_posix()
            and complete['object_binding'].get('sha256') == sha(formal_binding_path) == manifest.get('object_binding.json')
            and formal_binding.get('source_files') == snapshot, 'Root09正式对象绑定元数据与官方副本或源码不一致')

    dependency_path = integration / 'dependencies/dependency_report.json'
    require(sha(dependency_path) == complete.get('dependency_report_sha256') == manifest.get('dependency_report.json'),
            '正式依赖报告与构建/官方归档不一致')
    dependency = read(dependency_path)
    require(dependency.get('offline_link_and_sql_closure_passed') is True and dependency.get('errors') == []
            and dependency.get('binary') == str(integration / 'build/sn100_shell')
            and dependency.get('additional_binaries') == [str(integration / 'build/sn100_report_renderer')],
            '正式静态依赖报告未通过或属于其他程序')
    dependencies, runtime_files = {}, {}
    for item in dependency['files']:
        path = Path(item['path'])
        record = file_record(path, sha, allow_link=True)
        require(record['sha256'] == item['sha256'] and str(path) not in dependencies, '正式依赖文件已变或重复')
        dependencies[str(path)] = record
    for component in dependency['runtime_components'].values():
        for name in component.get('archived_files', []):
            path = Path(name)
            if not path.is_absolute():
                path = Path(dependency['sysroot']) / path
            runtime_files[str(path)] = file_record(path, sha, allow_link=True)

    build_dir = integration / 'arm64_build'
    require(build_dir.resolve(strict=True) == build_dir, '正式对象目录被重定向')
    build_files = {}
    for path in sorted(build_dir.rglob('*')):
        require(not path.is_symlink(), '正式对象/生成目录不能含链接：' + str(path))
        if path.is_file():
            build_files[path.relative_to(build_dir).as_posix()] = file_record(path, sha)
    require('build.lock' in build_files, '完整构建目录缺少正式锁文件')
    # 冻结前只能按已逐份SHA核过的正式源重建顺序；真正bind阶段始终使用官方frozen09。
    # 冻结后的复核也改读同一官方冻结树，并先核完整值表，不借正式源代替冻结头。
    order_source = Path(source_program)
    frozen = HERE / 'frozen_09/sn100_host'
    if frozen.exists():
        require(frozen.is_dir() and not frozen.is_symlink() and frozen.resolve() == frozen, '09冻结源码目录被重定向')
        api['verify_frozen'](frozen, snapshot)
        order_source = frozen
    objects, generated, main_object = api['object_binding'](integration, order_source, rows, source_program)
    main_names = {Path(item['path']).name for item in objects}
    helper_names = {name for name in build_files if name.endswith('.o')} - main_names
    require(all(re.fullmatch(r'helper_[A-Za-z0-9_]+\.o', name) for name in helper_names), '出现未归属的额外对象')
    for name in helper_names:
        api['elf_arm64'](build_dir / name, relocatable=True)
    counts = complete.get('objects', {})
    require(counts.get('main') == len(objects) and counts.get('helper') == len(helper_names)
            and counts.get('total') == len(objects) + len(helper_names)
            and counts.get('moc') == sum(item['kind'] == 'moc' for item in objects)
            and counts.get('resource') == sum(item['kind'] == 'resource' for item in objects),
            '正式主对象、辅助对象及moc/资源数量与实际文件不符')
    require(all(build_files.get(name, {}).get('sha256') == value for name, value in generated.items()),
            '生成文件与实际完整构建目录不符')
    metadata_names = ('build_exit.txt', 'build_complete.json', 'build_inputs.tsv', 'source_snapshot.json',
                      'input_verification.json', 'merge_inputs.json', 'object_binding.json')
    metadata = {name: file_record(integration / name, sha) for name in metadata_names}
    metadata['dependencies/dependency_report.json'] = file_record(dependency_path, sha)
    return {'release_id': release, 'formal_identity': identity, 'application_sha256': app_sha,
            'report_renderer_sha256': renderer_sha, 'package_sha256': package_sha,
            'source_archive': str(archive_path), 'source_archive_sha256': source_sha,
            'source_count': len(snapshot), 'listed_build_input_count': len(rows), 'unique_build_inputs': unique_inputs,
            'input_kinds': kinds, 'object_count': len(objects), 'helper_object_count': len(helper_names),
            'generated_file_count': len(generated), 'complete_arm64_build_file_count': len(build_files),
            'main_object': main_object, 'formal_objects_sha256': {Path(o['path']).name: o['sha256'] for o in objects},
            'complete_arm64_build_files': build_files, 'metadata_files': metadata,
            'official_files_sha256': manifest, 'official_manifest_sha256': sha(official / 'SHA256SUMS'),
            'dependency_files': dependencies, 'runtime_archived_files': runtime_files,
            'template_sha256': {name: sha(HERE / name) for name in api['TEMPLATE_NAMES']},
            'board_accessed': False, 'product_executed': False}


def execute(name, command, api):
    print('EXECUTING=' + name, flush=True)
    with (HERE / (name + '.log')).open('xb') as output:
        try:
            code = subprocess.run(command, cwd=HERE, stdout=output, stderr=subprocess.STDOUT,
                                  env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1')).returncode
        except OSError as error:
            output.write(('未能启动本步骤：' + str(error) + '\n').encode('utf-8'))
            code = None
    api['new_text'](HERE / (name + '.exit'), 'not_started\n' if code is None else str(code) + '\n')
    print(name + '_EXIT=' + str(code), flush=True)
    return code


def write_handoff(release, before, result, api):
    sha, read = api['sha'], api['read_json']
    contract = read(HERE / 'shutdown_contract.json')
    prepared = read(HERE / 'prepared_inputs.json')
    require(contract.get('release_id') == release
            and contract.get('main_sha256') == before['formal_identity']['main_sha256']
            and (HERE / 'review_release_id').read_text().strip() == release,
            '实际关闭合同或检查器发布身份不属于本批09')
    scope = dict(prepared.get('observation_scope', {}))
    require(scope.get('new_construction_proven') is False and scope.get('same_main_memory_bool_read') is False,
            '急诊观察范围缺失或越界')
    scope['release_id'] = release  # 只写派生handoff，静态scope候选仍保持原字节。
    markers = contract.get('required_markers')
    require(isinstance(markers, list) and markers and len(markers) == len(set(markers)), '实际关闭合同标记为空或重复')
    upload = {name: sha(HERE / name) for name in UPLOAD_NAMES}
    api['new_text'](HERE / 'upload_manifest_09.sha256', ''.join(value + '  ' + name + '\n' for name, value in upload.items()))
    record_names = ('prepared_inputs.json', 'source_snapshot_09.json', 'link_provenance.json',
                    'review_build_provenance.json', 'execution_09_inputs_before.json',
                    'execution_09_inputs_after.json', 'execution_09_result.json',
                    'checker_dependencies/dependency_report.json', 'observation_scope_09.json',
                    'staged_source_manifest_09.json', 'approved_final_merge_review.json', 'root_promoted_inputs_09.json')
    handoff = dict(result, schema='sn100-final-09-checker-handoff-v1',
                   status='checker_arm64_link_and_static_dependencies_passed_RK_pending',
                   formal_application_sha256=before['application_sha256'],
                   report_renderer_sha256=before['report_renderer_sha256'], package_sha256=before['package_sha256'],
                   source_archive=before['source_archive'], source_archive_sha256=before['source_archive_sha256'],
                   unique_build_inputs=before['unique_build_inputs'], listed_build_input_count=before['listed_build_input_count'],
                   helper_object_count=before['helper_object_count'], generated_file_count=before['generated_file_count'],
                   complete_arm64_build_file_count=before['complete_arm64_build_file_count'],
                   actual_driver_exit_code=0, before_after_identical=True,
                   before_after_sha256=sha(HERE / 'execution_09_inputs_before.json'),
                   contract_sha256=sha(HERE / 'shutdown_contract.json'), required_markers=markers,
                   required_marker_count=len(markers), observation_scope=scope, actual_runtime_check_count=None,
                   upload_file_count=len(upload), upload_files_sha256=upload,
                   upload_manifest_sha256=sha(HERE / 'upload_manifest_09.sha256'),
                   record_files_sha256={name: sha(HERE / name) for name in record_names},
                   lis_business_verified=False, current_alarm_runtime_verified=False,
                   administrator_password_verified=False, physical_input_or_screen_verified=False)
    api['new_json'](HERE / 'final_09_handoff.json', handoff)
    api['new_text'](HERE / 'final_09_build_handoff.md',
        '# 第09轮同版检查器构建交接\n\n'
        '发布身份：`' + release + '`。实际源码' + str(before['source_count']) + '份，列出输入'
        + str(before['listed_build_input_count']) + '行，唯一输入' + str(before['unique_build_inputs'])
        + '份；主程序对象' + str(before['object_count']) + '个，辅助对象' + str(before['helper_object_count'])
        + '个，生成文件' + str(before['generated_file_count']) + '份。\n\n'
        '冻结、同版ARM64检查器链接和静态依赖检查的实际退出码均为0，完整前后输入未变。'
        '尚未上传或运行，RK、真实LIS对端、当前告警回包、Task/N业务与在途关闭均待各自实证。'
        '急诊快照不证明创建来源或main同一内存布尔值。实际运行检查数仍待填写。\n\n'
        '8项上传物料见`upload_manifest_09.sha256`，完整身份及实际关闭合同见`final_09_handoff.json`。'
        '对象来源按真实构建输入和生成规则核对，不声称编译器trace或完整头依赖图。\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--integration-directory', type=Path)
    parser.add_argument('--release-id')
    parser.add_argument('--source-archive', type=Path)
    parser.add_argument('--source-archive-sha256')
    args = parser.parse_args()
    if not args.execute:
        print('FINAL_09_DRIVER_PREPARED=1; EXECUTED=0; FORMAL_INPUTS_REQUIRED=1; NETWORK_ACCESSED=0')
        return 0
    require(os.name == 'posix' and os.geteuid() != 0, '请使用普通WSL用户执行交叉构建')
    require(args.integration_directory is not None and args.source_archive is not None,
            '--execute须同时给出正式构建与官方源码包绝对路径')
    release = digest(args.release_id, '--release-id')
    source_sha = digest(args.source_archive_sha256, '--source-archive-sha256')
    from bind_build_09 import (ROOT as binder_root, HERE as binder_here, TEMPLATE_NAMES, build_lock,
        integration_path, sha, read_json, new_json, new_text, verify_stage, verify_frozen, object_binding, elf_arm64)
    from freeze_release_09 import official_archive
    require(binder_root == ROOT and binder_here == HERE, '09工具根目录归属不一致')
    integration = integration_path(args.integration_directory)
    require(integration == EXPECTED_WORK and args.source_archive.is_absolute(), '正式09路径不属于本次明确工作目录')
    api = dict(sha=sha, read_json=read_json, new_json=new_json, new_text=new_text,
               verify_stage=verify_stage, verify_frozen=verify_frozen, object_binding=object_binding, elf_arm64=elf_arm64,
               official_archive=official_archive, TEMPLATE_NAMES=TEMPLATE_NAMES)
    outputs = ('execution_09_inputs_before.json', 'execution_09_inputs_after.json', 'execution_09_result.json',
               'freeze_09.log', 'freeze_09.exit', 'build_review_09.log', 'build_review_09.exit',
               'checker_dependency_check.log', 'checker_dependency_check.exit', 'checker_dependencies',
               'frozen_09', 'prepared_inputs.json', 'source_snapshot_09.json', 'link_provenance.json', 'link_objects.txt',
               'status_login_review_09', 'status_login_review_09.new', 'review_09.o', 'review_release_id',
               'review_binary.sha256', 'review_elf_header.txt', 'review_build_provenance.json',
               'final_09_handoff.json', 'final_09_build_handoff.md', 'upload_manifest_09.sha256') + UPLOAD_NAMES[4:]
    for name in outputs:
        require(not (HERE / name).exists() and not (HERE / name).is_symlink(), '执行证据已存在，不能覆盖：' + name)
    # 读快照时取得正式锁；释放后交给freeze/build各自加锁，最后再核完整指纹未变。
    with build_lock(integration):
        before = audit_inputs(integration, release, args.source_archive, source_sha, api)
    new_json(HERE / 'execution_09_inputs_before.json', before)
    codes = {'freeze': execute('freeze_09', [sys.executable, str(HERE / 'freeze_release_09.py'),
                            str(integration), release, str(args.source_archive)], api)}
    if codes['freeze'] == 0:
        codes['build_review'] = execute('build_review_09', ['bash', str(HERE / 'build_review_09.sh'), release], api)
    if codes.get('build_review') == 0:
        build = read_json(HERE / 'review_build_provenance.json')
        codes['checker_dependency_check'] = execute('checker_dependency_check', [sys.executable,
            str(HERE / 'frozen_09/sn100_host/shell/check_arm64.py'), str(HERE / 'status_login_review_09'),
            '--sysroot', build['sysroot'], '--output-dir', str(HERE / 'checker_dependencies')], api)
    audit_error = None
    try:
        with build_lock(integration):
            after = audit_inputs(integration, release, args.source_archive, source_sha, api)
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        audit_error = str(error)
        after = {'release_id': release, 'audit_complete': False, 'error': audit_error}
    new_json(HERE / 'execution_09_inputs_after.json', after)
    unchanged = before == after
    success = unchanged and all(type(codes.get(name)) is int and codes[name] == 0 for name in STEP_NAMES)
    result = dict(release_id=release, actual_step_exit_codes=codes,
                  complete_source_archive_dependencies_objects_unchanged=unchanged, checker_built=False,
                  source_count=before['source_count'], object_count=before['object_count'],
                  dependency_file_count=len(before['dependency_files']), runtime_archived_file_count=len(before['runtime_archived_files']),
                  board_accessed=False, product_executed=False, network_accessed=False,
                  **{name: False for name in SCOPE_FALSE})
    if audit_error:
        result['after_audit_error'] = audit_error
    if success:
        try:
            binding, prepared = read_json(HERE / 'link_provenance.json'), read_json(HERE / 'prepared_inputs.json')
            unsigned_record = dict(binding)
            record_sha = unsigned_record.pop('record_sha256', None)
            canonical = json.dumps(unsigned_record, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')
            require(record_sha == hashlib.sha256(canonical).hexdigest(), '链接记录自身指纹不一致')
            require(binding.get('release_id') == prepared.get('release_id') == release
                    and binding.get('source_count') == before['source_count'] and binding.get('object_count') == before['object_count']
                    and binding.get('main_object_kept') is True and binding.get('main_object') == before['main_object']
                    and binding.get('formal_identity') == prepared.get('formal_identity') == before['formal_identity']
                    and binding.get('prepared_inputs_sha256') == sha(HERE / 'prepared_inputs.json'),
                    '最终09绑定未保留本批完整对象/正式入口')
            require({Path(item['path']).name: item['sha256'] for item in binding['objects']} == before['formal_objects_sha256']
                    and binding.get('template_sha256') == prepared.get('template_sha256') == before['template_sha256'],
                    '正式对象或冻结模板与执行前不一致')
            checker_dependency = read_json(HERE / 'checker_dependencies/dependency_report.json')
            require(checker_dependency.get('offline_link_and_sql_closure_passed') is True
                    and checker_dependency.get('errors') == [] and checker_dependency.get('binary') == str(HERE / 'status_login_review_09'),
                    '检查器静态依赖结果不属于本次实际检查程序')
            result.update(checker_built=True, checker_sha256=sha(HERE / 'status_login_review_09'),
                          checker_source_sha256=sha(HERE / 'status_login_review_09.cpp'), observer_sha256=sha(HERE / 'ad_wire_observer.h'),
                          lis_observer_sha256=sha(HERE / 'lis_connection_observer_09.h'),
                          current_alarm_observer_sha256=sha(HERE / 'current_alarm_observer_09.h'),
                          link_provenance_sha256=sha(HERE / 'link_provenance.json'),
                          link_record_sha256=binding['record_sha256'], main_object=binding['main_object'])
        except (OSError, ValueError, TypeError, RuntimeError, KeyError) as error:
            success = False
            result['binding_error'] = str(error)
    new_json(HERE / 'execution_09_result.json', result)
    if success:
        write_handoff(release, before, result, api)
    print(json.dumps(result, ensure_ascii=False), flush=True)
    return 0 if success else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, TypeError, RuntimeError, KeyError) as error:
        print('FINAL_09_DRIVER_FAILED: ' + str(error), file=sys.stderr)
        sys.exit(1)
