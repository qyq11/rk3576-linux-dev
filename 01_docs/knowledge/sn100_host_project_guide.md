# SN100 移植工程、AI 规则与资料入口

2026-10-22。本文件说明当前目录职责、Codex阅读入口、后续Skill管理和已检索资料。实施顺序与待办只维护在[详细移植计划](../plans/sn100_host_migration_plan.md)，实际过程统一追加[移植记录](../../02_records/20261022_sn100_host_migration.md)，环境事实继续见[Qt记录](../../02_records/20261022_qt_environment.md)，不复制第二份进度清单。

## 1. 当前目录与工程边界

```text
项目根目录/
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
│   │   └── AGENTS.md                 本工程约定，当前尚未放入业务源码
│   └── sn100_host_reference/         原SN100_host整体移入，只作参考
│       ├── Seerain450_PC.rar         老板提供的压缩原件
│       └── Seerain450_PC/
│           └── Seerain450_PC/
│               └── SeeRain450.pro    原版Qt5.14.2/MinGW32工程
└── temp/sn100_host/                  开始构建/验证时才创建的临时产物目录
```

当前新建的程序目录是 [`06_programs/sn100_host/`](../../06_programs/sn100_host/)，其[局部AGENTS](../../06_programs/sn100_host/AGENTS.md)已写入。参考工程整体移至相邻目录，原有内层结构和文件名保留；没有把原程序当作已移植工程。

参考包含大量Windows二进制、数据库/配置和个人IDE设置，默认由 `06_programs/.gitignore` 排除，继续在本机完整保留。后续逐模块审查、提取所需源码和可分发资源到正式工程并纳入版本管理；不整包 `git add -f`。需正式交付的依赖输入、安装/恢复材料按用途归档到 `07_backups/` 并遵循LFS/来源/SHA规则，测试数据匿名化。

移位校验见[位置和校验依据](../../04_references/sn100_host/reference_location.json)：文件数、总字节数一致；压缩原件和主工程/入口/接收器三个源码SHA前后一致。这不是对全部解压文件逐一重新计算SHA的声明。当前只移动和整理文件，不修改原代码、不启动软件、不访问仪器。

## 2. AGENTS、计划、知识、记录和Skill各管什么

| 内容 | 位置 | 管理原则 |
| --- | --- | --- |
| 全项目始终适用约定 | 根 `AGENTS.md` | 目录、构建部署、记录、授权范围；短小，链接专用入口 |
| SN100代码/构建约定 | `06_programs/sn100_host/AGENTS.md` | 原版参考、Qt/C++边界、邻近风格、平台适配、逐模块验收 |
| 计划与待办 | `01_docs/plans/sn100_host_migration_plan.md` | 四部分勾选、依赖门槛、当前模块和状态；不当Skill副本 |
| 持久知识/代码分析 | 本文件；以后必要时增加专题知识页 | 数据流、接口说明和决策依据；避免把全库源码重复摘抄 |
| 移植过程事实与实测 | `02_records/20261022_sn100_host_migration.md` | 从准备到逐模块移植/仪器验证持续追加，含资源与删减依据；未做结果不标完成 |
| 可复用的操作流程 | 未来根 `.agents/skills/<skill-name>/SKILL.md` | 一个明确任务一个Skill，按需触发，细节渐进加载；当前不预建空Skill目录 |
| 临时文件 | 根 `temp/sn100_host/` | 构建、日志、回放中间件/临时脚本，不与正式源码混放 |

**规范的归属：**“4空格/保留objectName/不整体格式化”这样的常驻工程要求放局部AGENTS；“怎样比较一个模块的Windows/RK输出并验证仪器”这样的可复用流程适合Skill。当前模块状态仍只维护在计划，实测仍只维护在记录。发现有效的新规则时更新其唯一维护位置，其他文件用链接引用。

官方说明：AGENTS按项目根到工作目录形成指导链；根目录任务不应假定已自动载入任意子目录的文件。因此根AGENTS明确要求进入SN100工作前读取局部AGENTS，而局部文件只放该工程特有内容。[AGENTS官方说明](https://developers.openai.com/codex/guides/agents-md/)

## 3. 后续Skill采用什么结构

官方当前说明，仓库技能从工作目录到仓库根的 `.agents/skills` 中发现。项目任务通常从仓库根启动，因此选择**仓库根 `.agents/skills/`**，描述限定SN100，便于共享与Git跟踪；不会修改个人全局Skill目录。本次已有个人技能路径沿用当前客户端提供的位置，不能由仓库规范推定其必须搬家。[Skill官方说明](https://developers.openai.com/codex/skills/)

下例为后续结构设计，**尚未创建**：

```text
.agents/skills/
└── sn100-module-migration/
    ├── SKILL.md                 name、description、适用范围、核心流程及资料路由
    ├── references/              某类模块确需的协议/验收说明，产生内容后再建
    ├── scripts/                 真正反复使用并已验证的构建/对照辅助，按需增加
    └── agents/openai.yaml       可选界面元数据，确需时再建
```

后续若跨平台构建、协议回放、UI适配各自形成稳定且不同的流程，再拆为 `sn100-cross-build`、`sn100-protocol-validation` 等；不是现在先批量创建。每个Skill必须有简短、准确的触发描述，避免名字叫“通用C++专家”却覆盖所有编程任务；不把49KiB以上的整份计划/所有模块规范塞进SKILL.md。

建议第一个Skill只负责：收到“移植/验收SN100某模块”时，定位当前计划和局部约定，核对原版行为，准备本模块依赖和用例，执行两端构建/回放，具备条件时完成RK+现有仪器验证，回收证据后更新同一记录和模块状态。设备参数缺失时询问，不从旧配置推测；用户只要规划/记录时保持该范围。风格和通用构建规则引用既有文件，避免重复。

创建时用 `skill-creator` 检查触发范围、YAML元数据、引用与实际路径，运行其校验工具；新增脚本须有实际输入输出验证。再用一个真实的小模块请求验证工作流。Skill结构有效不等于该模块已通过；从客户端技能列表/明确调用确认发现结果，刷新后仍不可见时再检查客户端支持情况。当前没有新增或安装Skill，只有结构规划。

## 4. Codex如何快速阅读

1. 从根README/AGENTS确认任务范围和实际目录，再读局部AGENTS。
2. 查看计划的“当前需要做”和当前模块卡，确认本轮是否只做记录/规划，是否具备仪器参数。
3. 用 `rg` 查该模块在参考工程里的 `.pro/.pri`、入口、发送/接收、数据对象、配置和依赖；只展开相关调用链。
4. 查对应官方参考和既有实测；需要可复用流程时才读匹配Skill及其所需references，避免一次加载全部技能。
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
| [Codex AGENTS](https://developers.openai.com/codex/guides/agents-md/)、[Skill](https://developers.openai.com/codex/skills/) | 常驻规则与可复用流程的作用、仓库发现位置、按需加载 | 依据现有项目约定设计AGENTS入口与未来根.agents/skills；不用另建一套平行计划体系 |

此前尝试OpenAI curated技能目录脚本曾返回HTTP403，完整目录未取得，限制保留在详细计划中。本轮没有下载/安装第三方Skill、运行外部Skill脚本或引入插件；不把作者仓库中的一般建议自动提升为本项目强制规范。

## 6. 逐模块验证的确认方式

用户已确认可使用当前已有SN100仪器，采用**完成一个模块就对该模块进行仪器验证**的节奏。接口/地址/端口/串口参数及运行状态由用户后续提供；当前只记录方案，不开展连接。

每模块用一张模块卡维护：范围和依赖、原版代码入口、Windows参考输入/输出、平台差异、构建版本、回放、RK/仪器连接条件、实际输入/输出及界面/DB变化、通过标准、证据/缺陷和下一步。具体模板、第一模块候选及放行状态见[计划2.6节](../plans/sn100_host_migration_plan.md#26-逐模块移植和现有仪器验收用户已确认)。

已有算法/第三方库阻断是完整系统的放行条件，不要求先把全系统库都解决才能做与其无关的模块；例如连接/只读状态子模块可以先隔离验证。每个模块完成都要回归已经通过的相关模块，全部模块之后仍需端到端联调与性能验收。

## 7. 资源调配与验证后删减代码

用户要求每模块都注意上位机运行资源。以相同输入/规模/吞吐/显示条件采集CPU、RSS/PSS/峰值、线程/队列、响应及日志/存储增长，必要阶段测温度/降频和同板控制竞争；预算先明确再调线程、轮询、缓存和展示频率，不通过降低必需功能、原始数据或告警可见性换性能。

代码删减先建立原版基线，并查两端构建、配置/机型、动态加载、字符串信号槽、自动槽、QRC和异常恢复等；必要时通过可撤回副本的禁用/候选补丁对照验证。确认对适用功能、数据/报文/仪器用例无影响后才纳入正式删减，删后仍回归并测资源；不能只凭未搜到引用或菜单注释决定，不确定用途保留，参考原件不清理。

详细执行门槛见[计划2.7节](../plans/sn100_host_migration_plan.md#27-每模块资源核查与代码删减门槛用户新增约定)，实际依据和前后数据写入[统一移植记录](../../02_records/20261022_sn100_host_migration.md)。该项目习惯也已补入个人work-habits，工程风格仍由局部AGENTS维护。
