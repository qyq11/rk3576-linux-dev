# RK3576 Linux 开发记录

## 先看文件用途

第一次打开工程，可先按下面的目录说明找文件；每层README介绍用途和阅读入口，实际进度仍看原计划，操作结果仍查原记录。

所有文件中的说明都按通俗中文维护：先写清用途、操作和结果，必要术语在原处解释。[长期写作约定](02_records/20261021_ai_workflow.md#all-files-plain-language)。

| 想找什么 | 从这里进入 |
| --- | --- |
| 文档、学习资料与开发计划 | [01_docs说明](01_docs/README.md) |
| 当时做过什么、通过到哪一步 | [02_records说明](02_records/README.md) |
| 按内容分类的现场照片、截图、可编辑圈注 | [03_photos分类目录](03_photos/README.md) |
| 板级资料、日志、测试清单与测量证据 | [04_references说明](04_references/README.md) |
| 安装/烧录/终端工具、独立GPIO和串口实验 | [05_tools说明](05_tools/README.md) |
| 自己维护的程序源码与构建入口 | [06_programs说明](06_programs/README.md) |
| 安装、恢复与回退所需输入 | [07_backups说明](07_backups/README.md) |
| AI使用的项目移植与文件管理流程 | [.agents说明](.agents/README.md) |

根目录其余文件：`AGENTS.md`是项目工作约定；`.gitignore`控制默认不纳入版本管理的本机文件；`.gitattributes`控制脚本换行、固定证据字节和大文件存储。`.git/`是Git内部历史，不作为业务目录编辑。`temp/`是本机构建、日志和可变运行数据，不随Git保存；其中可能有唯一依赖或尚需保留的数据，清理前须核查。

## 当前项目状态

**全部按钮核查（2026-10-27进行中）：**已按45份页面/弹窗盘点367个按钮和选项，并开始对照原Windows处理代码；[逐项核查表](01_docs/knowledge/sn100_buttons_rk_review.md)区分原版无动作、原版隐藏、移植未接和条件限制。用户选择等原数据库接入新版发布后再直接在RK检查，已安排本聊天自动接续；本轮尚未取得新的RK点击通过结论。

**原数据库与配置接入（2026-10-27）：**[文件用途、原目录保持及INI兼容要求](01_docs/knowledge/sn100_original_bin_files.md)已写入MD。当前接入窗口已按用户授权实施：141份资料含29份原DB复制核对通过；四份INI共134项有效配置读取一致；项目配置页面直接读写原项目库和单位库，RK后端90项、页面114项检查通过。完整主程序正在构建并准备更新到原部署位置；其他原DB业务、完整关联复制、配置实际应用及真实仪器仍待验证。[本轮记录](02_records/20261022_sn100_host_migration.md#original-db-rk-20261027)与[当前任务](01_docs/plans/sn100_host_migration_plan.md#original-database-reuse-20261027)。

**后续验证方式（2026-10-27确认）：每完成一项功能或修复，直接在RK上运行并核对实际结果。**电脑只编辑、检查源码、交叉编译ARM64和上传，不再安排Windows/WSL本机运行测试或Windows测试构建；RK不可达时登记待RK验证。编译、RK功能和接真实仪器分别验收，历史电脑结果保留原范围。[长期执行约定](02_records/20261021_ai_workflow.md#rk-direct-verification)

**项目配置本批RK结果（2026-10-27）：**后台56项、正式页面105项核对均0失败；空库录入/文件导入、单位、保存/取消/复制/删除、稀释/默认/稀释液、系数/结果启用、引用保护、写失败恢复和无种子重开已有RK证据。新发布`c89e4a65`已上传，主程序匿名旧结构打开及重开退出0；原现场数据保留，程序现已关闭。物理键鼠/HDMI、原版视觉、资源门槛/同板控制和长稳仍待验；未连接仪器，完整算法仍缺实现。[本批整合记录](02_records/20261022_sn100_host_migration.md#project-config-dispatch-20261027)

**最新推进约定（2026-10-27）：直接面向RK正式接机使用。**用户已有板子，回去可连接真实仪器测试。后续围绕完整项目配置→真实连接/任务→曲线计算→结果/报告推进，每项改动直接上RK验证，具备条件时接机；历史脱机阶段不再限制正式功能实现。本批已补项目配置的本地操作并直接在RK核对；接下来仍需核清真实连接与完整算法实现，配置保存不代表已用于检测。[当前实施卡](01_docs/plans/sn100_host_migration_plan.md#full-function-next-20261027)。本次确认不代表程序或仪器验收已经完成。

**原上位机功能逐项对照（2026-10-27）：**[查看代码已实现、仍缺实现和只待验证的操作清单](01_docs/knowledge/sn100_host_function_and_porting_review.md#original-feature-audit-20261027)。本地资料/预设/历史报告已有实现；真实检测、完整算法、校准应用、质控判定、LIS/实时告警等仍缺代码。RK启动首验不等于全功能通过，原发行版实际启用范围与逐页RK业务继续核对。

**当前：优先补齐原版全部实际功能（2026-10-27）。**用户确认RK可接现有仪器；具体型号/连接参数后续提供，先列信息清单并继续有依据的软件实现。[实施顺序与待补资料](01_docs/plans/sn100_host_migration_plan.md#full-function-next-20261027)。当前已部署版本仍为脱机实现，启动/正常关闭/重开及匿名中文报告生成通过，用户确认HDMI显示和鼠标操作；[20组手动用例](01_docs/knowledge/sn100_host_manual_test_guide.md)仍可用于本地核对。板端用`bash /home/rkshare/programs/sn100_shell/sn100.sh start|stop|status`手动控制（每次选一个动作），不设开机自启动；界面关闭按钮待修，真实仪器功能待实现和验收。全部约定功能实现后再适配小屏/分辨率。

**当前功能范围：**已有基础资料、样本本地预设、校准与质控本地业务、历史报告及文件输出等正式代码。检测调度、完整算法、真实结果/检测时保存的单位记录、LIS收发、实时告警、实体打印与一致业务备份恢复仍有实现或实物缺口。当前只是已有程序的RK首验通过，逐页业务、原Windows视觉、键盘/中文输入、同板控制及长时间运行稳定性另验。


**当前实现结论：**整套SN100功能还没有做完。医院、项目和试剂等本地资料，样本草稿，单个孔位的质控样本预设，质控历史查看，以及报告本地处理已有正式代码。实际检测任务下发、暂停和恢复，完整校准计算、反求结果、审核和应用，真实质控异常判定，真实结果及单位对应，实验室信息系统（LIS）收发，实时告警及动作保护，以及多份业务数据一起备份和恢复，仍缺部分代码或明确资料。打印按钮已接到报告程序，并在电脑上验证了“没有打印设备时提示失败”的处理；正常确认打印和实际出纸还没验证。离线原始通信消息导入组件，以及主程序新建数据、重开和关闭已通过电脑检查；这还不代表LIS通信完整。代码实现和实际测试分别记录。



**用户当前重点（2026-10-26 18:33）：**先逐项确认功能有没有实现，再优先补有依据、能离线完成的缺口。已实现但待验证、确实未实现、缺资料、只待设备验证分别登记；测试次数和源码数量不当作功能完成度。原三个窗口继续分担，每10分钟检查保持ACTIVE；新功能和修复做必要针对验证，已有通过项不反复跑。
更新：2026-10-26。目标是凝血分析仪调度、算法及统一 SN100 Qt 上位机。

本轮447离线结果：WSL主程序和报告辅助程序210对象从源码重新完整编译、Windows32与ARM64主程序和报告辅助程序编译、56项ARM离线依赖、完整35次调用、四组项目试剂复测和四独立程序启动关闭通过；匹配恢复包及短测均留证。保存提交后页面回读p95约107毫秒，高于尚未认可的100毫秒候选，不能称预算达标；历史图卡CPU绘制返回p95约53毫秒，不是屏幕合成延迟。[本批固定索引](04_references/sn100_host/qc_history_integration_20261026/checkpoint_index.json)。

**日常只看[接下来要做的事情](01_docs/plans/next_steps.md)。**模块边界查[移植计划](01_docs/plans/sn100_host_migration_plan.md)，复现操作查对应记录。

**学习工程：**[SN100功能与Linux移植分析](01_docs/knowledge/sn100_host_function_and_porting_review.md)说明已接功能、Windows代码差异、后续建议和阅读路线；验证状态按文内核查日期理解。

**历史426检查点（已固定版本）：**质控、校准、项目高级方案/试剂批组与报告剩余本地业务已接统一程序；上一轮426项输入（固定版本`5123bf14`）的完整35次离线调用、额外4组项目/试剂测试和4个独立界面启动/正常退出均通过。WSL实际运行、Windows32/ARM64主程序与报告辅助程序全量编译、ARM离线依赖及匹配应用/源码归档通过；40次保存操作的响应测量与30秒整程序资源占用初始测量已留证。[本轮总索引](04_references/sn100_host/qc_local_20261025/final/verification.json)。原Windows完整行为/实际使用的界面主题、RK实屏/输入、仪器/实际LIS/实体打印、真实结果产生、保存和显示流程、临床校准/质控、已确认的资源占用上限/同板实时控制及长时间运行稳定性仍待验；该历史检查点的语言设置仍占位；新中文页状态见上方当前说明，原版未启用的XLSX未开放。旧报告查询、8小时恢复，以及suite05校准引用崩溃和suite07登录1920宽度下位置或尺寸检查失败的直接原因尚未确证，原失败保留。

前次报告整合的三个窗口已结束，中文输入及PDF修复成果已核对。本次保留参考原件、运行数据和必要依赖；已确认的临时清理候选因工具策略拒绝删除而原地保留，不纳入Git。

此前371项输入的LIS检查点及旧361/353/348结果各自保留。用户2026-10-25晚授权四个现有聊天分担，上一轮本地门槛已完成；2026-10-26用户要求继续处理旧偶发和剩余离线事项，10分钟检查已恢复。[分工与后续](01_docs/plans/sn100_host_migration_plan.md#offline-four-windows-20261025)

本轮项目并发改名保护修前5通过/2失败、修后两轮各7通过/0失败；Windows编译暴露的报告/校准显式头文件问题已修并在当前快照重编验证。`e65d7730`、`57f3b032`及各失败轮次只作历史证据。[操作说明](06_programs/sn100_host/README.md#offline-four-window-usage) · [匹配恢复包](07_backups/sn100_host/offline_four_windows_20261025/README.md)

图形界面透明的按钮光晕动画无效重绘已修复，相同条件旧短观测约18.5%→0.86%单核；旧361整程序尾段2.31%与旧371各保留当批WSL基线，不相减推断纯设置开销。旧361的Windows主make单独的执行结果记录/prehash缺口、旧O05恢复及报告偶发故障原因均保留；371和本轮426的Windows执行结果记录分别归档。[371历史结果与边界](02_records/20261022_sn100_host_migration.md#o10-lis-settings-20261024)。

### 文中用语说明

下面保留必要的技术名称，便于查代码和原始记录。阅读时可按右侧含义理解。

| 写法 | 这里的意思 |
| --- | --- |
| WSL | Windows电脑中的Linux环境，本项目用它交叉编译RK程序 |
| ARM64 | RK板所用的64位处理器架构，编译时需要生成匹配的程序 |
| LIS | 实验室信息系统，用来接收检测任务、上传检测结果 |
| 两种界面运行方式 | 历史电脑测试使用的无窗口方式（offscreen）和有窗口方式（XCB/WSLg），都不代替RK实际屏幕操作 |
| p95 | 按耗时从小到大排列，95%的操作耗时不超过这个值 |
| 基线 | 本次测出的初始对照结果，供以后在相同条件下比较 |

测试中的“13/0”表示13项通过、0项失败；“退出码0”表示该次程序正常结束。它们都只说明所列测试范围，不能代表整套仪器业务通过。

## 进度与边界

仪器操作原版8子页和6项人工维护已接入统一程序：存储11/0、两种界面运行方式界面各10/0、相关已有功能的复测及WSL/Windows32/ARM64检查点通过，七个数据存取组件实际30秒运行/正常退出与资源留证；真实动作仍禁用。[结果与待验](02_records/20261022_sn100_host_migration.md#instrument-offline-result-20261024) · [发布包及匹配源码](07_backups/sn100_host/instrument_offline_20261024/README.md)。此固定包不包含之后继续开发的密码登录新源。

| 范围 | 已有结果 | 还要做 |
| --- | --- | --- |
| RK基础/实时内核 | 交叉部署通过；`6.1.99-rt36-tspi`、RT=1及60秒对比有证据 | 共享/所用外设复测、重复长测、重启与恢复 |
| Qt环境 | SN100所需Qt5.15.13、ARM64编译依赖及应用运行组件已有独立归档 | SN100业务页面实屏、中文、弹窗及资源 |
| SN100界面/报告 | 统一查询/保存/CSV/原模板预览/PDF、新旧功能一起复测和Windows、电脑Linux和RK版编译通过，60轮导出无失败 | 指定阅读器、原Windows行为/视觉、RK/仪器 |
| 通信 | 回放、回环、异常恢复、2h连续及25分钟恢复诊断通过 | 旧8h恢复19分10秒失败故障原因未明；外部仪器Transport与协议 |
| 算法 | 统计和两种凝血点研究原型、合成数据验证 | 真实曲线、参数、已确认的允许误差及对应业务 |

电脑离线、三目标编译、原Windows对照、RK实屏、仪器、资源占用上限与长时间运行稳定性分别验收。环境可用不能代替业务通过。

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
| 原版主工程与配置（当前层级） | [Seerain450_PC/SeeRain450.pro](06_programs/sn100_host_reference/Seerain450_PC/SeeRain450.pro)及同层[bin/](06_programs/sn100_host_reference/Seerain450_PC/bin/) |
| 安装、烧录和回退的正式输入/必要备份 | [07_backups/](07_backups/)；二进制镜像/压缩备份用Git LFS |
| 可重建产物、临时日志、隔离运行数据 | `temp/<program>/`；唯一sysroot/依赖清理前另核查 |

## 开发与运行入口

Windows与WSL使用当前同一份源码；默认WSL为Ubuntu-24.04/qyq。Windows、WSL x86_64、RK ARM64的构建与库相互隔离。

正式程序提供`build.sh <绝对输出文件>`。在WSL项目的`06_programs/`目录运行工作流示例：

```bash
bash rk.sh workflow_demo all -- 12 30
```

已实际返回aarch64、`12 + 30 = 42`和退出0。默认目标`lckfb@192.168.100.123`，部署到`/home/rkshare/programs/<program>/`；失败不部署旧产物。

SN100的WSL、Windows、独立页和统一主程序用法统一见[工程README](06_programs/sn100_host/README.md)。统一主程序RK准备使用`sn100_host/shell/rk.sh prepare`，根`rk.sh sn100_host all`仍是独立页；上板另按[首验卡](01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)。

## 已有记录

- [固定IP与SSH](02_records/20261021_固定IP地址.md) · [TTL/UART4](02_records/20261021_TTL串口调试验证.md) · [GPIO](02_records/20261021_gpio_output.md)
- [Samba与主目录](02_records/20261021_Samba共享文件夹.md) · [工作目录/WindTerm/历次清理](02_records/20261021_workspace_setup.md)
- [自动编译部署工作流](02_records/20261021_ai_workflow.md) · [设备架构与接口职责](02_records/20261022_system_architecture.md)
- [PREEMPT_RT构建、烧录、测量与回退](02_records/20261022_preempt_rt.md) · [SN100 Qt依赖与环境](02_records/20261022_qt_environment.md)
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
