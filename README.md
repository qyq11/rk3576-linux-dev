# RK3576 Linux 开发记录

记录立创泰山派 3M RK3576 的 Linux 开发过程。**从本 README 进入，每完成一件事，只保留一份能够独立复现的 Markdown 记录。**

更新日期：2026-10-21。当前板端运行 Ubuntu 24.04 系列系统、Linux 6.1.99 / aarch64。固定 IP、SSH、TTL 调试口、UART4 本地回环和 Samba 共享迁移已通过；GPIO 13 针的板端原生编译版本已实测 0 V → 3.28 V → 0 V。同一程序的 WSL ARM64 交叉编译、免密上传和 RK 直接运行也已验证成功，用户反馈交叉编译产物实测低电平 0 V、高电平 3.28 V。后续自行开发的 RK Linux 应用沿用交叉编译与部署方式，并推进凝血分析仪下位机调度、算法及外接屏幕 Qt 上位机；接口验证和实时补丁按实际需求继续安排。

## 项目结构

```text
项目根目录/
├── README.md                         # 总入口：框架、进度和事项导航
├── 01_docs/
│   ├── plans/                         # 待办、优先级和完成标准
│   │   └── development_plan.md
│   └── knowledge/                     # 原理、入门说明与操作知识
│       └── linux_basics.md             # 文件接口、GPIO、设备树、UART、WSL 与交叉编译
├── 02_records/
│   ├── 20261021_固定IP地址.md          # 固定 IP、重启保留和有线 SSH
│   ├── 20261021_TTL串口调试验证.md     # 调试口、UART4 回环及重启复测统一记录
│   ├── 20261021_Samba共享文件夹.md     # 已迁移至 /home/rkshare，Windows 读写通过
│   └── 20261021_gpio_output.md         # 13 针 GPIO：原生及交叉编译产物实测通过
├── 03_photos/                           # 所有任务的原始图片，统一直接存放
├── 04_references/                       # 原始参考资料
├── 05_tools/                            # 工具和可复用测试程序，按用途分目录
│   ├── flash/                           # RKDevTool 烧录工具及安装包，仅本地保存
│   ├── drivers/                         # DriverAssitant、PL2303 驱动，仅本地保存
│   ├── terminal/                        # MobaXterm 安装文件，仅本地保存
│   ├── virtualization/                  # VMware 安装包，仅本地保存
│   └── tests/                           # 可复用 C 测试源码，纳入版本管理
│       ├── gpio/gpio_output.c            # 原生及交叉编译；13 针高低电平实测通过
│       └── uart/uart4_loopback.c         # UART4 回环，C 版待板端编译与实测
└── temp/                                # 本地一次性测试脚本、原始输出与中间文件
```

## 已完成的事情

| 日期 | 事情 | 结果与入口 |
| --- | --- | --- |
| 2026-10-21 | 固定 IP 地址及 Windows SSH 访问 | [打开完整记录](02_records/20261021_固定IP地址.md)：板端 192.168.100.123/24，重启保留，ping 4/4 回复，SSH 登录及远程命令验证通过 |
| 2026-10-21 | TTL 串口测试与调试验证 | [打开完整记录](02_records/20261021_TTL串口调试验证.md)：CH340 / COM18 调试控制台可交互；UART4 `/dev/ttyS4` 的回环与重启复测通过，收发各 1600 字节、100/100 匹配、PASS |
| 2026-10-21 | Samba 共享与双向文件传输 | [打开记录](02_records/20261021_Samba共享文件夹.md)：已迁移至 `/home/rkshare`，Windows 可见 `gpio`、`lckfb` 且读写正常（用户确认）；迁移事项已结项 |
| 2026-10-21 | 单引脚 GPIO 输出与万用表实测 | [打开记录](02_records/20261021_gpio_output.md)：物理 13 针 GPIO2_C6，C 程序板端编译运行，低 → 高 → 低实测 0 V → 3.28 V → 0 V（用户现场读数） |
| 2026-10-21 | RK Linux 应用程序交叉编译与部署 | [同一 GPIO 记录第 5 节](02_records/20261021_gpio_output.md#5-wsl-交叉编译与部署补充2026-10-21)：WSL 构建 ARM64 程序，免密上传并在 RK 直接运行；用户确认功能验证成功，本次低、高电平实测 0 V / 3.28 V |

复现某件事时，打开表格中对应的一份记录即可，环境、命令、排查和验证证据都放在一起。

## 下一步与后续计划

**RK Linux 应用程序交叉编译与部署已通过 GPIO 示例验证。** WSL 2 Ubuntu 24.04 / x86_64、交叉 GCC 13.3.0、目标依赖和 SSH/SCP 免密访问已就绪；程序交叉编译并上传至 `/home/lckfb/gpio_output_cross` 后，RK 依赖检查、启动和实际 GPIO 功能验证均已通过，用户反馈本次低电平 0 V、高电平 3.28 V。[复现命令与验证结果](02_records/20261021_gpio_output.md)。

后续已确认三个大型项目方向：**凝血分析仪下位机调度、算法交叉编译与运行、RK 外接屏幕运行 Qt 上位机**。RK 应用统一在 WSL 交叉编译，部署后分别验证；C++ 算法和 Qt 另行准备对应的 G++、目标库及运行环境。UART 实际收发、GPIO 输入、CAN、PREEMPT_RT 及 PWM/SPI/I²C 继续按需求推进。待办和完成标准统一见 [开发计划](01_docs/plans/development_plan.md)。

## 资料与工具

- [Linux 基础知识](01_docs/knowledge/linux_basics.md)统一整理文件接口、GPIO 输入输出、设备树和 UART；第 7 节补充 WSL Ubuntu、用户主目录和命令执行位置，第 8 节说明 ARM64 交叉编译、依赖、产物检查及板端部署，附官方学习链接与用途说明。
- [立创泰山派 3M 官方文档](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/)是板级资料主入口；各事项使用到的具体来源写在对应记录或开发计划中。
- [40Pin 工程资源分配表](04_references/RK3576_40Pin工程资源分配表.jpg)是用户提供的后续工程规划；图中的候选复用功能需在实施前选定并验证。
- [资料网站汇总表](04_references/RK3576_立创_野火_资料网站汇总.xlsx)保留在 `04_references/`。芯片手册 `Rockchip RK3576 TRM V1.2 Part1.pdf` 仅本地保存，不包含在 Git 仓库中。
- `05_tools/` 按用途分类：`flash/` 放 RKDevTool，`drivers/` 放 DriverAssitant 和 PL2303 驱动，`terminal/` 放 MobaXterm，`virtualization/` 放 VMware；安装包及其解压目录均仅本地保存，不上传 Git。版本来自文件名及附带说明，安装与实际使用情况另行验证。
- [UART4 回环测试程序（C）](05_tools/tests/uart/uart4_loopback.c)纳入版本管理，GCC 编译及运行方法见 TTL 串口调试验证记录；C 版的板端编译与实测待完成。
- [GPIO 输出测试程序（C）](05_tools/tests/gpio/gpio_output.c)使用 libgpiod 1.x，手动设置单个引脚的高、低电平并持续保持；板端原生编译和 WSL 交叉编译两种方式均已完成 13 针功能验证，见[统一记录](02_records/20261021_gpio_output.md)。
- 当前仓库以操作记录为主，尚无 SDK、固件源码或项目构建脚本；需要时按具体任务准备。

## 记录方式

- 实验必须能看到并核对实际结果；通信和回环显示具体 TX/RX 数据，GPIO 等显示电平变化或测量证据，不再安排只输出统计和 PASS/FAIL 的回环任务。
- 后续自行开发的 RK Linux 应用统一优先在 WSL 交叉编译，再上传 RK；接口测试使用 C / GCC，C++ 算法及 Qt 按需求使用对应 AArch64 G++ 和目标开发环境。历史板端原生编译结果保留，记录中明确源码、依赖、编译、上传、运行与各阶段验证范围。
- 一级目录使用连续两位编号加用途名，当前为 `01_docs` 至 `05_tools`；调整目录后同步更新导航与链接。
- 文件和文件夹优先用简单、易懂的英文命名，名称直接说明用途。
- `05_tools/` 中安装工具按用途分目录，可复用测试源码放在 `tests/` 并按实验分类；一次性脚本、编译产物、日志和中间结果放在根目录 `temp/`，新增临时内容按任务分子目录，不与工具或正式源码混放。
- 计划统一维护在 `01_docs/plans/development_plan.md`，相关支持知识整合在 `01_docs/knowledge/linux_basics.md`；实测记录仍放在 `02_records/`，不为未完成的事情预建记录。
- 开发计划只保留待办、优先级、完成标准、判断和现有 Markdown 链接；操作说明放在相关笔记或事项记录中。
- 每件完成的事情整理为 `02_records/YYYYMMDD_事情名称.md`，记录目标、环境、步骤、命令、验证结果、问题及图片。相同事情的后续补充继续更新原文件。
- README 只维护总框架和事项入口；不再另拆全局板卡表、开发日志、测试摘要或多个 README。
- 图片按“日期_图片内容_序号”命名，原格式统一放在 `03_photos/`，不同任务共用，不建立任务子目录。
- 一次性脚本和中间文件需要时再建 `temp/`；记录中不保存密码、私钥等凭据。

## 获取项目

仓库：[qyq11/rk3576-linux-dev](https://github.com/qyq11/rk3576-linux-dev)。在其他电脑下载：

```powershell
git clone https://github.com/qyq11/rk3576-linux-dev.git
```

克隆后先读本 README。原始图片、资料汇总表和可复用测试程序纳入版本管理；本地工具安装包、解压目录和芯片 PDF 按 `.gitignore` 排除，按需从官方资料入口取得。
