# RK3576 Linux 开发记录

更新：2026-10-25。目标是凝血分析仪调度、算法及统一 SN100 Qt 上位机。

**日常只看[接下来要做的事情](01_docs/plans/next_steps.md)。**模块边界查[移植计划](01_docs/plans/sn100_host_migration_plan.md)，复现操作查对应记录。

**当前做到：**统一脱机程序已接原打印机及LIS本地设置，保留项目/单位、试剂基础、样本本地预设/任务、仪器/维护、回放/回环/登录、医院/账户/历史/CSV及预览/PDF。371新源的离线验证、三平台主辅/闭包及ARM成对恢复归档已核；原色布局保留。[当前总索引](04_references/sn100_host/lis_settings_20261024/verification.json)。实际LIS收发、任务/打印、原Windows、RK/仪器及预算/长稳另验。

前次报告整合的三个窗口已结束，中文输入及PDF修复成果已核对。本次保留参考原件、运行数据和必要依赖；已确认的临时清理候选因工具策略拒绝删除而原地保留，不纳入Git。

本批O10-2 LIS本地子页4已接入：独立各17/0、统一各5/0及原页面累计通过，保留9可见/12隐藏键和双草稿，保存不连接。371源三平台产物、响应/资源及恢复包已核，旧361/353/348不混用。2026-10-25用户已授权4个聊天推进质控、校准、项目和报告剩余本地业务，质控聊天兼任统一整合负责人；本轮正在累计验证，RK暂不可用。同日晚按用户新要求启用每10分钟进度/计划核对与现有窗口任务分发，本聊天持续整合。[本轮分工](01_docs/plans/sn100_host_migration_plan.md#offline-four-windows-20261025)

本轮项目并发改名缺口已实际复现并修复：相同测试修前5通过/2失败、修后7通过/0失败。修后426输入已重新全量WSL编译，冻结SHA以`57f3b032`开头，22个驱动全部鲜编；完整35次离线回归已全部通过，QC/校准两个独立程序已实际加载并正常退出。原suite05崩溃保留，已修正测试跨线程收集风险，直接原因未确证；目前补跑项目/试剂4组专项及2独立入口，随后串行做Windows/ARM、资源与恢复归档；修前`e65d7730`仅作历史证据，Windows首轮暴露的报告/校准显式头文件兼容问题已窄修并经主辅诊断构建0确认；新426快照`5123bf14`正在全量重新冻结验证，57f结果保留历史，尚无本轮最终发布包。[本轮操作说明](06_programs/sn100_host/README.md#offline-four-window-usage)

GUI透明halo无效重绘已修复，同条件旧短观测约18.5%→0.86%单核；旧361整程序尾段2.31%与当前371各保留本批WSL基线，不相减推断纯设置开销。旧361的Windows主make独立收据/prehash缺口、旧O05恢复及报告偶发根因均保留，本批371的Windows收据完整。[本轮结果与边界](02_records/20261022_sn100_host_migration.md#o10-lis-settings-20261024)。

## 进度与边界

仪器操作原版8子页和6项人工维护已接入统一程序：存储11/0、两QPA界面各10/0、适用累计及WSL/Windows32/ARM64检查点通过，七store实际30秒运行/正常退出与资源留证；真实动作仍禁用。[结果与待验](02_records/20261022_sn100_host_migration.md#instrument-offline-result-20261024) · [发布包及匹配源码](07_backups/sn100_host/instrument_offline_20261024/README.md)。此固定包不包含之后继续开发的密码登录新源。

| 范围 | 已有结果 | 还要做 |
| --- | --- | --- |
| RK基础/实时内核 | 交叉部署通过；`6.1.99-rt36-tspi`、RT=1及60秒对比有证据 | 共享/所用外设复测、重复长测、重启与恢复 |
| Qt环境 | `qt_demo` HDMI1600×900/USB键鼠及WSL→RK GDB通过 | SN100业务页面实屏、中文、弹窗及资源 |
| SN100界面/报告 | 统一查询/保存/CSV/原模板预览/PDF、累计回归和三目标构建通过，60轮导出无失败 | 指定阅读器、原Windows行为/视觉、RK/仪器 |
| 通信 | 回放、回环、异常恢复、2h连续及25分钟恢复诊断通过 | 旧8h恢复19分10秒失败根因未明；外部仪器Transport与协议 |
| 算法 | 统计和两种凝血点研究原型、合成数据验证 | 真实曲线、参数、认可误差及对应业务 |

电脑离线、三目标编译、原Windows对照、RK实屏、仪器、资源预算与长稳分别验收。最小demo通过不能代替业务通过。

SN100先用RK外接HDMI和USB键鼠逐模块验证，稳定后再适配触控小屏；保留原版颜色与布局。

## 文件放在哪里

| 内容 | 位置 |
| --- | --- |
| 日常待办/整体计划/技术移植计划 | [01_docs/plans/](01_docs/plans/) |
| 原理、工程结构、资料入口 | [01_docs/knowledge/](01_docs/knowledge/) |
| 同一事项持续追加的实际操作与验证 | [02_records/](02_records/) |
| 原始及标注图片 / 长期资料与证据 | `03_photos/` / `04_references/` |
| 接口测试源码 / 正式应用源码与构建 | `05_tools/tests/` / `06_programs/<program>/` |
| 只读参考工程 | `06_programs/sn100_host_reference/` |
| 安装、烧录和回退的正式输入/必要备份 | [07_backups/](07_backups/)；二进制镜像/压缩备份用Git LFS |
| 可重建产物、临时日志、隔离运行数据 | `temp/<program>/`；唯一sysroot/依赖清理前另核查 |

## 开发与运行入口

Windows与WSL使用当前同一份源码；默认WSL为Ubuntu-24.04/qyq。Windows、WSL x86_64、RK ARM64的构建与库相互隔离。

正式程序提供`build.sh <绝对输出文件>`。在WSL项目的`06_programs/`目录运行工作流示例：

```bash
bash rk.sh workflow_demo all -- 12 30
```

已实际返回aarch64、`12 + 30 = 42`和退出0。默认目标`lckfb@192.168.100.123`，部署到`/home/rkshare/programs/<program>/`；失败不部署旧产物。

SN100的WSL、Windows、独立页和统一壳用法统一见[工程README](06_programs/sn100_host/README.md)。统一壳RK准备使用`sn100_host/shell/rk.sh prepare`，根`rk.sh sn100_host all`仍是独立页；上板另按[首验卡](01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)。

## 已有记录

- [固定IP与SSH](02_records/20261021_固定IP地址.md) · [TTL/UART4](02_records/20261021_TTL串口调试验证.md) · [GPIO](02_records/20261021_gpio_output.md)
- [Samba与主目录](02_records/20261021_Samba共享文件夹.md) · [工作目录/WindTerm/历次清理](02_records/20261021_workspace_setup.md)
- [自动编译部署工作流](02_records/20261021_ai_workflow.md) · [设备架构与接口职责](02_records/20261022_system_architecture.md)
- [PREEMPT_RT构建、烧录、测量与回退](02_records/20261022_preempt_rt.md) · [Qt环境、显示与调试](02_records/20261022_qt_environment.md)
- [SN100所有模块、资源、删减与实物验证](02_records/20261022_sn100_host_migration.md) · [整体开发计划](01_docs/plans/development_plan.md)

## 资料与工作约定

板级资料从[立创泰山派3M官方文档](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/)进入；项目已有[Linux基础](01_docs/knowledge/linux_basics.md)、[SN100工程说明](01_docs/knowledge/sn100_host_project_guide.md)及[40Pin资源表](04_references/RK3576_40Pin工程资源分配表.jpg)。

AI工作遵循[根AGENTS](AGENTS.md)、[SN100局部AGENTS](06_programs/sn100_host/AGENTS.md)、[模块Skill](.agents/skills/sn100-module-migration/SKILL.md)和[文件Skill](.agents/skills/sn100-project-files/SKILL.md)。实际TX/RX、数据/实物变化及退出码必须可核对；无用途依据不删业务代码。

业务和进度解释采用通俗中文，先讲用途与操作，再讲已完成/待做和下一步；跨窗口约定与生效范围见[AI工作流记录第6节](02_records/20261021_ai_workflow.md#plain-language-communication)。

计划保持四部分勾选；README只留总入口。实际过程更新原事项记录，不另建平行日志；不保存患者隐私、密码、私钥或认证凭据。

## 获取项目

仓库：[qyq11/rk3576-linux-dev](https://github.com/qyq11/rk3576-linux-dev)。安装Git/Git LFS后：

```powershell
git lfs install
```

```powershell
git clone https://github.com/qyq11/rk3576-linux-dev.git
```

```powershell
git -C rk3576-linux-dev lfs pull
```

源码、正式记录/图片/证据和必要备份随Git保存；原参考工程、工具安装包和芯片PDF仅本地保留。后续仍按实际需要验证接口，不因计划排序推定硬件通过。
