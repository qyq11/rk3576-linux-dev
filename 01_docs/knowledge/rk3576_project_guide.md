# RK3576 项目使用与资料指南

这里集中说明开发环境、两块板的用途、文件归属和操作入口，承接原根目录 README 中的详细说明。具体任务维护在[开发计划](../plans/README.md)，历次实际结果维护在[操作记录](../../02_records/README.md)。

[返回项目首页](../../README.md)

## 两块板怎么区分

| 标记 | 用途 | 已有结果怎么理解 |
| --- | --- | --- |
| 板A，`board_a` | Qt上位机用板；2026-11-04起，后续SN100测试统一使用板A | 此前Qt环境、原板上位机部署及历史串口回环属于板A；每次新功能仍需按实际范围验证。 |
| 板B，`board_b` | 2026-11-04起用于安装到小车 | 此前SN100、五组串口及CAN0结果保留板B历史范围，不套用到板A。 |

操作前结合硬件序列号或已核实的SSH主机指纹确认目标，不能只凭IP地址。两板的配置、安装、部署、备份和验证分开记录；一块板通过不代表另一块也通过。

- [完整记录规则与身份依据](../../02_records/20261021_ai_workflow.md#two-board-records)
- [历史归属核查与待确认项](../../02_records/20261021_ai_workflow.md#historical-board-audit)
- [板A重新配置的完整过程](../../02_records/20261021_TTL串口调试验证.md#board-a-configuration-process-20261102)：包含板卡确认、有限备份、串口/CAN配置、重启及root登录，配置检查与实际收发分别说明。

## 开发环境与运行入口

WSL是Windows电脑中的Linux环境。本项目默认使用`Ubuntu-24.04`、用户`qyq`，从`/mnt/c/`访问Windows上的同一份源码。电脑用于编辑、源码检查、交叉编译和上传；ARM64是RK板使用的处理器架构，编译产物需与它匹配。

| 工作 | 入口与说明 |
| --- | --- |
| 普通RK程序 | 使用`aarch64-linux-gnu-gcc`交叉编译；每个正式程序提供`build.sh <绝对输出文件路径>`。 |
| SN100上位机 | 使用Qt5.15.13；构建、打包与独立页/统一主程序的区别见[工程说明](../../06_programs/sn100_host/README.md)，实际操作见[人工使用说明](sn100_host_manual_test_guide.md)。 |
| 整机系统镜像 | 使用专用WSL`RK3576-Build-22.04`、用户`qyq`；工作盘在`D:\WSL\RK3576-Build-22.04\ext4.vhdx`，源码在`/home/qyq/TaishanPi-3-Linux`，从[系统工程](../../06_programs/rk3576_system/README.md)进入。 |

Windows、WSL本机与RK使用的构建产物和库分开存放，不能混用。系统镜像环境迁移的核对范围见[原记录](../../02_records/20261030_system_image.md#wsl-move-d-20261102)。

### 通用编译、上传和运行示例

在WSL项目的`06_programs/`目录执行：

```bash
bash rk.sh workflow_demo all -- 12 30
```

这条命令会编译示例、上传到RK并实际执行，不是只查看文件。执行前先确认目标板身份；脚本默认连接`lckfb@192.168.100.123`，程序放在`/home/rkshare/programs/<program>/`，从部署目录启动。默认运行上限30秒，按任务通过`RK_RUN_TIMEOUT`调整；目录不同则使用`RK_REMOTE_ROOT`。

历史验证已返回`aarch64`、`12 + 30 = 42`及退出码0，见[工作流记录](../../02_records/20261021_ai_workflow.md)。上传后需核对SHA-256文件指纹及动态库，编译失败不得部署旧产物；这次文档整理没有重新执行该命令。

SN100的独立页与统一主程序有不同入口，按[工程说明](../../06_programs/sn100_host/README.md)选择。后续功能、界面和资源验证直接在RK执行；不再安排Windows/WSL本机产品运行，也不再做脱机、模拟服务器或本机回环自测。使用原配置和原配套资料连接主控，遇到控制或运行异常先查实际生效的INI配置文件；条件不足就记录待联机验证。详见[验证方式](../../02_records/20261021_ai_workflow.md#rk-direct-verification)和[后续联机安排](../../02_records/20261022_sn100_host_migration.md#remove-offline-data-20261102)。

## 文件放在哪里

| 内容 | 位置 |
| --- | --- |
| 日常待办、整体计划、移植计划 | [01_docs/plans/](../plans/README.md) |
| 原理、工程结构、使用说明 | [01_docs/knowledge/](README.md) |
| 同一事项持续追加的实际操作与验证 | [02_records/](../../02_records/README.md) |
| 原始及标注图片 | [03_photos/](../../03_photos/README.md) |
| 长期资料、日志与验证证据 | [04_references/](../../04_references/README.md) |
| 安装/烧录/终端工具及独立接口测试 | [05_tools/](../../05_tools/README.md)；独立测试在[tests/](../../05_tools/tests/README.md) |
| 正式应用源码与构建脚本 | [06_programs/](../../06_programs/README.md)，按`<program>/`分目录 |
| 原Windows参考工程 | `06_programs/sn100_host_reference/Seerain450_PC/`；主工程为`SeeRain450.pro`，数据库和配置在同层`bin/` |
| 安装、烧录、恢复与回退的正式输入 | [07_backups/](../../07_backups/README.md)；二进制镜像和压缩备份使用Git LFS管理 |
| 构建产物、临时日志、可变运行数据 | `temp/<program>/`，不随Git保存；清理前核查唯一依赖、数据和证据 |
| AI使用的项目流程 | [.agents/](../../.agents/README.md) |

参考工程只读、本地保留，不在其中修改、构建或使用旧配置连接仪器。正式源码、记录、图片、证据和必要备份随Git保存；原参考包、工具安装包和芯片PDF按现有本地范围保留。

根目录的`AGENTS.md`保存工作约定；`.gitignore`控制默认不纳入Git的文件；`.gitattributes`控制换行、固定证据字节和大文件存储。`.git/`是Git内部历史，不作为业务目录编辑。

## 查进度、操作和验证结果

| 想了解什么 | 从这里进入 |
| --- | --- |
| 整体路线与下一步 | [整体开发计划](../plans/development_plan.md) · [日常待办](../plans/next_steps.md) |
| SN100当前任务与待验条件 | [移植计划当前任务](../plans/sn100_host_migration_plan.md#prepare-without-manual-20261105) · [11月5日候选准备结果](../../02_records/20261022_sn100_host_migration.md#prepare-without-manual-20261105) |
| SN100历次实现、部署、失败和复测 | [统一移植记录](../../02_records/20261022_sn100_host_migration.md) · [功能与原版对照](sn100_host_function_and_porting_review.md) |
| 固定IP、SSH和终端登录 | [固定地址记录](../../02_records/20261021_固定IP地址.md) · [WindTerm与工作目录](../../02_records/20261021_workspace_setup.md) |
| 文件共享与主目录 | [Samba共享记录](../../02_records/20261021_Samba共享文件夹.md) |
| 串口、GPIO和CAN | [串口记录](../../02_records/20261021_TTL串口调试验证.md) · [GPIO记录](../../02_records/20261021_gpio_output.md) · [CAN记录](../../02_records/20261102_can.md) |
| 实时内核构建、烧录、测量和回退 | [PREEMPT_RT记录](../../02_records/20261022_preempt_rt.md)；PREEMPT_RT用于改善系统对控制任务的响应及时性 |
| Qt依赖、中文输入和恢复输入 | [Qt环境记录](../../02_records/20261022_qt_environment.md) |
| 整机镜像及编译环境 | [系统镜像记录](../../02_records/20261030_system_image.md) · [正式镜像与用法](../../07_backups/system_image/ubuntu24_server/README.md) |
| 设备分工与后续接口 | [架构与职责](../../02_records/20261022_system_architecture.md) · [接口需求计划](../plans/development_plan.md#hardware-interface-plan-20261027) |

原首页中的历次版本、测试数字、程序启停状态和清理结果，按各自日期查上面的原事项记录。旧记录中的“当前”“保持运行”只描述当时时点，不能用来判断此刻的板端状态。

编译通过、RK软件运行通过、实际屏幕/实体操作通过、连接真实仪器通过分别看。历史电脑测试只保留原范围；程序启动成功、文件复制齐全和测试次数都不能代表整套业务完成。界面尽量保留原版颜色和布局，完整功能移植与后续小屏适配按原计划推进。

## 常见写法怎么理解

| 写法 | 含义 |
| --- | --- |
| LIS | 实验室信息系统，用来接收检测任务、上传检测结果 |
| SHA-256 | 文件指纹，用来比较上传或复制前后的内容是否一致 |
| 动态库 | 程序运行时加载的公共组件，Linux下通常是`.so`文件 |
| offscreen / XCB / WSLg | 历史无窗口/有窗口运行方式，不代替RK实际屏幕操作 |
| p95 | 按耗时从小到大排列，95%的操作耗时不超过这个值 |
| 基线 | 在指定条件下测出的初始对照结果，供以后在相同条件下比较 |
| 13/0、退出码0 | 前者表示13项通过、0项失败；后者表示该次程序正常结束，均限所列验证范围 |

## 资料与维护约定

板级资料从[立创泰山派3M官方文档](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/)进入；项目已有[Linux基础](linux_basics.md)、[SN100工程说明](sn100_host_project_guide.md)及[40Pin资源表](../../04_references/RK3576_40Pin工程资源分配表.jpg)。

工作遵循[根目录约定](../../AGENTS.md)、[SN100局部约定](../../06_programs/sn100_host/AGENTS.md)、[模块移植流程](../../.agents/skills/sn100-module-migration/SKILL.md)和[工程文件管理流程](../../.agents/skills/sn100-project-files/SKILL.md)。说明文字使用通俗中文，写清用途、操作和核对方法，必要术语在原处解释；见[长期写作约定](../../02_records/20261021_ai_workflow.md#all-files-plain-language)。

根README用于项目展示和导航；本指南维护相对稳定的使用说明；计划保留四部分勾选任务；实际执行继续追加原事项记录。后续更新进度时直接修改对应计划和记录，只在项目范围或主要入口变化时调整首页。不保存患者隐私、密码、私钥或其他认证凭据。
