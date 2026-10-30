import hashlib
import json
import shutil
from pathlib import Path

root = Path.cwd()
out = root / '04_references/sn100_host/cleanup_20261030'

def change(name, transform):
    path = root / name
    raw = path.read_bytes()
    ending = '\r\n' if b'\r\n' in raw else '\n'
    text = raw.decode('utf-8').replace('\r\n', '\n')
    updated = transform(text)
    if updated != text:
        path.write_bytes(updated.replace('\n', ending).encode('utf-8'))

summary = '**2026-10-30整理检查点：**已清理26,900个可重建文件，约18.1 GB；构建依赖、运行数据和原始证据保留，`temp/`没有整体清空。上次停止交接和未采用的日期候选已归档。当前仍停在原质控/校准资料页与试剂批组选项的修后待RK验证阶段；下一步先重新交叉编译并在RK复测这两项，再整合发布。完整算法、真实仪器和其余业务尚未完成。本轮只整理并保存现有工作，未恢复开发、测试或部署。'
change('README.md', lambda t: t.replace('## 当前项目状态\n', '## 当前项目状态\n\n' + summary + '[清理结果与接续入口](04_references/sn100_host/cleanup_20261030/README.md)。\n', 1).replace('正式版本仍f8c，未提交或推送Git。', '上次停止时正式版本仍f8c，当时未提交或推送Git。').replace('(temp/sn100_host/original_db_20261027/integration/pause_handoff_20261028.md)', '(04_references/sn100_host/cleanup_20261030/handoff/pause_handoff_20261028.md)'))
change('01_docs/plans/next_steps.md', lambda t: t.replace('# 接下来要做的事情\n', '# 接下来要做的事情\n\n' + summary + '[停止交接与范围](../../04_references/sn100_host/cleanup_20261030/README.md)。以下历史事项按日期理解，执行优先看此停止点。\n', 1))
change('01_docs/plans/sn100_host_migration_plan.md', lambda t: t.replace('# SN100上位机移植计划\n', '# SN100上位机移植计划\n\n' + summary + '[归档停止交接](../../04_references/sn100_host/cleanup_20261030/handoff/pause_handoff_20261028.md)。\n', 1).replace('## 一、已完成\n', '## 一、已完成\n\n- [x] **中间文件清理与停止资料归档（2026-10-30）。**清理26,900个可重建文件约18.1 GB，10份交接与日期候选已复制核对；源码、依赖、数据、正式备份及原证据保留。本项只完成文件整理，不关闭业务待验项。[清单与结果](../../04_references/sn100_host/cleanup_20261030/README.md)。\n', 1).replace('## 二、当前需要做\n', '## 二、当前需要做\n\n- [ ] **恢复后的第一项：完成上次两项修后的RK复测。**前置是用户安排继续测试，并确认RK可用；中间程序已清理，先从当前源码重新交叉编译。分别在RK检查原质控/校准资料页的保存、取消、重开与原数据，及试剂批组选项的候选、取消、保存和模拟收发；保留旧失败，修后有实际输出与正常退出才关闭本切片。通过后再整合发布，日期日历、候选顺序、质控/校准任务与算法另接续。当前只保存整理结果，不自动启动上述工作。\n', 1).replace('../../temp/sn100_host/original_db_20261027/integration/pause_handoff_20261028.md', '../../04_references/sn100_host/cleanup_20261030/handoff/pause_handoff_20261028.md').replace('../../temp/sn100_host/original_db_20261027/integration/stop_handoff_20261027.md', '../../04_references/sn100_host/cleanup_20261030/handoff/stop_handoff_20261027.md'))
change('06_programs/sn100_host/README.md', lambda t: t.replace('\n', '\n\n' + summary + '[当前停止交接](../../04_references/sn100_host/cleanup_20261030/handoff/pause_handoff_20261028.md)。下方历史结果保留各自范围。\n', 1).replace('../../temp/sn100_host/original_db_20261027/integration/stop_handoff_20261027.md', '../../04_references/sn100_host/cleanup_20261030/handoff/stop_handoff_20261027.md'))
change('04_references/sn100_host/original_settings_integration_20261028/README.md', lambda t: t.replace('接续先看`temp/sn100_host/original_db_20261027/integration/pause_handoff_20261028.md`', '接续先看[已归档停止交接](../cleanup_20261030/handoff/pause_handoff_20261028.md)；2026-10-30已清理可重建程序和临时包，恢复时须重新交叉编译'))
change('04_references/sn100_host/README.md', lambda t: t.replace('| [cleanup_20261024/]', '| [cleanup_20261030/](cleanup_20261030) | 中间文件删除清单、实际结果、停止交接及未采用候选；不增加运行通过结论。 |\n| [cleanup_20261024/]', 1))
change('07_backups/sn100_host/original_db_20261027/README.md', lambda t: t.replace('原中间包仍在`temp`可追溯，具体文件与指纹见', '该中间包后来已在2026-10-30按用户要求清理，原替代过程与文件指纹保留；[本次清理范围](../../../04_references/sn100_host/cleanup_20261030/README.md)不影响本目录正式恢复包。原替代记录见'))
backup = root / '07_backups/sn100_host/original_db_20261027'
sums = backup / 'SHA256SUMS'
rows = sums.read_text(encoding='utf-8').splitlines()
rows = [hashlib.sha256((backup / 'README.md').read_bytes()).hexdigest() + '  README.md' if row.endswith('  README.md') else row for row in rows]
sums.write_text('\n'.join(rows) + '\n', encoding='utf-8', newline='\n')

record = '''

<a id="cleanup-and-checkpoint-20261030"></a>
## 2026-10-30：清理中间文件，保存停止点并准备Git提交

用户要求先删除中间文件并做一次提交，再说明当前进度与后续步骤。本轮沿用上次暂停边界，仅清理、归档和保存现有工作；没有恢复开发、编译、RK测试或部署。

- **删除范围与依据：**先读取根说明、工程约定、计划及双方最终交接，盘点Git改动和`temp/`。未发现正在使用本批产物的电脑/WSL构建或程序进程。只选编译对象、自动生成代码和规则、可重建程序、临时应用包及缓存；所有绝对目标先核实位于当前项目`temp/`内，再用PowerShell的`Remove-Item -LiteralPath`逐文件删除，没有递归删除目录。
- **实际结果：**26,900个文件，共18,105,264,556字节（约18.1 GB / 16.86 GiB），清单剩余0。源码、参考原件、构建依赖、运行数据、原始证据和正式恢复包保留，`temp/`没有整体清空。[删除清单与结果](../04_references/sn100_host/cleanup_20261030/README.md)。这些内容可从源码重新生成，不为清理另备份全部产物。
- **唯一工作资料先保存：**10份停止交接、源码指纹和未采用日期候选复制到`04_references/sn100_host/cleanup_20261030/`，逐份SHA-256相同，原件文字不重写。当前入口改指长期归档；历史交接中的临时程序和包路径可能已删除，后续须重新交叉编译。07正式恢复包保持，维护说明及其校验清单同步更新。
- **提交范围：**保存暂停前已存在的源码、测试源、业务记录、计划、图片和证据，以及本轮清理记录，不将`temp/`加入Git。原试剂测试目录中已登记指纹的`driver`属于固定验证原件，保持其字节，通过专用Git LFS规则保存。用户本次已明确要求提交；不推送远端。
- **当前真实进度：**原数据库/配置已在RK接入；后续候选的设置、历史报告和一个普通样本的录入、开始、暂停、恢复及曲线待计算保存有RK软件结果。最后正在处理原质控/校准资料页与试剂批组选项：主程序ARM64全量编译已通过，报告14/0、设置11/0；质控/校准首轮4/3、批组两轮5/2和6/1的失败保留。最新质控/校准驱动只编译、最新批组测试只修改源码，均待RK复测。正式部署最后只读记录仍f8c，本轮未连接板子重新确认。
- **恢复后先做：**从提交后的当前源码重新交叉编译，直接在RK复测上述两项，核保存/取消/重开和原库数据，确认后再整合发布。随后补日期日历、候选顺序、质控/校准任务、完整算法及真实检测到报告的流程；全部按钮业务、实际LIS收发、实体打印、告警保护、恢复、资源门槛和长期运行仍待逐项完成。当前仅报告下一步，不自行启动。

本轮验证只针对文件路径、删除结果、归档指纹、正式备份校验、文档引用和Git保存；不增加编译、RK功能或真实仪器通过结论。后续勾选继续维护在[唯一移植计划](../01_docs/plans/sn100_host_migration_plan.md)。
'''
change('02_records/20261022_sn100_host_migration.md', lambda t: t + record if 'id="cleanup-and-checkpoint-20261030"' not in t else t)

tools_dir = out / 'tools'
tools_dir.mkdir(exist_ok=True)
for name in ('plan_cleanup_20261030.py', 'run_cleanup_20261030.ps1', 'finalize_cleanup_docs_20261030.py'):
    shutil.copyfile(root / 'temp' / name, tools_dir / name)
print('Updated cleanup documentation and durable handoff links.')
