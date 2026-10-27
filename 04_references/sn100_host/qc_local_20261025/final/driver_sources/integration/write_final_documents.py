"""Reconcile existing documentation only after the concrete final gates exist."""
import argparse
import hashlib
import json
from pathlib import Path
import re

if not __debug__:
    raise RuntimeError('Document verification requires Python assertions enabled')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root-go', action='store_true', required=True)
args = parser.parse_args()
assert args.root_go
work = Path(__file__).resolve().parent.parent
root = work.parents[2]
formal = root / '04_references/sn100_host/qc_local_20261025'
final = formal / 'final'
backup = root / '07_backups/sn100_host/offline_four_windows_20261025'
read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
v = read(final / 'verification.json')
response = read(final / 'response/verification.json')
resource = read(final / 'resource/verification.json')
recovery = read(backup / 'verification.json')
git_attributes = read(formal / 'recovery_git_attributes.json')
assert git_attributes['all_archive_attributes_lfs'] and git_attributes['none_ignored']
assert git_attributes['attributes_exit'] == 0 and git_attributes['check_ignore_exit'] == 1
assert v['passed'] and v['source_inputs'] == 426 and v['suite_invocations'] == 35
assert v['source_manifest_sha256'] == '5123bf1442472450bbde23a59fc47287dc85ead34e006c523a96a016ac76cf29'
assert v['additional_project_reagent_test_invocations'] == 4
assert v['additional_project_reagent_standalone_invocations'] == 2
assert all(t[1:] == [0, 0] for t in v['qt_totals_by_invocation'].values())
assert response['passed'] and response['actual_exit'] == 0 and response['sample_count'] == 40
assert all(resource['checks'].values()) and resource['actual_child_exit_code'] == 0
assert recovery['release_id'] == v['release_id'] and (backup / 'README.md').is_file()
for line in (final / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert sha(final / name) == digest, name

index = '04_references/sn100_host/qc_local_20261025/final/verification.json'
summary = ('质控、校准、项目高级方案/试剂批组与报告剩余本地业务已接统一程序；'
           '当前426项输入（冻结`5123bf14`）的完整35次离线调用、额外4组项目/试剂测试和4个独立界面启动/正常退出均通过。'
           'WSL实际运行、Windows32/ARM64主程序与报告辅助程序全量编译、ARM离线依赖及匹配应用/源码归档通过；'
           '单次40保存响应与30秒整程序资源基线已留证。')
limits = ('原Windows完整行为/活动主题、RK实屏/输入、仪器/实际LIS/实体打印、真实结果链、临床校准/质控、'
          '认可资源预算/同板实时控制及长稳仍待验；语言设置仍占位，原版未启用的XLSX未开放。'
          '旧报告查询、8小时恢复，以及suite05校准引用崩溃和suite07登录1920几何失败的直接原因尚未确证，原失败保留。')
edited = {}
originals = {}

def doc(name):
    if name not in edited:
        path = root / name
        originals[name] = path.read_bytes()
        edited[name] = originals[name].decode('utf-8')
    return edited[name]

def line(name, prefix, replacement, top_only=False):
    body = doc(name)
    rows = body.splitlines()
    matches = [i for i, row in enumerate(rows) if row.startswith(prefix) and (not top_only or i < 20)]
    assert len(matches) == 1, (name, prefix, matches)
    rows[matches[0]] = replacement
    edited[name] = '\n'.join(rows) + '\n'

line('README.md', '**当前做到：**', '**当前做到：**' + summary + '[本轮总索引](' + index + ')。' + limits)
line('README.md', '本批O10-2 LIS', '此前371项输入的LIS检查点及旧361/353/348结果各自保留。用户2026-10-25晚授权四个现有聊天分担，本轮已完成相应可执行离线门槛；10分钟检查在本轮收口后暂停。[分工与后续](01_docs/plans/sn100_host_migration_plan.md#offline-four-windows-20261025)')
line('README.md', '本轮项目并发改名缺口', '本轮项目并发改名保护修前5通过/2失败、修后两轮各7通过/0失败；Windows编译暴露的报告/校准显式头文件问题已修并在当前快照重编验证。`e65d7730`、`57f3b032`及各失败轮次只作历史证据。[操作说明](06_programs/sn100_host/README.md#offline-four-window-usage) · [匹配恢复包](07_backups/sn100_host/offline_four_windows_20261025/README.md)')
line('06_programs/sn100_host/README.md', '**当前统一脱机入口', '**当前统一脱机入口已包含质控和校准本地页面。**' + summary + '[本轮总索引](../../' + index + ')。' + limits)
line('06_programs/sn100_host/README.md', '**统一界面使用', '**统一界面使用`shell/rk.sh prepare`构建、离线依赖检查和冻结打包；**显式deploy/check/start/status/stop/collect用于后续上机。根`rk.sh sn100_host all`仍为独立页，`shell/wsl.sh`为WSL界面入口。本轮426项输入的[匹配ARM应用/源码](../../07_backups/sn100_host/offline_four_windows_20261025/README.md)已归档；此前371等包保留，本轮没有连接RK或仪器。[首验卡](../../01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)。')
line('01_docs/knowledge/sn100_host_project_guide.md', 'O10-1打印子页3', 'O10-1打印子页3和O10-2 LIS子页4已接本地设置，`ui/frmlissetting.cpp`→`data/lis_settings_store.cpp`、`shell/lis_shell.cpp`仅保存意图，`data/serial_port_catalog.cpp`只读枚举。主菜单3校准、4质控已接各自本地页面及worker；语言2仍占位。' + summary + '[当前索引](../../' + index + ')。' + limits + '用法见[操作说明](../../06_programs/sn100_host/README.md#offline-four-window-usage)，全局仍沿第13节F01–F18唯一清单核差异。')
for name in ('01_docs/plans/next_steps.md', '01_docs/plans/development_plan.md', '01_docs/plans/sn100_host_migration_plan.md'):
    edited[name] = doc(name).replace('更新：2026-10-25。', '更新：2026-10-26。', 1)
    line(name, '**用户最新安排', '**用户最新安排（2026-10-25晚，本轮2026-10-26收口）：四个现有聊天已分担交付，本聊天完成公共整合与串行验证；本轮可执行离线门槛已有实际收据，10分钟检查在收口后暂停。RK尚不可用，保留实物、临床与完整原版对照边界。[四窗口卡](sn100_host_migration_plan.md#offline-four-windows-20261025)。**')
line('01_docs/plans/next_steps.md', '**现在做到：**', '**现在做到：**' + summary + '[当前交付](../../' + index + ')。' + limits)
line('01_docs/plans/development_plan.md', '主线仍是：', '主线仍是：PREEMPT_RT基础→业务需求与接口边界→调度/算法/Qt最小验证→按需接口与系统联调→环境定版。' + summary + '[本轮结果](../../' + index + ')。' + limits)
line('01_docs/plans/sn100_host_migration_plan.md', '**当前：**', '**当前：**' + summary + '[本轮索引](../../' + index + ')。' + limits, top_only=True)
line('01_docs/plans/next_steps.md', '- [ ] **本轮四窗口离线收口。**', '- [x] **本轮四窗口离线收口。**' + summary + '[实际结果](../../' + index + ')；关闭范围仅为该卡本地切片，下面实物和未知根因继续待验。')
line('01_docs/plans/development_plan.md', '- [ ] 下一O11-1', '- [x] O11-1质控本地管理及本轮校准、项目高级/批组、报告本地切片已接统一入口，当前426项输入的35次完整调用、4组专项、4独立界面、三平台主辅/ARM恢复和短基线通过。[结果](../../' + index + ')。完整临床业务与实物不在此勾选范围。')
for name in ('01_docs/plans/next_steps.md', '01_docs/plans/development_plan.md'):
    edited[name] = doc(name).replace('当前371源LIS检查点ARM主/源码恢复包', '当前426项输入四窗口检查点ARM主/源码恢复包')
line('01_docs/plans/next_steps.md', '- [ ] **其余软件功能：**', '- [ ] **其余软件功能：**本轮已完成质控/校准本地页、项目高级/试剂批组本地关联、报告显示/软隐藏/HTML；后续核语言/翻译、样本复位复测、运行数据恢复与应用日志。生产凭据/通用权限、实际LIS、XLSX、实体打印和真实动作按各自前置另验。')
plan = '01_docs/plans/sn100_host_migration_plan.md'
line(plan, '- [x] **项目改名检查与写入原子保护。**', '- [x] **项目改名检查与写入原子保护。**先取写事务再读当前名称并核引用；相同测试修前5/2、修后两轮各7/0，最终35次中仍为7/0。[独立交付](../../04_references/sn100_host/projects_remaining_20261025/project_reference_atomic_handoff.json)。当前最终源码冻结为426/5123，e65和57f仅作历史，原批组保存已在事务内检查。')
line(plan, '- [x] **晚间继续分工和10分钟查看。**', '- [x] **晚间分工和10分钟查看。**原项目、校准、报告三个聊天已交付并结束写入；本聊天按计划协调审查/修复和全部最终验证，本轮收口后暂停`sn100-10`，避免重复构建与空转。后续实物与材料前置见下方。')
line(plan, '- [ ] **质控页面与统一整合窗口。**', '- [x] **质控页面与统一整合窗口（本地切片）。**原主菜单4与质控管理/项目/批次/规则元数据、公共导航/会话/草稿/关闭守卫已接入。' + summary + '[最终证据](../../' + index + ')；真实规则与临床效力未开放。')
line(plan, '- [ ] **校准页面窗口。**', '- [x] **校准页面窗口（本地原始点）。**原主菜单3、记录/点表、项目/批次关联、保存/复制/软隐藏/恢复及权限通过本轮回归。拟合、逆算、审核应用与仪器动作继续待材料和实物。[原交付](../../04_references/sn100_host/calibration_local_20261025/handoff.json)')
line(plan, '- [ ] **项目剩余业务窗口。**', '- [x] **项目剩余业务窗口（本地方案/批组）。**高级方案及试剂批组本地关联、旧接口兼容和项目引用原子保护已验；最终4组11/0、9/0、11/0、11/0，两个独立入口正常退出。真实校准/ISI效力另验。[原交付](../../04_references/sn100_host/projects_remaining_20261025/handoff.json)')
line(plan, '- [ ] **报告剩余功能窗口。**', '- [x] **报告剩余功能窗口（可确认本地范围）。**显示列/排序、软隐藏/恢复、HTML保存/取消/错误恢复与原PDF回归通过；原版XLSX未启用，重算、真实结果链、上传和实体打印继续待验。[原交付](../../04_references/sn100_host/report_remaining_20261025/handoff.json)')
line(plan, '- [ ] **公共接口与整合门槛。**', '- [x] **公共接口与整合门槛（本轮离线）。**各模块隔离构建与匿名运行库，公共接线集中整合；独立库用持久引用保护及启动核对，并明确UUID/规范路径绑定和恢复限制，不宣称跨库外键或一次事务。426项源码、200个主辅WSL对象与22驱动绑定核对，当前35次无遗漏；Windows/ARM鲜编与短基线串行完成。')
line(plan, '- [ ] **交付与记录。**', '- [x] **交付与记录（本轮离线）。**三窗口原始handoff与后续修复独立保留，整合窗口核对最终清单、真实退出、源码/产物/恢复包SHA；README、指南、四部分计划和同一移植记录已同步。[总交付](../../04_references/sn100_host/qc_local_20261025/handoff.json)。' + limits)
line(plan, '- [ ] **校准/质控本地页面。**', '- [x] **校准/质控本地页面切片。**原页面、本地记录、批次及原始点回看已接统一菜单3/4，本轮离线收口见四窗口卡；真实曲线、临床拟合/逆算/质控规则继续待前置，不按本项关闭完整业务。')
edited[plan] = doc(plan).replace('（2026-10-25实施中）', '（2026-10-26本地切片收口）', 1)
line(plan, '- [ ] 保留原`frm_quality`', '- [x] 保留原`frm_quality`等5个原始UI文件字节、颜色/布局及原Print隐藏，原六角色无新增门槛；WSL实际1600/1920管理/批次/输入截图已核，原Windows活动主题及完整视觉等价仍待对照。')
line(plan, '- [ ] 独立正常/非法/取消/空数据/重复/存储失败', '- [x] 独立正常/非法/取消/空数据/重复/存储失败恢复、主菜单4/会话/忙态/关闭与完整累计通过，适用三平台/短资源/冻结同批收口；真实质控点/临床失控、仪器任务和校准计算另列，匿名CRUD不关闭全部F08。')

record = '02_records/20261022_sn100_host_migration.md'
assert 'id="offline-four-windows-final-20261026"' not in doc(record)
edited[record] = doc(record).rstrip() + '\n\n<a id="offline-four-windows-final-20261026"></a>\n### 2026-10-26：四窗口本地切片最终收口\n\n' + summary + '\n\n' + limits + '\n\n' + (
    '最终主辅源码为426/5123，完整回归使用suite_final_08，35次全部真实退出0、无跳过/遗漏；额外4组专项共42项（含各自初始化/清理），并与两个独立入口合计六次新构建分开计数。质控/校准独立入口另有7个实际阶段0。主/辅200对象全量新编，22驱动绑定167个当前主程序对象；登录测试准备修复后其中1驱动重编、21驱动在相同5123源码/对象/Makefile和逐件SHA核对下复用本轮先前构建，不能称22驱动最后一步全部重编。\n\n'
    'Windows32最终为全新目录全量主辅编译/链接（不启动Windows GUI、不做注册表行为验收）；ARM64全新主辅编译，逐调用收据/对象来源及DWARF、依赖、包内成员与426源码一致，现有sysroot只读复用，未安装或部署。当前依赖ELF数和产物/发布标识见总索引及07恢复清单。\n\n'
    '响应为单个原质控页40次名称/厂家实际保存，采集入队→忙态/操作完成/已提交页面，以及5ms合成Qt心跳；不等于物理输入→屏幕像素，也不等于九个worker全负载响应。资源为停止其他构建/GUI/重哈希后的单次30秒默认Guest整程序，9个SQL worker+2个INI worker、自有127.0.0.1名义1Hz只读Qj、原仪器页7、质控/校准初始化；实际TX/RX逐帧复核及正常关闭真实退出0。固定10≤t<30秒窗口未证明就绪时间对齐，VmHWM为生命周期高水位，采样可能遗漏瞬时峰值；初始空匿名目录到退出的总字节含初始化，不是存储增长斜率。预算、严格请求节拍、RT竞争和长稳不作通过结论。\n\n')
edited[record] += '本轮没有控制系统/文件冷热缓存；新建匿名运行目录不等于冷启动环境，不作冷启动性能结论。\n\n'
edited[record] += '实际响应复算：`' + json.dumps(response['arithmetic'], ensure_ascii=False) + '`。\n\n实际资源窗口：`' + json.dumps(resource['steady'], ensure_ascii=False) + '`。\n\n'
edited[record] += '实际自有回环解码消息数：`' + json.dumps(resource['wire_audit']['message_counts'], ensure_ascii=False) + '`；逐帧长度/校验和/分段及Qj请求和SIMULATED应答合同已核，严格请求节拍未验。匿名运行目录退出后共' + str(resource['runtime_file_count']) + '个文件、' + str(resource['runtime_bytes']) + '字节，未归档运行内容。ARM本轮主168+辅助独有2对象全部新编（辅助其余对象共用同轮新对象），56项离线ELF依赖无错，发布标识`' + v['release_id'] + '`。\n\n'
edited[record] += '原suite05测试线程收集风险改为主线程排队收集/真实worker队列屏障，未取得原崩溃栈；原suite07只改测试宿主显示/激活/几何准备，原450×571及立即可见范围断言保留，未声称生产布局修复。历史失败、修复前源码、真实退出和不确定性都保留。\n\n'
edited[record] += '[最终35次/平台/响应/资源证据](../' + index + ') · [本轮整合交付](../04_references/sn100_host/qc_local_20261025/handoff.json) · [匹配应用与源码恢复](../07_backups/sn100_host/offline_four_windows_20261025/README.md) · [截图核对](../04_references/sn100_host/qc_local_20261025/final_visual_review.json)。没有运行参考原件、连接RK/仪器/真实LIS、归档运行库或认证资料，也没有提交/推送Git。\n'

handoff = {'schema': 'sn100-four-windows-root-integration-handoff-v1', 'status': 'passed_local_slice_only',
    'source_inputs': 426, 'source_snapshot_sha256': v['source_manifest_sha256'],
    'current_final': {'path': index, 'sha256': sha(final / 'verification.json'), 'manifest_sha256': sha(final / 'SHA256SUMS')},
    'source_snapshot': {'path': '07_backups/sn100_host/offline_four_windows_20261025/source_snapshot.json', 'sha256': sha(backup / 'source_snapshot.json')},
    'operation_guide': '06_programs/sn100_host/README.md#offline-four-window-usage',
    'entry_and_rule_locations': ['quality_control/quality_control_page.cpp', 'quality_control/qc_store.cpp', 'calibration/calibration_page.cpp', 'calibration/calibration_store.cpp', 'projects/projects_store.cpp', 'reagents/reagent_store.cpp', 'history/report_remaining_store.cpp', 'history/report_controller.cpp', 'shell/main.cpp', 'shell/quality_shell.cpp', 'shell/calibration_shell.cpp'],
    'original_basis': ['06_programs/sn100_host/quality_control/origins.json', '04_references/sn100_host/calibration_local_20261025/original_audit.json', '06_programs/sn100_host/projects/origins.json', '06_programs/sn100_host/reagents/origins.json'],
    'verified': {'complete_suite_actual_invocations': 35, 'additional_project_reagent_tests': 4, 'standalone_qc_calibration_project_reagent': 4, 'platforms': 'WSL actual run; Windows32 and ARM64 fresh compile/link only', 'response_samples': 40, 'resource_seconds': 30},
    'artifacts': {k: v[k] for k in ('native_artifacts', 'windows_artifacts', 'arm64_artifacts', 'standalone_artifacts', 'release_id')},
    'recovery': {'path': '07_backups/sn100_host/offline_four_windows_20261025/verification.json', 'sha256': sha(backup / 'verification.json'), 'readme_sha256': sha(backup / 'README.md'), 'concrete_git_attributes': '04_references/sn100_host/qc_local_20261025/recovery_git_attributes.json', 'git_attributes_sha256': sha(formal / 'recovery_git_attributes.json')},
    'response_and_resource': {'response_arithmetic': response['arithmetic'], 'resource_fixed_window': resource['steady'], 'resource_limits': resource['limits'], 'cache_conditions': 'OS/file hot/cold cache not controlled; fresh anonymous runtime is not a cold-start performance claim'},
    'source_changes_after_owner_handoffs': 'See independent repair handoffs; original owner deliveries remain historical and do not substitute for current full suite/platform evidence',
    'transaction_and_restore_limits': 'Independent databases are not cross-database foreign keys/one transaction; reservations, fail-closed reconcile and UUID/canonical physical path binding require stopped coordinated migration/restore. No clearing UUID/path/holds to bypass mismatches',
    'pending': v['pending'], 'additional_unimplemented': ['Language settings/translation application', 'Real result/calibration/ISI/QC clinical chain', 'Original-disabled XLSX; actual LIS and printing prerequisites'],
    'production_data_or_credentials_archived': False, 'board_or_instrument_connected': False, 'git_committed_or_pushed': False}
refs = ['projects_remaining_20261025/handoff.json', 'calibration_local_20261025/handoff.json', 'report_remaining_20261025/handoff.json',
        'projects_remaining_20261025/project_reference_atomic_handoff.json', 'qc_local_20261025/qc_reference_repair_handoff.json',
        'calibration_local_20261025/calibration_guard_repair_handoff.json', 'calibration_local_20261025/calibration_reference_lifecycle_handoff.json',
        'password_login_20261024/login_geometry_repair_handoff.json', 'report_remaining_20261025/windows_include_continuation_02_handoff.json']
handoff['independently_preserved_handoffs'] = []
for name in refs:
    path = root / '04_references/sn100_host' / name
    assert path.is_file(), name
    handoff['independently_preserved_handoffs'].append({'path': path.relative_to(root).as_posix(), 'sha256': sha(path)})
assert not (formal / 'handoff.json').exists()
# Validate every target and dependency before mutating any existing document.
for name, body in edited.items():
    assert (root / name).read_bytes() == originals[name] and body.endswith('\n'), 'Concurrent document change: ' + name
for name in handoff['original_basis']:
    assert (root / name).is_file(), name
preimage = backup / 'documents_pre_final'
preimage.mkdir(exist_ok=False)
for name, content in originals.items():
    target = preimage / name
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('xb') as stream:
        stream.write(content)
    assert target.read_bytes() == content
handoff['document_preimages'] = {'directory': preimage.relative_to(root).as_posix(), 'files': {name: hashlib.sha256(body).hexdigest() for name, body in originals.items()}}
# Opening the new handoff exclusively reserves it; immediately recheck originals
# before replacing only these seven explicitly listed existing documents.
with (formal / 'handoff.json').open('x', encoding='utf-8') as stream:
    for name in edited:
        assert (root / name).read_bytes() == originals[name], 'Concurrent document change: ' + name
    for name, body in edited.items():
        (root / name).write_text(body, encoding='utf-8', newline='\n')
    stream.write(json.dumps(handoff, ensure_ascii=False, indent=2) + '\n')
print(json.dumps({'documents': list(edited), 'handoff': str(formal / 'handoff.json'), 'passed': True}, ensure_ascii=False))
