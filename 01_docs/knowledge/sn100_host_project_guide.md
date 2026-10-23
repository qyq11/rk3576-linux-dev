# SN100 移植工程、AI 规则与资料入口

更新：2026-10-24。本文件说明当前目录职责、Codex 阅读入口、已建立的 Skill 和已检索资料。当前统一入口已接入O03-4原模板预览/PDF，[运行与维护](../../06_programs/sn100_host/README.md#o03-4)见工程README；日常待办看[下一步清单](../plans/next_steps.md)，模块边界维护在[移植计划](../plans/sn100_host_migration_plan.md)，实际过程统一追加[移植记录](../../02_records/20261022_sn100_host_migration.md)，环境事实继续见[Qt记录](../../02_records/20261022_qt_environment.md)，不复制第二份进度清单。

## 1. 当前目录与工程边界

```text
项目根目录/
├── .agents/skills/                   项目级复用流程，详见第3节
├── AGENTS.md                         项目共同约定和SN100入口
├── README.md                         用户总入口
├── 01_docs/
│   ├── plans/
│   │   ├── development_plan.md        整体开发计划
│   │   └── sn100_host_migration_plan.md  移植阶段、当前模块、验收及待确认项
│   └── knowledge/
│       └── sn100_host_project_guide.md 本文件：结构/规范/资料
├── 02_records/
│   └── 20261022_sn100_host_migration.md 移植准备、模块过程及资源/删减/仪器验证统一记录
├── 03_photos/                        原图、标注图和可编辑原件
├── 04_references/sn100_host/
│   └── reference_location.json       原工程移位前后清单统计及关键文件SHA
├── 06_programs/
│   ├── sn100_host/                   正式移植工程
│   │   └── AGENTS.md                 本工程约定，业务源码按当前模块逐步加入
│   └── sn100_host_reference/         原SN100_host整体移入，只作参考
│       ├── Seerain450_PC.rar         老板提供的压缩原件
│       └── Seerain450_PC/
│           └── Seerain450_PC/
│               └── SeeRain450.pro    原版Qt5.14.2/MinGW32工程
└── temp/sn100_host/                  本机/ARM构建、隔离测试数据与临时日志
```

当前新建的程序目录是 [`06_programs/sn100_host/`](../../06_programs/sn100_host/)，其[局部AGENTS](../../06_programs/sn100_host/AGENTS.md)已写入。参考工程整体移至相邻目录，原有内层结构和文件名保留；没有把原程序当作已移植工程。

参考包含大量Windows二进制、数据库/配置和个人IDE设置，默认由 `06_programs/.gitignore` 排除，继续在本机完整保留。后续逐模块审查、提取所需源码和可分发资源到正式工程并纳入版本管理；不整包 `git add -f`。需正式交付的依赖输入、安装/恢复材料按用途归档到 `07_backups/` 并遵循LFS/来源/SHA规则，测试数据匿名化。

移位校验见[位置和校验依据](../../04_references/sn100_host/reference_location.json)：文件数、总字节数一致；压缩原件和主工程/入口/接收器三个源码 SHA 前后一致。这不是对全部解压文件逐一重新计算 SHA 的声明。2026-10-22 当日只移动和整理文件；2026-10-23 起授权在正式工作目录进行离线移植，参考原件仍保持不变。

## 2. AGENTS、计划、知识、记录和Skill各管什么

| 内容 | 位置 | 管理原则 |
| --- | --- | --- |
| 全项目始终适用约定 | 根 `AGENTS.md` | 目录、构建部署、记录、授权范围；短小，链接专用入口 |
| SN100代码/构建约定 | `06_programs/sn100_host/AGENTS.md` | 原版参考、Qt/C++边界、邻近风格、平台适配、逐模块验收 |
| 计划与待办 | `01_docs/plans/sn100_host_migration_plan.md` | 模块划分、依赖顺序、当前模块卡、四部分勾选和验收；WSL离线/Windows对照/ARM编译/板端/仪器状态分开，不当Skill副本 |
| 持久知识/代码分析 | 本文件；以后必要时增加专题知识页 | 数据流、接口说明和决策依据；避免把全库源码重复摘抄 |
| 移植过程事实与实测 | `02_records/20261022_sn100_host_migration.md` | 从准备到逐模块移植/仪器验证持续追加，含资源与删减依据；未做结果不标完成 |
| 可复用的操作流程 | 根 `.agents/skills/<skill-name>/SKILL.md` | 已建立模块开发/验收和工程文件管理两个 Skill；详细要求按需加载，不复制当前计划 |
| 临时文件 | 根 `temp/sn100_host/` | 构建、日志、回放中间件/临时脚本，不与正式源码混放 |

**规范的归属：**“4空格/保留objectName/不整体格式化”这样的常驻工程要求放局部AGENTS；“怎样比较一个模块的Windows/RK输出并验证仪器”这样的可复用流程适合Skill。当前模块状态维护在计划，下一步清单只保留用户需要的短摘要，实测仍只维护在记录。发现有效的新规则时更新其唯一维护位置，其他文件用链接引用。

官方说明：AGENTS按项目根到工作目录形成指导链；根目录任务不应假定已自动载入任意子目录的文件。因此根AGENTS明确要求进入SN100工作前读取局部AGENTS，而局部文件只放该工程特有内容。[AGENTS官方说明](https://developers.openai.com/codex/guides/agents-md/)

## 3. 已建立的Skill与使用方法

2026-10-23 按用户要求落地为项目技能，正式文件在**仓库根 `.agents/skills/`**，可随项目 Git 保存，不放 `temp/`，不改个人全局技能。

### 3.1 文件在哪里

```text
.agents/skills/
├── sn100-module-migration/
│   ├── SKILL.md                 模块开发/移植/重构/验收入口
│   └── references/
│       └── engineering-and-verification.md  可读业务代码、线程、响应/资源和RK验收
└── sn100-project-files/
    └── SKILL.md                 文件归属、移动改名/删除核查、Git和归档
```

可直接打开[模块 Skill](../../.agents/skills/sn100-module-migration/SKILL.md)、[业务可读性与验收细则](../../.agents/skills/sn100-module-migration/references/engineering-and-verification.md)、[文件管理 Skill](../../.agents/skills/sn100-project-files/SKILL.md)。没有占位脚本或空框架。

Windows 完整入口分别为：

```text
C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux\.agents\skills\sn100-module-migration\SKILL.md
C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux\.agents\skills\sn100-project-files\SKILL.md
```

WSL 对应同一文件，项目根前缀为 `/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/`；不另复制到 WSL 家目录。

### 3.2 后续给 AI 怎么说

下列是**复制给 AI 的任务提示词，不是终端命令**：

```text
使用 $sn100-module-migration，按当前模块卡继续 SN100 开发。先读根/局部 AGENTS 和当前计划，再读 skill 及对应参考；代码要让人能顺着业务入口看懂规则、数据变化和错误恢复。按现有设备条件完成可执行验证，分别报告离线、RK、仪器与响应/稳定性结果，更新同一移植记录。
```

```text
使用 $sn100-project-files，整理本次新增的 SN100 工程文件。先核对已有修改和引用，说明归属及受影响范围，保留用户原件；源码、生成物、匿名夹具、运行数据和正式证据按规则管理。移动后验证相关构建和功能，未具备环境的部分明确待验。
```

开发中涉及新增/移动/归档文件，可在同一条任务中同时写两个 skill。若客户端未显示技能选择器，直接要求“读取并遵循项目根 `.agents/skills/sn100-module-migration/SKILL.md` 和 `.agents/skills/sn100-project-files/SKILL.md`”，或粘贴上面的完整路径。

人工检查文件时，在项目根 PowerShell 中可分别执行：

```powershell
Get-Content -Raw .agents/skills/sn100-module-migration/SKILL.md
```

```powershell
Get-Content -Raw .agents/skills/sn100-project-files/SKILL.md
```

### 3.3 是不是会一直自动读

Codex 从当前工作目录到仓库根发现 `.agents/skills/`；先加载技能名称/描述/路径，任务匹配或显式调用后再读正文，参考按需要读。**不是每改一行都自动重新读取全部文件。** 根和局部 AGENTS 已增加相关任务开始时的读取要求；任务切换、规则修改或上下文丢失后重读所需部分。`$名称` 是显式调用，直接给路径也能要求读取。[官方说明](https://learn.chatgpt.com/docs/build-skills)

官方说明支持自动检测技能变更；未出现时可重启 Codex。本次检查文件结构、引用和受限场景使用，不把文件校验当成客户端选择器实测或软件验收。后续按真实模块使用结果修订，保留 Qt5 和当前授权边界。

## 4. Codex如何快速阅读

1. 从根README/AGENTS确认任务范围和实际目录，再读局部AGENTS。
2. 查看计划的“当前需要做”和当前模块卡，确认 WSL 离线范围、待对照项及设备条件；当前不涉及仪器动作的业务切片不要求先具备仪器参数。
3. 用 `rg` 查该模块在参考工程里的 `.pro/.pri`、入口、发送/接收、数据对象、配置和依赖；只展开相关调用链。
4. 按开发/验收或文件管理任务读第3节对应 Skill 与所需 references，再查相关官方资料和已有实测；不一次加载全部技能。
5. 只改正式工作工程；记录原版对应位置和实际差异，按本模块用例构建/验证。完成后更新计划状态与同一实测记录。

后续正式工程模块目录（如platform、protocol、data、algorithm、ui）随首个实际源码加入时建立；不预建空目录、空测试、假build.sh、占位回放数据或未经验证的`.pro`。

## 5. 移植Skill和参考资料检索结果

检索日期：2026-10-22。采用本地Skill清单、公开作者仓库和Qt/板卡官方资料；检索范围内未找到可直接覆盖“本工程Qt5.14.2/MinGW32 → Ubuntu24.04/Qt5.15.13/AArch64 + SN100仪器协议”的专用已验证Skill。这是检索结果，不是宣称互联网上不存在。

| 类别/来源 | 读到的适用内容 | 本项目处理方式 |
| --- | --- | --- |
| [Qt通用Skill候选](https://github.com/alvarosamudio/autoskills/blob/43ecc21da65e0ce820ff596954e9e2aa69142276/packages/autoskills/skills-registry/qt/SKILL.md) | 明确面向Qt6.x；涵盖Widgets、信号槽、网络、数据库、线程、CMake/qmake | 可参考分析维度，不能直接按Qt6/CMake改造当前Qt5工程；未安装/未执行 |
| [C++ Pro候选](https://github.com/Jeffallan/claude-skills/blob/main/skills/cpp-pro/SKILL.md) | 面向C++20/23、CMake、内存/并发与性能，含concepts等要求 | 与原MinGW7.3/C++14及QObject父子所有权不能直接套用；只记为候选，不据此升级语言标准或全库换智能指针 |
| [立创RK3576 Qt5.15开发指南](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-development-compilation/qt-5-15/) | 包括交叉编译、Kit、模块配置、部署和调试；当前主线是Buildroot/Qt5.15.11，Ubuntu路线显示待推出 | 板级参考价值高；与现有Ubuntu24.04/Qt5.15.13区别登记，不套用其必须烧录/工具链版本结论 |
| [立创命令行交叉编译](https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-development-compilation/qt-5-15/buildroot/command-line-cross-compilation.html) | 区分主机qmake、交叉工具链、目标sysroot和产物架构；误用主机库排查 | 借鉴检查方法，实际仍使用与当前Ubuntu目标匹配的工具和库；不是切换Buildroot的操作指令 |
| [Qt5.15高DPI](https://doc.qt.io/archives/qt-5.15/highdpi.html) | 逻辑坐标、DPR、字体和固定尺寸的关系 | 用于1600×900与原1920×1080逐页/弹窗适配，不把全局缩放当全部验证 |
| [Qt5.15线程与QObject](https://doc.qt.io/archives/qt-5.15/threads-qobject.html) | GUI、socket/timer、对象所属线程和排队连接约束 | 用于拆分协议、数据、算法与页面，审查原自移入QThread及跨线程UI指针 |
| [QtXlsx源码](https://github.com/dbzhang800/QtXlsxWriter)、[qtcsv源码](https://github.com/iamantony/qtcsv) | 双目标重建来源候选；qtcsv的Qt5分支与当前master有区别 | 先匹配本地版本/补丁/许可再用，不默认为相同DLL实现；算法源码仍需供应商提供 |
| [Codex AGENTS](https://developers.openai.com/codex/guides/agents-md/)、[Skill](https://developers.openai.com/codex/skills/) | 常驻规则与可复用流程的作用、仓库发现位置、按需加载 | 依据现有项目约定设置 AGENTS 入口与根 `.agents/skills/`；不用另建一套平行计划体系 |

此前尝试OpenAI curated技能目录脚本曾返回HTTP403，完整目录未取得；不能据此断言不存在相关技能。本轮没有下载/安装第三方Skill、运行外部Skill脚本或引入插件；不把作者仓库中的一般建议自动提升为本项目强制规范。

2026-10-23 补充核查了 Qt 官方团队和工程文件管理候选：

| 实际 Skill / 来源 | 能借鉴什么 | 当前项目的边界 |
| --- | --- | --- |
| [Qt 官方 qt-cpp-review](https://github.com/TheQtCompanyRnD/agent-skills/blob/main/skills/qt-cpp-review/SKILL.md) | QObject 生命周期、线程、错误、模型契约与性能审查 | 面向 Qt6，选择兼容 Qt5 的检查点；本次未安装/运行其 linter |
| [Qt 官方 qt-cmake-project](https://github.com/TheQtCompanyRnD/agent-skills/blob/main/skills/qt-cmake-project/SKILL.md) | Qt6/CMake 项目、目标与资源组织 | 会提出 Qt5→Qt6 迁移要求；不直接用于当前 qmake/Qt5 工程 |
| [Qt 官方 qt-qml-profiler](https://github.com/TheQtCompanyRnD/agent-skills/blob/main/skills/qt-qml-profiler/SKILL.md) | Qt6 二维 Qt Quick 性能分析 | 当前 Widgets 场景不直接适用，不以 QML 分析代替 Widgets 实测 |
| [ComposioHQ file-organizer](https://github.com/ComposioHQ/awesome-claude-skills/blob/master/file-organizer/SKILL.md) | 整理前盘点、移动映射和冲突处理 | 面向通用文件/下载目录，不覆盖 Qt 构建和动态资源依赖；不照搬按扩展名拆分、按日期清理 |

此次没有核实到可直接覆盖“Qt5 Widgets + SN100 工业仪器业务 + RK3576 资源/实物验收”的完整现成技能，因此自行编写两个项目适配 skill，引用来源而不复制整包。以后引入第三方代码/脚本时核查版本、依赖、许可和实际行为；不从“官方/大厂”称呼推定其适用于当前工程。

## 6. 逐模块验证的确认方式

2026-10-22 用户确认后续采用**完成一个模块就对该模块进行仪器验证**的节奏；2026-10-23 明确手边没有板卡及仪器，授权先在 WSL 离线开发。独立模块离线通过后可按依赖继续推进，板端及仪器状态保留待验证；设备具备后再逐模块补验，不能把模拟或本机流畅记成实物通过。接口/地址/端口/串口参数及运行状态由用户后续提供，不开展猜测性连接。

每模块用一张模块卡维护：范围和依赖、原版代码入口、Windows 参考输入/输出、平台差异、构建版本、回放、RK/仪器连接条件、实际输入/输出及界面/DB 变化、通过标准、证据/缺陷和下一步。已有O01示例为原医院信息设置页面，详细验收见[医院模块范围](../plans/sn100_host_migration_plan.md#20-当前离线模块-o01医院信息设置)；离线依赖顺序、V批次范围和进入条件见[逐模块仪器验收](../plans/sn100_host_migration_plan.md#26-逐模块移植和现有仪器验收用户已确认)。

已有算法/第三方库阻断是完整系统的放行条件，不要求先把全系统库都解决才能做与其无关的模块；例如连接/只读状态子模块可以先隔离验证。每个模块完成都要回归已经通过的相关模块，全部模块之后仍需端到端联调与性能验收。

## 7. 资源调配与验证后删减代码

用户要求每模块都注意上位机运行资源。以相同输入/规模/吞吐/显示条件采集CPU、RSS/PSS/峰值、线程/队列、响应及日志/存储增长，必要阶段测温度/降频和同板控制竞争；预算先明确再调线程、轮询、缓存和展示频率，不通过降低必需功能、原始数据或告警可见性换性能。

代码删减先建立原版基线，并查两端构建、配置/机型、动态加载、字符串信号槽、自动槽、QRC和异常恢复等；必要时通过可撤回副本的禁用/候选补丁对照验证。确认对适用功能、数据/报文/仪器用例无影响后才纳入正式删减，删后仍回归并测资源；不能只凭未搜到引用或菜单注释决定，不确定用途保留，参考原件不清理。

详细执行门槛见[资源与删减门槛](../plans/sn100_host_migration_plan.md#27-每模块资源核查与代码删减门槛用户新增约定)，实际依据和前后数据写入[统一移植记录](../../02_records/20261022_sn100_host_migration.md)。该项目习惯也已补入个人work-habits，工程风格仍由局部AGENTS维护。

## 8. WSL 离线开发与模块化移植依据（2026-10-23）

以下为本轮核对的官方资料。Qt5.15 归档页面当前标注 5.15.19，Ubuntu24.04 的 Qt 开发包为 5.15.13；这里只采用两者共有的接口/线程约束，实际安装与运行版本以环境记录为准。

| 官方来源 | 可用于当前项目的结论 |
| --- | --- |
| [微软 WSLg 图形程序说明](https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps) | WSL2 支持 Linux X11/Wayland 图形窗口与鼠标键盘交互；本机验证可用，但不能代替 RK 显示驱动、触摸或性能验证 |
| [Ubuntu24.04 qtbase5-dev](https://packages.ubuntu.com/noble/qtbase5-dev)、[SQLite 驱动包](https://packages.ubuntu.com/en/noble/libs/libqt5sql5-sqlite) | 可安装 Qt5.15.13 的本机开发库、qmake 与 SQLite 驱动；WSL x86_64 库与 AArch64 sysroot 各自隔离 |
| [Qt5 qmake SUBDIRS](https://doc.qt.io/archives/qt-5.15/qmake-variable-reference.html#subdirs) | qmake 已支持子项目和 `.depends`；模块化无需升级 Qt6/CMake，也无需先拆成动态插件或多个进程 |
| [Qt5 Widgets Model/View](https://doc.qt.io/archives/qt-5.15/model-view-programming.html) | 可将数据访问/业务与界面逐步分离，保留真实 `.ui` 和字段契约；不是要求一次改写全库控件 |
| [Qt5 线程与 QObject](https://doc.qt.io/archives/qt-5.15/threads-qobject.html)、[SQL 线程约束](https://doc.qt.io/archives/qt-5.15/threads-modules.html) | QWidget 只在主线程；耗时操作通过工作线程和排队信号返回结果。Qt5 SQL 连接只在创建线程使用，不把 GUI 创建的连接交给 worker |
| [Qt5 Test](https://doc.qt.io/archives/qt-5.15/qttest-index.html)、[SQLite integrity_check](https://sqlite.org/pragma.html#pragma_integrity_check) | 关键交互、信号和数据可做自动对照；数据库完整性成功返回 `ok`，外键另查 `foreign_key_check`，仍需验证具体业务字段与重启持久化 |

结合上述能力，本项目采用**按业务切片迁移、先保留功能基线再局部简化**的工程判断：已迁移的O01入口、UI、数据worker分开，保留原医院表字段和助记符映射，以参数绑定和事务替代字符串拼接。它是渐进提取，不是完整原程序已经可用。优化前后比较相同输入、UI/数据/错误语义和资源，删除仍遵守第7节的调用/配置/动态依赖证据；不确定用途保留。

首轮只写显式测试数据目录，不读取原生产库/旧配置、不自动连接网络。响应核对以真实操作、GUI 定时器延迟和固定负载测量为证；单纯窗口打开、测试输出 PASS 或机器更快不能替代此结论。每次实际结果只在统一记录写一次，计划同步对应勾选与待验范围。

## 9. 工程化管理的常用名称与本项目落点

“大厂规范”通常包含几类内容；公开的 Qt 与 Google 规范各有工程范围，未发现统一适用于所有工业 Qt 上位机的固定目录或性能数值。这里选择与当前 Qt5/Widgets/RK 环境匹配的要求。

| 常用名称 | 本项目怎样落实 |
| --- | --- |
| 编码规范 / Coding Conventions、Style Guide | 局部 AGENTS 保留旧风格；模块 skill 要求业务命名、完整而清楚的函数、规则/单位/状态变化注释、明确错误和生命周期 |
| 架构约束 / 分层 / 关注点分离 | 按实际需要划分 UI、业务、协议、传输、存储与平台；小模块保持简单，不强制 MVC/MVP/MVVM 或通用框架 |
| 非功能需求 / NFR | 把“稳定、不卡顿”展开为固定场景下的响应分布、最大阻塞、资源上限、持续运行和异常恢复；RK 与 WSL 分别取证 |
| 完成定义 / DoD、质量门禁 | 功能/字段/报文对照、实际构建/运行、目标显示、仪器、性能和长稳按各自门槛验收；不足的项目保持待验 |
| 软件配置管理 / SCM、可追溯性 | Git 管理源码/构建/skill/匿名输入，区分生成物与运行数据；版本/依赖/校验/输入输出/回退和证据链接可追溯 |

[Qt Coding Style](https://wiki.qt.io/Qt_Coding_Style)、[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)和[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)是借鉴来源；不据此把当前 C++14、Allman 或 qmake 全库改造成另一套风格。

用户特别要求**人能读懂业务、方便人修改**，因此验收还包括：维护者能从操作入口找到规则、数据落点/设备副作用及失败恢复；说明为什么如此处理。具体人审问题、实现边界和测量合同只维护在[业务可读性与验收参考](../../.agents/skills/sn100-module-migration/references/engineering-and-verification.md)。不以更多层、类、接口数量代替可维护性。

2026-10-23 用户补充**界面颜色和布局尽量保持原版，只做 Linux / RK3576 屏幕必需适配**。单模块移植也不自行换配色或重排页面；O01首版明显视觉差异已按原源码纠正并完成离线复测；原Windows实际活动主题/字体和RK实屏完整视觉仍待对照，功能/数据验证不代替视觉验收。执行时同时核对原 `.ui`、共享和动态样式、主题、字体及资源，保留同状态对照与必要差异原因；详细要求仍只维护在上述参考的“原版界面保持与最小适配”，根/局部 AGENTS 和模块 skill 均已接入。

文件管理的归属表、移动/改名引用检查和 Git/LFS 门槛只维护在[文件管理 Skill](../../.agents/skills/sn100-project-files/SKILL.md)。当前工作工程已经按入口、`ui/`、`data/` 和影子构建组织，`data/` 是源码目录；本次没有为统一模板移动正在开发的源码或删除未提交内容。

## 10. 凝血点研究接口与后续统一操作

用户2026-10-23确认：无供应商算法源码/Linux实现时，先自主实现两种**凝血曲线凝血点判定**原型，后续再做实际曲线测试；不是机器学习学习曲线或校准多项式拟合。正式接口位于[`coagulation/clot_endpoint.h`](../../06_programs/sn100_host/coagulation/clot_endpoint.h)，源码及厂商原理依据在[`origins.json`](../../06_programs/sn100_host/coagulation/origins.json)。它们与原AL4A封装分开，不沿用其Linux空成功分支。

| 接口 | 数值含义与结果 | 维护注意 |
| --- | --- | --- |
| `percentageEndpoint()` | 首末稳定窗口算术均值为基线/平台；求指定变化比例的唯一交点，线性插值时间 | 50%是当前试验设置；必须包含完整反应与末端平台，不能拿采集中最后一点当100% |
| `maximumDerivativeEndpoint()` | 用奇数点局部线性回归求方向正化的一阶导数，返回内部最大速率的采样时刻 | 能处理非均匀时间；不亚采样拟合峰顶，不将该时刻默认映射到全部项目临床CT |

`CurvePoint.timeSeconds`是**相对加试剂起点的非负秒数**，程序不减首点；原始AD索引须由上游换算，起采延迟不能丢失。`CurveDirection`必须显式指定，下降信号不靠猜测反转。`EndpointResult.ok=false`时数值字段不可用；CLI用`candidate_found`和空时间表达失败，并保留原因/警告。基线、平台、幅度、多峰与搜索边缘检查仅是本轮工程启发式，参数默认值不代表SN100或临床阈值。不得把两方法结果平均、失败时静默切换或直接送入患者报告/设备停机。

各模块保留独立入口；**O03-4统一主壳已接医院、账户会话/角色/注销、历史查询/患者保存/统计/CSV及原模板预览/PDF**。[统一入口运行与维护](../../06_programs/sn100_host/README.md#o03-4)说明同目录helper、已提交医院名、匿名项目配置与报告忙态守卫；三store仍各自在所属worker管理SQL。原八项主导航和用户设置菜单、布局/主题保留，未迁入业务仍明确不可用；默认Guest，显式匿名工程师只用于脱机模拟，生产认证与仪器联机另验。协议能力后续在后台供状态/业务使用，凝血研究接口须先经实际项目验证再接结果流程。[同一记录](../../02_records/20261022_sn100_host_migration.md#o03-4-offline)与[证据索引](../../04_references/sn100_host/integrated_report_20261023/index.json)维护实际结果，O03-3保留为历史三页基线。

最新任务与依赖顺序继续见[O03-4整合卡](../plans/sn100_host_migration_plan.md#o03-4-card)及[后续业务顺序](../plans/sn100_host_migration_plan.md#integration-next-steps)：已有报告能力已整合，试剂/样本仍需迁移真实业务代码。根`build.sh`保留医院/账户独立入口，统一壳用`shell/build.sh`同时产出主程序与helper；[RK准备与上机用法](../../06_programs/sn100_host/README.md#rk-prepare)区分本轮ARM构建/闭包/冻结通过与实际部署/插件/显示输入待验。上机条件仍看[RK离线首验卡](../plans/sn100_host_migration_plan.md#rk-offline-card)。O05当前只支持本机回环，仪器V01还需协议/连接参数和显式外部Transport；本知识页只提供入口，不维护另一套勾选任务。

<a id="sn100-code-risks"></a>
## 11. 原参考工程的代码风险

以下承接旧移植计划1.3节的初审依据，路径相对只读参考工程根目录；表中风险属于原参考代码，不代表正式移植版当前仍全部未修。已完成修复及复测以[统一移植记录](../../02_records/20261022_sn100_host_migration.md)为准，当前状态查[移植计划](../plans/sn100_host_migration_plan.md)。

| 级别 | 实证位置与现象 | 对移植的影响/必须完成的动作 |
| --- | --- | --- |
| 阻断 | `bin/Qt5Core.dll`、`Qt5Xlsx.dll`、`qtcsv.dll`、算法 `AL4ADLL.dll` 和日志 `GL4Ad.dll` 均为 PE x86 32 位 | RK不能直接加载；分别取得源码/ARM64库，或对日志等能力提供经过等价验证的跨平台替代 |
| 阻断 | `customtools/algorithmcore.cpp:539–604` 等 Linux 分支跳过初始化/计算仍返回 true；`AL4A_Driver.cpp:20–67` 依赖 LoadLibrary/GetProcAddress | “能编译/返回成功”不表示计算正确；必须逐一消除空实现假成功，用已知曲线对照原 DLL |
| 阻断 | `SeeRain450.pro` Linux 链接分支为空；`customtools/dumpreport.cpp:277–301` 存在未隔离 HANDLE / DbgHelp 调用 | Linux 构建路径并未完成；不能仅替换 qmake 命令就部署 |
| 阻断 | `logger.h:70–90` Linux 部分日志宏为空且存在 X/x 大小写问题；`logger.cpp:84–124` 实际实现主要在 Windows 分支 | 必须补齐跨平台日志、真实 TX/RX 和错误取证，不能删除日志求编译通过 |
| 高 | 原参考工程含 Network/XML/SQL/SerialPort/PrintSupport/Svg/Concurrent；`QtRPT.pri:1` 又引入 QML/Charts | 现有 demo 的 Core/Gui/Widgets sysroot 不完整；需完整目标依赖闭包和主机生成工具 |
| 高 | `frmmain.ui:5–25` 初始1920×1080；7个业务页初始1751×968；68个自有 UI 中27个无 layout，geometry 属性1723处 | 当前1600×900屏需要逐层布局适配和实屏验收；设计初始尺寸不等于全部为固定尺寸 |
| 高 | `uiadaptive.cpp:7–60` 基准1366×768，只处理名称含 Adptive 的对象，分别乘 x/y；未发现应用层调用 | 不能声称现有自适应已启用；不能直接全局调用后认为适配完成 |
| 高 | `frmmain.cpp:184–195,273–307` 登录前即创建通信线程、打开设备TCP并可能自动连LIS | 首次启动前先建立脱机/模拟入口；不能直接用原配置连接真仪器 |
| 高 | `tool/database/sqlite_helper.cpp:16–22` 默认 MEMORY journal、synchronous=OFF | 嵌入式持久性需要专门设计和验证，不能以减少写盘代替数据可靠性 |
| 高 | `public/systemcommon.cpp:601–620` 有 UNC 路径、net use；完整命令可能进入日志 | 重划本地/远程存储边界；日志不得包含连接凭据，文档不收录原配置值 |
| 高 | `main.cpp:289–294` QMovie::CacheAll 后对无效动画无限忙等；启动时一次创建8个页面 | 先解决有界失败和测量资源，再评估缓存/展示刷新/惰性加载 |
| 待核验 | 自移入QThread、UI对象指针进入LIS线程、同步引用信号、数据库事务归属 | 建立线程与对象生命周期表，再进行模块拆分；不能机械改为异步connect |

已发现的**原有问题**单列：`frmmain.cpp:367` 的 `sloLIStDisConnected()` 与实际 `slotLIStDisConnected()` 拼写不一致；`TimeCalculateThread` 虽被创建，但开始处的 `StartCalculate` 调用被注释；语言页只有中文且 Apply 槽为空；task/formula 目录存在而主菜单接线被注释。应先确认原版实际行为，再决定修复/启用范围，不把它们误记成迁移新增回归或已完成产品功能。

<a id="sn100-dependencies"></a>
## 12. 算法与第三方依赖核查依据

本表承接旧计划2.2依赖表，表示原参考工程提供的材料及完整交付前应核查的范围；已有切片的实际源码、补丁、构建和许可结果见[统一移植记录](../../02_records/20261022_sn100_host_migration.md)。库名、头文件版本及Windows DLL不能证明ARM实现齐全。

| 依赖 | 当前提供的状态 | 计划处理 | 验收/失败后的路径 |
| --- | --- | --- | --- |
| AL4A算法 | Windows32 DLL、供应商头/加载封装；部分Linux空实现 | 逐函数区分活跃/历史分支，确认真正调用库、版本、配置、符号、调用约定和许可；取得完整源码或AArch64 Linux库 | 初始化与所有实际使用计算均有结果/错误码；金样本达标。若无ARM实现，算法相关移植受阻；可讨论供应商移植或外部Windows算法服务，后者须明确偏离独立RK目标 |
| GL4A日志后端 | GL4Ad为Windows32 DLL；GL4A_Driver/logger封装 | 与算法分开处理；取得ARM64后端或用Qt/合适日志库实现等价分类、格式、轮转和错误行为 | 实际TX/RX、LIS、错误和运行日志可取证；日志替代不以取得算法源码为前提 |
| QtXlsx 0.3.0 | 头文件/Windows DLL、私有头路径 | 匹配源码/补丁后分别为MinGW32和ARM64重建，隔离Qt私有头依赖 | 读写既有模板、中文/日期/数值/公式与文件重新打开一致；替代库需单独批准格式差异并回归 |
| qtcsv 1.5.0 | 头文件/Windows DLL | 找匹配Qt5版本来源和许可证，双目标重建 | 编码/BOM、引号、逗号、换行、空值、中文和数值精度一致 |
| QtRPT 3.0.0 | 本地第三方源码/.pri，传递引入QML/Charts | 核查版本/补丁、模板及CommonFiles源码闭包，重建所需模块 | 预览/页数/分页/字段/PDF/实体打印；不能删打印入口求轻量 |
| SQLite/DAL | `tool/database/sqlite3/sqlite3.h:149`为3.42.0，`customtools/include/1_Sqlite3/sqlite3.h:126`为3.34.1 | 固定实际头文件、编译源码/链接库版本及选项，确认直接sqlite3和QtSQL实际调用范围；不混头文件和库 | 运行记录sqlite版本，建库/升级/事务/查询/异常恢复与原格式兼容 |
| QCustomPlot/MaterialUI/Eigen等 | 部分源码在工程内；QCustomPlot头声明GPLv3或以后版本，QtRPT本地license为Apache2.0 | 逐项保留版权/许可并核对已有商业授权及实际分发方案；同时检查CPU专用宏、对齐/SIMD假设 | ARM可构建，图表/数值/对齐边界通过，不携带x86专用指令；不能仅凭Qt本体许可推定全部依赖可分发 |
| 供应商头及其他内嵌库 | Eigen3.4.0/MPL2.0、spdlog1.8.0/MIT、nlohmann-json3.4.0/MIT；ALGL头有GPLv2+声明 | 追踪实际使用及实现来源，核对供应商修改、已有授权与需分发文件；QtRPT/QCustomPlot等各自许可分别登记 | 实际依赖与许可清单一致；ALGL的头文件存在不能证明实现源码齐全或可直接交叉编译 |
| 字体/软键盘/词库 | 内嵌PingFangBold约10.4MiB，另有输入法资源 | 核查分发许可和实际启用资源；必要时选许可清楚的中文字体替换并做版面回归 | 中文/符号覆盖、字段无截断、报告分页不变；未核实许可不宣称可正式分发 |

<a id="sn100-functional-scope"></a>
## 13. 功能范围F01–F18

本表承接旧计划3.3节的首轮源码清单；模块实施时继续展开按钮、配置、角色与异常用例。实际SN100启用范围和原有缺陷需与原Windows发行版核对，不能据源码存在、菜单齐全或按钮能点击推定功能完成。

| 功能ID/范围 | 当前主要代码位置 | 必须对照的内容 |
| --- | --- | --- |
| F01 启动/全局 | `main.cpp:59,90,222`；`frmmain.cpp:172–307` | 配置/主题/字体/机型、单实例、加载失败、导航/状态栏、退出、再次启动 |
| F02 登录/用户权限 | `frmlogin/maskdlglogin.cpp:105,145`；`frmmain.cpp:673,2077` | 有效/无效登录、角色、控件编辑权、退出权限回落、重新登录；匿名测试账户 |
| F03 试剂/耗材 | `frmreagent/frmreagentmaterial.cpp`；批组/瓶型/用量弹窗 | 资料、批号、孔位/通道、瓶型、余量/次数、扫描/探测/换位/清盘、恢复、异常提示和命令反馈 |
| F04 样本/任务 | `frmsample/frmsamplematerial.cpp:319,1085–1380,3810,4552` | 架孔/样本/任务关联、普通/急诊/重复/稀释/组合、增删改保存、任务下发、失败恢复、结果分流 |
| F05 开始/暂停/恢复 | `frmmain.cpp:1453,2020,606` | 连接/试剂/校准/未保存配置检查、拒绝原因、开始应答、暂停中/暂停/恢复、急诊与关机状态 |
| F06 分析项目/单位 | `frmsystem/frmsystemmaterial.cpp:680,1990,2141,2471` | 参数/默认值、加样/试剂动作、波长/稀释、算法配置、单位倍率、保存复制删除及依赖同步 |
| F07 校准 | `frmcalibration/frmcalibrationmaterial.cpp:231,351,1181,1299–1442` | 原始点、方法、变换/拟合/逆算、批号绑定、曲线图、重算、审核/保存/复制/删除、错误输入 |
| F08 质控 | `frm_quality/frm_quality.cpp`；`mask_dlg_add_control.cpp` | 控制品/项目/批号、靶值/上下限/规则、历史点、图表、越界提示；规则含义需业务确认 |
| F09 结果/报告 | `frm_report/frm_report_material.cpp:1148–2003` | 查询/排序/分页/行数、日期、患者匿名字段、结果值/单位/精度/异常标记、编辑/保存/删除和明细曲线 |
| F10 打印/PDF/导出 | 报告页、QtRPT模板、QtXlsx/qtcsv调用 | 打印机/纸型/边距/页数、中文字体、图表、分页与字段、CSV/XLSX内容、取消/失败、文件再次读取 |
| F11 LIS/HL7 | `commlis/liscommsetting.cpp`、`liscommthread.cpp`、`liscommreceiver.cpp` | TCP/串口、MLLP帧/HL7 2.3.1实际限制、QRY/DSR任务、ORU结果、ACK、自动收发、重连和重复/超长报文 |
| F12 仪器操作/维护 | `frmdriverset/frmdriverset.cpp:356,615,767,927,1002,1046,1364` | 板卡/状态/AD、版本/TCP配置、日周月维护、扫描/泵阀/针/抓手/清洗/标定/耗材测试；先模拟命令，实物逐项授权范围验收 |
| F13 告警/互锁 | `frmabnormal/maskdlgabnormal.cpp:74,186–239,386–472` | 告警编码/级别/描述、新旧协议、持续/消失/重复/复位/蜂鸣、历史、是否阻止启动 |
| F14 系统设置 | `frmuserset/`、医院/账户/打印机/LIS页 | 字典/医院信息、账户角色、保存重开、Linux设备名/不存在设备提示；语言占位现状单列 |
| F15 数据可靠性 | `tool/database/`、`AllDal`、旧`sqlbase` | 原库兼容、schema升级、事务/并发/重启、数据库忙/损坏/磁盘满提示、备份恢复、断电策略 |
| F16 交互/自绘 | `customwidgets/`、`customtools/Keyboard/`、`public/customeventfilter.cpp` | 键鼠/焦点/Tab/Enter/Esc、中文和软键盘、遮罩、弹窗、自绘孔位命中、曲线游标/缩放和单位 |
| F17 日志/诊断 | `customtools/logger.*`、`dumpreport.*` | 时间/版本/模块/线程、实际TX/RX和错误、轮转/背压、退出和崩溃取证、敏感字段脱敏 |
| F18 未启用/型号特有 | task/formula、语言Apply、估时调用、450/6600协议分支 | 对照原发行版确定启用范围；不因源码存在而宣称可用，不擅自移除SN100需要的功能 |

<a id="sn100-v-batches"></a>
## 14. 仪器逐模块验收V01–V10范围

本表承接旧计划2.6节的候选批次范围，按实际依赖逐模块验收并累计回归，可继续拆成更小批次。每批的开始条件、任务和当前状态只在[移植计划](../plans/sn100_host_migration_plan.md#26-逐模块移植和现有仪器验收用户已确认)维护；实际TX/RX、数据及实物结果追加[统一记录](../../02_records/20261022_sn100_host_migration.md)。

| 验收批次（候选） | 模块最小范围 | 该模块如何对现有仪器验证 | 前提/进入下一模块条件 |
| --- | --- | --- | --- |
| V01 连接和状态/版本显示 | 最小Qt框架、连接参数、TCP/帧协议、状态值对象、日志和状态页；不引入无关算法/DAL/报表 | 人工触发连接/查询，显示真实TX/RX与解析后的型号、版本、连接/工作状态；断开、超时和重连后不残留“已连接” | 确认主控接口/固件和查询语义、单/多客户端限制；所有实际发出命令均在该范围，未触发校时/配置写入/机械动作 |
| V02 配置与权限 | 本机配置/路径、登录角色、项目/单位等必要数据边界 | 匿名账号及配置双端保存重开，RK实屏检查权限和信息；与仪器返回型号/能力匹配 | V01通过；不把原生产库/用户配置直接覆盖到RK |
| V03 试剂 | 先显示/读取试剂状态，再逐项加入已确定的设置/探测操作 | 对照孔位、通道、余量和原版状态；写入/探测按本模块动作与响应验证并核对恢复 | V01/V02及相关数据依赖通过，接线/试剂/现场条件明确 |
| V04 样本与任务 | 逐种增加普通/急诊/重复/稀释；开始/暂停/恢复可再拆小批次 | 样本架孔位/任务ID、下发/应答、实际执行状态、取消/掉线/去重；已有模块回归 | 试剂/项目/状态基础满足；测试样本、控制器条件明确 |
| V05 算法结果 | 一种实际启用算法/结果链路一次 | 先金样本对照，再将仪器实际采集曲线按同一参数与原版比较，保留原始点/中间状态/结果/误差 | 真实ARM算法实现和认可的容差具备；不能拿空Linux分支验收 |
| V06 校准 | 按项目/方法分批接入校准与批号关联 | 实际校准流程、原始点/拟合/逆算、审核保存、后续结果应用 | 依赖算法和数据模块已通过 |
| V07 质控 | 按控制品/项目及规则逐步加入 | 仪器结果、目标范围、越界提示和历史图，与原版对照 | 算法/校准及规则基线具备 |
| V08 报告/导出 | 查询和明细先行，CSV/XLSX/PDF/实体打印分别验收 | 使用前述模块形成的匿名结果，对照字段/单位/精度/分页与文件或打印品 | 对应库、模板和实际打印条件具备；不等待全部报表能力才验收查询 |
| V09 LIS | 任务查询/结果上传/自动模式分批 | 测试对端收发/ACK、仪器任务和结果关联、失败重试/重复处理 | 测试LIS参数及对端具备，与仪器TCP重试分开 |
| V10 维护与告警 | 每项维护动作/告警链路独立小批次 | 对应实物动作、应答、互锁/复位/历史；不一次开放全部工程按钮 | 相关硬件范围和准备明确，前置模块已通过 |

V01只证明该批已确认的连接/状态范围。V10后仍须完整流程、最大负载、资源/RT同板并发、长稳、部署更新和回退；离线回放、编译及WSL资源基线不替代RK/仪器结论。
