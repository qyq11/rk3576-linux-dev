"""只在获准的新04/07目录保存有限证据；不联网、不运行产品、不覆盖旧指针原件。"""
import hashlib
import json
import shutil
import tarfile
from pathlib import Path

live = Path(__file__).resolve().parent
workflow = live.parent
root = workflow.parents[2]
run = live / 'auto_01'
prep = root / 'temp/sn100_host/board_b/20261102_status_login/integration_05_review_preparation'
evidence = root / '04_references/sn100_host/board_b/20261102_status_login_05'
backup = root / '07_backups/sn100_host/board_b/status_login_05_20261102'
release = 'e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270'
expected_pointer = '4bc683291814fea6f56fcd7358f992187f1186a991dd11572fbc266f40c2bfef'
expected_probe = '8b48f0cf698cfaf8f1aeee40323e32ccc9201e2650072b217a25c34e1b1f1f05'
expected_source = '73d54a7802463d41c820dfbfd914612e2e78070b9f237ced60a2ec7bf8c8f11d'
source_package = root / ('07_backups/sn100_host/board_b/workflow_integration_20261102/integration_05/sn100_sources_' + release + '.tar.gz')
expected_package = 'fada1890e24e6d390af1e8c7216a18bde16b0c98619e76e9d9a67c8ab79d352c'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
relative = lambda p: p.relative_to(root).as_posix()

if evidence.exists():
    raise RuntimeError('新证据目录已存在，停止而不是覆盖')
if sha(backup / 'current_before.json') != expected_pointer:
    raise RuntimeError('原current恢复记录不符，禁止覆盖')
if sha(prep / 'status_login_review_05') != expected_probe or sha(prep / 'status_login_review_05.cpp') != expected_source:
    raise RuntimeError('检查器不属于本次最终版本')
if sha(source_package) != expected_package:
    raise RuntimeError('05源码包指纹不符')
snapshot = json.loads((prep / 'source_snapshot_05.json').read_text())
with tarfile.open(source_package) as bundle:
    actual_sources = {}
    for member in bundle.getmembers():
        if not member.isfile() or not member.name.startswith('06_programs/sn100_host/'):
            raise RuntimeError('源码包出现非预期条目')
        name = member.name.removeprefix('06_programs/sn100_host/')
        if name in actual_sources:
            raise RuntimeError('源码包出现重名条目')
        actual_sources[name] = hashlib.sha256(bundle.extractfile(member).read()).hexdigest()
    if actual_sources != snapshot or len(actual_sources) != 639:
        raise RuntimeError('源码包与639输入不一致')

remote_entries = {}
for line in (run / 'remote_capture.sha256').read_text().splitlines():
    digest, name = line.split('  ', 1)
    name = name.removeprefix('./')
    path = Path(name)
    if path.is_absolute() or '..' in path.parts or name in remote_entries:
        raise RuntimeError('原远端清单路径不安全')
    if sha(run / path) != digest:
        raise RuntimeError('远端41项与电脑副本不符：' + name)
    remote_entries[name] = digest
if len(remote_entries) != 41:
    raise RuntimeError('远端原清单不是41项')

evidence.mkdir(parents=True)
copied = []
def copy(source, target):
    if not source.is_file() or source.is_symlink() or target.exists():
        raise RuntimeError('复制对象缺失、为符号链接或将覆盖：' + str(source))
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    if sha(source) != sha(target):
        raise RuntimeError('复制内容不一致')
    copied.append({'source': relative(source), 'target': relative(target), 'sha256': sha(target)})

archived_remote = []
for source in sorted(run.rglob('*')):
    if not source.is_file():
        continue
    name = source.relative_to(run).as_posix()
    if name.startswith('runtime/cache/'):
        continue
    copy(source, evidence / 'auto_01' / name)
    if name in remote_entries:
        archived_remote.append(name)

for pattern in ('upload*', 'activation*'):
    for source in sorted(live.glob(pattern)):
        if source.is_file():
            copy(source, evidence / 'steps' / source.name)
for name in ('run_live_05_auto_01.sh', 'run_live_05_auto_01.log', 'run_live_05_auto_01.exit',
             'live_05_postcheck.sh', 'live_05_postcheck.log', 'live_05_postcheck.exit',
             'run_board_b_script.py', 'prepare_board_b_live_05.py'):
    copy(workflow / name, evidence / 'steps' / name)
copy(live / 'archive_inputs_review.md', evidence / 'archive_inputs_review.md')
copy(Path(__file__).resolve(), evidence / 'steps/archive_status_login_05.py')

checker_names = (
    'status_login_review_05.cpp', 'run_case_on_board.sh', 'capture_application_exit.sh',
    'summarize_run.py', 'build_review_05.sh', 'bind_build.py', 'review_release_id',
    'prepared_inputs.json', 'preparation_checks.json', 'source_snapshot_05.json',
    'link_provenance.json', 'build_execution.json', 'final_execution_inputs.json',
    'expected_release_05.json', 'manual_checklist.md', 'ui_review_boundaries.md',
    'checker_scope_audit.md', 'build_binding_review.md', 'window_close_final_review.md',
    'exit_result_review.md',
)
for name in checker_names:
    copy(prep / name, evidence / 'checker' / name)

bundle_path = backup / ('status_login_review_05_' + expected_probe + '.tar.gz')
if bundle_path.exists():
    raise RuntimeError('新的检查器包已存在，不覆盖')
bundle_entries = {name: sha(prep / name) for name in checker_names}
bundle_entries['status_login_review_05'] = expected_probe
with tarfile.open(bundle_path, 'w:gz') as bundle:
    for name in sorted(bundle_entries):
        bundle.add(prep / name, arcname='checker/' + name, recursive=False)
with tarfile.open(bundle_path) as bundle:
    checked = {}
    for member in bundle.getmembers():
        if not member.isfile() or not member.name.startswith('checker/'):
            raise RuntimeError('新检查器包出现非预期条目')
        name = member.name.removeprefix('checker/')
        if name in checked:
            raise RuntimeError('新检查器包出现重名条目')
        checked[name] = hashlib.sha256(bundle.extractfile(member).read()).hexdigest()
    if checked != bundle_entries:
        raise RuntimeError('新检查器包逐文件回读不一致')
(backup / 'checker_bundle_manifest.json').write_text(json.dumps({
    'archive': bundle_path.name, 'sha256': sha(bundle_path), 'files': bundle_entries,
    'readback_verified_files': len(bundle_entries), 'source_639_archive': relative(source_package),
    'source_639_archive_sha256': expected_package,
}, indent=2) + '\n')

provenance = {
    'record_date': '2026-11-03 Asia/Shanghai', 'board': 'board_b', 'serial': '8314b1e8ffd0321f',
    'release_id': release, 'application_sha256': '609aa4b9a60109df4df241181e4622626ab9b77fba2b272ca0643a5bcf5506dc',
    'checker_source_sha256': expected_source, 'checker_binary_sha256': expected_probe,
    'source_snapshot_files': 639, 'formal_objects': 261, 'formal_main_kept': True,
    'build_execution_record': relative(evidence / 'checker/build_execution.json'),
    'source_639_archive': relative(source_package), 'source_639_archive_sha256': expected_package,
    'source_archive_readback_verified_files': len(actual_sources),
    'checker_archive': relative(bundle_path), 'checker_archive_sha256': sha(bundle_path),
    'checker_archive_readback_verified_files': len(bundle_entries),
    'current_before_sha256': expected_pointer, 'current_before_bytes_preserved': True,
    'remote_manifest': 'auto_01/remote_capture.sha256',
    'remote_manifest_sha256': sha(run / 'remote_capture.sha256'),
    'remote_files_verified_on_pc': 41, 'remote_files_selected_for_archive': len(archived_remote),
    'selected_remote_paths': archived_remote,
    'excluded_remote_paths': sorted(set(remote_entries) - set(archived_remote)),
    'exclusion_reason': '只排除runtime/cache中的GPU和字体缓存，原远端41项清单原样保留；全部41项先与电脑副本核对。',
    'pc_created_result_files': ['auto_01/parsed_summary.json', 'auto_01/independent_result_review.md',
                                'auto_01/independent_wire_review.md', 'auto_01/independent_exit_scope_review.md'],
    'software_scope': '原配置联机自动专项20项0失败；程序内控件事件与控件渲染，不代替实体键鼠/实屏。',
    'check_exit': 0, 'application_wait_exit': 0, 'wrapper_exit': 0, 'case_exit': 0,
    'tx_queued_frames': 7, 'tx_messages': ['Qj'], 'rx_frames': {'Qj': 10, 'Qq': 7},
    'required_markers': {'total': 15, 'startup_ready': 1, 'close_and_threads': 14},
    'ad_values': '7个Qq均声明25组但实际26组，业务校验拒绝，未通过AD数值接入验收。',
    'original_ini_hashes_unchanged': True, 'same_controller_alive': True, 'controller_pid': 79082,
    'postcheck_at': '2026-11-03T00:05:00+08:00', 'host_and_checker_process_count_at_postcheck': 0,
    'resource_scope': '4个含检查器的上位机短窗样本，首尾9.22秒，不是资源预算、同板控制或长稳验收。',
    'resource_values': {'rss_kib_peak': 448656, 'pss_kib_peak': 446094,
                        'cpu_percent_one_core_interval_arithmetic_mean': 101.26992192538358,
                        'event_loop_interval_p95_ms': 220, 'event_loop_interval_max_ms': 507,
                        'programmatic_action_ms': [448, 505, 896]},
    'pc_parse_history': '总协调报告默认GBK解析失败，显式-X utf8解析同一采集成功；没有重跑产品。',
    'pending': ['真实账户密码及各等级人工菜单', '实体键鼠及实屏完整明暗周期',
                '真实红色退出与重开', '实际测试绿灯', '完整仪器数值与AD字段对应',
                '42/48试剂位及PT800/405nm差异', '正式响应/资源预算/同板控制/长稳'],
    'restore_scope': '仅current原目标/属主及同版检查器；无配置内容、业务库、账户、凭据或整机环境备份。',
    'copied_inputs': copied,
}
if sha(backup / 'current_before.json') != expected_pointer:
    raise RuntimeError('归档过程中原current记录变化')
for target in (evidence / 'provenance.json', backup / 'provenance.json'):
    target.write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'evidence_files_copied': len(copied), 'remote_verified': 41,
                  'remote_archived': len(archived_remote), 'remote_excluded': len(remote_entries)-len(archived_remote),
                  'source_archive_verified': len(actual_sources), 'checker_bundle_verified': len(bundle_entries),
                  'bundle': relative(bundle_path), 'bundle_sha256': sha(bundle_path), 'current_before_unchanged': True}))
