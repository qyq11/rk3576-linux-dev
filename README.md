# RK3576 Linux 开发记录

记录立创泰山派 3M RK3576 的 Linux 开发过程。**从本 README 进入，每完成一件事，只保留一份能够独立复现的 Markdown 记录。**

更新日期：2026-10-21。当前运行 Ubuntu 24.04 系列系统、Linux 6.1.99 / aarch64。固定 IP、有线 SSH、三针 TTL 调试控制台及 UART4 本地回环与重启复测已通过；Samba 共享已迁移至 `/home/rkshare`，Windows 可见 `gpio`、`lckfb` 且读写正常（用户确认）；原路径重启复测通过，新路径重启复测待补。当前按工程资源表推进 UART 与 Windows 的实际收发、单引脚 GPIO 输出和输入事件；CAN 回环、实时补丁保留，PWM、SPI、I²C 列为待确认外设的后续扩展。

## 项目结构

```text
项目根目录/
├── README.md                         # 总入口：框架、进度和事项导航
├── 01_docs/
│   ├── plans/                         # 待办、优先级和完成标准
│   │   └── development_plan.md
│   └── knowledge/                     # 原理、入门说明与操作知识
│       └── uart_basics.md
├── 02_records/
│   ├── 20261021_固定IP地址.md          # 固定 IP、重启保留和有线 SSH
│   ├── 20261021_TTL串口调试验证.md     # 调试口、UART4 回环及重启复测统一记录
│   └── 20261021_Samba共享文件夹.md     # 已迁移至 /home/rkshare，Windows 读写通过
├── 03_photos/                           # 所有任务的原始图片，统一直接存放
├── 04_references/                       # 原始参考资料
├── 05_tools/                            # 可复用测试程序；安装包和解压目录仅本地保存
└── temp/                                # 本地一次性测试脚本、原始输出与中间文件
```

## 已完成的事情

| 日期 | 事情 | 结果与入口 |
| --- | --- | --- |
| 2026-10-21 | 固定 IP 地址及 Windows SSH 访问 | [打开完整记录](02_records/20261021_固定IP地址.md)：板端 192.168.100.123/24，重启保留，ping 4/4 回复，SSH 登录及远程命令验证通过 |
| 2026-10-21 | TTL 串口测试与调试验证 | [打开完整记录](02_records/20261021_TTL串口调试验证.md)：CH340 / COM18 调试控制台可交互；UART4 `/dev/ttyS4` 的回环与重启复测通过，收发各 1600 字节、100/100 匹配、PASS |
| 2026-10-21 | Samba 共享与双向文件传输 | [打开记录](02_records/20261021_Samba共享文件夹.md)：已迁移至 `/home/rkshare`，Windows 可见 `gpio`、`lckfb` 且读写正常（用户确认）；新路径重启复测待补 |

复现某件事时，打开表格中对应的一份记录即可，环境、命令、排查和验证证据都放在一起。

**待补验收：**Samba 新路径 `/home/rkshare` 重启后的重新访问与读写，继续追加到同一份记录。

## 下一步与后续计划

下一步用 CH340 等模块验证“Windows 发、RK 收”和“RK 发、Windows 收”，两端显示并核对实际数据；GPIO 输出用万用表测高低电平，GPIO 输入捕获外部电平变化并打印。CAN 回环继续保留，后续做 PREEMPT_RT、Samba 新路径重启复测，以及根据资源表推测的 PWM、SPI、I²C 外设工作。具体待办、复用限制与完成标准统一见 [开发计划](01_docs/plans/development_plan.md)。

## 资料与工具

- [串口 TTL 入门知识](01_docs/knowledge/uart_basics.md)放在支持知识目录，与开发计划分类维护。
- [立创泰山派 3M 官方文档](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/)是板级资料主入口；各事项使用到的具体来源写在对应记录或开发计划中。
- [40Pin 工程资源分配表](04_references/RK3576_40Pin工程资源分配表.jpg)是用户提供的后续工程规划；图中的候选复用功能需在实施前选定并验证。
- [资料网站汇总表](04_references/RK3576_立创_野火_资料网站汇总.xlsx)保留在 `04_references/`。芯片手册 `Rockchip RK3576 TRM V1.2 Part1.pdf` 仅本地保存，不包含在 Git 仓库中。
- `05_tools/` 本地已有 RKDevTool v3.37、DriverAssitant v5.14 和 VMware Workstation 25H2 安装包；版本来自文件名及附带说明，安装与实际使用情况另行验证。工具包及解压目录不上传 Git。
- [UART4 回环测试程序（C）](05_tools/uart4_loopback.c)纳入版本管理，GCC 编译及运行方法见 TTL 串口调试验证记录；C 版的板端编译与实测待完成。
- 当前仓库以操作记录为主，尚无 SDK、固件源码或项目构建脚本；需要时按具体任务准备。

## 记录方式

- 实验必须能看到并核对实际结果；通信和回环显示具体 TX/RX 数据，GPIO 等显示电平变化或测量证据，不再安排只输出统计和 PASS/FAIL 的回环任务。
- RK 板端程序使用 C，默认优先用 GCC 编译；交叉编译使用 AArch64 Linux GCC 工具链，记录源码、编译、上传和运行命令。
- 一级目录使用连续两位编号加用途名，当前为 `01_docs` 至 `05_tools`；调整目录后同步更新导航与链接。
- 文件和文件夹优先用简单、易懂的英文命名，名称直接说明用途。
- 计划统一维护在 `01_docs/plans/development_plan.md`，支持知识放在 `01_docs/knowledge/`；实测记录仍放在 `02_records/`，不为未完成的事情预建记录。
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
