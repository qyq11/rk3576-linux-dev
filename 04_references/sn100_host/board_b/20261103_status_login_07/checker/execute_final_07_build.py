#!/usr/bin/env python3
"""准备本批07的唯一冻结/检查器交叉编译入口；默认不执行。

总协调给出官方663源包及SHA后，才可加--execute。保存真实步骤退出码和前后
输入；不上传、不联板、不切current、不运行产品，不覆盖已有执行证据。
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
INTEGRATION = ROOT / 'temp/sn100_host/workflow_night_20261102/integration_07'
RELEASE = '94d1b9794f2d8f1ff776b498e62d29ba2ebd82fef807d7cda70b05837c3eccb1'
APP = '79dc525c85dcee4bf27fe9af95d07334b4b055631495467646854da84775834c'
RENDERER = '792483fe0555687aa60995bf44f1b5277c1bba27dc049fa40491e589fe1e298b'
PACKAGE = '0d387884c9fcba5012ddd897e571cfe579b8858561922fa2d980b23b04c885af'
SNAPSHOT = 'fd5614ad0673ab6896eeea1f7502ac09ec8a4ac4f98a338f5967a4ad3717fdaf'
SOURCE_COUNT, UNIQUE_INPUTS, MAIN_OBJECTS = 663, 370, 277


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


def save(name, record):
    with (HERE / name).open('x', encoding='utf-8') as stream:
        json.dump(record, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write('\n')


def audit_inputs(source_tar, source_sha):
    official = source_tar.parent
    require(source_tar.is_absolute() and source_tar.resolve(strict=True) == source_tar
            and source_tar.is_relative_to(ROOT / '07_backups/sn100_host/board_b')
            and official.name == 'integration_07'
            and source_tar.name == 'sn100_sources_' + RELEASE + '.tar.gz',
            '只接受最终07官方源码包，不读私有stage或第06版包')
    complete = read(INTEGRATION / 'build_complete.json')
    require((INTEGRATION / 'build_exit.txt').read_text().strip() == '0'
            and complete['arm64_build_and_package_passed'] is True
            and complete['release_id'] == RELEASE, '本批正式07构建未完成')
    require(complete['source_count'] == SOURCE_COUNT and complete['unique_build_inputs'] == UNIQUE_INPUTS,
            '本次真实源码/输入数量与协调方提供值不同')
    require(complete['objects']['main'] == MAIN_OBJECTS and complete['objects']['helper'] == 2
            and complete['objects']['total'] == MAIN_OBJECTS + 2, '主对象和辅助对象数量错配')
    require(complete['binaries'] == {'sn100_shell': APP, 'sn100_report_renderer': RENDERER}
            and complete['package_sha256'] == PACKAGE
            and complete['source_snapshot_sha256'] == SNAPSHOT, 'complete不是本批正式07身份')
    require(sha(INTEGRATION / 'build/sn100_shell') == APP
            and sha(INTEGRATION / 'build/sn100_report_renderer') == RENDERER, '实际主/报告程序与complete不符')
    require(sha(source_tar) == source_sha, '官方源码包SHA不等于协调方实际给出的SHA')
    snapshot = read(INTEGRATION / 'source_snapshot.json')
    require(len(snapshot) == SOURCE_COUNT and sha(INTEGRATION / 'source_snapshot.json') == SNAPSHOT,
            '实际源码清单与指定本批构建不符')
    require(read(official / 'source_snapshot.json') == snapshot, '官方包旁清单与实际构建不符')
    provenance = read(official / 'provenance.json')
    require(provenance['release_id'] == RELEASE and provenance['source_archive_sha256'] == source_sha
            and provenance['application_sha256'] == APP and provenance['source_files'] == SOURCE_COUNT
            and provenance['intended_board'] == 'board_b' and provenance['build_and_dependencies'] == 'passed',
            '官方源码包来源不属于本批已完成构建')
    official_files = {}
    for line in (official / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        digest, name = line.split('  ', 1)
        require(re.fullmatch(r'[a-f0-9]{64}', digest) and Path(name).name == name
                and name not in official_files and sha(official / name) == digest,
                '官方归档校验失败或清单重复：' + name)
        official_files[name] = digest
    require(official_files[source_tar.name] == source_sha
            and official_files[complete['package']] == PACKAGE
            and sha(INTEGRATION / 'build_complete.json') == official_files['build_complete.json'],
            '正式构建、官方程序包和源码包不一致')
    dependency_path = INTEGRATION / 'dependencies/dependency_report.json'
    require(sha(dependency_path) == complete['dependency_report_sha256']
            == official_files['dependency_report.json'], '依赖报告与complete/官方归档不符')
    dependency = read(dependency_path)
    require(dependency['offline_link_and_sql_closure_passed'] is True and not dependency['errors']
            and dependency['binary'] == str(INTEGRATION / 'build/sn100_shell')
            and dependency['additional_binaries'] == [str(INTEGRATION / 'build/sn100_report_renderer')],
            '正式程序的静态依赖检查未通过或属于其他批次')
    dependencies = {}
    for item in dependency['files']:
        path = Path(item['path'])
        require(sha(path) == item['sha256'], '依赖文件已变：' + str(path))
        dependencies[str(path)] = {'sha256': item['sha256'], 'resolved': str(path.resolve(strict=True))}
    runtime_files = {}
    for component in dependency['runtime_components'].values():
        for name in component.get('archived_files', []):
            path = Path(name)
            if not path.is_absolute():
                path = Path(dependency['sysroot']) / path
            runtime_files[str(path)] = {'sha256': sha(path), 'resolved': str(path.resolve(strict=True))}
    objects = {path.name: sha(path) for path in (INTEGRATION / 'arm64_build').glob('obj_*.o')}
    require(set(objects) == {'obj_' + str(i) + '.o' for i in range(MAIN_OBJECTS)},
            '本次主程序必须是连续277个对象，不能混入2个报告辅助对象')
    metadata = {name: sha(INTEGRATION / name) for name in (
        'build_exit.txt', 'build_complete.json', 'build_inputs.tsv', 'source_snapshot.json',
        'input_verification.json', 'merge_inputs.json')}
    metadata['official_manifest'] = sha(official / 'SHA256SUMS')
    return {'release_id': RELEASE, 'application_sha256': APP, 'report_renderer_sha256': RENDERER,
            'source_archive': str(source_tar), 'source_archive_sha256': source_sha,
            'source_count': len(snapshot), 'object_count': len(objects),
            'metadata_sha256': metadata, 'official_files_sha256': official_files,
            'dependency_files': dependencies, 'runtime_archived_files': runtime_files,
            'formal_objects_sha256': objects, 'board_accessed': False, 'product_executed': False}


def execute(name, command):
    print('EXECUTING=' + name, flush=True)
    with (HERE / (name + '.log')).open('xb') as output:
        result = subprocess.run(command, cwd=HERE, stdout=output, stderr=subprocess.STDOUT,
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
    with (HERE / (name + '.exit')).open('x') as output:
        output.write(str(result.returncode) + '\n')
    print(name + '_EXIT=' + str(result.returncode), flush=True)
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--source-archive', type=Path)
    parser.add_argument('--source-archive-sha256')
    args = parser.parse_args()
    if not args.execute:
        print('07_DRIVER_PREPARED=1; EXECUTED=0; FINAL_SOURCE_ARCHIVE_REQUIRED=1; NETWORK_ACCESSED=0')
        return 0
    require(os.geteuid() != 0, '使用普通WSL用户交叉编译')
    require(args.source_archive is not None and args.source_archive_sha256 is not None
            and re.fullmatch(r'[a-f0-9]{64}', args.source_archive_sha256), '须先取得官方07源码包及实际SHA')
    for name in ('execution_07_inputs_before.json', 'execution_07_inputs_after.json', 'execution_07_result.json',
                 'freeze_07.log', 'freeze_07.exit', 'build_review_07.log', 'build_review_07.exit',
                 'checker_dependency_check.log', 'checker_dependency_check.exit', 'checker_dependencies'):
        require(not (HERE / name).exists(), '执行证据已存在，不能覆盖：' + name)
    before = audit_inputs(args.source_archive, args.source_archive_sha256)
    save('execution_07_inputs_before.json', before)
    codes = {'freeze': execute('freeze_07', [sys.executable, str(HERE / 'freeze_release_07.py'),
                                             str(INTEGRATION), RELEASE, str(args.source_archive)])}
    if codes['freeze'] == 0:
        codes['build_review'] = execute('build_review_07', ['bash', str(HERE / 'build_review_07.sh'), RELEASE])
    if codes.get('build_review') == 0:
        build = read(HERE / 'review_build_provenance.json')
        codes['checker_dependency_check'] = execute('checker_dependency_check', [
            sys.executable, str(HERE / 'frozen_07/sn100_host/shell/check_arm64.py'),
            str(HERE / 'status_login_review_07'), '--sysroot', build['sysroot'],
            '--output-dir', str(HERE / 'checker_dependencies')])
    after = audit_inputs(args.source_archive, args.source_archive_sha256)
    save('execution_07_inputs_after.json', after)
    unchanged = before == after
    success = unchanged and all(codes.get(name) == 0 for name in ('freeze', 'build_review', 'checker_dependency_check'))
    result = {'release_id': RELEASE, 'actual_step_exit_codes': codes,
              'complete_source_archive_dependencies_objects_unchanged': unchanged,
              'dependency_file_count': len(before['dependency_files']),
              'runtime_archived_file_count': len(before['runtime_archived_files']),
              'source_count': SOURCE_COUNT, 'object_count': MAIN_OBJECTS,
              'checker_built': success, 'board_accessed': False, 'product_executed': False,
              'n_function_verified': False, 'in_flight_n_close_verified': False}
    if success:
        binding = read(HERE / 'link_provenance.json')
        require(binding['source_count'] == SOURCE_COUNT and binding['object_count'] == MAIN_OBJECTS
                and binding['main_object_kept'] is True, '最终绑定未保留本批完整主对象/真实入口')
        result.update(checker_sha256=sha(HERE / 'status_login_review_07'),
                      checker_source_sha256=sha(HERE / 'status_login_review_07.cpp'),
                      observer_sha256=sha(HERE / 'ad_wire_observer.h'),
                      link_provenance_sha256=sha(HERE / 'link_provenance.json'),
                      link_record_sha256=binding['record_sha256'], main_object=binding['main_object'])
    save('execution_07_result.json', result)
    print(json.dumps(result, ensure_ascii=False), flush=True)
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
