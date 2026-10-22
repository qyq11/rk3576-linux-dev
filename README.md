# RK3576 Linux 开发记录

记录立创泰山派 3M RK3576 的 Linux 开发过程。**从本 README 进入，每完成一件事，只保留一份能够独立复现的 Markdown 记录。**

更新日期：2026-10-22。当前板端运行 Ubuntu 24.04 系列系统、Linux `6.1.99-rt36-tspi` / aarch64，PREEMPT_RT 已生效，启动核对与两轮 60 秒延迟初测已完成，见下文实时内核进展。此前固定 IP、SSH、TTL 调试口、UART4 本地回环和 Samba 共享迁移已通过；GPIO 13 针的板端原生编译版本已实测 0 V → 3.28 V → 0 V。同一程序的 WSL ARM64 交叉编译、免密上传和 RK 直接运行也已验证成功，用户反馈交叉编译产物实测低电平 0 V、高电平 3.28 V。这些接口与共享在新内核下的实际功能仍需分别复测。后续自行开发的 RK Linux 应用沿用交叉编译与部署方式，并推进凝血分析仪下位机调度、算法及外接屏幕 Qt 上位机。

## 项目结构

```text
项目根目录/
├── README.md                         # 总入口：框架、进度和事项导航
├── AGENTS.md                         # AI 编写、编译、部署与运行约定
├── 01_docs/
│   ├── plans/                         # 待办、优先级和完成标准
│   │   └── development_plan.md
│   └── knowledge/                     # 原理、入门说明与操作知识
│       └── linux_basics.md             # 文件接口、GPIO、设备树、UART、WSL 与交叉编译
├── 02_records/
│   ├── 20261021_固定IP地址.md          # 固定 IP、重启保留和有线 SSH
│   ├── 20261021_TTL串口调试验证.md     # 调试口、UART4 回环及重启复测统一记录
│   ├── 20261021_Samba共享文件夹.md     # 已迁移至 /home/rkshare，Windows 读写通过
│   ├── 20261021_gpio_output.md         # 13 针 GPIO：原生及交叉编译产物实测通过
│   ├── 20261021_workspace_setup.md     # 程序目录与 WindTerm WSL 启动目录
│   ├── 20261021_ai_workflow.md         # AI 自动编译、上传与板端执行验证
│   ├── 20261022_preempt_rt.md          # 实时内核统一记录：RT 启动与短测已完成，镜像和备份见 07_backups
│   ├── 20261022_system_architecture.md # 工程整体设备架构、原图与接口职责
│   ├── 20261022_qt_environment.md      # Qt 编译、Windows / RK 调试与圈注操作记录
│   └── 20261022_qt_environment.json    # 同一 Qt 事项的结构化实测数据
├── 03_photos/                           # 所有任务的原始图片，统一直接存放
├── 04_references/                       # 原始参考资料
├── 05_tools/                            # 工具和可复用测试程序，按用途分目录
│   ├── flash/                           # RKDevTool 工具与安装包仅本地保存
│   │   └── configs/                     # 分区配置的本地工具副本，正式归档见 07_backups
│   ├── drivers/                         # DriverAssitant、PL2303 驱动，仅本地保存
│   ├── terminal/                        # MobaXterm、WindTerm 安装文件，仅本地保存
│   ├── virtualization/                  # VMware 安装包，仅本地保存
│   └── tests/                           # 接口 C 源码、RT 测试脚本与本机作图程序
│       ├── gpio/gpio_output.c            # 原生及交叉编译；13 针高低电平实测通过
│       ├── uart/uart4_loopback.c         # UART4 回环，C 版待板端编译与实测
│       └── preempt_rt/                   # 实际测试脚本原稿、本机作图程序与校验清单
├── 06_programs/                         # 正式应用与构建部署入口
│   ├── .gitignore                       # 防止误提交本地编译产物
│   ├── rk.sh                            # WSL 构建、检查、上传与运行
│   ├── workflow_demo/                   # 已在 RK 执行通过的 C 工作流示例
│   └── qt_demo/                         # Qt 交叉编译、HDMI 显示与 USB 键鼠验证及启动入口
├── 07_backups/                          # 安装镜像与必要回退资料，随 Git / Git LFS 保存
│   ├── preempt_rt/                       # RT 镜像、原内核备份、配置、分区 cfg 与 SHA-256
│   ├── home_path/                        # 主目录登记变更的必要回退信息与脚本
│   ├── root_session/                     # WindTerm 自动进入 root 的最小配置回退资料
│   └── qt_environment/                   # Qt ARM64 构建输入、来源、校验与使用范围
└── temp/                                # 本地一次性测试脚本、原始输出与中间文件
```

## 程序写在哪里

本机后续开发统一使用当前 Windows 项目目录 `C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux`，WSL 对应根目录为 `/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux`。当前 WindTerm 的 `wsl` 会话使用 Ubuntu 用户 `qyq`，默认进入该项目下的 `06_programs` 程序目录。

**Qt 后续编译位置已明确：Windows 编码与本机调试，同一台电脑的 WSL 从共用源码交叉编译 RK 版本，再上传 RK 直接运行与验收，全部编译在电脑完成。** 当前 Qt Creator Desktop Kit 生成 Windows EXE；RK 版本由电脑 WSL 生成 AArch64 Linux 程序。板端特有问题才按需远程调试，详见 [Qt 记录](02_records/20261022_qt_environment.md)。

| 内容 | 保存位置 |
| --- | --- |
| 正式业务程序、算法、Qt 应用的源码与构建文件 | `06_programs/<program>/`，开始编写该程序时再建子目录 |
| GPIO、UART 等独立接口测试源码 | 沿用 `05_tools/tests/<interface>/` |
| 可重新生成的可执行文件、编译中间文件、临时日志 | `temp/<program>/` |
| 需要长期保存的验证记录与图片 | 分别放 `02_records/`、`03_photos/` |
| 安装、烧录及回退所需的镜像、匹配模块、配置和已有备份 | `07_backups/<事项>/`，随 Git / Git LFS 保存 |

例如开始编写调度程序时可建立 `06_programs/controller/`；算法和 Qt 程序按实际内容命名，不提前建立空项目。Windows 编辑器和 WSL 使用同一份文件；之前 GPIO 实验的 `~/rk3576-linux-dev` 是历史构建目录，后续以当前项目为准。具体配置与验证见[工作目录设置记录](02_records/20261021_workspace_setup.md)。

**AI 自动开发工作流已实际跑通：**在 `06_programs/<program>/` 写源码 → WSL ARM64 GCC 编译 → 检查架构 → SSH/SCP 上传至 `/home/rkshare/programs/<program>/` → 核对文件和运行库 → 板端执行并返回输出、退出码。产物和日志仍放本机 `temp/<program>/`。在 WindTerm 的 WSL `06_programs` 目录执行：

```bash
bash rk.sh workflow_demo all -- 12 30
```

示例在 RK 实际返回 `ARCH=aarch64`、`12 + 30 = 42`、退出码 0。以后可直接要求 AI“在 06_programs 下写 xxx，按工作流编译、部署并运行”；项目约定保存在 [AGENTS.md](AGENTS.md)，分步命令、调整部署目录及验证范围见[工作流记录](02_records/20261021_ai_workflow.md)。

## 已完成的事情

| 日期 | 事情 | 结果与入口 |
| --- | --- | --- |
| 2026-10-21 | 固定 IP 地址及 Windows SSH 访问 | [打开完整记录](02_records/20261021_固定IP地址.md)：板端 192.168.100.123/24，重启保留，ping 4/4 回复，SSH 登录及远程命令验证通过 |
| 2026-10-21 | TTL 串口测试与调试验证 | [打开完整记录](02_records/20261021_TTL串口调试验证.md)：CH340 / COM18 调试控制台可交互；UART4 `/dev/ttyS4` 的回环与重启复测通过，收发各 1600 字节、100/100 匹配、PASS |
| 2026-10-21 / 22 | Samba 共享与主目录统一 | [打开记录](02_records/20261021_Samba共享文件夹.md)：共享位于 `/home/rkshare`，此前 Windows 读写由用户确认；账号主目录已登记为 `/home/rkshare/lckfb`，旧链接已移除，独立 SSH 与板端文件读写核查通过，配置和密钥保持不变 |
| 2026-10-21 | 单引脚 GPIO 输出与万用表实测 | [打开记录](02_records/20261021_gpio_output.md)：物理 13 针 GPIO2_C6，C 程序板端编译运行，低 → 高 → 低实测 0 V → 3.28 V → 0 V（用户现场读数） |
| 2026-10-21 | RK Linux 应用程序交叉编译与部署 | [同一 GPIO 记录第 5 节](02_records/20261021_gpio_output.md#5-wsl-交叉编译与部署补充2026-10-21)：WSL 构建 ARM64 程序，免密上传并在 RK 直接运行；用户确认功能验证成功，本次低、高电平实测 0 V / 3.28 V |
| 2026-10-21 / 22 | 程序目录与 WindTerm 会话 | [设置与升级记录](02_records/20261021_workspace_setup.md)：正式应用入口为 `06_programs/`；WindTerm 2.7.0 的 WSL 默认进入该目录，普通权限启动已验证；RK `lckfb` 会话已设为连接后自动进入 root，保存配置与板端交互提权已核验，首次 GUI 连接待打开确认 |
| 2026-10-21 | AI 编写、编译、部署与运行工作流 | [实测记录](02_records/20261021_ai_workflow.md)：C 示例由 WSL 交叉编译后自动上传并在 RK 执行，输出 `12 + 30 = 42`；构建失败阻止上传和运行错误码回收已验证 |
| 2026-10-22 | PREEMPT_RT 构建、启动、短测与文件归档 | [统一记录](02_records/20261022_preempt_rt.md)：`6.1.99-rt36-tspi` 已启动，原始测试脚本和对比图已归档；[镜像、回退资料与测试工具](07_backups/preempt_rt/)共 36 份原始文件，SHA-256 一致；外设、长测与恢复待验收 |
| 2026-10-22 | Qt 工程、HDMI 显示与 Windows / RK 调试 | [打开统一记录](02_records/20261022_qt_environment.md)：圈注说明本机 Qt Creator 4.11.1 / Qt 5.14.2 的工程配置、Ctrl+B、F5 / F10、变量和输出保存；RK Qt 5.15.13 的 HDMI / USB 键鼠现场验收及 WSL→RK GDB 链路通过。含反汇编故障修复、现有脚本、证据位置和下一步；图形化 RK 远程 Kit、业务界面、配套屏与自启待做 |

复现某件事时，打开表格中对应的一份记录即可，环境、命令、排查和验证证据都放在一起。

**工程整体架构已归档：**见[设备架构记录](02_records/20261022_system_architecture.md)及[用户原图](03_photos/20261022_system_architecture_01.png)。PC 仅负责人机交互，经 TCP/IP 连接负责通信和控制的 Linux 工控板；工控板通过 CAN 连接 8 张轴卡、RS485 连接数据采集、RS232 连接条码扫描，另有 Input 模块待细化。此处记录设计关系，实际接口功能按需验证。

2026-10-22 已清理根目录空临时文件、重复副本及过期中间文件，保留当前开发依赖和原始验证依据；范围见[工作目录记录第 9 节](02_records/20261021_workspace_setup.md#9-项目临时文件清理2026-10-22)。

## 下一步与后续计划

[开发计划](01_docs/plans/development_plan.md)按**已完成、当前需要做、未完成即将执行、将来需要做**四部分维护。当前优先推进 **PREEMPT_RT 实时内核验证**：板端已运行 `6.1.99-rt36-tspi`，镜像、配置及 155 个模块核对通过。60 秒短测中，普通 → RT 的最大唤醒延迟为空载 **376 → 267 μs**、CPU 压力下 **128 → 143 μs**，不能据单轮数据认定全面改善。[原始测试脚本](02_records/20261022_preempt_rt.md#2-测试方法与原文件)、[结果对比图](02_records/20261022_preempt_rt.md#3-结果对比)和[镜像及回退范围](02_records/20261022_preempt_rt.md#5-镜像烧录与回退)已集中整理。正式镜像、配置、必要备份与当次测试工具包位于 [07_backups/preempt_rt/](07_backups/preempt_rt/)，随 Git / Git LFS 保存。**共享与外设复测、重复长测、额外重启和恢复仍待验收。** WSL 到原 VM、RK 的免密 SSH 已通过；WindTerm [`rk3576-vm` 会话](02_records/20261021_workspace_setup.md#7-新增原-vm-的免密连接会话2026-10-22)通过 WSL 进入 SDK。

后续先明确凝血分析仪的业务流程与接口，再分别推进下位机调度、算法和 Qt 外接屏幕的最小验证。串口、GPIO、CAN、PWM、SPI、I²C 在开发用到时逐项验证；所需环境稳定后，开展[开发环境备份与新板快速部署](01_docs/plans/development_plan.md#开发环境备份镜像制作与新板快速部署)。当前任务展开前提、下一步和验收，详细命令与实测过程继续放在各事项记录中。

## 资料与工具

- [Linux 基础知识](01_docs/knowledge/linux_basics.md)统一整理文件接口、GPIO 输入输出、设备树和 UART；第 7 节补充 WSL Ubuntu、用户主目录和命令执行位置，第 8 节用逻辑图说明编写、交叉编译、传输与 RK 运行，并介绍依赖、产物检查及板端部署，附官方学习链接与用途说明。
- [立创泰山派 3M 官方文档](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/)是板级资料主入口；各事项使用到的具体来源写在对应记录或开发计划中。
- [40Pin 工程资源分配表](04_references/RK3576_40Pin工程资源分配表.jpg)是用户提供的后续工程规划；图中的候选复用功能需在实施前选定并验证。
- [资料网站汇总表](04_references/RK3576_立创_野火_资料网站汇总.xlsx)保留在 `04_references/`。芯片手册 `Rockchip RK3576 TRM V1.2 Part1.pdf` 仅本地保存，不包含在 Git 仓库中。
- `05_tools/` 按用途分类：`flash/` 放 RKDevTool，`drivers/` 放 DriverAssitant 和 PL2303 驱动，`terminal/` 放 MobaXterm 和 WindTerm，`virtualization/` 放 VMware；这些安装工具与解压目录仅本地保存。官方 [Linux 分区配置](07_backups/preempt_rt/flash/TaishanPi-3M-RK3576_Linux.cfg)随正式备份归档，`flash/configs/` 保留工具副本；使用方法见[实时内核记录第 5 节](02_records/20261022_preempt_rt.md#5-镜像烧录与回退)。RT 实际测试原稿与作图程序保存在 [tests/preempt_rt/](05_tools/tests/preempt_rt/)，可从已归档原始结果重新出图。WindTerm 升级验证见[统一记录第 4 节](02_records/20261021_workspace_setup.md#4-windterm-升级到-270)。
- [UART4 回环测试程序（C）](05_tools/tests/uart/uart4_loopback.c)纳入版本管理，GCC 编译及运行方法见 TTL 串口调试验证记录；C 版的板端编译与实测待完成。
- [GPIO 输出测试程序（C）](05_tools/tests/gpio/gpio_output.c)使用 libgpiod 1.x，手动设置单个引脚的高、低电平并持续保持；板端原生编译和 WSL 交叉编译两种方式均已完成 13 针功能验证，见[统一记录](02_records/20261021_gpio_output.md)。
- 当前仓库以操作记录为主，已有 `06_programs/rk.sh` 构建部署入口与 C 示例；SDK、固件源码及具体业务程序按任务准备。
- [WindTerm 查看 RT 运行输出](02_records/20261022_preempt_rt.md#7-windterm-查看-rt-运行输出2026-10-22)提供现有 cyclictest 的 20 秒动态统计命令，第 7.1 节补充用户 RK3568 截图对应的 5 线程与一次性 hackbench 命令；板端执行和 SSH 输出已验证，物理串口显示仍待接回 CH340 后现场核对。
- [用户 RK3576 截图分析与调优顺序](02_records/20261022_preempt_rt.md#72-用户-rk3576-实测截图解读与后续调优)解释参数、约 273 秒窗口与 RK3568 参考图的比较限制，并记录已核查的大小核、`schedutil` 调频及跟踪支持；后续先统一基准，再做可恢复的单项调优，尚未更改运行设置。

## 记录方式

- 操作前先核查对已有配置环境的影响，再决定是否备份：无影响不额外备份；会修改或覆盖的，只备份受影响且恢复所需的部分。方案扩大时重新核查，配置保留与功能复测分别说明。
- 实验必须能看到并核对实际结果；通信和回环显示具体 TX/RX 数据，GPIO 等显示电平变化或测量证据，不再安排只输出统计和 PASS/FAIL 的回环任务。
- 后续自行开发的 RK Linux 应用统一优先在 WSL 交叉编译，再上传 RK；接口测试使用 C / GCC，C++ 算法及 Qt 按需求使用对应 AArch64 G++ 和目标开发环境。历史板端原生编译结果保留，记录中明确源码、依赖、编译、上传、运行与各阶段验证范围。
- 一级目录使用连续两位编号加用途名，当前为 `01_docs` 至 `07_backups`；调整目录后同步更新导航与链接。
- 文件和文件夹优先用简单、易懂的英文命名，名称直接说明用途。
- `05_tools/` 中安装工具按用途分目录，可复用测试源码放在 `tests/` 并按实验分类；一次性脚本、编译产物、日志和中间结果放在根目录 `temp/`，新增临时内容按任务分子目录，不与工具或正式源码混放。
- 正式应用源码与构建文件放在 `06_programs/<program>/`；已有 `workflow_demo` 验证示例和 `rk.sh` 工作流入口，其他程序开始开发时再建子目录。
- 计划统一维护在 `01_docs/plans/development_plan.md`，相关支持知识整合在 `01_docs/knowledge/linux_basics.md`；实测记录仍放在 `02_records/`，不为未完成的事情预建记录。
- 开发计划按四部分维护，保留勾选框，依据任务依赖与用户优先级排序；完成项保留关键结果和记录，当前任务写清前提、下一步、关键子任务与验收，远期任务保留范围和启动条件，不按字数一刀切压缩。
- 每件完成的事情整理为 `02_records/YYYYMMDD_事情名称.md`，记录目标、环境、步骤、命令、验证结果、问题及图片。相同事情的后续补充继续更新原文件。
- README 只维护总框架和事项入口；不再另拆全局板卡表、开发日志、测试摘要或多个 README。
- 图片按“日期_图片内容_序号”命名，原格式统一放在 `03_photos/`，不同任务共用，不建立任务子目录。
- 安装、烧录与回退必需的镜像、模块、配置及已有备份放根目录 `07_backups/<事项>/`，随 Git 保存，二进制镜像和压缩备份用 Git LFS；保留 SHA-256、来源及恢复范围，不能只放 `temp/` 或项目外。
- 一次性脚本和中间文件需要时再建 `temp/`；记录及归档中不保存密码、私钥等凭据。

## 获取项目

仓库：[qyq11/rk3576-linux-dev](https://github.com/qyq11/rk3576-linux-dev)。安装 Git 和 Git LFS 后，在其他电脑先初始化 LFS：

```powershell
git lfs install
```

下载项目：

```powershell
git clone https://github.com/qyq11/rk3576-linux-dev.git
```

克隆后拉取镜像与备份的实际内容：

```powershell
git -C rk3576-linux-dev lfs pull
```

克隆后先读本 README。原始图片、资料汇总表、可复用测试程序和 `07_backups/` 正式归档纳入版本管理；本地工具安装包、解压目录和芯片 PDF 按 `.gitignore` 排除，按需从官方资料入口取得。
