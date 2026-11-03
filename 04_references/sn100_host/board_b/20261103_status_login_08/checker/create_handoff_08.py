"""编译真正结束后整理交接值；不上传、不运行产品、不连接板卡。"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def read(name):
    return json.loads((HERE / name).read_text())


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def write(name, content):
    with (HERE / name).open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(content)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--actual-driver-exit', type=int, required=True)
args = parser.parse_args()
require(args.actual_driver_exit == 0, '真实driver退出非0，不能交付成功材料')
execution = read('execution_08_result.json')
require(execution['checker_built'] is True and execution['actual_step_exit_codes'] ==
        {'freeze': 0, 'build_review': 0, 'checker_dependency_check': 0}, '三个实际步骤未全部成功')
require(sha(HERE / 'execution_08_inputs_before.json') == sha(HERE / 'execution_08_inputs_after.json')
        and execution['complete_source_archive_dependencies_objects_unchanged'] is True,
        '前后输入不一致')
prepared, link = read('prepared_inputs.json'), read('link_provenance.json')
before, owner = read('execution_08_inputs_before.json'), read('official_object_binding.json')
contract, dependency = read('shutdown_contract.json'), read('checker_dependencies/dependency_report.json')
scope = read('observation_scope_08.json')
release = execution['release_id']
require(release == prepared['release_id'] == link['release_id'] == contract['release_id'] == scope['release_id'],
        '本轮发布身份不一致')
checker = HERE / 'status_login_review_08'
require(sha(checker) == execution['checker_sha256'], '检查器改变')
require(sha(HERE / 'status_login_review_08.cpp') ==
        '6ab71d0de6009968372da7dcc2b30926e6481fc673745d36b0737ad32f8d8dbc', '检查源码不是冻结候选')
require(dependency['offline_link_and_sql_closure_passed'] is True and not dependency['errors'], '检查器依赖未通过')
require(scope['new_construction_proven'] is False and scope['same_main_memory_bool_read'] is False,
        '急诊观察范围扩大')

names = ['status_login_review_08', 'review_release_id', 'run_case_on_board.sh', 'capture_application_exit.sh',
         'shutdown_contract.json', 'required_shutdown_markers.txt', 'forbidden_shutdown_markers.txt',
         'shutdown_contract_release_id']
manifest = ''.join(sha(HERE / name) + '  ' + name + '\n' for name in names)
write('upload_manifest_08.sha256', manifest)
write('execution_08_driver_exit.txt', str(args.actual_driver_exit) + '\n')

record = {
    'schema': 'sn100-final-08-checker-handoff-v1', 'release_id': release,
    'status': 'checker_arm64_link_and_static_dependencies_passed_RK_pending',
    'formal_application_sha256': prepared['formal_identity']['application_sha256'],
    'source_archive': prepared['source_archive'], 'source_archive_sha256': prepared['source_archive_sha256'],
    'source_count': link['source_count'], 'object_count': link['object_count'],
    'unique_build_inputs': owner['unique_build_inputs'],
    'main_object_kinds': dict(Counter(item['kind'] for item in link['objects'])),
    'helper_object_count': owner['counts']['helper_objects'],
    'all_aarch64_object_count': owner['counts']['all_objects'],
    'complete_arm64_build_file_count': len(before['complete_arm64_build_files_sha256']),
    'generated_file_count': len(link['generated_sha256']), 'main_object': link['main_object'],
    'formal_snapshot_sha256': prepared['formal_identity']['formal_snapshot_sha256'],
    'frozen_snapshot_sha256': prepared['frozen_snapshot_sha256'],
    'checker_sha256': sha(checker), 'checker_bytes': checker.stat().st_size,
    'checker_source_sha256': sha(HERE / 'status_login_review_08.cpp'),
    'observer_sha256': sha(HERE / 'ad_wire_observer.h'),
    'actual_driver_exit_code': args.actual_driver_exit,
    'actual_step_exit_codes': execution['actual_step_exit_codes'],
    'before_after_identical': True, 'before_after_sha256': sha(HERE / 'execution_08_inputs_before.json'),
    'contract_sha256': sha(HERE / 'shutdown_contract.json'),
    'required_marker_count': len(contract['required_markers']),
    'required_markers': contract['required_markers'],
    'n_source_guard': contract['n_shutdown_guard'], 'task_source_guard': contract['task_shutdown_guard'],
    'observation_scope': scope, 'actual_runtime_check_count': None,
    'upload_file_count': len(names), 'upload_manifest_sha256': sha(HERE / 'upload_manifest_08.sha256'),
    'upload_files_sha256': {name: sha(HERE / name) for name in names},
    'dependency_file_count': len(dependency['files']), 'link_record_sha256': link['record_sha256'],
    'formal_dependency_file_count': len(before['dependency_files']),
    'runtime_archived_file_count': len(before['runtime_archived_files']),
    'record_files_sha256': {name: sha(HERE / name) for name in [
        'execute_final_08_build.py', 'execution_08_result.json', 'link_provenance.json',
        'review_build_provenance.json', 'prepared_inputs.json', 'source_snapshot_08.json',
        'official_qa_handoff.json', 'official_object_binding.json', 'observation_scope_08.json',
        'initial_snapshot_provenance_review.md', 'checker_dependencies/dependency_report.json']},
    'board_accessed': False, 'product_executed': False, 'current_switched': False,
    'n_function_verified': False, 'in_flight_n_close_verified': False, 'task_business_verified': False,
    'in_flight_task_single_outer_close_verified': False, 'new_construction_proven': False,
    'same_main_memory_bool_read': False, 'administrator_password_verified': False,
    'physical_input_or_screen_verified': False,
    'binding_method': '按冻结build.sh和实际build_inputs重建并对照正式owner对象表；不是编译器trace或完整头文件依赖图。',
}
write('final_08_handoff.json', json.dumps(record, ensure_ascii=False, indent=2) + '\n')
text = f'''# 第08轮检查器编译交接

正式发布`{release}`的同版ARM64检查器已完成交叉编译和静态依赖核对。三个实际步骤退出均为0，driver真实退出为0。没有上传、连接板卡、切换current或运行产品。

完整身份和上传的8项文件见`final_08_handoff.json`、`upload_manifest_08.sha256`。本轮实际冻结{link['source_count']}份源码，链接{link['object_count']}个正式主程序对象，核对{len(link['generated_sha256'])}个生成文件，保留真实main对象；正式构建目录440项及依赖前后指纹一致。对象来源按固定脚本和输入顺序重建，不声称是编译器trace或完整头文件依赖图。

检查器SHA-256：`{sha(checker)}`。检查器cpp仍为已审6ab候选原字节；内联AD观察头仍为原c198字节。关闭合同按同批源码和实际runner生成，实际{len(contract['required_markers'])}项；包括N空闲关门就绪和真实Task关门就绪，不把它们当成业务通过。

后续由总协调串行核对板B身份、实际配置及准备结果，再切换并仅运行一次既定自动专项。主动发送仍限原Qj，不输入真实管理员口令、不制造Task/N、不改INI或构造场景。实际基础检查与可选急诊项数量从运行数组取，现在不填写24或25的实际总数。

原输出即使含“初建”也保留原文。派生报告固定`new_construction_proven=false`、`same_main_memory_bool_read=false`；比较通过最多说明当前急诊初始视图与当前未变化INI配置一致。创建来源、另一布尔值分支、手选保存重开、Task/N真实业务及在途关闭、实体输入/屏幕和测试绿灯仍待对应验证。
'''
write('final_08_build_handoff.md', text)
print(json.dumps({key: record[key] for key in ['release_id', 'checker_sha256', 'source_count', 'object_count',
      'generated_file_count', 'required_marker_count', 'upload_manifest_sha256']}, ensure_ascii=False))
