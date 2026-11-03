#!/usr/bin/env python3
"""仅执行本次已获准的06冻结和检查器交叉编译，保存实际退出码及前后输入。

不上传、不连接板卡、不启动产品。任何阶段失败均保留现场，不覆盖后重试。
"""
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
RELEASE = '14ce9714aaa0017c7ad78ff3f551d978672e5def79b96e817605295d7e59020c'
APP = 'f3e814b4110b14ddbaef8c4516ab8a5b1c43747c45dcd4fff54ad3af0806ec6c'
REPORT = '0049f525076c24d4fd3c7a6415edb447431bf8f60e816708c1403d5c436942c0'
ARCHIVE = 'c1a21586e8b9a1adaaefd9ac1743f8b0f74c04a45f37001450de61b42402369b'
INTEGRATION = ROOT / 'temp/sn100_host/workflow_night_20261102/integration_06'
OFFICIAL = ROOT / '07_backups/sn100_host/board_b/workflow_integration_20261102/integration_06'
SOURCE_TAR = OFFICIAL / ('sn100_sources_' + RELEASE + '.tar.gz')


def require(value, message):
    if not value:
        raise RuntimeError(message)


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def save(name, value):
    with (HERE / name).open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write('\n')


def audit_inputs():
    complete = read(INTEGRATION / 'build_complete.json')
    require(complete['arm64_build_and_package_passed'] is True, '正式构建未完成')
    require(complete['source_count'] == 649 and complete['unique_build_inputs'] == 357,
            '最终批次输入数量与协调方提供值不同')
    require(complete['binaries'] == {'sn100_shell': APP, 'sn100_report_renderer': REPORT},
            'complete记录不属于指定的最终06程序')
    require(sha(INTEGRATION / 'build/sn100_shell') == APP
            and sha(INTEGRATION / 'build/sn100_report_renderer') == REPORT, '实际程序与complete不符')
    require(sha(SOURCE_TAR) == ARCHIVE, '官方源码包不是本次授权批次')
    snapshot = read(INTEGRATION / 'source_snapshot.json')
    require(len(snapshot) == 649 and sha(INTEGRATION / 'source_snapshot.json') == complete['source_snapshot_sha256'],
            '完整源码清单与complete不符')
    report_path = INTEGRATION / 'dependencies/dependency_report.json'
    require(sha(report_path) == complete['dependency_report_sha256'], '依赖报告与complete不符')
    dependency_report = read(report_path)
    require(dependency_report['offline_link_and_sql_closure_passed'] is True
            and not dependency_report['errors'], '正式依赖静态闭包未通过')
    require(dependency_report['binary'] == str(INTEGRATION / 'build/sn100_shell')
            and dependency_report['additional_binaries'] == [str(INTEGRATION / 'build/sn100_report_renderer')],
            '依赖报告的程序路径属于其他批次')
    sysroot = Path(dependency_report['sysroot'])
    dependencies = {}
    for item in dependency_report['files']:
        path = Path(item['path'])
        digest = sha(path)
        require(digest == item['sha256'], '依赖文件与正式报告指纹不符：' + str(path))
        dependencies[str(path)] = {'sha256': digest, 'resolved': str(path.resolve(strict=True))}
    runtime_files = {}
    for component in dependency_report['runtime_components'].values():
        for name in component.get('archived_files', []):
            path = Path(name)
            if not path.is_absolute():
                path = sysroot / path
            runtime_files[str(path)] = {'sha256': sha(path), 'resolved': str(path.resolve(strict=True))}
    official_files = {}
    for line in (OFFICIAL / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        require(Path(name).name == name and sha(OFFICIAL / name) == digest, '官方归档校验失败：' + name)
        official_files[name] = digest
    require(sha(INTEGRATION / 'build_complete.json') == official_files['build_complete.json']
            and sha(report_path) == official_files['dependency_report.json'], '正式构建与归档报告不一致')
    objects = {path.name: sha(path) for path in (INTEGRATION / 'arm64_build').glob('obj_*.o')}
    require(set(objects) == {'obj_' + str(i) + '.o' for i in range(267)}, '本轮必须是连续267个真实main对象')
    metadata = {name: sha(INTEGRATION / name) for name in
                ('build_exit.txt', 'build_complete.json', 'build_inputs.tsv', 'source_snapshot.json',
                 'input_verification.json', 'merge_inputs.json')}
    metadata['official_manifest'] = sha(OFFICIAL / 'SHA256SUMS')
    return {'release_id': RELEASE, 'application_sha256': APP, 'report_renderer_sha256': REPORT,
            'source_archive_sha256': ARCHIVE, 'source_count': len(snapshot), 'object_count': len(objects),
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
    require(os.geteuid() != 0, '仅用普通WSL用户交叉编译')
    for name in ('execution_06_inputs_before.json', 'execution_06_inputs_after.json', 'execution_06_result.json',
                 'freeze_06.log', 'freeze_06.exit', 'build_review_06.log', 'build_review_06.exit'):
        require(not (HERE / name).exists(), '本批执行证据已存在，不得覆盖：' + name)
    before = audit_inputs()
    save('execution_06_inputs_before.json', before)
    codes = {}
    codes['freeze'] = execute('freeze_06', [sys.executable, str(HERE / 'freeze_release_06.py'),
                                          str(INTEGRATION), RELEASE, str(SOURCE_TAR)])
    if codes['freeze'] == 0:
        codes['build_review'] = execute('build_review_06', ['bash', str(HERE / 'build_review_06.sh'), RELEASE])
    after = audit_inputs()
    save('execution_06_inputs_after.json', after)
    unchanged = before == after
    success = unchanged and codes.get('freeze') == codes.get('build_review') == 0
    result = {'release_id': RELEASE, 'actual_step_exit_codes': codes,
              'complete_source_archive_dependencies_objects_unchanged': unchanged,
              'dependency_file_count': len(before['dependency_files']),
              'runtime_archived_file_count': len(before['runtime_archived_files']),
              'source_count': before['source_count'], 'object_count': before['object_count'],
              'checker_built': success, 'board_accessed': False, 'product_executed': False}
    if success:
        binding = read(HERE / 'link_provenance.json')
        require(binding['object_count'] == 267 and binding['source_count'] == 649
                and binding['main_object_kept'] is True, '实际绑定未保留最终批次完整main对象')
        result.update(checker_sha256=sha(HERE / 'status_login_review_06'),
                      checker_source_sha256=sha(HERE / 'status_login_review_06.cpp'),
                      observer_sha256=sha(HERE / 'ad_wire_observer.h'),
                      link_provenance_sha256=sha(HERE / 'link_provenance.json'),
                      link_record_sha256=binding['record_sha256'], main_object=binding['main_object'])
    save('execution_06_result.json', result)
    print(json.dumps(result, ensure_ascii=False), flush=True)
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
