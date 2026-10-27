# AI 编写、交叉编译、部署与运行工作流

2026-10-21｜Windows → WSL Ubuntu-24.04 → RK3576｜**全流程实际执行通过。**

AI 在当前 Windows 工程的 `06_programs/` 编写 C 源码，调用 WSL 的 AArch64 GCC 编译，经 SSH/SCP 自动复制到 RK 的指定目录，在该目录执行，回收输出和退出码。Windows 与 WSL 共用源码，不再复制到历史的 `~/rk3576-linux-dev`。

## 1. 位置与环境

| 内容 | 本次实际配置 |
| --- | --- |
| 工程根目录 | `C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux` |
| WSL | `Ubuntu-24.04`，用户 `qyq`，本机架构 `x86_64` |
| 编译器 | `aarch64-linux-gnu-gcc`，已有 GCC 13.3.0 |
| 板端 | `lckfb@192.168.100.123`，`aarch64`，免密 SSH 已复核 |
| 可复用入口 | [06_programs/rk.sh](../06_programs/rk.sh) |
| 示例源码与构建文件 | [main.c](../06_programs/workflow_demo/main.c)、[build.sh](../06_programs/workflow_demo/build.sh) |
| 本机编译产物 | `temp/workflow_demo/build/workflow_demo` |
| 本机逐次日志 | `temp/workflow_demo/logs/日期_时间_动作_进程号.log` |
| 板端可执行文件 | `/home/rkshare/programs/workflow_demo/workflow_demo` |
| 板端工作目录 | `/home/rkshare/programs/workflow_demo` |
| Windows 对应共享路径 | `\\192.168.100.123\rkshare\programs\workflow_demo` |

源码、脚本和本记录可纳入 Git；`temp/` 仍被忽略。`.gitattributes` 固定 `.sh` 为 LF 换行，便于 Windows 编辑后由 WSL 执行。工作流仅使用已有工具，本次未安装新软件。

本次 AI 工具进程尝试直接通过 Windows UNC 读取该路径时，被 Windows 的 SMB 登录策略拒绝，未在该进程复核共享访问。上述部署和运行已通过 SSH/SCP 实际完成，不依赖当前进程的 Samba 登录状态；此前用户确认的共享读写结果保留在原 Samba 记录中。

## 2. 一条命令完成

在 WindTerm 的 **wsl 会话、当前 `06_programs` 目录**执行：

```bash
bash rk.sh workflow_demo all -- 12 30
```

`all` 可省略；`--` 后的参数传给板端程序。该命令的实际流程如下。

| 阶段 | 脚本实际动作 |
| --- | --- |
| 编译 | 调用该程序的 `build.sh`，生成临时产物，检查 ELF 为 AArch64 后保留正式产物 |
| 连接 | 免密 SSH 登录，核对目标架构为 `aarch64` |
| 上传 | 在板端程序目录生成暂存文件，通过 SCP 上传 |
| 检查并安装 | 核对两端 SHA-256；动态程序执行 `ldd` 检查；检查通过才替换该目录下的可执行文件 |
| 执行 | 进入该程序的部署目录，以 `lckfb` 身份执行，默认运行上限 30 秒 |
| 返回结果 | 显示真实输出、退出码，保存整次运行日志；失败时退出码非零 |

本次全流程命令的实际输出摘录：

```text
[connect] lckfb@192.168.100.123
user=lckfb
machine=aarch64
[remote SHA-256] c827faac640bd53628456a5b38bd17469222ab1bf7c1d3188f5a2a2f24828792
[deployed] /home/rkshare/programs/workflow_demo/workflow_demo
[run] cwd=/home/rkshare/programs/workflow_demo timeout=30s
[args] 12 30
Hello from the RK3576 AI workflow!
HOST=localhost
ARCH=aarch64
CWD=/home/rkshare/programs/workflow_demo
INPUT left=12 right=30
OUTPUT 12 + 30 = 42
[exit] 0
```

`HOST=localhost` 是板卡实际配置的主机名。WSL 的本机架构为 `x86_64`；上述 SSH 目标、`ARCH=aarch64` 和板端路径共同确认程序在 RK 执行。`ldd` 实际解析到板端的 `libc.so.6` 和 `/lib/ld-linux-aarch64.so.1`，无缺失库。本次依据为真实命令输出，未采集新的终端照片，也未进行硬件引脚测试。

## 3. 分步使用与调整目录

只编译检查，完全不连接板卡：

```bash
bash rk.sh workflow_demo build
```

重新编译并上传，暂不运行：

```bash
bash rk.sh workflow_demo deploy
```

只运行板端已经部署的版本，本条不重新编译或上传：

```bash
bash rk.sh workflow_demo run -- -7 20
```

本次实际返回 `INPUT left=-7 right=20`、`OUTPUT -7 + 20 = 13`，退出码为 0。

如后续指定其他部署根目录，使用下面的配置形式；程序最终路径为 `<RK_REMOTE_ROOT>/<程序名>/<程序名>`。此条为配置示例，本次未部署至示例路径：

```bash
RK_REMOTE_ROOT=/home/rkshare/apps bash rk.sh workflow_demo all -- 12 30
```

如任务需要较长运行时间，可设置秒数；`0` 表示不限制。下面也是配置示例：

```bash
RK_RUN_TIMEOUT=120 bash rk.sh workflow_demo run -- 12 30
```

SSH 目标可通过 `RK_HOST` 调整；当前默认是 `lckfb@192.168.100.123`。程序名使用小写字母、数字和下划线，以字母开头；部署路径使用不带空格的绝对路径。脚本不保存密码，也不修改 SSH 配置。

## 4. 故障验证

在已有成功部署的基础上，使用 `temp/ai_workflow/` 中的一次性验证脚本和编译器替身检查失败路径，没有修改正式 C 源码。

| 验证输入 | 本次实际结果 |
| --- | --- |
| 正常参数 `-7 20` | 板端输出 `-7 + 20 = 13`，退出码 0 |
| 非整数参数 `invalid 30` | 回收具体错误提示，工作流退出码 2 |
| 参数含字面量 `$(touch …)` | 按普通参数传递，程序返回 2；核实板端没有生成对应文件 |
| 编译器主动返回 42 | 工作流返回 42，未进入上传阶段 |
| 编译器替身输出 x86_64 ELF | AArch64 架构检查阻止上传，返回 1 |
| 上述两种构建失败之后 | 板端原可执行文件 SHA-256 均未改变 |
| 部署路径含 `..` | 参数校验拒绝执行，返回 1 |
| `build` 单独执行 | 编译成功、返回 0，没有连接板卡 |

最后已恢复使用真实 AArch64 GCC 再次构建成功；板端保留成功部署的示例程序。临时故障验证材料可清理，正式程序和工作流入口继续保留。

## 5. 后续让 AI 使用

以后可以直接提出具体需求，例如：

> 在 06_programs 下写一个名为 xxx 的 C 程序，实现……，按现有工作流编译、部署到 RK 并运行，把实际输出给我。

根目录 [AGENTS.md](../AGENTS.md) 已保存项目执行约定；其用途依据 [OpenAI 官方 AGENTS.md 说明](https://learn.chatgpt.com/docs/agent-configuration/agents-md)。AI 按当前授权功能完成编写、编译、上传和执行；出现源码错误时修正后重试，最终报告实际结果。

新增程序时才创建 `06_programs/<program>/`，提供源码和 `build.sh`。构建脚本接收一个**绝对输出文件路径**，成功时在该路径生成 ARM64 可执行文件；可参照本次示例。示例中的实际 GCC 编译方式是：

```bash
aarch64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror workflow_demo/main.c -o ../temp/workflow_demo/build/workflow_demo
```

该命令从 `06_programs` 执行，输出目录已由前面的工作流建立。实际自动化入口会先编译到 `.new` 文件，验证后才更新本机产物；`all` 和 `deploy` 每次重新构建，编译失败不会上传旧版本。

**当前范围：**工作流部署单个可执行文件，前台执行并收集输出。它以普通用户运行，当前示例只读取系统信息并计算。后续涉及 GPIO、串口、CAN 时，按所需设备权限、接线和具体收发内容单独验证；Qt 的交叉工具链、目标库、显示环境及资源文件部署按实际项目补充。持续后台服务和开机自启也需按具体程序另行配置。

<a id="plain-language-communication"></a>
## 6. 2026-10-25：跨窗口的业务解释与进度沟通约定

用户明确表示尚不熟悉上位机业务，要求解释通俗明了，并将这一习惯长期保存，供以后各窗口使用。

- 先让用户明白功能有什么用、使用者怎么操作，再按问题说明目前做到哪里、还缺什么、下一步做什么及原因；必要时用具体操作或小例子，简单问题直接回答。
- 少用模块编号、英文缩写和专业术语；必须使用时先解释含义，编号配上能看懂的功能名称。
- 说“完成”时区分只完成编译、电脑离线测试通过、RK板端运行通过、真实仪器验证通过，避免将部分通过说成整个功能完成。

本次将默认沟通规则写入本机全局 `C:/Users/52400/.codex/AGENTS.md`，同步到个人 `C:/Users/52400/.codex/skills/work-habits/SKILL.md`；项目根 [AGENTS.md](../AGENTS.md)保留简短同义约定，[README](../README.md)提供本记录入口。仅更新沟通规则与记录，不改变移植计划、业务源码、数据或既有验收结论。

生效范围：全局约定供本机使用该Codex目录的新会话读取，项目约定随仓库保留；其他电脑/远程执行环境需读取项目约定或另行同步全局规则。已打开的旧会话若尚未读取更新，需要提醒其重新读取相关AGENTS，不宣称已立即同步到所有活动窗口。[OpenAI官方读取规则](https://learn.chatgpt.com/docs/agent-configuration/agents-md)说明全局/项目指令的加载顺序及每次启动重新构建指令链的方式。

核查：已回读全局/项目约定及技能正文，个人技能格式校验通过，相关文档差异的空白检查通过；保留已有未提交修改，未提交或推送Git。

<a id="rk-direct-verification"></a>
## 7. 2026-10-27：以后每项改动直接在RK实际验证

用户明确要求“不需要用电脑进行测试，直接在RK上去运行”，并确认将后续方式写入相关文件：“以后不用在Windows上进行测试，我们做一件事情之后，直接在RK上进行实际验证”。本约定适用于本RK3576项目后续开发、修复和配置变动。

- 电脑用于编辑、源码/文档检查、ARM64交叉编译和上传；不再运行Windows/WSL本机版本做功能、界面或资源测试，也不安排Windows测试构建，不把电脑测试作为上板前置。
- 每完成一项可操作的功能或修复，即生成新ARM64版本，上传并核对文件内容、运行组件，然后在RK执行实际操作、核对保存与重开、失败/取消及适用的硬件结果。多个窗口开发时，RK部署和操作由一个窗口统一安排。
- 编译通过不能保证RK能运行；RK启动通过不能代表按钮/保存业务通过；RK本地业务通过不能代替接真实仪器。记录实际输入、输出、数据变化、退出码和未验项。
- RK不可达时保留代码与新构建产物，明确“待RK验证”，不改用电脑运行来补结论。缺仪器连接参数只阻对应接机验证，纯页面配置仍可在RK核对。
- 原Windows功能/外观以只读源码、已有截图和用户提供的信息核对，不重新运行参考程序。历史电脑测试记录、原始输出和固定归档保持原范围，不改写成RK通过。

执行约定已写入根/工程AGENTS、项目技能、README、当前计划、工程指南、手动说明及个人`work-habits`技能；旧阶段的电脑测试步骤不作为新任务安排。此更新只确认后续执行方式，没有新增功能、RK运行或仪器通过结论；没有修改运行数据、系统环境或参考原件，不额外备份，不自动提交/推送Git。

<a id="all-files-plain-language"></a>
## 8. 2026-10-27：所有文件中的说明都使用通俗中文

用户在询问移植计划中的“核SHA/板端动态库及私有中文字体”后，确认所有说明都应像聊天里的解释一样简单易懂，并进一步明确“不仅是这份文件，对于所有的文件都是”。这条要求适用于聊天，也适用于README、计划、记录、操作指南、知识笔记和代码注释；以后新写或修改时都要遵守。

- 直接写清做什么、有什么用、使用者怎么操作，以及如何确认结果；不把一串缩写和检查数字当作解释。
- 必要术语在文件原处说明。例如“核SHA”写成“比较文件指纹，确认上传前后内容一致”；“检查动态库”写成“确认程序所需的运行组件齐全且能加载”；“私有中文字体”写成“程序自带、单独供本应用使用的中文字体”。
- 功能编号同时配中文名称，历史测试数字保留，并说明数字代表什么。代码已有、编译通过、RK实际操作通过、接真实仪器通过仍分别记录。
- 命令、路径、代码名称和原始输出保持准确。有文件校验清单的历史归档及参考原件保留原样，在当前维护的说明中补充解释。

要求已写入本机全局AGENTS、个人work-habits技能、项目根和SN100工程AGENTS；根README提供本节入口。当前项目的现有说明也按这套要求进行整理，具体修改和文档核查结果追加在[SN100移植记录](20261022_sn100_host_migration.md#plain-language-docs-20261027)。这次只改说明文字，不新增程序或硬件验证结论；保留其他窗口已有修改，不自动提交或推送Git。
