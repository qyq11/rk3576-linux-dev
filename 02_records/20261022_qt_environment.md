# Qt 工程、HDMI 运行与 Windows / RK 调试记录

2026-10-22｜泰山派 3M RK3576｜**HDMI / USB 键鼠现场验证、Windows Qt Creator 图形调试、WSL→RK 命令行远程调试均已通过。**

日常在 Windows 编译、查看界面和调试通用逻辑；涉及 Linux、真实设备时，用电脑上的 WSL 调试器连接 RK 的轻量 `gdbserver`。下面按当前这台电脑、当前 `qt_demo` 工程说明操作。配套[结构化实测数据](20261022_qt_environment.json)保留版本、哈希和历史验收依据；同一事项继续更新本文。

## 1. 工程与软件在哪里

工程根目录：`C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux`。下文项目内路径都相对此根目录。

| 内容 | 当前实际位置或版本 |
| --- | --- |
| 打开的工程文件 | [06_programs/qt_demo/qt_demo.pro](../06_programs/qt_demo/qt_demo.pro) |
| 两端共用的源码 | [app_main.cpp](../06_programs/qt_demo/app_main.cpp)；当前界面由 C++ 创建，没有 `.ui` 文件 |
| 本次使用的 IDE | **Qt Creator 4.11.1**，`C:\Qt5.14\Qt5.14.2\Tools\QtCreator\bin\qtcreator.exe` |
| Windows Qt / Kit | **Qt 5.14.2 / Desktop Qt 5.14.2 MinGW 64-bit** |
| Windows Qt 目录 | `C:\Qt5.14\Qt5.14.2\5.14.2\mingw73_64` |
| Windows 编译器 / 调试器 | MinGW GCC **7.3.0** / GDB **8.1**，目录 `C:\Qt5.14\Qt5.14.2\Tools\mingw730_64\bin` |
| RK 系统 / Qt | Ubuntu 24.04.5、aarch64、`6.1.99-rt36-tspi`；已有 **Qt 5.15.13** |
| RK 交叉编译器 | WSL `Ubuntu-24.04` / `qyq`，`aarch64-linux-gnu-g++ 13.3.0` |
| 本机工程设置 | `06_programs/qt_demo/qt_demo.pro.user`，机器相关文件，由 Git 忽略 |

**Qt Creator 的版本和 Qt 库的版本是两回事。** 本机 5.14.2 已实际编译通过，不必为打开这个 `.pro` 先换成板端版本。Windows 生成 x86-64 `.exe`；RK 使用匹配 5.15.13 头文件、链接库重新交叉编译为 AArch64 ELF。两端共用源码，分别构建；新增 API 时仍需检查两端兼容性。`RK_BOARD` 只在板端构建开启，用于隔离 EGLFS / KMS 等设置。

## 2. Windows：打开工程并编译

### 2.1 打开与选择构建配置

1. 启动上表所列 **Qt Creator 4.11.1**，用“文件 → 打开文件或项目”打开 `06_programs\qt_demo\qt_demo.pro`。
2. 初次配置时选择 **Desktop Qt 5.14.2 MinGW 64-bit**。已配置工程则点左侧扳手“项目”，在 **Build & Run** 下选择这个 Kit 的 **Build**。
3. 顶部选 **Debug**，构建目录设为下面的完整路径，再按 **Ctrl+B** 编译。

```text
C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux\temp\qt_demo\qtcreator_debug
```

![圈注：Kit、Debug、构建目录与编译按钮](../03_photos/20261022_qt_build_marked_01.png)

[完整原图](../03_photos/20261022_qt_build_01.png)｜[可编辑圈注 SVG](../03_photos/20261022_qt_build_marked_01.svg)。截图中的目录层级黄字是旧 IDE 的建议；本工程已从此目录实际构建并命中断点。

底部 **“4 编译输出”** 查看 qmake、g++、链接过程和错误；**“1 问题”** 用于定位报错文件与行号。构建结束无错误，且生成 `temp/qt_demo/qtcreator_debug/debug/qt_demo.exe`，才进入运行。修改 `.pro` 后可在“构建”菜单重新执行 qmake，再构建；普通 `.cpp` 修改直接 Ctrl+B。

本机 Release / Profile 已分别指向 `temp/qt_demo/qtcreator_release`、`temp/qt_demo/qtcreator_profile`；当前验证使用 Debug。

### 2.2 本工程的 Run 设置

在相同 Kit 下点 **Run**，核对下面三处。当前已保存，关闭并重开 IDE 后仍保留；换电脑或重建 `.pro.user` 时重新检查。

![圈注：关闭 Run in terminal，保留库目录加入 PATH，核对 Debug 可执行文件](../03_photos/20261022_qt_run_settings_marked_01.png)

[完整原图](../03_photos/20261022_qt_run_settings_01.png)｜[可编辑圈注 SVG](../03_photos/20261022_qt_run_settings_marked_01.svg)。**取消 `Run in terminal`；保留 `Add build library search path to PATH`。** 普通运行点左下绿色三角；调试使用带小虫的三角或 F5。

## 3. Windows：断点、单步、变量与调用栈

### 3.1 先在入口处验证调试链路

打开 `app_main.cpp`，找到当前第 **83 行** `QApplication app(argc, argv);`。光标放在该行按 **F9**，左侧出现红点；按 **F5** 启动调试。程序应停在源码中，显示黄色箭头、`main` 调用栈和参数。

![圈注：源码断点、argc、main 调用栈与 breakpoint 状态](../03_photos/20261022_qt_debug_marked_01.png)

[完整原图](../03_photos/20261022_qt_debug_01.png)｜[可编辑圈注 SVG](../03_photos/20261022_qt_debug_marked_01.svg)。本次 GUI 不带额外参数，`argc=1`；此前脚本带启动参数，`argc=7`，两者不矛盾。行号随以后源码修改可能变化，以语句为准。

| 操作 | 当前工程中怎样使用 |
| --- | --- |
| F9 | 在当前源码行添加 / 取消断点；调试前确认红点实际存在 |
| F5 | 尚未调试时启动；暂停时继续运行，直到下个断点或退出 |
| F10 | 执行当前行，通常不进入函数；本次从第 83 行到第 84 行已验证 |
| F11 | 需要追踪自己写的函数时进入函数；Qt / 系统库源码符号未完全配齐 |
| Locals / 局部变量 | 看当前作用域的值；展开 `argv` 等对象，等赋值执行后再读对应变量 |
| Stack / 调用栈 | 查看“谁调用了这里”；选择自己工程的帧，返回对应源码与局部变量 |
| Expressions | 添加需要持续观察的表达式，例如 `clicks`；仅在其可访问的作用域内解释结果 |
| Stop Debugger 红色停止按钮 | 结束调试；验证正常退出时，应继续运行后点击应用里的 EXIT |

视图不见时，在 Debug 模式的 **Views** 中勾选 **Locals、Stack、Breakpoints、Expressions**。入口处尚未执行初始化的变量可能显示随机值或“无法访问”，这不单独证明程序故障。

### 3.2 真正排查按钮逻辑时，断在回调里

入口断点只用于确认环境。下一次练习可在当前 **第 212 行** `++clicks; ++totalIncrements; ...` 设断点，F5 显示界面后点击“点击 +1”。在回调中查看 `clicks` / `totalIncrements`，F10 后确认从 0 变为 1，继续运行后界面计数与 `COUNTER` 日志一致。输入提交可在当前 **第 219 行** `lastSubmitted = input->text(); ...` 设断点，检查输入文本和 `submissions`。

**该回调断点练习是下一步任务；已通过的是入口断点、F10 和继续运行后的界面操作。** 暂停 Qt 主线程时，界面和计时器会一起停住；按 F5 后继续。Qt 事件循环 `app.exec()` 持续等待事件，排查交互时优先在相关回调设断点。

## 4. 怎样查看并保存相关信息

### 4.1 程序输出与状态文件一起看

按 F5 继续后，在测试窗口输入 `QT_DOC_OK`、按 Enter，再点击 EXIT。本次为补采文档图片，由电脑自动操作完成，显示如下。

![本机运行原图：Qt 5.14.2、windows 平台与 QT_DOC_OK 回显](../03_photos/20261022_qt_desktop_01.png)

点 Qt Creator 底部 **“3 应用程序输出”**，核对实际输入和退出原因。

![圈注：应用程序输出中的 SUBMIT、EXIT 和调试完成状态](../03_photos/20261022_qt_output_marked_01.png)

[完整原图](../03_photos/20261022_qt_output_01.png)｜[可编辑圈注 SVG](../03_photos/20261022_qt_output_marked_01.svg)。本图只截取输出区域，原图完整保留。实际摘录：

```text
2026-10-22T15:38:59.236 SUBMIT {"count":1,"text":"QT_DOC_OK"}
2026-10-22T15:39:06.279 EXIT code=0 reason=exit_button
15:39:07: Debugging has finished
```

| 信息 | 到哪里看、怎样保存 |
| --- | --- |
| 编译命令、错误、链接结果 | 底部“编译输出”；在输出文本中全选、复制，保存到 `temp/qt_demo/` 下的当次 `.log` |
| `START / READY / COUNTER / SUBMIT / EXIT` | 底部“应用程序输出”；保存实际输入、实际输出及退出码，不能只写“通过” |
| 本次界面状态 | 当前工作目录的 `state.json`；IDE 默认位于 `temp/qt_demo/qtcreator_debug/state.json` |
| 应用生成的界面图 | 同目录 `render.png`；是 QWidget 渲染图，区别于电脑窗口截图或 HDMI 实物照片 |
| IDE 自身调试问题 | Debug → Views → **Debugger Log**；看加载的 EXE、GDB、符号、信号；右键可保存日志 |
| 正式证据 | 关键输入输出摘入本文 / 配套 JSON，原图放 `03_photos/`；临时日志在 `temp/`，默认不随 Git 保存 |

本次 `state.json` 实际为 `platform=windows`、`qt_version=5.14.2`、`submissions=1`、`submitted_text=QT_DOC_OK`、`exit_reason=exit_button`；这次只验证输入提交，计数为 0。之前另一轮测试的计数为 1、提交文本为 `WIN_QTCREATOR_OK`，详见配套 JSON。`key_presses` / `mouse_presses` 是 Qt 事件分发计数，可能包含传播事件，不当作物理按键次数。

Debugger Log 用来排查调试器，应用程序输出用来看业务打印。保存或分享 Debugger Log 前检查内容：其中可能包含环境变量和进程数据，只保留必要片段。此处保存方式参考 [Qt 官方 Debugger Log 说明](https://doc.qt.io/qtcreator/creator-debugger-log-view.html)；界面操作以上述本机 4.11.1 实测为准。

### 4.2 留下一条能复现的问题记录

在同一记录补充：**时间与平台 → 源码版本 / SHA-256、Kit 和 Debug 配置 → 操作与输入 → 预期 → 实际输出、截图及栈帧 → 修改 → 同样输入的复测结果。** 不把编译成功、软件回显、板端执行和实物动作混成一次验收。

在 **Windows PowerShell、项目根目录**可查询当前源码标识：

```powershell
Get-FileHash -Algorithm SHA256 .\06_programs\qt_demo\app_main.cpp
```

本次入口调试使用的源码 SHA-256 为 `f498bda87931b8bae566e3e1e0fcbeff3b6086794e97799c084d63aeffe2476f`。需要复查 Windows Qt 工具版本时使用明确路径，避开本机 PATH 中 Anaconda 的 qmake：

```powershell
& 'C:\Qt5.14\Qt5.14.2\5.14.2\mingw73_64\bin\qmake.exe' -v
```

### 4.3 不依赖 IDE 的复查入口

仍在 **Windows PowerShell、项目根目录**，使用现有 [windows.ps1](../06_programs/qt_demo/windows.ps1) 编译并自动验证 GDB 断点、参数、回溯、单步和 10 秒退出：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\06_programs\qt_demo\windows.ps1 -Action debug-check
```

本入口使用独立的 `temp/qt_demo/windows_debug_x64/`，日志为 `debugger.log`；不是 IDE 的 `qtcreator_debug/`。其中应出现 `DEBUG_BREAKPOINT_REACHED`、`DEBUG_SINGLE_STEP_COMPLETE`、正常退出。只想运行测试窗口时，把脚本参数改为 `-Action run`，点击 EXIT 结束。

## 5. 本次遇到的反汇编 / ?? 问题

[修复前的用户原图](../03_photos/20261022_qt_debug_error_01.png)中，状态为 `signal-received`，调用栈是 `??`。进一步日志显示：终端模式执行 `attach`、`No symbol table is loaded`、64 位 DLL 与 `target architecture i386` 不兼容，随后停在 **SIGTRAP**。

本次 Qt 5.14.2、64 位 MinGW / GDB 和 EXE 的调试符号都已核对。**关闭本工程 Run 的 `Run in terminal` 后，同一 EXE 立即正常命中源码断点。** 移到新的 temp 构建目录、关闭重开 IDE 后也验证通过。这个结论限于本次旧 IDE 的启动问题，不将所有 SIGTRAP 都归因为这个开关。

以后出现相似问题，依次核对 Run 的 EXE 路径、Debug 构建、Kit / GDB 位数和 Debugger Log 中的具体信号；构建失败时先修复构建，避免运行旧 EXE。受影响的旧工程配置已单独备份，见 [windows_qtcreator/source.json](../07_backups/qt_environment/windows_qtcreator/source.json)，其中写明恢复方法和范围。

## 6. RK：交叉编译、HDMI 运行和轻量远程调试

### 6.1 在 HDMI 屏幕运行

下列命令均在 **WindTerm 的 wsl 会话 / Ubuntu-24.04 / qyq** 执行，不是在板端终端执行。先进入共用源码目录：

```bash
cd /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/06_programs
```

首次准备或清理 sysroot 后，校验并解包已归档的 Qt 5.15.13 构建输入：

```bash
bash qt_demo/prepare.sh
```

HDMI 和 USB 键鼠接好后，重新编译、检查 AArch64、上传、核对 SHA-256 / 动态库，再运行，最长 1800 秒：

```bash
bash qt_demo/run.sh start 1800
```

程序部署到 `/home/rkshare/programs/qt_demo/qt_demo`，以 `lckfb` 身份运行；临时启动器分配 tty7，应用退出后恢复 tty1，没有配置开机自启。本机编译产物是 `temp/qt_demo/build/qt_demo`，构建 / 部署日志在 `temp/qt_demo/logs/`。

查看本次运行状态与末尾日志：

```bash
bash qt_demo/run.sh status
```

优先点屏幕 EXIT 正常退出；需要远程停止时：

```bash
bash qt_demo/run.sh stop
```

退出后回收最新一次 **HDMI 启动记录**：

```bash
bash qt_demo/collect.sh
```

输出保存为 `temp/qt_demo/evidence/<本次编号>/` 的 `app.log`、`state.json`、`render.png`、`runtime.txt` 和已结束时的 `exit_code`。查看 `SUBMIT` 的真实文本、`EXIT` 的原因和代码；`runtime.txt` 包含内核、实时标志、活动终端等核对信息。`collect.sh` 不回收下面独立的远程调试目录。

![历史 RK 界面渲染图，Qt 5.15.13 / eglfs / 1600×900](../03_photos/20261022_qt_hdmi_render_01.png)

这是应用渲染图。**实物 HDMI 显示、点击、键盘输入、Enter 提交及退出返回终端由用户现场确认。** 首次现场验收的最后实际提交为 `RK3576_ok`，14:23:31.550 正常退出、代码 0，随后活动终端为 tty1；不改写为本次 Windows 的输入。

需要重新查看 RK 的 Qt 包版本，在 **板端 SSH / HDMI 终端**执行：

```bash
dpkg-query -W libqt5core5t64 libqt5gui5t64 libqt5widgets5t64
```

本次三项版本均为 `5.15.13+dfsg-1ubuntu1`；应用 `START` 中的 `Qt 5.15.13` 是运行时的再次核对。

### 6.2 电脑运行 GDB，RK 只运行辅助进程

已有 WSL `gdb-multiarch 15.1` 与板端应用目录中解包的 `gdbserver 15.1`；板端未安装 IDE。通过 **SSH stdio** 连接，未开放调试 TCP 监听端口；主调试器读取电脑上的源码、ELF 和 sysroot。该机制见 [GDB 官方 gdbserver / SSH 说明](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Server.html)。

仍在 **WSL 的 `06_programs` 目录**，自动重新构建 Debug、部署并验证远程断点：

```bash
bash qt_demo/debug_rk.sh check
```

已实测命中 `app_main.cpp:67`，`argc=7`、回溯和单步通过，出现 `RK_REMOTE_BREAKPOINT_REACHED` / `RK_REMOTE_SINGLE_STEP_COMPLETE`；Qt 5.15.13 运行 10 秒后退出码 0。日志在 `temp/qt_demo/remote_debug/<本次编号>/debugger.log`，同目录回收状态和渲染图。

需要手动停住、查看变量时：

```bash
bash qt_demo/debug_rk.sh interactive
```

进入 `(gdb)` 后按需要逐条操作：`continue` 到 main 断点，`print argc` 查看参数，`bt` 看回溯，`next` 单步，最后 `quit` 结束会话。交互模式的初始暂停点不一定就是 main，先 `continue`。

该交互模式不自动保存完整日志。要记录后续 GDB 输出，先在 **`(gdb)` 提示符**指定本机文件：

```gdb
set logging file /mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/qt_demo/remote_debug/interactive.log
```

然后启用记录，再进行上述变量与单步操作：

```gdb
set logging enabled on
```

结束记录时执行下面一条，然后 `quit` 结束会话。此为 GDB 15.1 的使用方法；本次已保存的自动验证日志由 `check` 模式生成。

```gdb
set logging enabled off
```

**当前边界：** 远程脚本固定使用 `offscreen`，验证项目源码调试，不接管 HDMI / USB 输入；Windows 原生 Qt Creator 的 RK 远程 Kit 尚未配置。当前 MinGW GDB 调 Windows 目标，不能直接替代 AArch64 Linux 调试器。Qt 插件和部分系统库源码符号未完全配齐。一次 main 断点处测得 gdbserver RSS 4460 KiB，约 4.35 MiB，只是辅助进程的单次快照，不含 Qt 应用内存。断点暂停和 Debug 构建不作为实时延迟验收。

构建与辅助工具的来源、校验及恢复资料统一保存在 [07_backups/qt_environment/](../07_backups/qt_environment/)；此前新增依赖范围见配套 JSON。本文整理期间未再改动板端配置。

## 7. 已完成与下一步

下面是本事项的阶段状态；全项目依赖和优先级继续以[开发计划](../01_docs/plans/development_plan.md)为准。

**已完成**

- [x] `.pro` 跨平台工程；Windows 64 位 Debug、F5 入口断点、局部参数、栈、F10 和继续运行 / 退出。
- [x] RK Qt 5.15.13、HDMI / USB 键鼠现场验收；WSL→RK 的 GDB 命令行远程链路。
- [x] 构建、Run 设置、断点和日志的圈注图，原图与可编辑 SVG 分开归档；统一本文、JSON、README 与计划入口。

**当前需要做**

- [ ] 先按第 3.2 节练习一次按钮回调断点：核对 `clicks` 从 0 到 1、界面和日志一致。以此掌握在实际功能位置排查问题。

**未完成即将执行**

- [ ] 明确首个业务页面要显示的状态、数据、操作和错误处理，以及 Qt 与调度 / 算法的接口边界；在当前工程基础上确定下一步页面范围。
- [ ] 确有板端特有故障、需要图形化远程调试时，再配置电脑端 AArch64 GDB / Qt Creator Kit，并用真实目标进程验收；需要 HDMI 时另核对启动和附加方式。

**将来需要做**

- [ ] 业务输入明确后，验证真实后端数据、通信异常、退出 / 恢复和日志关联。
- [ ] 按使用需要确认 MIPI DSI 配套屏型号与触摸；按交付要求验证自启、重启恢复、长期稳定性及界面负载下的实时性。
