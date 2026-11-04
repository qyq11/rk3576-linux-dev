"""把本轮明确白名单证据保存到新固定目录，逐份读回；不改旧归档或操作板卡。"""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
LIVE = HERE / 'runtime_batch10_01'
CALLS = ROOT / 'temp/sn100_host/root_manager_20261103'
DEST = ROOT / '04_references/sn100_host/board_b/20261103_integration_10_keep_running'
required_review = HERE / 'integration_10_live_evidence_independent_review_01.md'
assert required_review.is_file()
assert (CALLS / 'post_live_batch10_01.exit').read_text().strip() == '0'
assert (LIVE / 'parser.actual_exit').read_text().strip() == '0'
review = json.loads((LIVE / 'parser.stdout.json').read_text())
assert review['software_result'] == 'pass' and review['application_exit'] is None
assert review['details']['check_count'] == 1 and review['details']['failed'] == 0
files = {}


def add(source, relative):
    assert source.is_file() and not source.is_symlink() and relative not in files
    files[relative] = source


for source in sorted(LIVE.rglob('*')):
    if source.is_file():
        add(source, 'observation/' + source.relative_to(LIVE).as_posix())
for stem in ('prebuild_live_20261103_01', 'pre_stage_live_20261103_01',
             'install_runtime_metadata_batch10_01', 'prepare_flat_batch10_01',
             'diagnose_prepare_flat_batch10_01', 'diagnose_proc_identity_batch10_01',
             'diagnose_proc_identity_batch10_02', 'prepare_flat_batch10_02',
             'preflight_new_batch10_02', 'stop_old_batch10_02', 'activate_new_batch10_01',
             'launch_new_batch10_02', 'wait_lis_batch10_02', 'post_live_batch10_01',
             'communication_keep_running_batch10_01'):
    for suffix in ('.sh', '.log', '.exit', '.started'):
        add(CALLS / (stem + suffix), 'commands/' + stem + suffix)
add(CALLS / 'board08.py', 'commands/board08.py')
for name in ('keep_running_launch_review_01.md', 'runtime_prepare_independent_review_01.md',
             'runtime_prepare_independent_review_02.md', 'runtime_kernel_classification_review_01.md',
             'runtime_control_scripts_independent_review_01.md', required_review.name):
    add(HERE / name, 'reviews/' + name)
for name in ('prepare_runtime_batch10_01.py', 'prepare_runtime_batch10_02.py',
             'fix_runtime_process_classification_01.py', 'make_runtime_controls_01.py',
             'make_runtime_controls_02.py', 'make_activate_batch10_01.py',
             'make_proc_identity_diagnostic_01.py', 'make_post_live_batch10_01.py',
             'collect_runtime_lis_batch10_01.py', 'collect_runtime_proofs_batch10_01.py',
             'archive_runtime_batch10_01.py', 'run_file_phases_01.py'):
    add(HERE / name, 'root_tools/' + name)
for stem in ('preflight_new_batch10_01', 'stop_old_batch10_01', 'launch_new_batch10_01', 'wait_lis_batch10_01'):
    add(CALLS / (stem + '.sh'), 'unused_controls_01/' + stem + '.sh')
for source in sorted((HERE / 'file_phase_run_01').iterdir()):
    add(source, 'file_phase_outer/' + source.name)
for name in ('wait_lis_observation.sh', 'review_lis_observation.py'):
    add(ROOT / '05_tools/tests/sn100_host/lis_keep_running_20261103' / name, 'tools/' + name)

DEST.mkdir()
hashes = {}
for relative, source in files.items():
    raw = source.read_bytes()
    target = DEST / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('xb') as stream:
        stream.write(raw)
    assert target.read_bytes() == raw
    hashes[relative] = hashlib.sha256(raw).hexdigest()

summary = {
    'format': 'sn100-integration-10-keep-running-summary-v1',
    'board': 'board_b', 'serial': '8314b1e8ffd0321f',
    'boot_id': '0eb94389-f120-42ad-9042-9c21d96a1481',
    'release_id': 'c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e',
    'application_pid': 669382, 'application_start_ticks': '2102645',
    'application_sha256': '50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258',
    'source_files': 687, 'build_inputs': 395, 'main_objects': 291, 'helper_objects': 2,
    'build_and_file_phases_actual_exit': 0, 'waiter_actual_exit': 0, 'parser_actual_exit': 0,
    'software_checks': 1, 'software_failed': 0, 'duration_ms': 15002, 'startup_wait_ms': 6344,
    'application_still_running_at': '2026-11-03T22:29:50+08:00',
    'main_original_controller_tcp_established': True,
    'application_exit': None, 'shutdown_evaluated': False,
    'lis_business_result': 'pending', 'ascii_manual_operation_verified': False,
    'physical_screen_verified': False, 'instrument_verified': False, 'resource_budget_verified': False,
    'original_controller_pid': 5008, 'original_controller_start_ticks': '118180',
    'unchanged_configurations': 7, 'unchanged_data_pointers': 3,
    'camera_and_3a_kept_running': True,
    'historical_prepare_remote_exit': 79, 'fixed_prepare_remote_exit': 0,
    'source_scope': 'six_accepted_patches_excludes_mg_and_five_lis_field_candidates',
}
readme = '''# 第10批：板B被动观察后保留程序运行

本批发布为`c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e`。完整ARM64交叉编译、依赖、打包及暂存/归档/采集/收口真实退出均0；687份源、395个不同构建输入、291个主对象和2个报告辅助对象逐份绑定。文件阶段原件另见[238份固定证据](../../workflow_next_20261103/integration_next_02/README.md)；[正式程序、源码恢复包](../../../../07_backups/sn100_host/board_b/workflow_integration_20261103/integration_next_02/README.md)保持原样。

板B已用原配置、原资料、原主控启动这个版本。新进程PID669382、启动计数2102645，22:18:20启动；被动观察等待就绪6344毫秒、观察15002毫秒，顶部LIS控件及配置来源1项通过、0失败，等待器/采集/独立解析真实退出0。22:20:42后验同一进程已就绪且仍运行，没有新应用exit_code或result。应用退出保持null，未评价关闭，复核结束没有关闭程序。这只证明本段软件与活进程，不代替人工实屏或长稳。

当前LIS仍为原127.0.0.1:3800客户端，连接未建立，自动获取/发送/模拟均关闭；真实下载、上传和业务确认待验。旧26项的25通过/1失败原件保留，本轮只观察受影响1项。通讯测试ASCII显示/输入和六种确定正文、校准空批号、普通开始的无写准备、LIS消息来源已编译进新包；本轮没有真人点击发送、登录、机械动作或真实Task/N业务，不把启动观察当作这些功能验收。未重发Qk、未运行Windows/WSL产品、脱机模拟或假报文。

旧b31程序仅为载入新版本正常关闭一次，真实旧退出0、干净关闭1，凭据原件SHA为`4619885a1c8f3d5b99b034c7168ce7021a7aaefb4a154fc9ad0475a4912ebcb0`。新程序直接使用既有supervise的0时限、空session和显式三观察变量，避开旧start的30秒就绪超时关闭分支；它仍有监督异常/外部信号路径，不能声称永不退出。观察等待超时只退出工具，不发关闭信号或重开第二程序。

运行准备首轮远端退出79，原脚本、远端退出及只读诊断保留。原检查要求内核线程两次调度状态相同，实际sugov:4出现D→S、flags与启动计数未变；仅在新运行脚本要求两次PF_KTHREAD位和启动身份稳定，其他活用户exe不可读仍拒绝，新准备退出0。没有改已固定的文件阶段九工具、配置或旧模板，没有因此重编产品。01等待器权限源审问题在执行前另以02生成器修为root:lckfb750；新目录和原报告均保留。旧preflight的cwd提示及等待器两条cp-n工具警告也保留，均未改变实际退出0。

原主控PID5008/启动计数118180、5000监听、七份INI/CFG及三个资料指针前后保持；相机服务PID455358、8080与3A PID139148保持。相机窗口协调期间不启停/调参；按该窗口原720p/10帧声明登记背景，未借此证明本轮分辨率实测。资源副本146行含1行表头，仅是145个启动阶段样本，不证明响应、资源预算或同板实时控制通过。

22:29:50另做只读连接核对，新上位机669382与原主控5008在本板5000端口的两端TCP均为ESTAB，同一应用继续运行、七配置/三指针仍匹配。未发送查询或重启主控。命令行沿既有统一入口保留`--offline`名称，同时实际指定`--original-db-root`及`--instrument-control`；未用匿名资料、模拟会话或脱机用例，实际启动参数原文随证据保存。

`observation/capture_01/`保留已封口六文件及清单，`observation/remote_proofs/`是额外九个板端原件；`commands/`保存真实脚本、输出和退出；`reviews/`保留独审及首次问题；`root_tools/`为总控生成/收集原件，`unused_controls_01/`未执行，不能当运行结果。文件阶段外层退出也独立保存在`file_phase_outer/`。这些原件用于追溯，不能在当前版本上直接重跑切换脚本。

单孔Mg04及五份LIS纯字段均未纳本批，真正用户写入、急诊立即下发、真人操作、真实仪器和资源验收继续待做。没有新提交、推送或中间文件清理。
'''
for name, raw in (('summary.json', (json.dumps(summary, ensure_ascii=False, indent=2) + '\n').encode('utf-8')),
                  ('README.md', readme.encode('utf-8'))):
    with (DEST / name).open('xb') as stream:
        stream.write(raw)
    hashes[name] = hashlib.sha256(raw).hexdigest()
with (DEST / 'SHA256SUMS').open('x', encoding='ascii', newline='\n') as stream:
    for name, digest in sorted(hashes.items()):
        stream.write(digest + '  ' + name + '\n')
assert set(hashes) == {p.relative_to(DEST).as_posix() for p in DEST.rglob('*') if p.is_file() and p.name != 'SHA256SUMS'}
for relative, digest in hashes.items():
    assert hashlib.sha256((DEST / relative).read_bytes()).hexdigest() == digest
print('LIVE_FIXED_FILES=' + str(len(hashes)))
print('LIVE_EVIDENCE=' + DEST.relative_to(ROOT).as_posix())
print('ALL_FIXED_BYTES_READBACK_MATCHED=1; BOARD_OPERATED=0')
