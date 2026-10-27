# Qt 上位机移植记录：SN100 → RK3576

开始：2026-10-22。**这是本次Qt上位机移植的唯一过程主记录，后续模块开发、构建、部署、仪器验证、资源测量、代码调整/删减、问题和复测持续追加到本文。**

当前状态（2026-10-24）：**O07项目/单位、试剂基础及O08样本本地预设/任务已独立离线通过并接统一程序，修后offscreen/XCB各7/0、60秒项目/样本各268次保存；WSL/Windows32/ARM64主辅构建及新ARM包通过。** [本轮证据与边界](#catalog-sample-offline-20261024)保留来源、实测与未验；医院/账户/历史/报告原结论保留，报告累计首次旧查询UI失败与原样复测4/0均留档，原因未定位。原Windows完整行为/活动主题、生产参数、RK业务实屏/资源、真实下发/执行/扣减、实际SN100协议/曲线及整机长稳仍待验，O05的8h恢复失败未关闭；本轮没有连接设备或改参考原件。下文旧段保留各轮历史事实。

入口：[详细移植计划](../01_docs/plans/sn100_host_migration_plan.md) · [工程/Skill管理说明](../01_docs/knowledge/sn100_host_project_guide.md) · [局部工程约定](../06_programs/sn100_host/AGENTS.md) · [既有Qt环境实测](20261022_qt_environment.md)

## 1. 目标和当前工程位置

目标是在RK3576运行SN100上位机，在外接屏幕通过键鼠操作，按原版行为和SN100实际配置保持功能、通信和数据一致。Windows/WSL 共用源码，当前先在 WSL x86_64/WSLg 开发与验证离线模块；最终由电脑 WSL 交叉编译 ARM64 版本，RK 直接运行，板端特有问题再远程调试。

| 内容 | 当前位置/已知条件 |
| --- | --- |
| 正式移植目录 | [`06_programs/sn100_host/`](../06_programs/sn100_host/)，2026-10-22 仅有 AGENTS，2026-10-23 起加入 O01 源码、构建/测试文件；验收见当前模块卡与后续实施证据 |
| 老板原工程 | [`06_programs/sn100_host_reference/`](../06_programs/sn100_host_reference/)，原根目录SN100_host整体移入，只作参考 |
| 原主工程 | `06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC/SeeRain450.pro` |
| 原压缩包 | `06_programs/sn100_host_reference/Seerain450_PC.rar`，保留原件 |
| Windows原版 | 用户截图和文件核查：Qt5.14.2 / MinGW32，截图为Release；与qt_demo的64位Kit区分 |
| RK目标候选 | 当前Ubuntu24.04 / AArch64 / Qt5.15.13，RT内核6.1.99-rt36-tspi；本轮未重测或切换 |
| 首轮屏幕 | 当前HDMI1600×900；原UI初始1920×1080，需逐页适配 |
| 仪器 | 已确认后续可用 SN100；2026-10-23 当前手边无设备，连接方式/地址/端口/固件及运行状态待实物实施时提供 |
| 产物和临时日志 | 开始构建/验证后放根 `temp/sn100_host/`，不写到原参考bin |

SN100功能与CV450/6600大致相同是用户确认的背景；具体孔位、通道、命令、算法和维护差异仍需对照，不能推定完全相同。Qt版本两路线（保留5.15.13兼容源码 / 私有5.14.2）尚待代表模块验证后定版，不因版本号不同默认刷机。

## 2. 2026-10-22：已完成的准备与实际结果

### 2.1 首轮源码分析和调研

已阅读主工程、入口、平台代码、业务/通信/数据链路和UI，定位算法Windows32 DLL、Linux空实现假成功、日志/DbgHelp适配、完整Qt依赖、分辨率/字体与数据可靠性等问题；细节和代码定位维护在[计划1.3节](../01_docs/plans/sn100_host_migration_plan.md#13-代码审查已发现的关键问题)。算法AL4A与日志GL4A分开处理；不能用界面启动或编译成功代替计算/业务正确。

检索到通用Qt6/C++20候选Skill，但未找到经核查能直接覆盖本工程Qt5/ARM64/SN100协议的完整移植技能；未安装/执行外部Skill。新增立创RK3576 Qt指南参考，其当前主线为Buildroot/Qt5.15.11，与本板Ubuntu/Qt5.15.13有差异；官方链接、适用范围和检索限制见[工程说明第5节](../01_docs/knowledge/sn100_host_project_guide.md#5-移植skill和参考资料检索结果)。

### 2.2 工作工程与参考工程分开

按用户要求新建工作目录，将老板原工程及压缩包整体移入相邻参考目录。校验依据保存为 [`reference_location.json`](../04_references/sn100_host/reference_location.json)。

| 核对项 | 实际结果 |
| --- | --- |
| 移位前后文件数 | 3567，一致 |
| 移位前后总字节 | 1,160,786,644，一致 |
| 压缩原件SHA-256 | `30E3717BE5A0CCD2BA9D633DD19742F74DF627B6475077D767CC7C4147EB004D`，前后一致 |
| 主工程/入口/接收器SHA | SeeRain450.pro、main.cpp、commandreceiver.cpp前后一致，具体值见上述JSON |
| 核验边界 | 不是全部解压文件逐一重算SHA；未修改原文件内容、运行原软件或连接仪器 |

2026-10-22 Windows移动时报告旧内层目录被其他进程占用；当时核对原路径无文件，仅留下空目录。2026-10-23复核时发现Qt Creator重新写入一份个人设置；与参考目录比较仅保存时间不同，实际配置一致，已删除该残留及3个空目录。原参考工程统计及关键SHA再次核对通过，未改原件；结果见上述JSON及[同一清理记录](20261021_workspace_setup.md#91-根目录复核2026-10-23)。

参考包包含原二进制/数据库/配置/个人IDE设置，默认由 `06_programs/.gitignore` 排除并完整保存在本机；正式工程后续按模块提取审查过的源码/资源。局部AGENTS、根AGENTS、总体计划、详细计划、Qt环境记录与入口链接已随目录更新。

### 2.3 规范与Skill管理

已写入 `06_programs/sn100_host/AGENTS.md`：原版参考边界、邻近代码风格、Qt/C++/平台与对象归属、逐模块验收、资源与代码删减规则。根AGENTS提供入口；不改变其他程序的局部规则。

后续可复用流程采用根 `.agents/skills/<skill-name>/SKILL.md`，常驻规范仍在AGENTS，待办仍在计划，实测仍在本文。本轮仅设计结构，未预建空Skill或安装候选。第一次模块流程走通后，再提炼专用迁移Skill并验证触发、引用和实际使用效果。

## 3. 用户确认的执行与验证方式

采用“**一个模块移植完成 → 该模块在现有仪器验证 → 通过后进入下一个模块**”，全部模块之后仍做累计/端到端回归、资源与实时并发、长稳、部署和回退验收。

每个模块先明确原版入口/行为、SN100配置、必需依赖、输入/预期输出、异常路径和可观察的仪器结果；完成Windows与WSL ARM64构建及必要回放，再进行RK实屏/仪器验证。CPU或依赖等无关模块可先分析准备，不能因资料齐全就把未实测模块关闭为完成。纯UI/数据模块用实际屏幕与匿名数据及相关仪器链路验证，不强行安排机械动作。

连接参数待用户后续提供，本轮不猜测、不连接；现有RK部署地址不等同于已确认的仪器主控地址。实际允许的查询/写入/动作随当前模块范围明确，不直接启动原主程序沿用旧自动配置。

首批候选V01为**最小连接和状态/版本显示**。代码依据：

- `commandsender.cpp:271` 生成候选状态查询载荷Q/j；完整协议还包括头、长度、XOR和尾，必须确认当前SN100固件和连接角色。
- `commandreceiver.cpp:184–200` 每5次状态回复触发 `signalSendTime()`，主窗将其连到发送时间；首次登录收包另有试剂请求/配置检查。这些副作用需要隔离，不能把整条旧调用链当纯只读。
- 状态解析对空/短字段需要边界检查，首批同时验证错误长度/校验、拆包/粘包、断线/超时和重连显示；与原版分时对照，未确认多客户端能力前不同时抢占连接。

**V01目前只做了范围和调用链分析，尚未实现或验证。** 详细候选顺序和模块卡字段维护在[计划2.6节](../01_docs/plans/sn100_host_migration_plan.md#26-逐模块移植和现有仪器验收用户已确认)。

## 4. 用户新增的资源调配和代码删减习惯

### 4.1 每个模块都核查运行资源

记录与输入/数据量/吞吐/显示模式对应的CPU、RSS/PSS及峰值、线程/FD/队列、响应延迟、日志和磁盘增长；在相关阶段补温度/降频、图表/导出压力以及对同板控制/RT任务的影响。先确定整机与GUI/算法/控制/系统余量预算，再按测量调整线程/轮询/重绘/缓存等，不能假设Windows必然更快或RK必然承受不了。

优化和对照使用同功能、同数据规模/负载与Release条件；Debug断点不用于性能结论。隐藏页可暂停纯展示刷新，必要通信/采集/告警继续运行；不通过丢原始数据、停日志取证或删业务功能求轻量化。当前没有完整上位机资源实测，不能填写推测数值作为通过依据。

### 4.2 删减前先验证，正式删后再回归

无用代码允许删减，但先证明不影响已承诺功能：核对Windows/RK两目标构建、调用链、配置/机型开关、字符串SIGNAL/SLOT、自动槽、插件/动态加载、资源/QRC及异常恢复路径。搜索不到引用、菜单被注释、当前数据未走到分支，不单独构成删除证据。

先保留模块基线，必要时在可撤回工作副本做禁用/候选补丁的对照试验，覆盖受影响功能、数据/报文、异常与适用仪器用例；证据支持后再纳入正式删减。正式删后再次构建/回归，记录实际资源变化和回退方式；证据不足或对照失败时保留。老板参考原件不做代码清理，实际日志、错误处理、算法和未核实型号功能不能当“无用”删掉。

本习惯已同步工程AGENTS、详细计划、[工程说明](../01_docs/knowledge/sn100_host_project_guide.md)和个人 `work-habits/SKILL.md` 的本项目条款。**本轮没有删减任何业务代码。**

## 5. 后续过程如何追加

同一模块的分析、修改、构建失败、部署、仪器验证、资源测量、问题和复测放在一起，以日期/模块ID作小节；后续模块继续追加在本文，不新建另一份移植主记录。需要时再增加表格/截图和详细命令，不预填未发生结果。

每次实际推进保留：模块ID/范围与源代码对应、构建版本/Qt/工具链/SHA、输入和原版预期、Windows/RK实际结果、具体TX/RX或其他可核对输出、屏幕/DB/实物变化、资源条件与数据、错误/退出码、删减依据（如有）、缺陷与复测、证据及回退/下一步。编译、回放、板端、仪器分开标记。

原图与标注分开放 `03_photos/`，原始日志放 `temp/sn100_host/`，需保留的证据按事项归档到 `04_references/sn100_host/` 并从本文链接；计划只同步状态。患者/凭据信息不进入Git、截图或日志。2026-10-22 当时下一步候选为 V01；2026-10-23 按用户新要求先推进 O01 离线模块，具体当前状态只以计划模块卡和实际回收证据为准。

## 6. 2026-10-23 工程化 Skill、文件管理和业务可读性

### 6.1 本次范围与实际变更

用户要求查找工业 Qt 上位机的相关 skill/工程规范，并把可运行、稳定、不卡顿、RK3576 可行性、文件管理和人可理解/修改的业务代码要求落地。核查时工作树已有 O01 入口、UI、数据访问及构建/测试文件，计划和局部 AGENTS 正在更新；本次保留这些未提交工作，只新增项目技能并补充入口、说明和状态，不移动/改写业务源码。

本次操作仅写流程 Markdown，不安装依赖、不覆盖运行配置/数据，不需要额外备份；未修改参考工程或 RK 环境，未连接板卡/仪器。本节不替另一项正在进行的 O01 实现工作登记构建/功能/性能结果。

新增正式文件：

| 文件 | 作用 |
| --- | --- |
| [sn100-module-migration/SKILL.md](../.agents/skills/sn100-module-migration/SKILL.md) | 当前模块读取、原版行为/依赖、实现、分环境验证及交付流程 |
| [engineering-and-verification.md](../.agents/skills/sn100-module-migration/references/engineering-and-verification.md) | 人能理解的业务命名/调用流程/规则注释、适度分层、Qt5线程/SQL/停止边界、测量与RK验收 |
| [sn100-project-files/SKILL.md](../.agents/skills/sn100-project-files/SKILL.md) | 源码/资源/夹具/生成物/运行数据/证据/备份归属，移动改名的引用依据及Git/LFS核查 |

根/局部 AGENTS 已要求相关任务开始时读取，规则变化或任务切换后重读相关部分。完整 Windows/WSL 路径、人工阅读方式、`$名称` 调用及可复制任务提示词集中在[工程管理说明第3节](../01_docs/knowledge/sn100_host_project_guide.md#3-已建立的skill与使用方法)，README 和既有两份计划同步入口/勾选；没有新增平行移植主记录或全局技能。

### 6.2 检索与采用依据

找到 Qt 官方团队的 `qt-cpp-review`、`qt-cmake-project`、`qt-qml-profiler` 等公开 skill，主要针对 Qt6；通用 `file-organizer` 针对下载/文档整理。选择参考审查维度和盘点方法，不直接套用 Qt6/CMake/QML 升级、按扩展名拆散文件或按年龄清理。具体一手链接与适用范围见[工程管理说明第5节](../01_docs/knowledge/sn100_host_project_guide.md#5-移植skill和参考资料检索结果)。未安装或运行这些第三方 skill 的脚本。

常用术语及项目落点为编码规范、架构约束、非功能需求、完成定义/质量门禁、软件配置管理/可追溯性；公开规范各有工程范围，不把某种目录、风格或响应数值称为所有“大厂”统一要求。业务可读性要求包括：从操作入口追到校验/业务规则、状态或数据变化、输出及失败恢复；关键规则/单位/副作用用简短中文说明；复杂度不足时不增加框架、空类和无用接口。

“不卡顿”落实为固定负载/Release/显示条件下的操作响应分布与最大值、事件循环延迟、CPU/RSS/PSS/线程/FD/队列和存储增长、异常恢复与长稳；板端另验实际显示/输入、温度/降频及同板控制影响。参考中的 100/500 ms、2/8/24–72 小时只是待明确条件的项目候选，不是已确认阈值、行业标准或通过数据。缺设备时继续当前获准离线范围，RK/仪器保持待验。

### 6.3 本次核查结果与边界

- 使用本机 WSL `Ubuntu-24.04 / qyq / python3` 执行全局 `skill-creator/scripts/quick_validate.py`，分别检查两个新 skill；两次输出均为 `Skill is valid!`，退出码均为 0。没有新增可执行脚本。
- 最终本地 Markdown 相对路径检查覆盖 README、管理说明、两份计划、本文与三个 skill 文件：8 份文件、141 个本地链接，缺失为 0；最终复查两 skill 仍均为 `Skill is valid!`。`git diff --check` 通过。
- `git check-ignore -v` 核对新 skill、正式 `.pro`、匿名夹具示例路径和临时日志路径：仅 `temp/sn100_host/logs/sample.log` 命中根 `/temp/`；新 skill/源码/夹具没有被忽略。示例路径只用于规则匹配，没有为此创建假数据文件。
- 独立子代理实际读取两 skill、项目入口、当前模块卡与源码，按“无 RK/仪器，整理医院设置业务与文件并判断完成状态”的受限场景审查：正确保留现有入口/UI/存储结构，未要求先建统一框架，区分 `data/` 源码与临时运行库，也未把窗口可打开或历史测试日志当稳定完成。当前 skill 未发现内部路径断裂或实质冲突；实际模块结果继续在对应实施小节取证。

以上证明流程文件结构有效、引用可达且场景判断符合当前边界。**未实测客户端新增技能选择器；未据此新增 Windows/WSL 业务运行、ARM64构建、RK实屏、仪器、长稳或资源通过结论。** 后续在真实模块交付中执行和完善这些规则；技能正文按需读取，不是每行代码都自动重读，更不能代替实际验收。

### 6.4 原版界面保持要求与 O01 首版差异（2026-10-23）

用户反馈另一聊天开发的首版与原界面明显不同，明确要求后续移植的颜色、布局尽量保持原版，仅做 Linux 与 RK3576 屏幕必需调整；单模块移植也遵循此要求。本轮使用 `skill-creator` 更新项目技能，未修改并行开发中的业务源码或截图。

只读对照原 `frmuserset/frmhospinfosetting.ui/.cpp` 与正式工作版 `ui/frmhospinfosetting.ui/.cpp`，可确认：

| 差异 | 源码依据与待处理范围 |
| --- | --- |
| 配色/控件状态 | 工作 `.ui:8–19` 新增浅蓝底、蓝色实心选中/保存等 QSS；原 `.ui:16–35` 为白/透明底、灰边框，原 `.cpp:251–262` 选中标签使用主题色下划线。原实际主题还依赖共享样式/加载链，未读取运行 INI 来推定数值 |
| 信息顺序 | 原 `.ui` 中表格在上、医院名称在下；工作 `.ui:37–49` 将医院名称放到标签和表格之前，属于区域顺序变化，不能仅按缩放解释 |
| 实验信息与字体层级 | 工作 `.ui` 加入原页没有的离线横幅/标题/来源/路径等，并改变默认字号和标题层级；Linux 字体可能需要替代，但整套视觉变化尚无必需性依据 |

以上是源码差异和用户反馈，不是本轮运行截图验收。O01 模块卡已增加“首版视觉对照与纠正”待办；当前新样式不能自动成为后续模块基线，已有功能/数据结果保留其原验证范围，不据此标视觉通过。

模块 skill 正文增加保留原版的核心规则，参考增加“原版界面保持与最小适配”：先查 `.ui`、共享/动态样式、主题/字体/图标及截图；必要改动逐项记录原依据、前后表现、平台/屏幕原因和对照结果；实验说明不重做业务区域，缺视觉依据时保留源码风格并登记待核实。文件 skill 补充主题/资源关联的保留要求，根/局部 AGENTS、README、知识入口和两份计划同步；G4 中原先笼统要求全面纳入 layout/标准紧凑布局的条款收窄为实际需要时做最小适配。

更新后两个 skill 的 `quick_validate.py` 均输出 `Skill is valid!`、退出码 0，`git diff --check` 通过。本次交付是规则和待办更新，**尚未纠正首版界面代码、重跑应用、完成原版截图对照或 RK 屏幕验收**，未向另一聊天发送消息；后续按同一模块卡落实纠正并复测。

## 7. 2026-10-23：WSL Qt 环境与 O01 医院信息脱机实验

**7.1–7.5 保存首版功能实验；当前源码布局纠正和最新复测见 7.6。首版截图不作为后续模块的视觉基线。**

### 7.1 这次完成了什么，文件放在哪里

按用户最新要求先做一个能运行、能操作、能核对数据的真实业务切片。选取原上位机“用户设置 → 医院信息设置”，保留医院名称、科室、病人类型和送检/检验/审核医生五类字典。当前是单独页面，完整主界面、登录/权限、算法、LIS 与仪器通信尚未接入；没有从参考包复制生产数据库、INI、患者数据或二进制库。

| 内容 | 唯一入口/位置 |
| --- | --- |
| 源码及日常使用 | [工程简明说明](../06_programs/sn100_host/README.md)：入口 `main.cpp`，页面 `ui/`，存储 `data/`，业务验证 `tests/` |
| 模块范围、依赖及下一步 | [移植计划当前 O01 卡](../01_docs/plans/sn100_host_migration_plan.md#20-当前离线模块-o01医院信息设置)，不在本记录复制另一份待办 |
| 构建、可变运行数据、临时日志 | 根 `temp/sn100_host/`；WSL、Windows32、ARM64 构建各自隔离；`runtime/` 是继续点击体验用的数据副本 |
| 长期实验数据与日志 | [offline_hospital_20261023](../04_references/sn100_host/offline_hospital_20261023/)：测试数据库、INI、标记、实际 CSV、JSON、测试日志及 SHA256SUMS |
| 原始截图/标注 | 根 `03_photos/20261023_sn100_hospital_*`，原 PNG 与可编辑标注 SVG、渲染 PNG 分开保存 |
| 安装与构建输入 | [WSL amd64 安装包与清单](../07_backups/qt_environment/wsl_amd64/source.json)、[新增 ARM64 SQL 输入](../07_backups/qt_environment/sn100_arm64/source.json)；复用已有 `qt5_arm64/` 基础归档 |

环境变更仅为 WSL 新增 **169 个包、约 298.95 MB 安装包**（首批 165 包，随后补 native pkg-config 及 3 个依赖），未升级或删除已有包。Qt Creator 的四份 Kit XML 先前不存在，本次新登记原生 Qt5/GCC/GDB Kit，未覆盖旧 Kit；不需要整机备份。精确版本、安装前后清单和恢复范围见上述 source.json；`.deb` 已匹配 Git LFS。Windows Qt、现有 ARM 交叉编译器和 RK 均未更改。

WSL 为 Ubuntu 24.04.5 / x86_64，Qt/qmake **5.15.13**、G++ **13.3.0**；Qt Creator **13.0.0** 本身使用 Qt6.4.2，但本工程 Kit 明确使用 Qt5.15.13。通过 XCB 接入 WSLg，1200×760、Fusion/Noto 中文字体；IDE Kit 已登记，实际终端构建运行通过，IDE 内 F5 调试本轮未验收。构建脚本限定 Linux 工具 PATH，避免继承 Windows Strawberry `pkg-config`。

### 7.2 原版对应、业务入口与有意差异

原版依据为 `frmuserset/frmhospinfosetting.{ui,h,cpp}` 的表头、五类切换、增删改查、`memoniConvert()` 和医院名键，以及 `tool/database/hospital/hospital_information_table.{h,cpp}` 的表结构。原五个文件的 SHA 和字段映射见[来源清单](../06_programs/sn100_host/origins.json)。20,902 字符助记符映射逐字一致；原/新算法对 65,536 个 UTF-16 码元的静态语义对照差异为 0，运行测试另外核对“血液科→XYK”等真实显示。静态对照不代替完整 Windows 原程序运行基线。

维护时从 `main.cpp` 查看数据路径和线程生命周期；从 `ui/frmhospinfosetting.cpp::on_btnSave_clicked()` 追到输入校验、`requestSave`、`HospitalStore::save()` 事务和结果回显；字段约束集中在 `data/hospital_store.cpp::validateSave()`。本切片不包含仪器命令。

保留原 `hospital_information(id,column_one,column_two,column_three,type)` 及 `keshi/bingren/songjian/jianyan/shenhe` 含义。名称最多 6 个中文/字母/数字；备注最多 32 字符；医生工号最多 6 个字母/数字。助记符自动生成且只读。

本轮有意适配如下，不能称为已证明原完整软件等价：

- 原绝对坐标页面改为布局，保留原类名和主要控件名，采用 Linux 可用字体及标准确认框；软键盘后续另验。
- 页面只投递请求并显示结果，SQLite 连接在独立工作线程创建/使用/关闭；写入用绑定参数和 `BEGIN IMMEDIATE`，200 ms 锁超时，`synchronous=FULL`，不沿用旧 DAL 强制关闭同步的写法。
- 原逻辑先写医院名再逐行保存，可能部分成功。本切片先校验全部输入，再将当前类字典和医院名一起事务提交。医院名以新增 `app_settings` 的 `hospital/name` 为权威，提交后原子导出同键 UTF-8 INI，保留其他 INI 键；导出失败独立警告并可重试，不能当成数据库未提交。此方案仍需后续整合全程序配置读取。
- 只接受独立实验目录：新目录建立 `offline-data.tag`，已有未标记库/INI、无效标记和参考目录被拒绝。定向边界检查 14 项通过，见[检查结果](../04_references/sn100_host/offline_hospital_20261023/storage_boundary_checks.json)。这是实验数据保护，不是生产库升级/导入机制。

没有删除原参考工程。未引入完整 `AllDal`、`SystemCommon`、登录或算法依赖是本模块范围选择；没有据此宣称这些功能无用或已完成全库删减。布局/线程/SQL 修改有行为回归，未测得完整原版资源基线，因此不宣称优化百分比。

### 7.3 实际界面、输入与数据结果

QtTest 向真实 XCB/WSLg 窗口投递鼠标点击、键盘粘贴及确认框操作，运行的是正式页面与真实 SQLite 工作线程，不是空实现。写入进程 PID 771、另起读回进程 PID 810 均退出码 0；分别 3 passed / 0 failed（包含初始化和清理）。写入实验约 8.697 s，重开读取约 0.230 s，后者不是应用冷启动指标。用户还可在已打开的正式入口继续手动操作。

![O01 保存后实际界面与圈注](../03_photos/20261023_sn100_hospital_saved_marked_01.png)

[未修改原图](../03_photos/20261023_sn100_hospital_saved_01.png) · [可编辑标注](../03_photos/20261023_sn100_hospital_saved_marked_01.svg)。截图为实际 QWidget 抓取；标注没有改写界面中的字段和终端数据。

最终医院名称为 **WSL脱机复测医院**，数据库实际内容如下（全部为新建匿名实验数据）：

| type | 名称 column_one | 助记符 column_two | 备注/工号 column_three |
| --- | --- | --- | --- |
| keshi | 血液科2 | XYK2 | 锁库重试 O'Brien 中文 |
| bingren | 门诊 | MZ | 门诊 O'Brien |
| songjian | 送检员 | SJY | SJ001 |
| jianyan | 检验员 | JYY | JY001 |
| shenhe | 审核员 | SHY | SH001 |

五类各 1 条、共 5 条；SQLite `integrity_check=ok`，医院名 INI 回读一致。先分别新增五类，再修改科室；取消及切换类别的“保留/放弃”均有核对；删除的“取消/确认”均验证，点击第二行后只删选中行；空名称被拒绝。中文、数字、引号未截断或误当 SQL。原始数值及期望对照见[write_result.json](../04_references/sn100_host/offline_hospital_20261023/write_result.json)、[实际 CSV](../04_references/sn100_host/offline_hospital_20261023/actual_data.csv)。

![独立进程重开读回](../03_photos/20261023_sn100_hospital_reopened_marked_01.png)

[重开原图](../03_photos/20261023_sn100_hospital_reopened_01.png) · [重开实际字段](../04_references/sn100_host/offline_hospital_20261023/read_result.json)。第二个进程重新创建连接并逐类读回，字段/行数与写入进程一致。

![数据库锁冲突失败与可见错误](../03_photos/20261023_sn100_hospital_locked_error_marked_01.png)

[错误原图](../03_photos/20261023_sn100_hospital_locked_error_01.png)。独立连接持有写锁时点击保存，约 220.432 ms 后明确失败；期间有 22 次 GUI 心跳、最大间隔 21.287 ms，关闭窗口被拒绝以等待真实结果。失败前后数据库快照完全一致，界面保留草稿，解锁后重试保存成功。

### 7.4 响应、资源与修复依据

负载为五类各 1 条记录，Release 构建，在上述 WSLg 窗口做 CRUD/异常及 **100 次类别切换＋100 次保存**。操作完成时间从鼠标事件到结果信号及 UI/DB 读回；定时器间隔用于观察事件循环阻塞，不等于屏幕绘制延迟或硬实时保证。测试的 150 ms 最大心跳间隔断言是本实验的短时回归保护值，不是行业标准或 RK 放行门槛。

| 观测项 | 实际值 |
| --- | --- |
| 类别切换（含前置操作，107 样本） | P95 8.169 ms，最大 12.744 ms |
| 成功保存并重读（108 样本） | P95 93.754 ms，最大 103.415 ms |
| 10 ms GUI 心跳（837 样本） | P95 间隔 11.412 ms，最大间隔 51.864 ms |
| 锁库失败单例 | 220.432 ms；无部分提交；GUI 仍处理心跳 |
| 正式入口静置约 5.17 s 的采样 | RSS/峰值 RSS 102,332 KiB（约 99.93 MiB），PSS 最大 94,843 KiB，4 线程、12 FD |
| 同一静置窗口 CPU | 按进程 CPU ticks / 墙钟 / 单逻辑核计算为 0.0%；采样精度下未增长，不表示任何负载均为零 |
| 测试数据文件 | SQLite 20,480 字节，INI 38 字节；没有生产负载或长期增长结论 |

[操作指标汇总](../04_references/sn100_host/offline_hospital_20261023/performance_summary.json)使用 nearest-rank P95；[运行资源原值](../04_references/sn100_host/offline_hospital_20261023/runtime_resources.json)保留全部采样。当前没有业务队列压力、冷启动计时、长稳、RK 资源预算、温度/降频或同板实时控制数据，均保留待验。

| 发现的问题 | 本轮处理与验证 |
| --- | --- |
| cellWidget 吞掉点击，点击第二行后表格仍选第一行 | 焦点/点击时动态同步当前索引；真实点击后删除回归通过 |
| 类别读取失败会把旧草稿当作新类别 | 恢复已显示类别；保持错误可见，避免错误归类 |
| 保存中关闭窗口会误导为取消保存 | 忙时拒绝关闭，等待保存结果；锁库实验实际覆盖 |
| 保存已提交后再次读取失败，可能丢新 ID 导致重复插入 | 先接收已提交快照及 ID，再发独立复核读取；错误明确区分“已提交、复核失败” |
| INI 导出可能清掉其他键或误用历史库 | 保留键、无效文件不覆盖、实验标记与参考路径保护；14 项定向检查通过 |
| deferred 写事务先读后升级锁，可能立即失败 | 改为 BEGIN IMMEDIATE，先取得写锁再校验/写入；实际测得锁等待、无部分提交和解锁重试 |

前两次实验暴露的问题在修复后使用新实验目录复测，正式归档只选最终 `agent_gui_20261023_03` 的证据；失败过程仍在本机 temp 日志，不混作通过数据。

### 7.5 构建、复现与后续状态

同一 O01 源码的 WSL x86_64 / Windows Qt5.14.2 MinGW32 / ARM64 构建均通过。ARM 是 ELF64 AArch64，SHA-256 为 `5bedfdbe2d1b3279ffb3f92289b47d7634ec245064490028d96d2d02e2b092e2`；Windows 只构建本切片，未构建或运行参考原工程。全部源文件和三目标产物指纹见[build_metadata.json](../04_references/sn100_host/offline_hospital_20261023/build_metadata.json)。本次没有部署板端；SQLite 插件、EGLFS 和实际外设仍需在目标系统核对。

在 WSL `Ubuntu-24.04 / qyq`、项目 `06_programs/` 中运行正式入口：

```bash
bash sn100_host/wsl.sh run
```

打开登记好的 WSL 原生 Qt5 Kit：

```bash
bash sn100_host/wsl.sh ide
```

重做实验须使用新的绝对目录；先写入、再另起进程读回，每条命令独立执行：

```bash
SN100_EVIDENCE_DIR="$PWD/../temp/sn100_host/experiment_new" bash sn100_host/wsl.sh test writeExperiment
```

```bash
SN100_EVIDENCE_DIR="$PWD/../temp/sn100_host/experiment_new" bash sn100_host/wsl.sh test readExperiment
```

上述目录若已有数据库，写入实验会拒绝覆盖。正式归档数据保持只读证据，继续体验使用 `temp/sn100_host/runtime/` 副本。环境重装入口为 `setup_wsl.sh`（只安装阶段使用 root）；开发、构建与运行均用 qyq。ARM64 `build.sh` 接收一个绝对输出路径并只在编译/架构检查成功后替换目标，依赖从正式归档恢复，不连接设备取文件。

**状态：O01 的 WSL 离线短测通过，可继续依赖满足的离线模块；O01 完整原版行为对照、板端显示/资源、仪器相关整合、长稳与同板控制仍待验。** 下一模块范围在计划中收敛；原代码未知用途的部分继续保留。根 README、总体计划、当前模块卡与 Qt 环境入口已同步，未自动提交或推送 Git。

### 7.6 原版界面纠正与复测

按本轮新增的界面保持要求，对首版多余的重新设计做了纠正。当前窗口保留原 `.ui` 的 **801×551 业务区域**，在独立宿主中居中；该区域内 17 个原业务控件的坐标、尺寸和层级逐项相等。医院名恢复至表格下方，底部仍为“添加 → 删除 → 保存 → 取消”。标签页、表格及按钮沿用原局部/共享 QSS，移除首版新增的大标题、横幅和区域重排；刷新、结果和数据路径单独放实验页脚。

![当前版本：原源码布局纠正后的实际WSLg界面](../03_photos/20261023_sn100_hospital_restored_marked_01.png)

[未修改原图](../03_photos/20261023_sn100_hospital_restored_01.png) · [标注SVG](../03_photos/20261023_sn100_hospital_restored_marked_01.svg) · [17个控件的坐标及样式来源对照](../04_references/sn100_host/offline_hospital_20261023/visual_revision/visual_geometry_comparison.json)。上述对照是原源码结构与实际 WSL 窗口的核查，不是原 Windows 发行版运行截图。

必要适配只有独立宿主的位置、Linux 缺失 PingFang 时的 Noto 字体替代，以及移除无效 pointsize=-1 属性；原 18px 样式与字号层级仍保留。颜色依据为原 `.ui`、`frmhospinfosetting.cpp`、`public/stylecolor.cpp` 和交付参考 `theme_config.ini` 中明确可用的非敏感视觉键，运行时不加载参考配置。实际 Windows 活动主题、原生按钮/表头和字形度量仍待对照。保留原样式后 WSL 的按钮呈灰底；[Qt5 官方说明](https://doc.qt.io/archives/qt-5.15/stylesheet-reference.html#qpushbutton-widget)指出原生边框可能盖住背景色，因此没有为追求主题键中的蓝色而另加换色规则。

用新实验目录 `agent_gui_20261023_04` 重做相同的全部 CRUD、取消/删除确认、空值拒绝、锁库、200 次重复操作及两个独立进程重读。写入 PID707、重读 PID719 均为 3 passed / 0 failed、退出码0；数据仍是前表五类各1条，医院名/字段/助记符/引号/INI一致，完整性检查为 ok。写入约8.758s、重读223ms；重读数字不作为冷启动指标。

| 最新短测项 | 实际结果 |
| --- | --- |
| 类别切换，107样本 | P95 7.426 ms；最大11.673 ms |
| 成功保存并重读，108样本 | P95 85.835 ms；最大105.235 ms |
| 10ms GUI心跳，837样本 | P95间隔11.091 ms；最大间隔68.176 ms |
| 锁库失败 | 231.553 ms，期间23次心跳、最大间隔24.364 ms，无部分提交；解锁重试通过 |
| 当前正式入口约5.06s静置采样 | RSS/峰值103,064 KiB，PSS最大93,949 KiB，4线程、12 FD；CPU ticks采样未增长，按单核口径0.0% |

最新数据、CSV、两进程日志、资源采样和指纹集中在[visual_revision](../04_references/sn100_host/offline_hospital_20261023/visual_revision/)，与首版证据分开。其[write_result](../04_references/sn100_host/offline_hospital_20261023/visual_revision/write_result.json) / [read_result](../04_references/sn100_host/offline_hospital_20261023/visual_revision/read_result.json)保留期望与实际值；[重开原图](../03_photos/20261023_sn100_hospital_restored_reopened_01.png)和[锁库错误原图](../03_photos/20261023_sn100_hospital_restored_error_01.png)可直接核对。

纠正后重新构建 WSL x86_64、Windows Qt5.14.2/MinGW32 和 ARM64，均成功。当前 ARM64 SHA-256 为 `4af975de37764d403ea994ae392cbaf5400468a9cf9eb1ef9e75f8bb6744f522`，三目标指纹与当前源文件清单见[最新版构建数据](../04_references/sn100_host/offline_hospital_20261023/visual_revision/build_metadata.json)。原首版正式入口已正常关闭、退出码0，当前已启动纠正后的正式程序，继续使用 `temp/sn100_host/runtime/` 的可变体验数据，不覆盖归档。启动命令仍是 `bash sn100_host/wsl.sh run`。

**当前结论：WSL环境、O01脱机功能/数据与短时响应验证通过；无依据的首版视觉重设计已按原源码纠正。原 Windows 发行版视觉/完整行为、RK实屏与资源、仪器、长稳和同板控制仍待验。** 下一步先补原版视觉基线与必要平台差异对照，再按计划收敛下一离线模块；不因本轮通过而宣称完整上位机已移植完成。

### 7.7 按更新后的 skill 复核医院界面与 WSL 复测

用户在本窗口明确要求按最新 skill 恢复医院信息模块的原版颜色/布局、保留业务功能并在 WSL 验证。接手时共享工程已有 7.6 的纠正版，本轮保留该成果，以原源码再次逐项核对；未覆盖另一窗口的账户模块，也未擅自重做视觉。只纠正医院页源码注释和 `origins.json` 中“Noto 替代不可用 PingFang”的笼统说法：原工程 `main.qrc:50` 含 `fonts/PingFangBold.ttf`，原入口会加载它。当前正式副本使用已安装 Noto；原字体的引入/使用条件与实际字形度量仍待核实，不能称 Linux 必然不支持原字体。

![本轮实际 WSL 界面及标注](../03_photos/20261023_sn100_hospital_visual_recheck_marked_01.png)

[未修改原图](../03_photos/20261023_sn100_hospital_visual_recheck_01.png) · [可编辑标注](../03_photos/20261023_sn100_hospital_visual_recheck_marked_01.svg) · [本轮全部数据与日志](../04_references/sn100_host/offline_hospital_20261023/visual_recheck/)。标注位于原图外，未修改或遮盖字段；原图与本轮 QtTest 的 `saved.png` SHA-256 相同。

**视觉核对：**业务面板 801×551，16 个子控件均保留且 XML 坐标/尺寸与原版相同（面板加子控件共 17 个）。原标签在表格上方，医院名位于表格下方，底部顺序为“添加 → 删除 → 保存 → 取消”；保留原共享按钮/表格 QSS、选中下划线和参考视觉键，不再使用首版重排与整套新配色。独立宿主居中和业务区域外的脱机页脚单独列为宿主差异；[逐项几何对照](../04_references/sn100_host/offline_hospital_20261023/visual_recheck/geometry_check.json)为源码结构检查，不等同原 Windows 活动主题/截图对照。

**构建与实际运行：**WSL Ubuntu-24.04 / qyq、x86_64、Qt5.15.13、G++13.3、Release、WSLg/XCB。使用新的 `temp/sn100_host/visual_recheck_20261023_iyyn4n/` 构建/数据目录，未共用其他窗口的默认构建树或可变数据库。先干净构建医院入口及既有 `offline_test`，再分别启动写入/重开进程；PID 491 / 498 均为 3 passed / 0 failed（含初始化和清理）、退出码 0，分别约 9.355 s / 0.241 s。

测试实际覆盖五类新增、保存、读取与助记符/工号/备注，医院名称和 INI，科室的共享编辑/删除路径、删除取消/确认、未保存更改的保留/放弃、空字典名称拒绝、锁库无部分提交及恢复，以及 100 次切类 + 100 次保存。其他四类未逐类重复全部编辑/删除分支，不扩大覆盖结论。五类最终各 1 条、共 5 条，包含中文和 `O'Brien`，两个进程字段相同，SQLite `integrity_check=ok`；[write_result](../04_references/sn100_host/offline_hospital_20261023/visual_recheck/write_result.json)、[read_result](../04_references/sn100_host/offline_hospital_20261023/visual_recheck/read_result.json)和[CSV](../04_references/sn100_host/offline_hospital_20261023/visual_recheck/actual_data.csv)保留真实输入/输出。

| 本轮短测 | 实际结果 |
| --- | --- |
| 类别切换，107 样本 | P95 9.145 ms，最大 11.821 ms |
| 保存并重读，108 样本 | P95 93.264 ms，最大 98.574 ms |
| 10 ms GUI 心跳，890 样本 | P95 间隔 11.285 ms，最大间隔 48.543 ms |
| 锁库失败 | 221.661 ms；22 次心跳，最大间隔 21.269 ms；无部分提交，解锁重试成功 |
| 正式入口初次启动后约 5 s 静置采样 | RSS/峰值 101,892 KiB，PSS 最后样本 94,321 KiB，4 线程、11 FD；进程 CPU ticks 在采样精度下未增长，单核口径 0.0%，不代表业务负载均为零 |

**公共入口的并行变化与复核：**测试期间另一窗口接入账户模块，`main.cpp/.pro` 发生变化，医院 UI/存储/测试输入保持不变。中途一次全工程尝试因账户文件未齐失败，后续输入校验又识别到编译期间公共入口变化，均未作为正式通过结果。为避免混合版本，从正式目录读取并二次核对本次构建输入，形成仅在 temp 使用的不可变源码快照，再构建完整入口；未修改正式源码位置或把快照作为第二套工程维护。[构建输入指纹](../04_references/sn100_host/offline_hospital_20261023/visual_recheck/official_build_inputs.json)和[最终构建日志](../04_references/sn100_host/offline_hospital_20261023/visual_recheck/native_build.log)保留来源，临时失败日志留在上述 temp。

最终新产物 SHA-256 为 `30679618dbe75ab458c6df328623367edcc6635dd850bc4ec83712e061e26f81`。实际以 `--offline --module hospital --data-dir <本轮隔离runtime绝对路径>` 打开，人工自动化查看正式窗口、科室/病人类型/审核医生切换、刷新读回，正常关闭并取得 `EXIT code=0`；首个独立入口也正常退出 0。未在正式入口进入账户分支或操作其数据。[正式入口输出/退出码](../04_references/sn100_host/offline_hospital_20261023/visual_recheck/official_hospital_run.log)和[汇总](../04_references/sn100_host/offline_hospital_20261023/visual_recheck/verification_summary.json)已归档；正式窗口的只读操作未改变数据库字节，归档前医院模块源码仍与最终构建指纹相符。

**结论：**原源码颜色/样式和布局已保留，业务功能与 WSL 短时回归通过；本轮未新增 Windows/ARM64 构建或连接 RK/仪器。原 Windows 活动主题、字形及原生控件实际视觉、RK 实屏/资源、长稳与同板控制仍待验。所有新证据有 SHA256SUMS，保留已有未提交修改，未自动提交或推送。

## 8. 2026-10-23：O03 范围审查与并行分工

用户询问能否多窗口并行，并安排本窗口查看03模块需要移植什么。实际只读核查另一聊天最新任务，确认其正在按授权推进 **O02-1账户设置匿名脱机移植**。本窗口读取根README/AGENTS、局部AGENTS、两个项目skill及业务验收参考、现有计划/记录/知识说明，再审查O03相关原源码和正式O01页面接口；两个只读子代理分别复核启动副作用、页面/会话契约与文件冲突。

**结论：可并行分析和准备边界独立的切片，完整O03集成依赖O02接口与离线交付。** 本轮仅修改计划、本文及根README说明，未提取O03业务源码、构建或启动新程序；没有新增运行、响应/资源或视觉通过结论。参考原件、现有业务源码/构建脚本及匿名数据库保持本轮操作前状态，不连接RK或仪器。

### 8.1 O03到底包含哪些工作

下表原版路径相对 `06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC/`，行号为本次静态定位，后续以函数/objectName为准。

| 内容 | 源码依据与本次发现 |
| --- | --- |
| 原主框架与视觉 | `frmmain/frmmain.ui`的`frmMain/frmBg`均为1920×1080；`statusArea`=(167,0,1753,117)，`stkCenterAdptive`=(169,117,1751,963)，菜单列表宽167。布局、原主题、自定义控件和图标须一起审查，不只复制空白栈 |
| 八项主导航 | `frmmain/frmmain.cpp:44`起依次创建试剂设置、样本设置、结果报告、校准曲线、质量控制、分析项目、用户设置、仪器操作；`:197`起固定索引0–7，`:454`把按钮ID接入页面栈。当前不启用被注释的计算公式菜单 |
| 用户设置子导航 | `frmuserset/frmuserset.cpp:81`起创建账户、医院、语言、打印机、LIS五页；`:103`默认进入账户，`:108`起五个切换槽。先接O01、待O02交付后接账户，其余页面必须保留未移植状态 |
| 页面生命周期 | `frmmain/frmmain.cpp:172`起一次创建所有业务页；切换只改索引，且`:455/460`还刷新试剂/项目数据。最小主壳只实例化已交付模块，不能把原刷新副作用漏掉后称完整等价 |
| 焦点、弹窗与软键盘 | 原菜单/开始/暂停使用NoFocus；主壳未找到自定义QShortcut。`public/customeventfilter.cpp:28/48`处理可编辑框及触摸开关下的软键盘；不是现成的通用快捷键系统。`customwidgets/cusmessagebox.cpp:21/39/66/115/144`包含信息、询问、警告、错误与遮罩模态，确认/取消及焦点恢复需分别核验 |
| 会话与权限接入 | 原`frmmain/frmmain.cpp:508/524/2077`连接账户注销、广播刷新并回落Guest；`customtools/userpower.cpp:21`按`can_role_id`显式集合判断通用权限，不能按角色数值大小代替。O02-1不迁移通用权限表，O03不可自行虚构相关能力 |

原UI还依赖`CusMenuButton/CusBadgeButton/CusView/CusFlageImage/QtMaterialFlatButton`，图标多为`./resource/...`运行相对路径；后续提取需保留图标与动态QSS关联，明确资源定位，保持原色/顺序/层级。1920×1080到1600×900的必要适配、Linux字体及原Windows实际主题分别取证；本轮没有生成或比较运行截图。

### 8.2 必须隔离的原启动联动

- 原`frmmain/frmmain.cpp:184/289`创建并打开设备TCP；`:273`起按旧配置创建/自动连接LIS。`:296`起还启动登录/初始化定时器，不能整体搬入构造函数。
- `communication/commandreceiver.cpp:184/198`每5次状态包触发校时，经主窗`:391`接至`frmdriverset/frmdriverset.cpp:2461/2475`发送命令。首次状态包`:595`起以及`frmlogin/maskdlglogin.cpp:76`起还查询试剂、等待并检查配置。
- 主窗`slotTimeTickerOut():699`把显示与设备ready/开始/暂停状态混在一起；`slotShutDown():641`、蜂鸣器`:2041`和告警`:2065`可能发送设备命令。脱机主壳只展示明确的脱机/未知或标明模拟的数据，不启动这些命令链。
- 原`main.cpp`启动中包含旧配置、算法、AllDal和主窗初始化；加载动画还在`:289`起忙等有效帧。本切片沿正式脱机入口准备，不复制原完整启动链或生产配置。

### 8.3 页面接口与并行修改边界

正式O01的`ui/frmhospinfosetting.h:24`已支持传入`HospitalStore*`和父窗口，并提供`isReady()/isBusy()/hasUnsavedChanges()/operationFinished()`。但是`confirmDiscard()`为私有，`ui/frmhospinfosetting.cpp:517`仅在`closeEvent()`检查忙态和未保存；**切换堆叠页不会自动触发该检查**。O03编码前须约定离页/整个主窗退出守卫，覆盖继续编辑、放弃、保存中拒绝离页及操作完成后恢复。worker/数据库停止顺序继续遵守正式`main.cpp`现有关闭库、退出线程并等待的路径。

| 分工 | 独立范围与交接条件 |
| --- | --- |
| O02窗口 | 账户UI/存储、匿名会话、角色业务与账户测试；其接口为单一规则来源，O03不复制另一套账户类。会话快照/变化与注销接口须在接入前确认，密码不传给主壳 |
| 本窗口O03 | 本轮已完成范围审查；后续主壳/用户设置容器、导航与生命周期、典型弹窗及容器测试可独立准备。先嵌O01，待O02稳定后再接账户/权限/注销并累计回归 |
| 公共文件 | `main.cpp/sn100_host.pro/wsl.sh/build.sh/origins.json`、共享样式、根/工程README及唯一计划/记录需要一个整合窗口串行修改，不能依靠窗口不同避免覆盖。本轮保留O02模块卡，只局部追加O03准备卡和本节 |
| 构建/运行数据 | 当前`wsl.sh`固定`temp/sn100_host/wsl_release/wsl_tests/runtime`。后续O03拟使用`temp/sn100_host/o03/`下按架构分开的构建、日志/测试与运行数据；独立目标支持前不与O02同时运行默认脚本，不共用数据库。此路径仅为计划，本轮未创建空目录 |

下一步与可验收子任务已放在[计划O03并行准备卡](../01_docs/plans/sn100_host_migration_plan.md#并行准备卡-o03主框架与页面接入)，包括100次页面往返、未保存/忙态/弹窗/焦点、保存重开和退出、固定条件响应/资源取证。当前只确认静态范围和依赖，O03编码、WSL运行、Windows/ARM64构建、原版视觉、RK/仪器、长稳和同板控制全部待验；离线并行不改变设备具备后逐模块实物补验的门槛。

<a id="o05-1-offline"></a>

## O05-1：协议回放与只读模拟状态（2026-10-23）

用户先安排本聊天做O04；只读核查发现“并行移植 SN100 04 模块”聊天已在同一目录实现该模块，用户随后明确回复“哦，那就继续进行05模块的开发”。本轮因此只开发O05。按 `sn100-module-migration`、`sn100-project-files` 和 `work-habits` 读取当前计划、根/局部规则、工程说明及业务/资源验收参考；既有未提交修改保留，其他窗口的源码和构建不由本轮改写。

**本轮结论：O05-1 源码契约的离线切片通过。** WSL Qt5.15.13 Release 的63项协议测试、25事件回放、10000条状态短测及14项命令行核查通过；Windows Qt5.14.2/MinGW32对同一夹具回放通过，逐事件JSON与WSL逐字节一致；ARM64交叉构建通过。完整O05、真实SN100协议版本、Transport、RK/仪器及长稳仍未验收。

### 实现与原版依据

正式代码在 `06_programs/sn100_host/protocol/`，仅依赖QtCore/C++14/qmake，不创建网络、数据库、仪器、算法或GUI对象。[来源清单](../06_programs/sn100_host/protocol/origins.json)保存8份只读原源码的SHA-256、函数、25字段逐行定位和差异；没有构建或启动参考工程，没有读取其INI、历史生产数据库、患者信息或凭据。

| 维护入口 | 业务职责与结果 |
| --- | --- |
| `frame_codec.cpp::encodeMessage()` | 原 `slotSendCmdToA53/addProtocalShell/GetXorCheckValue` 对应的双向组帧；每片最多2000业务字节，index/total大端16位，XOR包含四字节分包头及业务数据 |
| `frame_codec.cpp::FrameDecoder::feed/expire/reset()` | 收字节→找帧头→校验长度/帧尾/XOR→有界顺序重组→交付完整消息；噪声/错误/乱序/超时明确返回，断线清除半帧和组包 |
| `status_session.cpp::parseStatus()` | 按原 `CommandReceiver::pcQueryStatus()` 解析Qj主字段和原始子系统块，先完整核验局部快照再交付；支持4至25个已知主字段，缺失字段不伪造默认值，未知机型子系统仅保留原始字段 |
| `status_session.cpp::StatusSession` | 模拟连接→只读Qj→等待/观测状态→有限重试/失效→断线/重连；至多一个在途请求，状态不写原全局变量，无校时/首次收包查询或控制动作 |
| `protocol/main.cpp` | 读取显式合成JSON事件，显示具体模拟TX/RX/状态/错误，验证预期，生成日志与汇总；事件分批返回Qt事件循环，输入≤1MiB/10000事件、日志≤32MiB，输出目录独占创建并拒绝覆盖 |
| `tests/protocol_test.cpp`、`tests/fixtures/protocol_replay.json` | 独立literal字节、错误/恢复/边界及匿名合成25事件；不是从仪器抓包。回放产物在独立 `temp/sn100_host/protocol/` |

实际状态查询为 **Qj（51 6A）**。TX外壳 `02 03 LEN_BE16 DATA XOR 04 05`，RX外壳 `06 07 LEN_BE16 DATA XOR 08 09`；DATA含分包头，帧总长为LEN+7。固定输入Qj实际生成：

```text
SIM TX 02 03 00 06 00 00 00 01 51 6A 3A 04 05
```

原源码派生合成Qj输入（不是实物响应）：

```text
Qj[25][SIMULATED][2][1][0][][1][0][1][sim-v1|sim-v2][SIM-001][0][0][0][offline-v1][0][0][1][1][10][LinuxNew][20][18][3][10][0][0]
实际解析：model=SIMULATED，main_status=2/ready，led_status=1，error_work_type=0，general_count=25，subsystems=0。
后续合成main_status=5/testing、6/paused均按同一入口解析并逐字段核对。
```

[完整25事件实际TX/RX](../04_references/sn100_host/protocol_replay_20261023/replay/tx_rx.txt)、[逐事件输入/快照/错误/核对结果](../04_references/sn100_host/protocol_replay_20261023/replay/events.jsonl)、[匿名输入与期望](../06_programs/sn100_host/tests/fixtures/protocol_replay.json)可逐项对照。坏XOR事件不产生状态，后续合法帧恢复；并发Qj被拒绝，模拟第1000ms只重试一次，第2000ms重试耗尽，200000ms无有效状态断线；重新连接后重新接收状态。

### 原版缺陷、有意差异与本轮修复

- 原接收代码前导噪声移除 `stxIndex+1` 会误删帧头，本实现保留完整帧头及跨输入块的首字节；原长度、包数和Qj字段缺少边界检查，本实现拒绝非法输入并避免半更新状态。
- 原半帧超时常量1000在10ms定时器计数，注释存在1秒/10秒矛盾；本轮明确定义为单调时间10000ms无进展。每消息最多64片/128000字节、单次输入65536字节、子系统64块/每块256字段均为本轮本地预算，不能当作已确认的仪器规格。
- 线上没有消息ID、ACK/NAK或业务去重依据。只跳过未完成组包中完全相同的前一分片；完整同值状态继续刷新，不宣称能识别网络上所有重复或延迟旧状态。未来真实Transport须在连接代次和真实协议下另验。
- 1000ms请求超时及最多一次重试只是本轮只读Qj的模拟策略。原静默规则按任意收包/日志更新且首次无包不计时，本轮从模拟连接起按有效Qj计200000ms；查询和接收均统一推进超时。两项策略须与真实SN100版本核实，不能用于自动重发运动/测试命令。
- 原Qj接收每5次校时、登录首包触发试剂/配置检查，本模块不引入这些副作用。原代码存在SN450/SN600字段分支，不能据此确认真实SN100兼容；ASCII字段/已知枚举及未来新增主字段也需实物协议对照。
- 独立审查发现默认快照 `toJson()` 可能越界、事件入口未统一推进静默、回放遗漏 `expect_main_status` 及输出目录检查存在并发竞争；均已修复并分别通过单测/命令行负例。测试辅助代码首轮使用Qt5不支持的接口导致编译失败，修正后重新构建运行；没有把旧测试产物当通过。O01累计回归首条内联WSL命令在shell解析阶段失败，改为临时严格退出脚本后实际重新构建/运行。

### 实际构建、测试和资源

| 验证 | 实际结果 |
| --- | --- |
| WSL协议测试 | Qt5.15.13/G++13.3/x86_64 Release，63 passed / 0 failed，107ms，构建和运行退出0；含literal、所有切点/逐字节/粘包、噪声、方向、错误长度/XOR/尾、分包重复/冲突/乱序、上限、超时、断线、字段/枚举、原子状态及JSON边界 |
| 命令行与独立进程 | 14项核查通过：两个进程相同输出SHA、错主状态退出4、错误预期类型/范围/拼写、分数时间、非法十六进制和不支持动作退出2；已有目录文件SHA不变；两进程争用同一新目录只有一个退出0，另一个退出2 |
| 25事件回放 | 25条记录、7次合成状态、6帧只读Qj、0个预期不符，退出0；事件时间为虚拟单调毫秒，最终WSL回放实际57ms，不代表等待了200秒 |
| Windows切片 | Qt5.14.2/MinGW7.3 32-bit Release，PE32/Intel80386，构建及25事件回放退出0；i386构建、宿主x86_64分别记录。`events.jsonl`与WSL逐字节一致；未运行原完整Windows参考工程 |
| ARM64切片 | WSL AArch64 GCC13.3，ELF64/AArch64构建及NEEDED检查通过，仅依赖QtCore及标准运行库，未连接/部署板卡；[构建与源码指纹](../04_references/sn100_host/protocol_replay_20261023/build_metadata.json)保存三目标SHA |
| O01累计回归 | 在O05专属临时目录干净构建现有O01测试，WSLg/XCB真实交互写入3 passed/0 failed，8631ms；另起进程读回3 passed/0 failed，216ms，均退出0、完整性ok。没有更改共享构建/运行库；不代表尚在并行开发的其他模块已整合通过 |

固定负载为25事件后，以同一合法合成帧额外接收10000次，20次/批、批间返回事件循环；QtCore单线程、无GUI，保留每一帧具体RX及状态行。最终运行如下，**仅是WSL基线，不能折算RK或长期稳定性**：

| 采样项 | 结果与口径 |
| --- | --- |
| 实际持续/输入输出 | 2131ms；10025条记录、10007次状态观测、6帧模拟Qj、0个预期失败；实际日志22735739字节（包含逐事件字段展开），32MiB上限以内 |
| receive解析完成耗时 | 10000样本，P95 0.007777ms，最大0.159591ms；这是解析完成时长，不是GUI可见反馈或网络时延 |
| 10ms事件循环心跳 | 212样本，间隔P95 12.575546ms，最大19.971103ms；包含日志写入负载，无GUI绘制验收含义 |
| 进程资源 | `/proc`目标PID每5ms采样，391样本/约2.1766秒；RSS/高水位峰值11176KiB，PSS最大7871KiB，1线程、最多7FD；CPU为目标进程utime+stime ticks，单核口径约21.59%，HZ=100，含日志输出 |
| 容量/增长 | 回放结束线缓冲0、组包0；事件后观测线缓冲最大20字节，压力部分无残留组包。测试另验证128000字节消息及65537字节单次拒收；事件后采样不代表内部瞬时分配峰值，短测不证明长期无增长 |

资源原始采样、63项测试日志/JUnit、命令行核查、三目标构建记录、Windows/WSL逐事件文件、完整压力TX/RX及SHA清单统一保存在[正式证据目录](../04_references/sn100_host/protocol_replay_20261023/)。压力测试15,485,049字节的重复字段展开JSON留在temp可重建，完整SHA在元数据；长期已保留全部实际TX/RX及状态行，不重复归档该展开文件。全部正式证据逐文件SHA核查；构建产物留temp。

O01本轮累计回归的[保存原图](../03_photos/20261023_sn100_protocol_regression_saved.png)、[另进程读回原图](../03_photos/20261023_sn100_protocol_regression_reopened.png)、[锁库错误原图](../03_photos/20261023_sn100_protocol_regression_locked_error.png)为实际窗口截图，未重绘；本次没有修改业务界面，不新增原版视觉通过结论。

### 复现与交付边界

在WSL Ubuntu-24.04/qyq的 `06_programs/` 下，用新的绝对输出目录运行；已有目录会拒绝，不会覆盖历史结果：

```bash
bash sn100_host/protocol/wsl.sh run --input "$PWD/sn100_host/tests/fixtures/protocol_replay.json" --output-dir "$PWD/../temp/sn100_host/protocol/replay_new"
```

独立协议测试：

```bash
bash sn100_host/protocol/wsl.sh test
```

ARM64构建（仅本地输出，不部署）：

```bash
bash sn100_host/protocol/build.sh "$PWD/../temp/sn100_host/protocol/arm64_release/sn100_protocol_replay"
```

回放可加 `--stress 10000`；`summary.json`最后写出且 `failed_expectations=0`、进程退出0才登记该次通过，缺少汇总或非0退出均不能当成功。未来真实Transport喂入相同codec/状态解析入口，socket对象/线程、接收块限流、连接代次及仪器参数需在V01另验，不能据本轮模拟连接直接上线。

本轮新增协议源码/构建脚本、协议测试、匿名JSON及来源清单；未移动、删减原业务代码或修改参考原件。独立运行数据、构建和日志放temp；只局部同步唯一记录、计划和两个README。无系统/既有配置覆盖，无需额外备份；未连接或修改RK/仪器，未自动提交/推送Git。

<a id="o05-3-offline"></a>
## O05-3：异常恢复、真实200秒静默与小时级长稳（2026-10-23）

用户要求继续05剩余离线异常恢复/长稳，板卡/仪器待验，且避免并行冲突。重读两个SN100项目skill及业务验收参考，沿用work-habits的具体输入输出/正式证据规则。仅修改O05的`tcp_transport.h/.cpp`、新增`soak_main.cpp/.pro`、专属脚本及内部测试/本地监督程序；构建、故障数据与后台状态隔离`temp/sn100_host/protocol/o05_3/`，其他窗口主壳/页面/数据库/GUI测试未操作，公共文档仅补O05局部。无UI变化、业务删减、环境安装/配置覆盖，不需新增环境备份；既有Network正式输入保持。未连接或部署RK/仪器。

### 异常恢复入口与实际验证

`ReadOnlyTcpTransport::createSocket(QObject*)`为唯一新增protected工厂，默认创建真实QTcpSocket，仍强制NoProxy、127.0.0.1数值回环及Qj/Qk/QH白名单。工厂返回空、错误所有权/线程或已有连接时拒绝，工厂回调内改代次也检查；正常CLI没有注入选项。测试通过socket虚函数控制返回值，不引入通用通信框架或生产外部端点。

| 检查 | WSL Qt5.15.13 x86_64 Release | Windows Qt5.14.2 MinGW32 Release |
| --- | --- | --- |
| 故障组 | 14 passed，713ms，退出0 | 14 passed，1082ms，退出0 |
| 注入永久零写60ms预算 | 实测79ms截止 | 实测72ms截止 |
| 注入Connecting60ms预算 | 实测61ms截止 | 实测71ms截止 |
| 真实不读对端 | 7859次入队/65次背压拒收，峰65526字节 | 15141次入队/25次拒收，峰65533字节 |
| 真实写停滞恢复 | 160ms内触发write_timeout并清队列，重连Qj金字节一致 | 254ms内触发write_timeout并清队列，重连Qj一致 |

14项是QtTest报告项（含init/cleanup）。短写、一次/持续零写、−1、短写后−1、Connecting均是**显式测试注入**；正短片实际交给QTcpSocket并在对端核对，不能称OS真实短写。真实不读场景保留正常完整write返回，并以Qt待写无进展触发截止；总预算≤65536。另设SO_LINGER{1,0}后abort，Qt两端映射peer_closed，恢复通过；没有抓包，不据此声称已观察RST位。关闭、旧队列/定时器、error/disconnected直接槽重连、恢复后只收到新的完整Qj均核对。原始日志/XML/退出码在[本轮证据](../04_references/sn100_host/protocol_stability_20261023/offline_checks/)。

### 真实200秒静默与累计回归

新`client_silence_test.cpp`同进程并行两个独立模拟服务/客户端：A连接1.5秒后收一次有效Qj，之后只有Qq/非法Qj；B始终没有有效Qj。195秒时各发QH保留在途等待，静默通知的直接槽再请求必须拒绝，断开后pending只取消一次、fresh清除，再用同一对象重连有效Qj恢复。未缩短或替换计时源。

| 实际截止 | WSL | Windows32 |
| --- | --- | --- |
| A：最后有效Qj后 | 200007ms | 200002ms |
| B：connected后 | 200015ms | 200010ms |
| 整组 | 3 passed/0 failed，201587ms，退出0 | 3 passed/0 failed，201756ms，退出0 |

持续收到其他流量未延长状态期限；3项含init/cleanup。两端源文件前后SHA一致。随后在全新8个构建树累计回归原core63、messages63、TCP27、client19，两端合计344报告项通过、0失败/跳过；所有qmake/build/test退出0、源码前后指纹相同，八组无C++警告或make时钟提示。O05-2旧产物/证据保持；本轮不重复GUI实验。

### 长稳目标、失败与短时复测

`soak_main.cpp`仅自建本机模拟端，Qq携带严格递增序号，Qj/Qk/QH固定字段逐项核对；查询实际对端RX与独立完整金帧逐字节比较。`ReadOnlyClient`默认本目标120ms/一次只读重试，每N个逻辑请求故意不回首次；恢复模式先排空已发诊断，再送旧连接两字节半帧、20ms后主动关闭并重连。故障窗口内新入队而未获对端RX的旧只读字节单独记`unconfirmed_on_cancel`并按代次退役，不能冒称送达。最终请求完成＋明确取消＝逻辑请求，实际RX＋取消未确认字节＝入队次数×13；旧字节不参与新连接期望。

心跳/查询耗时只存2002个固定桶，p95为1ms桶上界；单在途、待写有界、模拟端Qt待写≤32768。全量TX/RX/解析按默认4MiB分段，单轮总512MiB，旧段不删除；低预算参数可复测，最多4096段。进度每10秒原子写，保存失败退出3。正常退出须真实经过时长，诊断/心跳样本均≥标称90%，无>120秒心跳间隔、字节/序号守恒和自然排空成立；这些是离线负载完整性守卫，不是RK性能门槛。

发现并修复的是本轮新harness边界：①取消窗口内恰好入队的旧重试期望曾带入新连接，现明确记未确认字节后退役；②Windows首轮心跳可早于connected，曾在未连接时触发重连，现首次connected后才启用重连期限；③日志同步失败可能清空peer后继续write，现log返回失败并停止后续I/O，QPointer及终态守卫防重入；④最终摘要写失败时，progress也回写failed/outputs_saved=false，避免留下通过假象。原失败日志保留，不改旧O05-2证据或据此推定仪器问题。

| 最终冒烟 | WSL | Windows32 |
| --- | --- | --- |
| 负载 | 70.052秒，50Qq/秒目标、每2秒重连/每2查询不回首个回复 | 15.009秒，同规则 |
| 实际诊断/恢复 | 3394发送/3394按序收到，34次重连 | 731/731，7次重连 |
| 查询 | 86逻辑＝78完成＋8取消，124入队×13＝1612实际RX字节，0取消未确认字节 | 20逻辑＝18完成＋2取消，28×13＝364实际RX，0取消未确认字节 |
| 心跳间隔 | 6965样本，p95桶上界12ms/最大148.775ms | 1500样本，p95桶上界11ms/最大19.593ms |
| 日志 | 1519575字节，4KiB测试分段382份 | 324408字节，82份 |

两端退出0。另12秒持续模式、3项真实输出故障（总额耗尽、下一段路径被目录占用、摘要路径被目录占用）均按预期0/3退出，失败的progress/stdout不称已保存；8项CLI/旧输出保护均退出2且旧摘要SHA不变。一次70秒测试误给1MiB预算，在48.128秒如实失败，随后改足4MiB测试预算复测，并保留该原始失败。一次2秒后台冒烟因宿主245.957ms心跳间隔致样本不足90%被拒绝，未放宽门槛；改为每阶段12真实秒后冻结worker串行复查通过。单独SIGTERM监督进程测试得到interrupted，自己启动的目标已回收，不进入第二阶段。以上短测不是小时级通过。

原生最终ELF SHA=`41c8b50b1f6bd8bcfb658d3ef5420111089f43b403226dc0d2c8af150f7dfa72`；ARM64新目标构建/ELF/直接DT_NEEDED通过，SHA=`c0b7bc6339a9589a850d7a9d220a3695b2f71e9c965f905df54393d2f310b1a4`，没有板端加载或部署。初期DrvFs生成Makefile曾领先毫秒产生提示；新专属构建脚本qmake后等待150ms，不改时钟/环境，最终构建及八组累计回归日志单独保存。

### 正式小时级任务：2h通过，8h恢复中途失败

北京时间2026-10-23 **17:54:33**启动`long_run_01`，监督PID2332、初始目标PID2333；PID仅作这次取证，不作为后续盲目操作依据。先7200真实秒持续通信，通过后再28800秒每60秒半帧/重连；均10Qq/秒、每秒轮询只读查询、每10个逻辑请求不回首个回复。冻结后的目标及本地监督程序在专属任务运行，不随工作源码后续变化；每阶段前核对目标SHA。Python仅用于本机采样/监督，不在RK运行。

监督器每1秒流式保存/proc RSS/PSS/CPU/线程/FD，统计峰值、单核CPU百分比及首/末600样本均值；目标10秒原子进度，正常运行不写公共文档。采样暂停或墙钟/单调钟偏差>120秒即失败，不改电源策略，不将宿主长休眠算成持续通信；异常只终止自己启动的子进程并保留证据。阶段结束再独立流式重核全部日志、序号、金字节和退出码，保存完整资源及SHA到正式专属目录。启动时两阶段均未勾通过；现已回收：**2h持续通过，8h恢复在19分10.178秒失败**，详见下方终态复核。

用户选择结束或失败自动通知，曾建立本聊天heartbeat“SN100 O05 长稳结果回收”（`sn100-o05`），每15分钟检查；正常及仅第一阶段完成时保持安静。20:27轮询发现失败后只读核对、保存原始证据并局部更新文档，已删除该自动跟进；没有修改冻结程序、重跑测试或操作其他进程。终态见[正式任务状态](../04_references/sn100_host/protocol_stability_20261023/long_run_01/job_status.json)，原始状态仍保留于`temp/sn100_host/protocol/o05_3/long_run_01/status.json`。

[937份已完成检查及SHA](../04_references/sn100_host/protocol_stability_20261023/offline_checks/SHA256SUMS)包含异常/静默/八组回归、全量分段收发、失败及复测、33个源码/15个产物指纹；[冻结长稳输入及来源快照](../04_references/sn100_host/protocol_stability_20261023/long_run_01/job.json)与小时级结果分开。复测入口见[工程README](../06_programs/sn100_host/README.md#o05-3-异常恢复与长稳)，待办见[当前卡](../01_docs/plans/sn100_host_migration_plan.md#o05-3-card)。板卡加载/运行/实屏、仪器及SN100实际协议/编码、原Windows整套行为、24–72h完整业务与同板控制继续待验，完整O05/V01不关闭；不自动提交或推送。

<a id="o05-3-long-run-01-result"></a>
### 2026-10-23 20:27回收：2h持续通过，8h恢复失败

本节为同一`long_run_01`的终态，不是新测试。两阶段均使用冻结ELF SHA=`41c8b50b1f6bd8bcfb658d3ef5420111089f43b403226dc0d2c8af150f7dfa72`；15份首阶段SHA文件、27份冻结输入及程序哈希独立复核匹配，未修改源码/环境或扩大到板卡仪器。失败检测后监督和目标PID均已不存在，没有按PID执行停止动作；自动跟进`sn100-o05`已删除。

| 阶段 | 真实时长与退出 | 结论 |
| --- | --- | --- |
| 2h持续通信 | 程序7200.006秒；监督7200.962秒；退出0 | 按本轮WSL控制台负载合同通过；71962条Qq连续且全收，7200逻辑查询完成、7924次入队对应103012字节实际peer RX，结束自然排空及关闭清理成立 |
| 计划8h恢复 | 实际1150.178秒（19分10.178秒）；退出1，停止时间北京时间20:13:48.982 | 未完成/未通过；19次计划重连，11485条Qq按序全收，但第1160个逻辑查询Qk一次只读重试耗尽；保留失败终态，不将前缀或首阶段当8h通过 |

首阶段独立重读182101条事件及全部线日志，诊断序号1..71962无缺口；15项清单均匹配且没有漏列阶段文件。完整结果见[首阶段verified](../04_references/sn100_host/protocol_stability_20261023/long_run_01/continuous_2h/continuous_2h.verified.json)，不是仅依据运行时间或passed字段勾选。

| 资源/响应，WSL Release | 首2h完整阶段 | 恢复阶段失败前缀 |
| --- | --- | --- |
| 采样 | 7161次，无采样错误 | 1144次，无采样错误 |
| RSS/VmHWM、PSS观测峰 | 12912KiB、9729KiB | 12796KiB、6217KiB |
| 首/末600样本RSS均值 | 12898.447→12900KiB，末600样本均为12900KiB | 12748.280→12791.220KiB，短时前缀不能证明长期趋势 |
| 线程/FD、单核CPU口径 | 1线程/最多9FD，0.8082% | 1线程/最多9FD，约0.7860% |
| 10ms心跳 | 717173样本，p95桶上界11ms，最大484.341ms | 114474样本，p95桶上界11ms，最大265.898ms |
| 日志 | 38204770字节/10段 | 6042584字节/2段，29175条事件 |

首阶段PSS首/末600均值7137.305→6349.855KiB，受共享库分摊等环境因素影响，不解释为优化收益；RSS均值增1.553KiB及有限峰值只描述所采样窗口，不证明绝对无泄漏。事件循环最大间隔如实保留；当前无GUI，不能用该合同通过推导用户界面不卡顿、RK资源或同板控制达标。第二阶段最后120秒外部1Hz采样最大间隔1.048950秒（全前缀最大1.079880秒），未显示秒级大停顿；但1Hz不能排除亚秒停顿，且目标心跳只有聚合最大值，不能定位那次265.898ms发生在哪里。

失败请求为generation59的Qk，冻结目标以每次120ms预算、最多1次只读重试运行；以下是原始单调elapsed，不把入队称为socket写出或对端ACK：

| 事件 | elapsed_ms | 证据 |
| --- | --- | --- |
| 首次Qk入队 | 1149852 | sequence29168，完整金帧`02 03 00 06 00 00 00 01 51 6B 3B 04 05` |
| 模拟端实际读取 | 1149855 | sequence29169，同13字节 |
| 按实验规则不回首次 | 1149856 | sequence29170，`peer.deliberately.withheld.first.reply` |
| 第二次Qk入队 | 1149978 | sequence29173，attempt2；距首次126ms |
| 请求失败 | 1150169 | sequence29174，`client.timeout`；距重试入队191ms |
| 实验终止 | 1150172 | sequence29175，`passed=false`；最终摘要1150178ms |

失败前没有第二次Qk的`peer.RX.actual`、Qk回复入队或客户端解析记录；1276次入队×13−16575实际peer RX＝13字节，正好对应摘要`expected_query_wire_remaining=13`。独立重读全部失败前缀，11485条诊断序号及已观察查询字节成立，但最后13字节未确认。因此不能声称全部查询送达，也不能由缺少peer读取日志断言TCP丢包、重试绝未到内核/对端缓冲。现有日志缺少逐次pump/write/bytesWritten与定时器实际处理链，且实验finish会停止peer读取并close/abort，**根因仍待定位**；请求超时本身没有冒称TCP已断开，关闭来自harness失败退出策略。

失败程序退出1由冻结监督器`child.wait()`返回值及其捕获的错误元组、原始stdout/summary交叉核对；摘要/日志均保存成功。完整前缀、1Hz资源、终态和独立审计已加入[失败归档及SHA](../04_references/sn100_host/protocol_stability_20261023/long_run_01/recovery_8h_failed/SHA256SUMS)，首阶段原清单保持。[失败复核JSON](../04_references/sn100_host/protocol_stability_20261023/long_run_01/recovery_8h_failed/failure_review.json)与[tcp时间链复核](../04_references/sn100_host/protocol_stability_20261023/long_run_01/recovery_8h_failed/tcp_review.json)分别保留事实和不支持的结论。

下一步只列入[当前卡](../01_docs/plans/sn100_host_migration_plan.md#o05-3-card)：补足写出及事件处理时标，在相同预算/负载下受控复现，依据证据决定修复，再冻结新轮8h测试；本heartbeat未自动修复、放宽期限或重启测试。8h恢复、24–72h完整业务、RK加载/运行/实屏、仪器与同板控制继续待验，完整O05/V01不关闭。

<a id="o05-2-offline"></a>
## O05-2：诊断消息、异步回环TCP与只读请求衔接（2026-10-23）

按用户“继续05能离线完成的后续、板卡/仪器待验、避免其他窗口冲突”推进。使用项目`sn100-module-migration`、`sn100-project-files`及已确认的`work-habits`规则。本轮源码仅新增协议/内部测试目标，临时构建/数据隔离`temp/sn100_host/protocol/o05_2/`；未改其他窗口的主壳、页面、数据库或共享构建，公共文档局部补写。无UI主题/布局变化，无业务代码删减，未启动参考工程或读取旧INI/生产库，未连接/修改RK或仪器。

### 业务入口、原版依据与有意差异

| 入口 → 规则 → 输出 | 失败与恢复 |
| --- | --- |
| `parseReadOnlyMessage`接收Qj/Qq/Qk/Ql/QK/QL/QH → 先校验UTF8、括号、计数及资源界限 → 发布纯值`ReadOnlyMessage`；Qj复用原解析器 | 非法输入返回具体错误，输出参数保持不变；不把坏报文当空列表清告警。当前UTF8离线配置不猜GBK，现场编码待验 |
| `ReadOnlyTcpTransport::connectLoopback/queueQuery` → 只允许127.0.0.1数值回环端口、NoProxy和Qj/Qk/QH白名单 → QtNetwork异步双向收发、复用FrameDecoder | 无自动重连/重试；预算拒收整帧原子失败；关闭/硬错误清理队列和旧代次；对端FIN先限量排空Qt已收尾包再断开，残缺帧明确报错 |
| `ReadOnlyClient::request` → 整帧入队成功后开始单请求等待 → 严格同命令合法观测先`messageObserved`、再`requestFinished` | 默认1000ms/最多1次只读重试，可配置1..60000ms/0..3次。拒收入队不建立等待；到期、断线、主动关闭一次性结束；回调内的新连接/新请求用generation与revision隔离 |
| `loopback_main`和`client_demo_main`自建模拟端 → 实际收到的查询与独立固定金字节比较 → 控制台具体TX/RX、`wire.jsonl`与最终`summary.json` | 只接受新绝对输出目录，日志分别限16MiB/1MiB；结果/日志保存失败明确失败退出，不覆盖旧证据。没有仪器地址选项 |

原版依据来自`commandsender.cpp::getQueryStatusCommand/getQueryAlarmCommand/getQueryConsumeCommand`及`commandreceiver.cpp::pcQueryCmd`对应分支；6份参考文件逐字节SHA、字段规则与合成夹具位置见[diagnostic_origins.json](../06_programs/sn100_host/protocol/diagnostic_origins.json)，帧与原200秒常量依据沿用[origins.json](../06_programs/sn100_host/protocol/origins.json)。未运行原Windows整套软件，因此只称源码规则对照及两平台新目标实测。

Qq按原字符串通道键处理Base/1/01，重复通道保留首次，AD字符串不换算单位；Qk/Ql保留错误码/参数，QK/QL保留板端描述/措施，保持顺序与重复行，措施不执行。QH分别解析has/used，重复键按原字典后值覆盖。新增诊断最大128000字节、4096字段、单字段4096字节、集合1024项，都是本地预算，不宣称固件界限。Qj继续其原有更严格输入范围。

请求层成功只代表收到最新同类型合法观测。帧无事务ID，不能证明回复属于第一次还是重试，不能称仪器ACK；Qq/Ql/QL/QK均不能提前完成Qk。有效Qj更新状态新鲜度，非法Qj不更新；200000ms无有效Qj关闭连接，这是比原“任意字节/日志更新静默时钟”更严格的明确差异。该实际200秒墙钟用例尚待补验，原O05-1合成时间用例不冒充本层实测。

传输和请求QObject均在所属事件线程使用，无`waitFor*`阻塞或额外工作线程。每次读取4096字节、每轮最多16384，续读排队；Qt读缓冲65536，用户待写＋Qt待写默认32768/最大65536，单次pump默认4096。连接/写停滞预算默认1000ms；显式close立即取消，FIN仅排空已接收字节。1字节quantum测试证明分次pump，不证明操作系统实际发生短写/零写。

### 实际输入、收发和缺陷复测

回环查询为以下完整线帧，实际对端RX另记，入队/`bytesWritten`不称对端确认：

| 查询 | TX十六进制 |
| --- | --- |
| Qj | `02 03 00 06 00 00 00 01 51 6A 3A 04 05` |
| Qk | `02 03 00 06 00 00 00 01 51 6B 3B 04 05` |
| QH | `02 03 00 06 00 00 00 01 51 48 18 04 05` |

诊断演示按前2字节/余包分段并粘连返回7条：`Qj[4][SIM-LOOPBACK][2][1][0][0]`、`Qq[3][Base][11 22 33 44][1][123][1][999]`、`Qk[1][SIM-E1][A|B]`、`QK[1][SIM-E2][模拟提示][仅供观察，不执行动作]`、`QH[2][SIM-PT][5][SIM-PT][7][1][SIM-PT][3]`、`Ql[0]`、`QL[0]`。核对Qq通道1为123、Base原字符串，QH has为7/used为3、告警内容及命令顺序；Windows与WSL全部值及业务载荷十六进制一致，TCP分段与时间不要求相同。

请求演示故意不回第一次Qj，第二次才回有效状态，再发Qk/QH；Qk前先送Qq/Ql/QK通知，不能提前结束等待。两端实际收到Qj、Qj、Qk、QH共52字节，4次入队、3个查询完成、6条观测；WSL145ms/Windows81ms，退出0。关闭后`disconnected`且fresh=false。具体内容、解析值与跨平台对照均在[证据目录](../04_references/sn100_host/protocol_transport_20261023/)。

| 实际运行 | WSL Qt5.15.13 x86_64 Release | Windows Qt5.14.2 MinGW32 Release |
| --- | --- | --- |
| 诊断解析 | 63通过/0失败，128ms | 63通过/0失败，10ms |
| TCP传输 | 27通过/0失败，464ms | 27通过/0失败，1188ms |
| 请求衔接 | 19通过/0失败，481ms | 19通过/0失败，889ms |
| 真实TCP FIN尾包 | 2000帧/64000字节全收，0丢失 | 2000帧/64000字节全收，0丢失 |
| 固定负载 | 60037ms收10000额外＋7初始消息 | 1029ms收1000额外＋7初始消息 |

发现并修复3类问题，原失败日志/XML单独保留：①12字节预算曾被静默提升到13，现整帧拒收，无pending；②2000帧后对端FIN曾使WSL丢976帧、Windows丢1488帧，现终止状态限量排空，两端完整收到；③成功曾早于观测发布，直接槽关闭/重连时旧请求误报成功，现先观测再核对generation/revision。另补1ms请求预算、故意迟5ms处理及超时完成槽新建同类请求，旧观测均不能完成新等待。语法/构建及双端全组复测通过。

原O05-1在本轮重新运行63项（102ms）与25事件回放，均退出0；新两个入口各4项CLI错误/旧输出保护检查通过。未修改GUI源码，本轮未与其他窗口争用焦点重复GUI实验；历史O01累计回归仍是O05-1证据，不冒称本轮又做一次。

### 资源基线、构建和归档

Release控制台、真实回环、7初始＋10000额外Qq均匀安排60秒，完整JSONL落盘。父进程名义10ms采样`/proc`，实际5689次/60.075秒；同机其他聊天构建并行，未做峰值归因。方法、逐样本及完整线日志均归档。

| WSL观测 | 实际值和口径 |
| --- | --- |
| CPU/内存 | CPU单核口径1.964%；采样RSS及内核VmHWM12968KiB、采样PSS9593KiB；1线程、最多8FD |
| 事件循环/解析 | 10ms心跳5980样本，实际间隔p95=10.528ms/最大147.695ms；解析p95=0.022365ms/最大0.143442ms。均非UI绘制/输入及硬实时结论 |
| 缓冲/日志 | 客户端待写观测峰39字节、模拟端Qt待写峰936字节、codec回调后线缓冲峰2字节；关闭前待写/半帧/组装均0。日志7715430字节，小于16MiB |

缓冲数字是已命名回调采样口径，不含未读Qt socket或瞬间DecodeBatch峰值；关闭后清零另记，未用清理值冒充自然排空。Windows1000条短测心跳间隔p95=10.695ms/最大11.564ms、解析p95=0.0124ms；负载/时长不同，不作为跨平台性能优劣比较。未知RK预算未放行，2h/8h及更长稳定性、同板实时控制干扰仍待验。

两种可运行目标均生成ELF64 AArch64并核查直接DT_NEEDED；未在板端加载或执行。SHA-256：回环`a575b8dfece7317483fcaeb705043cc8fd634f720a390d89b1cbc44ff98373ec`，请求演示`dd810366f679ccbea9ac86a2a308b03fe80278d23f0e574553d7cd911b4f59d2`。额外QtNetwork及其实际链接所需7个ARM64包在[正式环境输入](../07_backups/qt_environment/sn100_protocol_arm64/source.json)，保留官方URL/版本/包索引/SHA，现有`.deb` LFS规则适用；只校验解压到O05专属临时sysroot，不安装进WSL或RK、不改共享QtCore。归档为本次普通TCP交叉构建输入，不是完整Ubuntu安装仓库/TLS配置或板端运行依赖已满足的证明；目标加载器/ABI仍待板端核查。

[71份证据及SHA256SUMS](../04_references/sn100_host/protocol_transport_20261023/)含实际双端TX/RX、两个演示、60秒逐样本、六组测试日志/XML、原失败证据、回归、构建及34份源码/12个产物指纹。[工程README](../06_programs/sn100_host/README.md#o05-2-诊断消息与本机-tcp)给出分离的WSL测试/演示与ARM64构建命令；每次选新输出目录。无配置覆盖，无需环境回退备份；本轮回退范围仅新增O05目标/归档与局部文档，保留旧O05-1及其他模块成果。不自动提交或推送。

离线切片通过，完整O05/V01不关闭。下一步离线故障注入还包括真实OS短写/零写/写停滞、受控连接黑洞及本层200秒静默墙钟；真实SN100协议/编码/子系统/端点、原Windows完整行为、RK运行/实屏/资源、仪器与长稳/同板控制均保留待验，详见[当前模块卡](../01_docs/plans/sn100_host_migration_plan.md#o05-2-card)。

<a id="o04-1-offline"></a>
## O04-1：匿名历史查询、明细与 GBK CSV（2026-10-23）

用户允许模块化并行，并指定本窗口负责04模块。按项目两个skill及业务/视觉验收参考实施，正式源码新增在`06_programs/sn100_host/history/`，使用独立`history.pro/main.cpp/wsl.sh/build.sh`和`temp/sn100_host/history/`；未修改其他模块入口、共享构建脚本、已有运行数据或参考原件。O03后续再做主框架接入，完整O04仍保留未完成。

### 实现与原版依据

原版链路为`FrmReportMaterial`历史三表→`MaskDlgQueryReport`筛选→`ReportDataTable::QueryReportDatas`→`MaskDlgReportDetail`患者字段保存；CSV来自`exportReport/export_report`，原实现仅在`__WIN32`下实际执行、编码GBK。打印另经`QtReport::PrintReportData → QtRPT::printExec(true)`加载模板，PDF及条件XLSX在预览工具栏；后者原源码标under construction。本轮完成查询/明细/患者保存/CSV，未伪造模板输出。

维护时从`history/main.cpp`查看宿主和线程关闭；从`HistoryPage::savePatient()`追到输入、实际修改字段与请求，再到`HistoryStore::savePatient()`参数绑定、事务提交和成功反馈。SQL连接在worker中创建/使用/关闭，UI只保留一个在途请求并核对请求ID；锁等待200ms。CSV按查询快照流式写入同目录临时文件，严格GBK检查，成功后不覆盖地发布；容量限制为64MiB/100万报告、最多64结果列、每报告64任务及单次100个选中ID，未测这些上限负载。

[来源清单](../06_programs/sn100_host/history/origins.json)保留原文件/函数、SHA和有意差异。`report_data.id/row_id`与结果`fid`关联保持；匿名库使用显式`report_columns/report_values`契约，不是生产库升级或导入工具。只从[手写匿名夹具](../06_programs/sn100_host/tests/fixtures/history_reports.json)初始化，不读取原数据库、旧连接配置、真实患者或认证字段，不运行算法/网络/LIS/设备动作。

### 原页面与必要 Linux 适配

![O04-1完整业务区与证据标注](../03_photos/20261023_sn100_history_page_marked_01.png)

[未修改原图](../03_photos/20261023_sn100_history_page_01.png) · [可编辑标注SVG](../03_photos/20261023_sn100_history_page_marked_01.svg) · [几何/样式逐项对照](../04_references/sn100_host/offline_history_20261023/visual_geometry_comparison.json)。完整业务区图为实际QWidget抓取，不能据此认定整个页面同时显示在900高视口中。

主页面23个、查询16个、详情59个、导出4个原内部几何项保持；三个弹窗仅将原整屏动画遮罩裁为内部`view_frame`。主题依据为明确可用的非敏感视觉键：右栏`28/28/28/255`、右按钮`127/127/127/255`、白字、选中行`183/215/255/255`，`ReportTableType=1`对应垂直结果表；没有把医院设置页的蓝色直接用于报告页。表头沿用原`#444444/#D6D6D6/#DDDDDD`及60px层级。

主业务区1751×968、详情内容1341×930保持，独立外层滚动让小屏菜单/分页/保存取消可达。Linux缺失字体以Noto替代；Material/ripple/mask控件用Qt5原生控件及有来源QSS最小提取，分页图标暂用对应箭头字形。原曲线、重算、上传、模板打印等保留原位置并禁用/提示。以上是源码与WSL运行对照，原Windows活动主题/动画/原生控件和RK实际屏仍待单独验收，不能标完整视觉一致。

![1600×900宿主滚动后的分页和右栏可达性](../03_photos/20261023_sn100_history_pagination_marked_01.png)

[原图](../03_photos/20261023_sn100_history_pagination_01.png) · [标注SVG](../03_photos/20261023_sn100_history_pagination_marked_01.svg)。测试先`ensureWidgetVisible`滚到控件，再真实点击；没有仅向视口外控件派发点击就判通过。

### 匿名输入、实际输出与错误恢复

最终正式批次为`experiment_20261023_final_04`：写入PID695、独立读回PID703、分页PID708均为3 passed/0 failed（各含初始化/清理），退出码0；分别约7.054s/0.297s/0.617s。前面批次用于集成、原主题/校验纠正与复现补充，仍在temp，不混入最终证据。

| 输入/动作 | 实际核对结果 |
| --- | --- |
| 6份匿名报告，5份可见，1份隐藏 | 2026-10-23含00:00:00和23:59:59共3份；相邻日前后一秒排除；隐藏报告未进入查询/明细/CSV，保存亦有visible约束（保存隐藏项为源码核查） |
| 样本号3＋条码`ANON,"003`，日期由真实日历选择 | 精确命中同一份报告，逗号/单双引号未变成SQL；非法样本号明确拒绝，参数绑定生效 |
| 明细只改年龄，再另改完整患者字段；取消一份姓名草稿 | 未编辑长文本/换行/未知性别保持；取消未提交；结果与原始任务表未因患者保存变化 |
| 保存并另起进程读回 | 姓名`NO'Brien中`、年龄42、性别女、科室`DO'Brien科`、申请单号`T-'003-UP`、备注`中文备注,"引号" O'Brien`逐字段一致；其他医生/类型/床号见实际JSON |
| GBK日期查询CSV | 12列、3份报告4数据行（PT复测两行）；引号/逗号/多行原文由独立CSV解析器核对；勾选样本3只导出该报告的2行 |
| 匿名项目精度/单位 | PT 12.34→12.3、13.46→13.5；INR 1.236→1.24；APTT 0→0.00；FIB 2.345→2.35g/L；D-D 0.123456×1000→123.456ng/mL；未知RAWX精度保留原文；非法数值为`****`。这些是明确夹具，不是仪器项目配置 |
| 45份可见的独立分页数据集 | 三页20/20/5，首页/上页/下页/末页真实点击；在首页执行全查询导出46行、45个不同报告，未漏页/重复报告 |
| 外部连接持有写锁后保存 | 223.396ms后可见失败；期间22个心跳、最大间隔15.129ms，无部分提交、草稿保留、忙时关闭拒绝；解锁重试通过 |

![患者字段保存与结果明细](../03_photos/20261023_sn100_history_patient_marked_01.png)

[未修改完整内容图](../03_photos/20261023_sn100_history_patient_01.png) · [SVG](../03_photos/20261023_sn100_history_patient_marked_01.svg) · [外层实际滚动窗口](../03_photos/20261023_sn100_history_patient_scroll_01.png) · [锁库失败时客户端原图](../03_photos/20261023_sn100_history_locked_01.png) · [独立进程重开原图](../03_photos/20261023_sn100_history_reopened_01.png)。客户端抓图不包含系统标题栏，错误判据以完成信号、保留草稿和前后数据库/心跳原值为准；标注仅额外圈框/文字，没有重绘字段或终端结果。

实际字段/库完整性见[write_result](../04_references/sn100_host/offline_history_20261023/write_result.json)、[read_result](../04_references/sn100_host/offline_history_20261023/read_result.json)；[分页原值](../04_references/sn100_host/offline_history_20261023/pagination_result.json)、[GBK原始CSV](../04_references/sn100_host/offline_history_20261023/data/exports/day_initial.csv)与[只选报告CSV](../04_references/sn100_host/offline_history_20261023/data/exports/selected_3.csv)可复核。SQLite `integrity_check=ok`；[8项目录边界](../04_references/sn100_host/offline_history_20261023/storage_boundary_checks.json)覆盖相对路径、参考目录、未标记旧库、坏标记/异源库、符号链接拒绝，以及专用目录创建/重开，哨兵与异源库SHA不变。

### 响应、资源与已知行为差异

条件为Ubuntu-24.04/qyq、Qt5.15.13/G++13.3、Release、WSLg/XCB、Fusion/Noto，屏幕2560×1600/DPR1/约96.10DPI，测试宿主1600×900。固定5份可见匿名报告做100次查询和100次保存，另独立测45份分页；有10ms GUI心跳。操作从鼠标投递到真实完成信号，计时器间隔是事件循环代理，不代表绘制延迟/硬实时。

| 测量 | 最终批次结果 |
| --- | --- |
| 查询102样本（含前置2次） | P95 11.636ms；P99 12.899ms；最大17.773ms |
| 保存100样本 | P95 39.353ms；P99 42.684ms；最大44.725ms |
| 10ms心跳625样本 | P95间隔12.624ms；P99 40.891ms；最大96.023ms |
| 正式入口约6.019s静置采样 | RSS/峰值105252KiB，PSS最大59217KiB；4线程、11FD。含启动CPU按单逻辑核口径1.015%，首秒后ticks采样未增长（0.0%，不表示任何负载为零） |
| 活动QtTest进程约7.163s | CPU35.507%/单逻辑核，RSS/峰值132280KiB，PSS最大82521KiB；6线程、16FD。包含自动化与额外验证连接，不能当裸应用业务预算 |
| 存储/日志 | 匿名SQLite53248字节，初始/保存/选中CSV分别506/496/331字节；写入日志10889字节。无长期增长或上限负载结论 |

[指标汇总](../04_references/sn100_host/offline_history_20261023/performance_summary.json)的操作P95/P99使用nearest-rank；[裸入口资源原样本](../04_references/sn100_host/offline_history_20261023/runtime_resources.json)和[活动测试样本](../04_references/sn100_host/offline_history_20261023/write_resources.json)包含100ms采样原值。电脑其他并行工作未做隔离，WSL数据不折算RK预算；长稳、温度/降频、同板控制仍待测。

| 原版问题/本轮差异 | 处理与范围 |
| --- | --- |
| 查询复选框bool未传到DAL，实际日期/非空条码仍过滤 | 保留源码实际传值和SQL条件，不凭复选框文字重设规则；非法/反向日期、非数字样本号明确报错，修正反向警告文案 |
| 原患者字段正则`^[A-Za-z0-9].{8}$`含ASCII首字符与长度歧义 | 原校验保留，真实输入测试使用允许域；中文首字符录入等需原Windows对照，不能宣称全部输入行为已正确。去掉误用maxLength导致的载入截断，只回写实际改动字段 |
| 单行显示会改变换行，未触碰未知性别可能变男 | 保留原始快照，仅回写明确编辑字段；只改年龄时已核对其余字段完全一致 |
| 原明细把数值0误判为失败、NaN/尾随文本处理不一致 | 明确有限数值校验，0.00有效、非法值`****`，未知精度保留raw；属于有意数据可靠性差异，完整原运行数值对照仍待验 |
| 原复测均值/CV、完整列配置与模板链 | 本轮逐条保留原始结果，未伪造均值/CV或曲线；这些能力列入后续O04切片，不据本轮CSV通过称完整报告等价 |

### 构建、复现与本轮结论

WSL x86_64、Windows Qt5.14.2/MinGW32与ARM64应用均构建通过；ARM为ELF64 AArch64，SHA-256 `5df71191a35186e103b6a5fb60bfd3502945f2def11a8196a741516e6e55665e`。Windows仅构建本切片，未运行原参考工程。[构建元数据](../04_references/sn100_host/offline_history_20261023/build_metadata.json)保留源码/三目标/测试产物SHA、架构、工具链和动态依赖；原`main.cpp/SeeRain450.pro`与既有SHA基线一致。原生构建曾提示WSL/Windows不足1秒时间戳偏差，已在源码稳定后完成全部受影响目标构建与运行；不据告警部署旧产物。

O01在另一个独立构建/匿名目录累计回归，写入8.321s、另进程读取0.209s，均3 passed/0 failed、退出0；[日志](../04_references/sn100_host/offline_history_20261023/build_logs/o01_write.log)和[字段结果](../04_references/sn100_host/offline_history_20261023/o01_regression/write_result.json)已保留。O02/O05由各窗口独立交付，本轮未重复其完整验收，也未修改其入口/数据。

日常运行与三条独立实验命令见[工程使用方法](../06_programs/sn100_host/README.md#o04-1-匿名历史查询与-csv)。在WSL的`06_programs/`中启动：

```bash
bash sn100_host/history/wsl.sh run
```

仅新增正式源码/匿名夹具、temp构建/实验与证据，没有移动、删除原业务代码，没有系统/既有配置覆盖、环境安装、RK上传或仪器动作，因此未做额外环境备份；未自动提交/推送Git。正式原图放03_photos、数据/日志放上述04_references；temp可重建，归档不可继续写入体验数据。

**结论：O04-1的WSL匿名功能/数据、短时响应、三目标构建和O01累计回归通过。** 源码几何/有来源主题已保持，原Windows完整行为/视觉、RK显示/输入/插件/资源、仪器整合、长稳及同板控制待验；均值/CV、原模板预览/PDF/条件XLSX及实体打印继续按[模块卡下一步](../01_docs/plans/sn100_host_migration_plan.md#o04-1-card)实施，完整O04未标完成。

<a id="o02-1-offline"></a>

## O02-1：账户设置匿名脱机移植（2026-10-23）

**本轮结论：账户设置的 WSL 脱机功能与短时响应核验通过，O01 累计回归通过。** 先更新现有计划，再按两个项目 skill 提取、实现和核验；没有启用原生产登录、网络或仪器。原 Windows 实际行为/视觉、RK 实屏/资源、正式登录及设备状态整合、长稳/同板控制仍待验。

### 实际界面与数据

![账户页验证标注](../03_photos/20261023_sn100_account_marked_01.png)

[未修改原图](../03_photos/20261023_sn100_account_add_01.png)；原三页顺序、40个控件及 XML 几何保留，源码局部 QSS 仅替换字体。窗口1200×900、DPR=1、逻辑DPI约96.10；原业务组高811，未压缩到O01的小画布。左右两组仅同步水平平移，内部相对位置/尺寸不变。缺失苹方字体替换为 Noto Sans CJK SC，原 Windows 活动主题和字体度量没有实测对照。

![修改账户页](../03_photos/20261023_sn100_account_update_01.png)

保留原只读用户名、旧/新信息分组、大箭头、角色框和确认修改按钮。[删除页](../03_photos/20261023_sn100_account_delete_01.png)、[Guest隐藏列表](../03_photos/20261023_sn100_account_guest_01.png)、[另进程重开](../03_photos/20261023_sn100_account_reopened_01.png)分别留有原图。截图前已清除所有临时密码输入。

![原错误弹窗透明层](../03_photos/20261023_sn100_account_locked_dialog_01.png)

这是原组件的**透明弹窗层截图**，不包含下面宿主像素，透明区预览为黑色不代表运行中将宿主涂黑。保留480×208内容区、原图标/按钮PNG、文字色、280ms淡入/淡出、上移50px/OutCubic和阴影；17张资源与原件SHA一致。资源路径改为QRC；遮罩只覆盖本宿主窗口并居中，关闭按钮保留原13px图标/尺寸，未提取Material水波纹。6个内容控件几何对照见[弹窗对照JSON](../04_references/sn100_host/offline_account_20261023/dialog_visual_comparison.json)。

最终数据库共7行；独立进程公开字段逐项相同，`integrity_check=ok`。工程师视图按原过滤规则显示5行（工程师和管理员行隐藏）：

| name | role | isdefault | 实际结果说明 |
| --- | --- | --- | --- |
| offlineSend | 1 | 0 | 匿名送检账号 |
| offlineTest | 1 | 0 | 测试中按原忙态分支从2改为1，重开仍为1 |
| offlineCheck | 3 | 0 | 空新密码分支已测试后恢复临时测试密码 |
| offlineEngineer | 4 | 0 | 匿名工程师会话 |
| offlineAdmin | 5 | 0 | 管理员删除保护；非法修改索引另用隔离边界库测试 |
| lockRetry | 1 | 0 | 锁库失败无写入，解锁重试后保存 |
| A留存1 | 2 | 0 | ASCII首字符与中文组合实际输入/读回 |

完整ID和字段见[去密码CSV](../04_references/sn100_host/offline_account_20261023/accounts_without_credentials.csv)、[写入结果](../04_references/sn100_host/offline_account_20261023/write_result.json)、[新进程读回](../04_references/sn100_host/offline_account_20261023/read_result.json)。**认证库、密码或密码派生摘要没有归档。** 原 `pwd` 明文列只在 `temp/` 可变匿名库中保留，生产认证策略另列验收；正式匿名夹具只含姓名、角色和默认标记。

### 业务入口、原规则与必要差异

从 `ui/frmaccountsetting.cpp` 的 `on_btnAdd/on_btnDel/on_btnUpdate_clicked` 可追到 `data/account_store.cpp` 的 `add/remove/update`；`slotUserChanged/frmGetAccount` 管选项及列表过滤。原 `user_information(id,name,role,pwd,isdefault)` 字段与类型保留，角色编号来自原枚举，1–4显示名来自原下拉选项。未读取原角色数据库，role0使用无匹配空串；5的中文名称仅为匿名映射，原显示名待对照，正常列表中该行隐藏。

| 已核对的源码规则 | 本轮实际核验 |
| --- | --- |
| Guest禁增/表隐藏；角色1–4只显示role<4，管理员只显示role<5；可选角色上限为工程师 | 五角色选项、过滤及Guest实际窗口通过 |
| 添加非空/两密码一致/重复名；删除先确认、核验目标密码、拒删上级及管理员 | 具体错误文案通过；取消、锁库和伪造角色分别有数据不变断言 |
| 更新校验旧密码、确认新密码；更新及删除当前人成功后注销 | 提交后会话清空、列表隐藏和注销信号通过 |
| 原忙态更新条件与注释相反：同索引拒绝、改为低角色可通过 | 两个模拟分支均按源码保留并通过，不作为新安全设计 |
| 原允许空新密码；管理员index4越界→空选项→role0 | 空密码保存/恢复通过；独立匿名边界库实测role0保存 |
| 双击可给四个用户名框赋值，含只读修改框；原更新无目标等级比较 | 实际双击修改另一目标通过；没有擅加“只能修改自身” |
| 原用户名正则实际要求17字符，但按钮未检查acceptable状态；更新可造成重名 | 16/17字符及短名提交、重名更新已实测；按名删除全部匹配仅静态核对，重复名删除实测待补 |

角色并非所有业务都只按数值大小判断；原 `user_powers.can_role_id` 通用权限表、默认账号设置、完整登录与首次收包/设备联动本轮未接入。显式 `setSession(name,actionReady)` 是脱机模拟入口，角色从匿名库读取，不代表完成生产密码登录。

必要实现调整：SQL参数绑定、`BEGIN IMMEDIATE`事务、`synchronous=FULL`/`journal_mode=DELETE`及200ms锁等待；SQL连接在worker线程内创建/使用/关闭；UI一次仅一个在途操作。每次写在事务内重查当前身份/角色，拒绝失效会话和伪造不可选角色。成功提交后先清注销身份、再重读、再提示，区别于原先弹窗后注销。双击连接从原按行反复连接改为一次；Guest清旧行后隐藏；角色在忙操作中变化时立即按新角色过滤缓存行。参考原件没有删改，未以“无用”名义删除原业务。

### 核验负载、响应和资源

环境：Ubuntu-24.04 / qyq / x86_64，Qt5.15.13 / GCC13.3、C++14、Release、WSLg/XCB、Fusion、Noto。最终账户05批次写进程PID447，115.684秒；读进程PID726，122ms，均3 passed / 0 failed（包含初始化/收尾）。100次页面按钮操作与100次实际写入（50增+50删），另含规则/错误用例；原弹窗动画与自动确认保留。

| 项目 | 实际观察值与口径 |
| --- | --- |
| GUI心跳 | PreciseTimer周期10ms，11531样本；间隔p95=11.098ms，最大52.231ms；是事件循环代理指标，不是屏幕最终呈现延迟 |
| 请求到提交 | 新增51样本p95=55.50ms、最大61.85ms；删除50样本p95=56.26ms、最大61.11ms；从排队请求到GUI收到提交信号 |
| 点击到最终确认结束 | 新增p95=658.46ms；删除p95=1243.86ms。包含原弹窗显示/收起和自动确认；删除还有前置确认弹窗，不能当作SQL阻塞或直接与100ms反馈候选门槛比较 |
| 状态信号 | 新增p95=1.53ms；删除的信号在确认弹窗后产生。仅槽内信号时延，不声称已测完整像素反馈预算 |
| 锁库失败/恢复 | 请求到错误222.99ms；含错误弹窗的全过程812.48ms；期间77心跳、最大43.31ms。失败前后公共数据一致、无部分提交，忙态关闭被拒绝，解锁重试通过 |
| 活跃测试进程 | CPU约14.48%（全部进程CPU时间/墙钟，单逻辑核口径，包含动画和测试）；578次200ms资源采样，RSS/峰值131204KiB、PSS最大119243KiB，5线程，FD14–16 |
| 正式入口短测 | 新进程窗口可见约276ms（不是数据加载完成时刻）；观察5.028秒，CPU约0.20%，RSS最大101880KiB，PSS最大95031KiB，4线程/11FD，正常关闭退出0；日志227字节 |
| 存储 | 账户库12288→12288字节；最终7行。测试日志包含编译与测试输出，不当作产品持续日志增长；大数据量、2小时以上/长稳与资源上限仍待验 |

独立真实worker边界测试9 passed / 0 failed（7个业务组、865ms）：新建必须显式播种/禁止既有库重置，缺/坏tag，相对路径、DB/tag符号链接、合成参考目录别名，损坏和三类不匹配schema原文件字节不变，引号/中文参数绑定，DB角色来源/伪造角色/失效身份/角色热变化，原跨目标/重名/空密码/default字段/管理员role0。合成别名只指向temp测试目录，未用真实参考原件冒险测试。

O01累计回归独立02批次：写8.527秒、读回通过，均3 passed / 0 failed；五类CRUD、200次操作、锁库及取消/错误通过，心跳最大54.574ms。[回归证据](../04_references/sn100_host/offline_account_20261023/hospital_write_result.json)。

过程中保留了三类失败/纠正依据：首个账户测试的worker spy早于GUI回复，修正测试等待条件；独立角色降级用例发现旧过滤残留，修正缓存重滤并复测；原弹窗恢复后补上uic所需ui头路径，首次编译失败未启动旧程序。另一次O01与其它GUI实验交叠时卡在确认框，主动终止测试（不是产品自行崩溃），单独运行02批次通过；后续GUI用例串行。

### 构建、保存位置与后续交接

最终构建指纹与依赖见[构建清单](../04_references/sn100_host/offline_account_20261023/build_metadata.json)、[动态检查](../04_references/sn100_host/offline_account_20261023/dynamic_checks.txt)、[SHA清单](../04_references/sn100_host/offline_account_20261023/SHA256SUMS)。WSL干净构建、Windows Qt5.14.2/MinGW32及最终ARM64构建均通过。原生SHA `2d6d47eb99a571bf29cd640c412bcae58d83cb6967d5d89487efd100b69c742f`；Windows SHA `d40293b658089769f14ac43087208f196a7054135a785d414694c81c32ba08d2`；ARM64 SHA `d360674da75c634e6364aa219c0b665365771adc9809297e36d94c1158286eb4`。只检查编译及架构/依赖，没有部署或连接板卡。

| 内容 | 位置与恢复范围 |
| --- | --- |
| 正式源码/构建 | `06_programs/sn100_host/`：main/.pro、ui、data、resources、tests；源对应及SHA追加同一origins.json |
| 临时运行/构建 | `temp/sn100_host/`：原生/Windows/ARM隔离；账户05批次、边界01、O01回归02和可变账户库不进入Git |
| 长期非认证证据 | `04_references/sn100_host/offline_account_20261023/`：公开CSV/JSON、来源/布局对照、测试/构建日志及SHA；原图/标注统一直接放03_photos |
| 计划与使用入口 | 原移植计划O02-1卡；根README和工程README提供入口，不另建每模块主记录 |

在WSL的 `06_programs/` 中执行 `bash sn100_host/wsl.sh account`。默认匿名工程师/模拟ready；首次匿名初始密码仅保存在temp下的 `account_bootstrap.secret` 供本地输入。自定义绝对目录的首次建立与再次启动均已实际运行退出0，五行匿名数据未重置。[完整使用与复测命令](../06_programs/sn100_host/README.md)。本轮未改系统/Qt环境或覆盖既有配置，无额外环境备份；无自动提交/推送。

O02-1可作为后续O03页面接入的离线接口：`frmAccountSetting(AccountStore*)`、`isReady/isBusy/statusText`、`operationFinished`、`signalLoginOut`及worker会话/存储信号；工程整合时仍须累计回归。完整原版视觉/行为、RK显示输入与插件、正式认证/默认账号和真实设备状态、仪器、长稳及同板控制继续待验，O01/O02均不标为完整实物完成。

<a id="o06-2-offline"></a>
## O06-2：两种凝血点候选算法研究原型（2026-10-23）

**本轮交付：自主实现百分比法和一阶导数最大速率法，完成必要合成输入/边界核查及WSL、Windows32、ARM64构建。实际SN100曲线和按项目方法评估按用户要求留待后续；完整O06、临床或AL4A等价均未标通过。**

### 用户范围与01–05核对

本聊天按两个项目skill、业务验收参考和work-habits执行，保留已有未提交成果。开工时核对源码与证据：O01、O02-1、O04-1、O05-1分别完成记录所列离线切片，O02/04/05仍有完整功能待验；O03原先只有范围审查，随后“梳理SN100 03模块移植”聊天收到新指令，开始独立`shell/`主壳与O01接入。本聊天不重复修改其主壳/页面/共享构建或操作其自动化测试窗口。

用户确认目前没有AL4A源码/Linux实现，要求上网找两种凝血曲线凝血点判定算法先放入工程，后续再测试；最终明确不是机器学习学习曲线，也不是一/三阶校准拟合。本轮新增`06_programs/sn100_host/coagulation/`自主研究目标，而不是把Windows DLL换后缀或迁入原Linux空成功。源文件与构建、匿名夹具均在正式工程；三目标产物、合成运行数据/日志在`temp/sn100_host/coagulation/`。

### 资料、算法和业务入口

| 方法 | 已查原始依据 | 本轮自行实现 |
| --- | --- | --- |
| 百分比法 | [Siemens/Sysmex培训资料](https://nacce.siemens-healthineers.com/wp-content/uploads/2022/12/T05012.002-Sysmex-CS-Systems-Sample-Result-Evaluation-Clotting-Assays_eff-date-12-19-2022.pdf)的百分比检测原理 | 首末参考窗口算术均值定义基线/平台，方向正化后求指定变化比例，先取50%；唯一阈值交点线性插值 |
| 一阶导数最大速率法 | [Diagnostic Grifols QNext厂商手册](https://usermanual.wiki/Diagnostic-Grifols/215550-3932834.pdf)§9.2.2.2，印刷页9-31～9-33；镜像中已核对作者身份 | 奇数点局部线性回归估计一阶导数，支持非均匀时间；取指定方向内部最大速率的采样时刻，不做亚采样峰拟合 |

[原始研究的CFWA方法](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0320111)也分别描述百分比交点与速度峰。资料支持方法原理，不证明本原型与厂商滤波/保护细节、AL4A或SN100等价；两种候选时间不可普遍等同。[来源/实现选择清单](../06_programs/sn100_host/coagulation/origins.json)及[接口知识](../01_docs/knowledge/sn100_host_project_guide.md#10-凝血点研究接口与后续统一操作)保留边界。

入口由`coagulation/main.cpp`读取显式输入合同，调用`clot_endpoint.cpp::percentageEndpoint()` / `maximumDerivativeEndpoint()`；共用`prepareCurve()`核查完整采集、点数、时间、方向、幅度和首末平台。输入为合成/匿名`[加试剂后秒数,光学值]`，非负严格递增，不减首点时间；上升/下降必须显式指定。基线/平台窗口、搜索时间、百分比、导数窗及稳定性/多峰参数均写进实际输出。

两种方法分别输出候选时间、索引/阈值或峰速率、错误和警告，不计算平均候选、不静默回退。失败时间为JSON null，原始输入及SHA一并保留；新绝对输出文件使用`NewOnly`原子创建，同名拒绝，参考目录不能作为输入或输出。CLI解析失败退出2、输出写入失败3、任一方法不能判点4；两方法均得到数学候选才退出0，仍明确`clinical_validated=false`、`al4a_equivalence_verified=false`、`hardware_verified=false`。

所有窗口/幅度/平台/多峰门槛是工程试验值。首末窗口极差和相对斜率检查可拒绝明显未成平台，但不声称排除了所有慢漂移、干扰、双相或未凝血情况。本切片没有业务界面变化，不新增视觉一致结论；尚未将新候选时间接入患者报告、校准/INR/FIB换算或设备停止逻辑。

### 固定输入和实际输出

手工不对称夹具含31点：首端光学值10、末端110，11秒为55、12秒为80，5点局部回归窗口配置明确。实际运行：

```text
基线=10；终末平台=110；变化幅度=100；50%阈值=60
percentage：11 + (60-55)/(80-55) = 11.2 s，括号索引11/12
maximum_derivative：t=11 s，局部回归斜率=20 optical units/s，窗口索引9..13
进程退出0；两种时间分别保留，未写成临床结果。
```

WSL Qt5.15.13/G++13.3 Release实际执行，Windows Qt5.14.2/MinGW7.3 32位应用输出两方法数值对照一致，ARM为ELF64/AArch64构建通过。19项QtTest、12项CLI合同/输出保护核查、15组独立合成复核通过；包括下降反射、非均匀时间、非零起采/平移缩放、平坦/低幅、未完平台、重复/负时间、搜索边缘、多阈值/分离双峰及错误参数。

独立不对称Gompertz输入得到百分比52.9308602976秒、导数峰50秒；时间平移/缩放和下降反射按合同变化。这些是合成数学结果，不是测得的仪器误差。审查发现负采样时间初版未拒绝，补上`negative_time`后重编并完成15组复测；文档中曾写中位数的描述已按实际窗口均值纠正。编译失败不启动旧产物；本轮未连接或部署RK。

### 短时资源与交付边界

另外观察一次100000点合成Logistic曲线、101点导数窗的WSL运行：输入2,755,479字节，百分比计算1.657891ms、导数66.247814ms，整个CLI墙钟0.321270秒；10ms外部采样得到RSS最大78,500KiB、内核VmHWM峰值84,696KiB、PSS最大72,777KiB、1线程/6FD，采样跨度内CPU约58.13%单逻辑核。JSON解析和输入/结果展开包括在进程资源内，算法耗时与整个CLI分开；这是单次预算观测，没有操作耗时分位数或GUI响应/长稳结论。

[正式证据](../04_references/sn100_host/clot_endpoint_20261023/)保存源码/产物指纹、三目标构建/动态依赖、固定输入/实际结果、各检查与原始采样；最大预算大文件留temp，正式保存生成脚本、SHA/字节数和去大点数组摘要以便复现。构建只需QtCore与标准库，不引入SQL/Network/GUI，也不创建新线程/队列。RK资源门槛、温度/降频、同板控制、长稳与界面异步调用仍待实际场景确定；不能将WSL数值折算成板端达标。

本次仅新增独立源码/匿名输入/证据并局部更新原计划/README/知识入口，无业务代码删除、无环境安装/配置覆盖、无生产数据/原件修改，不需额外环境备份，没有自动提交或推送。复现命令维护在[工程README](../06_programs/sn100_host/README.md#o06-2-凝血曲线凝血点算法研究原型)。后续先取得实际曲线并确认信号单位/采样/加试剂零点/掩蔽期和项目，再冻结参数与独立参考时间/认可误差，评估正常、延长、低幅、噪声、无凝血、双相及截断数据。满足范围后再接O06业务链、主界面和逐模块仪器验收。

<a id="o06-1-offline"></a>
## O06-1：质控统计离线切片与核心依赖核查（2026-10-23）

在用户澄清凝血点目标前，本轮已从实际`frmQuality → CusQualityChart::refreshChart() → getStaticInfo/staticInfo_Impl()`调用链提取独立`quality/`统计切片；本轮保留这一已完成成果。原均值、极差、样本方差`(sum((x-mean)^2)-sum(x-mean)^2/n)/(n-1)`、标准差和CV=`SD/(mean+1e-12)`有真实本地实现，CV为带符号比值、不乘100。空/非有限/超预算/算术溢出明确失败；单点样本方差/SD/CV不可用，均值绝对值≤1e-12时CV不可用，JSON输出null及原因。这些有意数值边界见[来源清单](../06_programs/sn100_host/quality/origins.json)。

六组手算输入实际结果包含：`[8,10,12] → mean=10,range=4,sampleVariance=4,SD=2,CV≈0.2`；`[1,2,3,4] → mean=2.5,sampleVariance=5/3`；零均值、单点、恒值、负均值各自按合同输出。WSL与Windows32各19项单测、19项CLI检查通过，另进程与Windows六组完整results逐字节相同；ARM64构建通过，仅QtCore/标准库。合法`[1e308]`重复测量中原checksum可能溢出的实现已改为保存最后均值，并回归通过，未将inf当结果成功。

WSL对第一组3点合法输入重复10000次（每批20次返回事件循环）实际0失败：计算P95=0.000143ms、P99=0.000433ms、最大0.254290ms，10ms心跳最大间隔10.466814ms；CLI墙钟0.627821秒、1.6383%单逻辑核、RSS/VmHWM最大13,084KiB、PSS最大6,608KiB、1线程/6FD。仅为该极小固定负载的短时本机基线，不代表大数据、临床规则或RK响应达标。[正式统计证据与SHA清单](../04_references/sn100_host/offline_quality_20261023/)包含原始采样、三目标指纹、单测/CLI与实际结果。

[核心依赖审查](../04_references/sn100_host/offline_quality_20261023/algorithm_dependency_audit.json)保存21份原源码SHA及活跃凝血/拟合/校准/试剂/报告调用。当前AL4A仅有Windows PE32/x86 DLL与主要声明头，213份C/C++源审查范围内未找到核心实现或Linux`.so/.a`；原Linux封装多条计算路径跳过计算却返回true，因此没有直接迁入。自主凝血原型只覆盖上节两个候选方法，不自动补足其余AL4A能力。原R4s比较、低限优先级、连续SD/同侧/趋势规则存在源码疑点和合成反例，完整证据已保留，本轮不迁入判定/报警，也不自行改临床规则。

### 统一界面与后续操作说明

已通过computer-use将现有WSL历史页切到前台并查看，保留[当前介绍截图](../03_photos/20261023_sn100_history_introduction_01.png)：左侧报告列表、中间所选报告结果、右侧查询/详情/导出，灰色操作尚未接入。截图为实际独立O04页面，仅作界面介绍，不是统一主窗或本轮算法视觉验收。

随后O03-1已具备可运行主壳，本聊天按用户“打开界面介绍”的要求，用其新构建产物启动独立预览：`--offline --viewport 1600x900 --data-dir <根>/temp/sn100_host/o06_interface_intro/runtime`。实际日志返回`START module=O03-1`及`SHELL_READY`，通过computer-use定位该窗口并切到前台，[主壳实际介绍截图](../03_photos/20261023_sn100_shell_introduction_01.png)已保存。保持原1920×1080业务画布，小宿主可滚动；左侧八项主导航、顶栏状态/Guest、中央医院页可见。当前仅O01接入，其他业务页及账户会话仍待O03后续整合，不把这个预览称全系统统一功能完成。预览使用独立新数据、不重编/改动O03目标、未操作其测试窗口；按用户要求留窗口供查看，未据此添加退出或视觉验收结论。

统一操作由O03主壳逐步接入：原八项导航依次为试剂、样本、结果报告、校准、质控、分析项目、用户设置、仪器操作；医院和账户在用户设置子菜单，历史页到结果报告，协议能力在后台，凝血候选接口在项目验证后再进结果链。接入时沿用单一会话/权限、离页未保存守卫和worker退出顺序，做累计回归；设备具备后从V01真实连接/状态起逐模块验收，再做全链路/资源/长稳/部署回退。

<a id="o03-1-offline"></a>

## 2026-10-23：O03-1 脱机主框架与 O01 接入

**结论：本轮获准的O03-1在WSL离线短测通过。** 已读取最新根/局部规则、两个项目skill及业务验收参考，保留其他窗口未提交成果；独立`shell/`目标接入O01，未接入O02账户/权限或仪器通信。主/子导航、未保存保留/放弃、忙态拦截、锁库恢复、五类数据独立进程读回、正式入口正常退出均有实测。Windows32与ARM64最终源码构建通过；原Windows运行视觉/完整行为、1600宿主完整滚动操作、RK/仪器及长稳不据此放行。

### 文件、调用入口与并行边界

| 内容 | 本轮位置与职责 |
| --- | --- |
| 正式主壳 | `06_programs/sn100_host/shell/`：main管理外部宿主和worker；`frmMain::selectMainPage()`、`frmUserSet::selectUserPage()`管理导航；原UI、自定义控件、Material按钮及实际资源一并提取 |
| O01增量 | `ui/frmhospinfosetting.{h,cpp}`新增`setEmbeddedInShell()/requestLeave()/statusChanged()`；保留独立入口、现有存储和异步CRUD。新增离页守卫只在宿主调用，未改O02账户规则 |
| 独立构建/测试 | `shell/shell.pro/shell.pri/wsl.sh/build.sh`；`tests/shell_test.cpp/pro`。Windows/WSL/ARM产物、生成文件、日志和匿名数据按目标隔离在`temp/sn100_host/o03/` |
| 长期证据 | [offline_shell_20261023](../04_references/sn100_host/offline_shell_20261023/)：匿名SQLite/INI、公开CSV/JSON、实际日志、资源原值、来源/布局对照、构建指纹及SHA清单；原图直接保存在`03_photos/` |
| 受保护文件 | 根main/.pro/wsl/build/origins及O02账户UI/Store共10份开工前后SHA一致，见[指纹对照](../04_references/sn100_host/offline_shell_20261023/protected_files_comparison.json)。没有改O02源码、账户库或其入口，也没有关闭其他窗口/预览进程 |

O01嵌入时移出独立实验布局，恢复原业务面板`(330,10,801,551)`，隐藏独立页脚；数据库错误和忙态沿原81px状态标签显示“脱机／失败、处理中、未保存、就绪”，完整原因在tooltip，设备状态仍未知。离页/退出先拒绝在途操作；未保存默认保留；确认放弃时同步恢复最后成功读入/提交的快照，数据库零写入，随后再切换。焦点用QPointer恢复；重复导航不创建第二个页面或worker。

### 原版视觉依据与本轮原图

![O03-1真实WSLg窗口：原主壳内的医院信息页面](../03_photos/20261023_sn100_shell_hospital_01.png)

[未保存提示原图](../03_photos/20261023_sn100_shell_unsaved_01.png) · [未接入导航原图](../03_photos/20261023_sn100_shell_navigation_01.png) · [锁库错误原图](../03_photos/20261023_sn100_shell_locked_01.png) · [独立进程重开原图](../03_photos/20261023_sn100_shell_reopened_01.png)。上述1920×1080画布截图来自实际显示的WSLg/XCB测试窗口`QWidget::grab()`；没有重绘、换皮肤或用静态稿代替运行界面。

两份原UI的47个控件按objectName核对class及已声明geometry，未发现差异；原八项主菜单、五项用户设置子菜单及右侧灰色Material按钮/交互动画保留，33张PNG与参考运行资源SHA逐项一致。O01业务面板在主壳全局位置为`(538,176)`，对应原主栈/容器/布局边距与面板坐标之和。动态主题依据及68份原文件SHA见[shell来源清单](../06_programs/sn100_host/shell/origins.json)，结构核对见[视觉对照JSON](../04_references/sn100_host/offline_shell_20261023/visual_comparison.json)。这是源码结构与WSL运行对照，未执行参考工程，不等于原Windows发行版视觉验收。

必要差异分别登记：相对图片路径改QRC；沿用当前安装Noto字体，原PingFang实际度量仍待核实；本切片默认主导航6/子导航1以进入O01，原完整程序默认0/0；其余页面明确标“尚未接入（脱机）”，设备/账户动作禁用；外部滚动宿主保留原1920×1080画布而不重排控件。未保存框继承原按钮样式后出现末字裁切，已按文字/图标/边距增加最小宽度并重拍核对；原无效`rgba(99,99,99)`改为等色`rgb(99,99,99)`，首轮QSS警告消失。保留了Material实际依赖；参考包未提供对应代码许可证，后续分发前另核实，不冒用图标许可证。

**1600×900验证边界：**正式入口在该宿主尺寸下就绪、采样并正常退出。WSLg跨进程`QScreen::grabWindow()`返回全黑，已由像素检查标为不可用并排除出正式图片；[退出JSON](../04_references/sn100_host/offline_shell_20261023/formal_app_exit.json)保留原因。该项不声称完成1600全部滚动/右侧菜单操作或视觉验收，RK实屏另验。

### 实际输入、数据与关闭结果

最终03批次写进程PID2443：8组集成断言通过，QtTest含初始化/收尾为3 passed/0 failed、6218ms；另进程读回3 passed/0 failed、535ms。覆盖八项主菜单和五项子菜单实际点击、100次页面往返、同一O01对象与焦点恢复、主/子导航未保存取消及确认放弃、真实SQLite锁库时主/子切换和关闭拒绝、解锁重试、未保存退出的保留/放弃。确认放弃前后独立SQL快照相等；失败无部分提交，成功结果在新进程仍一致。

| 匿名数据 | 实际结果 |
| --- | --- |
| 医院名称 | `O03保存重开医院 O'Brien`，SQLite与兼容UTF-8 INI、新进程界面一致 |
| 五类字典 | 科室“血液科/XYK”、病人类型“门诊/MZ”、送检“送检员/SJY”、检验“检验员/JYY”、审核“审核员/SHY”，各1行；工号/备注及引号中文逐字段核对 |
| 锁库恢复 | 失败保留草稿，实际错误`database is locked`，可见状态为“脱机／失败”；解锁后备注`锁库恢复后保存 O'Brien 中文`保存成功 |
| 数据库 | 20480字节、5行，`integrity_check=ok`；未保存退出草稿没有进入SQLite或INI |
| 正式进程退出 | 原生X11窗口关闭消息仅发给测试启动PID对应的窗口，经过正常closeEvent；实际输出`SHELL_READY → STORE_CLOSED → THREAD_STOPPED true → EXIT code=0`，04复核为3 passed/0 failed、5274ms |
| O01累计回归 | 在O03自己的构建/匿名数据目录重做原write/read：五类新增/保存/读取、科室编辑/删除共享路径、200次操作及取消/错误/锁库通过，写8910ms、读222ms，均3 passed/0 failed；未把其他四类编辑/删除记为逐类实测 |

实际字段和断言见[write_result](../04_references/sn100_host/offline_shell_20261023/write_result.json)、[read_result](../04_references/sn100_host/offline_shell_20261023/read_result.json)、[actual_data.csv](../04_references/sn100_host/offline_shell_20261023/actual_data.csv)；累计回归见同目录`hospital_write_result.json/hospital_read_result.json`。数据均为本轮手写匿名输入，没有读取生产库/参考账号/连接参数，没有设备TX/RX。

### 响应、资源与复测口径

Ubuntu-24.04/qyq、x86_64、Qt5.15.13、GCC13.3、C++14、Release、Fusion/Noto、WSLg/XCB。测量时保留其他窗口，背景存在O04查看器及ARM64交叉编译，未隔离整机负载。导航计时为鼠标注入至`navigationChanged`选页信号，不包含合成器/像素呈现；10ms心跳为相邻事件处理间隔，包含测试截图/PNG编码及独立SQL核验开销，不把它当纯产品反馈延迟。

| 项目 | 实际观察值 |
| --- | --- |
| 导航信号，128样本 | p95 25.803ms，最大29.085ms；其中100次为重复往返 |
| 10ms GUI心跳，198样本 | 间隔p95 81.146ms，最大455.376ms，含上述采证开销 |
| 锁库失败 | 261.686ms，期间18次心跳、最大间隔108.125ms；未部分提交，解锁后恢复 |
| 正式入口ready后约5.016秒静置 | 单核CPU约16.35%；RSS最大118876KiB、PSS最大70964KiB、生命周期RSS峰值127376KiB；7线程、15FD。不是冷启动或长稳数据 |
| 活跃QtTest进程约6.264秒 | 单核CPU约80.15%，RSS最大144368KiB、PSS最大98059KiB、8线程/17FD，含测试及PNG开销，不能当正式入口业务CPU预算 |

[测量汇总](../04_references/sn100_host/offline_shell_20261023/measurement_summary.json)与原值保留在同目录。空闲CPU并非接近0；原Material状态机包含无限halo动画及持续update，是后续保持动画/视觉语义前提下的测量候选，当前未做因果优化试验、未删除动画。RK资源预算、温度/降频、同板控制、较大数据量、长稳和整机负载仍待验，不能把这轮短测标为全部“不卡顿/低资源”达标。

首轮失败及纠正保留：测试误把原不可勾选的子按钮当checked，修测试而保留原按钮；隐藏页脚后反馈不可见，接原状态区并验证错误色/完整tooltip；确认框文字裁切按字体度量修复；PID定向合成Alt+F4在WSLg未送达关闭，测试超时后的QProcess仅终止其自有子进程，未记为产品自然崩溃。改用原生WM_DELETE_WINDOW后正式退出通过；跨进程黑图已如实标不可用。早期make有约0.35–0.65秒共享文件时间偏差警告，最终原生程序/集成测试强制全量重编，未改系统时钟或使用旧产物。

### 构建、复现与当前状态

最终WSL x86_64、Windows Qt5.14.2/MinGW32 Release及ARM64均编译成功，架构/动态依赖与完整源码指纹见[build_metadata](../04_references/sn100_host/offline_shell_20261023/build_metadata.json)及[SHA清单](../04_references/sn100_host/offline_shell_20261023/SHA256SUMS)。原生SHA为`45ced0028cedbdb56876f94132b07da5c4dfa1f99a38f725af1d43325b4c1763`；Windows为`7c1f489bc73a405dc631aa1a7256467a2f90badbcf89e9f351ea782ad7e02bec`；ARM64为`bc72ea919c8b0d07cf8ea098516b4d82c1d3509783483a871ebb2a89bb95f56a`。ARM构建每次重编独立对象、有界并行，编译/链接/架构检查成功后才发布目标；未部署或执行ARM程序。

在WSL的`06_programs/`运行`bash sn100_host/shell/wsl.sh run`；需要1600宿主可追加`--viewport 1600x900`。默认数据为`temp/sn100_host/o03/runtime/`，支持另给绝对`--data-dir`，不使用归档库继续体验。[工程README](../06_programs/sn100_host/README.md#o03-1-脱机主框架与医院信息接入)列出独立write/read/正式退出及O01累计回归命令；后续入口/导航/离页规则均可从本节第一表定位。

计划、根/工程README已同步。当前仅关闭O03-1对应WSL离线子任务；O03-2账户/权限、其它已交付页面接入、原Windows完整运行对照、RK/仪器、1600完整交互及资源/长稳继续按同一计划推进。没有业务删减、系统环境覆盖、额外备份或自动提交/推送；回退本切片可继续使用保留的原O01/O02入口，现有运行数据不迁移或重置。

<a id="o03-2-offline"></a>
## 2026-10-23：O03-2 账户页、角色与注销接入主框架

**结论：O03-2的WSL离线功能核验通过，响应峰值定位和完整实物验收仍未关闭。** 按用户本窗口要求接入O02账户页，保持原版布局、角色与原提示框；读取根/局部AGENTS、当前计划/记录、工程说明及两个项目skill和验收参考，README局部更新另按readme-authoring核对命令。沿用O03独立目标，所有新构建、临时匿名数据和日志在`temp/sn100_host/o03_2/`，医院/账户分别在`hospital/`与`account/`；未复用O02运行库、未改参考原件、未连接或部署设备。旧O03-1产物和数据保留。

后续同日已完成[心跳机制与采证开销专项排查](#o03-2-latency)；本节保留当时原始峰值、结论与覆盖范围，不覆盖旧测量。

### 实现、原版依据与维护入口

| 入口/边界 | 本轮结果与依据 |
| --- | --- |
| `shell/main.cpp` | 可选`--accounts`启用O03-2；两store分别在各自worker初始化、使用和关闭。默认Guest，`--test-session`明确模拟身份，`--device-state`只模拟账户规则条件，仪器仍脱机/未知；不启用生产登录、原全局权限表、TCP/LIS/算法启动链 |
| `frmMain` / `frmUserSet` | 账户index 0、医院index 1；主壳仍进入用户设置。`accountPage()/accountSession()/requestLogout()`公开稳定接口，`sessionChanged/loggedOut`同步唯一worker会话；保留无AccountStore构造兼容旧O03-1。切页复用一页/一store，不重复初始化 |
| 原角色规则 | 角色0–5及选项/列表过滤来自O02和原`frmaccountsetting`；角色1–4仅显示角色<4的账户，管理员5显示角色<5，操作选项保留原1..min(role,4)。管理员更新默认索引越界、运行态同角色修改拒绝而降级可成功、允许空新密码等原分支均保留，不根据数字大小发明全局ACL |
| 会话与注销 | 角色由匿名库读取，每次写入重新核对身份/角色。原顶栏显示角色及姓名；宽度不足省略，完整身份/模拟说明在tooltip。手动注销先检查两页busy及O01草稿，再排队`setSession(empty)`，收到真实Guest才发`loggedOut`；pending期间拒绝主/子导航及退出。成功更新账户总会注销、删除当前账户成功注销；自动Guest立即生效，不再触发医院放弃确认或清除其草稿 |
| 账户离页增量 | `setEmbeddedInShell()`取消独立宿主居中位移；`requestLeave()`忙态拒绝，成功离页清六个密码框；Guest清四个用户名、角色选项及列表缓存。原O02单页入口、UI/密码校验/角色业务及提示框保持，旧`signalLoginOut`保留兼容；账户会话不再在UI重复伪造清除 |
| 数据与退出 | 新账户库仅显式临时秘密播种，脚本不重播种既有库；继续使用O02路径/tag/schema/参考目录保护。事件循环退出后在所属worker阻塞排队close，再quit/wait两个线程；不从GUI线程使用worker SQL连接 |

本轮修改`shell/main.cpp`、主/用户容器cpp/h、独立pri/构建/WSL脚本、来源清单，新增账户资源QRC和`shell_account_test.*`；只对共享`ui/frmaccountsetting.cpp/h`增加嵌入、离页与会话清理适配。根O01/O02入口/构建、AccountStore cpp/h、账户和两个主壳UI共11份开工指纹均未改变，见[保护清单](../04_references/sn100_host/offline_shell_account_20261023/protected_after.json)。另一窗口新增的历史接入准备文件未加入本轮构建/源码清单；本轮未删减原业务代码、未覆盖系统环境，因此不额外做环境备份。

### 原版界面与截图

原/现主壳、用户设置、账户页的33+14+40=87个控件，class、objectName和声明几何逐项一致，本轮三份UI字节指纹未变。账户原1471×881画布、表格`(10,30)`及业务区`(410,30,641,811)`保留；17张账户PNG与原件SHA一致，箭头SVG仅末尾LF差异、解析XML相同；原主壳33PNG和Material视觉依赖继续保留。[逐控件/资源/规则引用审查](../04_references/sn100_host/offline_shell_account_20261023/original_layout_audit.json)与[当前来源清单](../06_programs/sn100_host/shell/origins.json)记录必要差异。

实际WSLg/XCB窗口用`QWidget::grab()`采证，已逐张打开核对，无跨进程黑图；密码字段均清空。主画布1920×1080，Noto字体替换与原Windows活动主题/字体仍待运行对照，不能只凭坐标相同判定完整视觉一致。

按work-habits另存编号标注，原图未覆盖；标注SVG可编辑，渲染PNG已打开检查指向、文字及无遮挡。

![O03-2账户页：原列表、业务区、顶栏身份与导航的编号标注](../03_photos/20261023_sn100_shell_account_account_marked_01.png)

[账户页原图](../03_photos/20261023_sn100_shell_account_account_01.png) · [可编辑SVG标注](../03_photos/20261023_sn100_shell_account_account_marked_01.svg)

![O03-2人工注销后：Guest及保留的医院快照](../03_photos/20261023_sn100_shell_account_logout_marked_01.png)

[注销原图](../03_photos/20261023_sn100_shell_account_logout_01.png) · [可编辑SVG标注](../03_photos/20261023_sn100_shell_account_logout_marked_01.svg)

| 截图 | 实际状态 |
| --- | --- |
| [账户页](../03_photos/20261023_sn100_shell_account_account_01.png) | 工程师匿名会话，原账户列表/三个页签/输入区及顶栏身份 |
| [Guest](../03_photos/20261023_sn100_shell_account_guest_01.png) | 列表隐藏、无可用角色选项，拒绝Guest写入；仅留公开测试目标姓名 |
| [医院页](../03_photos/20261023_sn100_shell_account_hospital_01.png) | 原业务区及实际保存的中文/英文/撇号输入 |
| [未保存确认](../03_photos/20261023_sn100_shell_account_unsaved_01.png) | 原确认路径，“保留更改”默认；取消保留草稿，放弃恢复已提交快照 |
| [注销后](../03_photos/20261023_sn100_shell_account_logout_01.png) | 顶栏真实Guest、注销禁用，医院已提交内容保留 |
| [锁库恢复](../03_photos/20261023_sn100_shell_account_recovered_01.png) | 有限等待失败后无部分写入，解锁重试新增成功 |
| [独立进程重开](../03_photos/20261023_sn100_shell_account_reopened_01.png) | 工程师会话及实际持久化列表读回 |

### 输入、实际输出与累计回归

最终[12个独立测试进程](../04_references/sn100_host/offline_shell_account_20261023/validation_summary.json)共42 passed/0 failed，包含各进程初始化/清理项，不能当作42个独立业务用例。O03-2写入、重读、正式入口退出及独立导航各3 passed；旧O03-1写/读/正式退出各3 passed；O01与O02独立写/读各3 passed；账户边界9 passed。

O03-2综合实验12组具体验证在[write_result](../04_references/sn100_host/offline_shell_account_20261023/write_result.json)：Guest与五角色选项/列表/顶栏、Guest写拒绝、伪造高角色拒绝、库内角色降级及身份失效即时刷新、隐藏页权限/密码清理、100次真实鼠标账户/医院切换、O01未保存取消/放弃、运行态原分支、更新及删除当前人自动注销、手动注销取消/接受/重复、锁库期间主/子导航/注销/退出拒绝及恢复。另测模拟running时原按钮人工注销成功，未借设备状态伪造真实仪器ready。

覆盖边界：各角色动态核对了选项、列表可见性/行数，没有逐角色比较完整用户名集合；O03-2锁库试验锁的是账户Add，医院busy另在旧无账户主壳回归，尚未专门组合验证“带账户壳的医院保存锁等待中人工注销拒绝”。静态守卫已覆盖两页，但这两项不写成专门动态通过，已交给后续整合回归补验。

医院具体输入为`O03-2保存重开医院 O'Brien`、`血液科`、自动助记符`XYK`及备注`O03-2匿名科室 O'Brien`。独立进程重开核对医院与账户两库`integrity_check=ok`，7个账户的公开字段及1条科室数据完全一致：原5匿名角色、保留的`O03Retained`角色2及锁库重试`O03LockRetry`角色1；临时自删除账户不存在。见[公开CSV](../04_references/sn100_host/offline_shell_account_20261023/public_rows.csv)及[read_result](../04_references/sn100_host/offline_shell_account_20261023/read_result.json)。未保存放弃及注销前后快照比较为0草稿写入。认证库/tag/临时密码及其派生凭据不归档，长期证据只保留公开列。

O01累计回归核对五类新增/保存/独立重读，科室编辑/删除共用路径、取消/锁库恢复及200次操作；本轮不据此称五类分别完整CRUD。O02原单页累计回归另验100次真实增删、角色/密码原分支、确认/错误/注销、锁库与重开；账户边界覆盖拒绝无秘密隐式建库/旧库重置、未标记/软链/参考目录/损坏结构、角色伪造/失效会话等。原独立入口没有被主壳接入替换。

正式入口以`--offline --accounts --viewport 1600x900`启动，等双页ready后采样，再按其PID匹配窗口发送原生`WM_DELETE_WINDOW`，正常退出0；实际输出依次含`SHELL_READY / HOSPITAL_STORE_CLOSED / ACCOUNT_STORE_CLOSED / THREADS_STOPPED true / EXIT code=0`，见[正式入口结果](../04_references/sn100_host/offline_shell_account_20261023/formal_app_exit.json)。只证明1600宿主启动/退出，完整滚动及右侧交互留给后续整合，不据此关闭1600全部交互验收。

### 响应、资源、失败与复测

| 条件 | 实测及限制 |
| --- | --- |
| 综合功能实验 | Release/WSLg/XCB、1920画布、匿名5角色起始数据；106个成功导航信号p95 15.612ms/最大31.379ms；10ms GUI心跳1050样本p95 26.814ms、最大1556.009ms。范围包含测试驱动、弹窗、截图和外部SQL快照，**峰值来源未分离，保留待定位，不宣称满足500ms无响应候选线** |
| 单独导航负载 | 新数据/新进程，100次真实切页，每次点击后让事件循环运行10ms；不抓图、不编辑、不弹窗、不外部SQL。导航p95 22.962ms/最大25.092ms，心跳100样本p95 46.734ms/最大48.590ms；点击到导航信号不等于合成器像素显示。见[navigation_focus](../04_references/sn100_host/offline_shell_account_20261023/navigation_focus.json)，不能替代或抹去综合峰值 |
| 正式入口空闲短测 | ready后约5.014秒，CPU占单逻辑核15.959%，RSS最大119900KiB、PSS最大102075KiB、进程启动以来峰值RSS127668KiB、8线程/18FD。保留原Material动画，未作取消动画等视觉变化；WSL短测不代表RK预算/同板控制或长稳达标 |
| 存储/测试开销 | 综合测试含SQL核对/PNG采证的CPU约64.73%单核，不能当正常业务CPU。最终医院库20480B、账户库12288B、INI48B；这是固定匿名小规模终值，未完成大规模增长预算。采样原值在write/formal JSON，不用测试进程资源冒充产品空闲资源 |

失败与复测：首轮测试把原`CusBadgeButton(QWidget)`当成`QAbstractButton`导致测试空指针，改为正确控件类型；第二轮未等待新表格布局/剪贴板传输，补真实鼠标键盘输入后的等待，未用`setText`代替。旧主壳回归在模态关闭后窗口未激活的焦点断言失败，先激活本测试窗口并等待原生active，保留所有编辑焦点恢复断言后通过。纯导航首轮同步信号过快导致无timer样本，按上述10ms事件循环间隔重测。ARM首轮两个手动rcc资源符号重名，分别加唯一`-name`后重新编译/链接通过。最终通过日志及原因在同一[验证汇总](../04_references/sn100_host/offline_shell_account_20261023/validation_summary.json)，失败产物不用于运行/部署。

### 构建、归档与后续交接

WSL x86_64 GCC13.3/Qt5.15.13、Windows Qt5.14.2/MinGW7.3 32位Release、ARM64 GCC13.3/Qt5.15.13均构建通过；本机动态依赖无缺失，目标不链接QtNetwork，ARM确认为AArch64，未部署或执行。见[构建指纹](../04_references/sn100_host/offline_shell_account_20261023/build_manifest.json)、[依赖核对](../04_references/sn100_host/offline_shell_account_20261023/dependencies.json)及[本轮冻结源码清单](../04_references/sn100_host/offline_shell_account_20261023/source_manifest.json)。归档文件、7张原图与4份标注文件均有SHA，见[SHA256SUMS](../04_references/sn100_host/offline_shell_account_20261023/SHA256SUMS)和[截图指纹](../04_references/sn100_host/offline_shell_account_20261023/screenshots.json)；公共主壳此后交给O03-3整合，后续live源码变化不回写本轮快照，必须另建构建/回归证据。

独立只读复核已归档：[公开证据/指纹](../04_references/sn100_host/offline_shell_account_20261023/public_evidence_review.json)、[测试断言与覆盖](../04_references/sn100_host/offline_shell_account_20261023/test_evidence_review.json)、[worker/SQL/关闭顺序](../04_references/sn100_host/offline_shell_account_20261023/store_thread_review.json)、[视觉/会话/生命周期](../04_references/sn100_host/offline_shell_account_20261023/visual_and_lifecycle_review.json)。未发现可确认的阻断缺陷或退出竞态；复核是静态/公开证据审查，不算新增运行通过。发现的审查文案“注销重置添加页签”已在本轮audit JSON纠正为“保留当前页签、清编辑缓存”，保留复核报告作为发现/纠正链。响应、逐角色姓名集合、医院busy+注销组合及资源长稳边界按上文保留。

WSL在`06_programs/`执行`bash sn100_host/shell/wsl.sh run`，默认Guest；显式模拟工程师加`--test-session offlineEngineer`。新匿名体验目录为`temp/sn100_host/o03_2/runtime/`，不要拿归档数据继续写入。测试命令与隔离路径见[工程README](../06_programs/sn100_host/README.md#o03-2-账户接入权限与注销)，旧O01/O02入口继续可用。本轮没有自动提交/推送Git。

计划、根/工程README及来源清单已同步。本轮稳定的页/会话/注销/离页接口可交给后续历史整合窗口，公共shell文件需串行修改并重新累计回归；同一工作区的“确认 WSL 下 Qt 移植验证”聊天已完成独立只读复核，归档/图片/保留产物指纹与文档链接最终核对通过。保留原Windows完整行为/视觉、正式认证/全局user_powers、综合心跳峰值定位、RK屏幕/输入/资源、仪器/同板控制、长稳及1600完整交互待验，**不关闭完整O03或实物验收**。

<a id="o03-2-latency"></a>
## 2026-10-23：O03-2 心跳峰值机制、正常操作与采证开销分离

**18个WSLg/XCB独立进程的专项对照全部通过，已确认同步测试驱动造成事件循环饥饿的机制。** 不做外部截图、SQL快照和GUI资源采样时，连续100次同步导航仍复现1.669–1.832秒心跳间隔，整个点击阶段没有timer交付；每次点击后让事件循环运行10ms，三轮最大间隔降至50.1–52.7ms。正常业务三轮最大86.8–117.2ms，独立整窗抓取加PNG保存65.7–77.1ms。原布局、主题、280ms开/关弹窗动画及产品内部截图路径全部保留，未修改业务实现。

原历史1556.009ms样本不改写。它没有阶段标签，不能事后证明该单个样本的精确起因、把剩余时间归给PNG，或分摊百分比。本轮完成的是有阶段证据的同类机制复现及开销分离；完整O03、产品资源预算、像素反馈、RK/仪器和长稳仍分别待验。

### 范围、原版行为与可维护入口

按用户本窗口最新要求，重读根/局部规则、同一计划/事项记录、工程说明、两个项目skill及验收参考。公共产品文件由O03-3整合聊天维护，GUI与其串行，测试结束已返还；本轮新增`tests/shell_latency_test.cpp/.pro`、`tests/shell_latency.sh`和`tests/prepare_shell_latency.py`。原`tests/shell_account_test.cpp`只在50对导航中每次点击后增加`QTest::qWait(10)`及原因注释；逐字对照确认100次操作、240处`QVERIFY/QCOMPARE`和12组业务结果记录均未删减。无业务删减，无主题/几何/动画改动，无设备连接/部署、系统调频/调度修改或自动Git提交。

| 维护入口 | 作用与约束 |
| --- | --- |
| `PhaseProbe` | 单调时钟记录每个阶段起止、每次10ms PreciseTimer实际处理区间、阶段内心跳交付数、CPU及停止时未交付尾段。逐一计算“心跳区间∩阶段”，嵌套阶段不能直接求和 |
| `burstNavigation` / `pacedNavigation` | 同一导航、同一原动画、100次点击；仅是否逐次让出事件循环不同。可另开显式截图或200ms GUI `/proc`采样对照 |
| `normalOperations` | 工程师会话下10次账户Add/Delete对、10次医院保存、未保存取消/放弃及人工注销取消/接受；外部采证在trace结束后执行，产品原弹窗和内部截图照常执行 |
| `evidenceOverhead` | 分别计时抓图、预抓图片转PNG、预编码文件写入、原整窗抓取保存、两库公开SQL校验、资源采样和匹配ASCII输入；不拿总操作时长冒充SQL/GUI阻塞 |
| `prepare_shell_latency.py` / `shell_latency.sh` | 只冻结真实依赖的132份公开文件，复制前后及每次运行核SHA；新尝试、新构建、新数据目录。构建失败不运行旧产物，默认不触碰O02/O03旧目录 |

当次冻结的是含O03-3兼容接口的当前主壳，实例只接O01/O02，没有创建历史页；静态链接的历史core仅为当前API依赖。新实验起始为5个匿名角色账户和空医院库；旧峰值发生时的中间状态为6账户/1科室，历史归档只有源SHA而非完整旧树，所以不称完全相同旧产品/旧数据的重跑。所有正常输入使用ASCII真实键盘事件；中文/IME、全角色写入、Update/自动注销不在本专项正常负载内，历史功能通过记录与其覆盖边界继续保留。

### 环境、负载和原始结果

Ubuntu-24.04/qyq，WSL2 x86_64 6.18.54.1，GCC13.3、Qt5.15.13 Release，WSLg/XCB；2560×1600宿主、1920×1080原画布、DPR1、约96.10DPI、Noto Sans CJK SC 11/Fusion。实际QtTest QWidget注入默认鼠标/键盘延迟均0ms。三个轮次按固定顺序轮转六条件，每次新进程/新库，共18个不同PID；没有随机化、专用CPU或频率控制，数值按本次环境保留，不能把组间差值全解释为单一因素的效应。

| 条件（各3进程） | 每进程最大心跳间隔（ms，依轮次） | 可据此判断 |
| --- | --- | --- |
| 连续100次同步导航 | 1831.738 / 1778.990 / 1669.168 | 没有外部截图/SQL/GUI采样也出现秒级间隔 |
| 先抓图保存再连续导航 | 1997.484 / 1807.026 / 2149.263 | 同一心跳区间可覆盖采证和连续点击，须看阶段交集 |
| GUI资源采样+连续导航 | 2030.360 / 1698.067 / 1955.387 | 采样器打开不能令timer插入同步点击循环 |
| 每次换页后`qWait(10)` | 52.680 / 50.140 / 50.245 | 三轮均保留100次点击；心跳p95为42.736 / 46.660 / 48.284ms |
| 正常业务、无外部采证 | 117.169 / 86.820 / 93.707 | 心跳p95为25.983 / 23.262 / 23.866ms，原弹窗/动画保留 |
| 截图与公开核验分项 | 79.628 / 79.286 / 75.426 | 逐项及匹配输入对照，不等同正常业务CPU |

所有新结果的业务断言、双worker关闭均true，每进程QtTest为3 passed/0 failed（含初始化/清理，不能称54个独立业务用例）。全部9个含burst的进程均在100次连续点击阶段交付0次心跳，各有一个真实心跳区间完整覆盖该阶段。首个纯burst阶段1803.901ms、进程CPU1653.451ms；对照其前后心跳及阶段交集，排除了“必须有外部PNG或资源采样才出现秒级间隔”的解释。burst仅约16个心跳样本时p95可等于max，不当作稳定分布估计。

![六条件各三个独立进程的心跳最大间隔](../03_photos/20261023_sn100_latency_summary_01.png)

[原始分析JSON](../04_references/sn100_host/offline_shell_latency_20261023/analysis_summary.json) · [逐阶段CSV](../04_references/sn100_host/offline_shell_latency_20261023/phase_summary.csv) · [逐心跳摘要CSV](../04_references/sn100_host/offline_shell_latency_20261023/heartbeat_summary.csv) · [可编辑统计SVG](../03_photos/20261023_sn100_latency_summary_01.svg)

![新运行中抓图和连续点击与心跳区间的对照](../03_photos/20261023_sn100_latency_timeline_01.png)

该图来自新`capture_burst_03`，不是历史1.556秒样本。每点表示相邻两次回调区间的末端，竖线为该区间长度；空白处没有连续采样。金色/蓝色带为真实阶段，归因检查的是区间交集。[可编辑时间轴SVG](../03_photos/20261023_sn100_latency_timeline_01.svg)与全部逐次原值一并保留。500ms仅项目候选观察线，不能据此宣布RK或完整系统放行。

### 抓图、存储、输入和正常动画的区别

| 分项 | 样本数 | 实际墙钟范围（ms） | 口径 |
| --- | --- | --- | --- |
| `window_grab` | 15 | 14.877–21.539 | 只抓当前窗口为QPixmap |
| `pregrabbed_png_encode` | 15 | 34.265–55.036 | 预抓QPixmap保存到QBuffer，含转换/分配和PNG编码，不是纯压缩器耗时 |
| `preencoded_file_write_flush` | 15 | 3.839–7.333 | 预编码字节经QFile写入/flush/close；未fsync，不表示物理落盘持久性 |
| 原`grab().save()` | 15 | 65.661–77.092 | 直接复现原整窗抓取及PNG文件保存；不把分项跨轮相加替代实测 |
| 两库公开SQL核验 | 15 | 18.927–24.838 | 两次公开字段快照及integrity_check，不是业务worker提交时间 |
| `/proc`资源采样 | 15 | 1.086–1.925 | 复现测试资源读取调用，含smaps_rollup |
| 匹配ASCII剪贴板/键盘输入 | 各30 | 约0.65–1.22 / 0.53–0.99 | 字段文本断言均通过；剪贴板有X11传输warning，不能推广中文/IME或认定系统剪贴板可靠 |

三轮正常trace各约21.54–21.69秒，心跳1779/1825/1814次，停止尾段2.114/4.143/6.208ms独立登记，不伪造为已交付样本。首轮删除操作约1.23–1.28秒的总流程期间仍交付100–112次心跳，证明等待原提示框/动画的完成时长与事件循环冻结不同。每轮实际Add/Delete各10次、医院保存10次，公开核对通过且worker正常关闭；账户库12288B、医院库20480B为本小夹具终值，没有完成大规模增长或长稳预算。

正常负载具体输入为`LatencyPair00`至`LatencyPair09`逐个新增再删除，医院名依次`Latency hospital 0`至`Latency hospital 9`；未保存草稿为`Latency unsaved navigation`和`Latency unsaved logout`。三轮实际公开输出均为原5匿名账户、无`LatencyPair`残留、医院名`Latency hospital 9`、0条字典行、两库`integrity_check=ok`；取消保留草稿，确认放弃恢复最后提交值，人工注销后Guest且密码框清空，均有业务断言而非仅计时成功。

正常trace进程CPU占单逻辑核约58.997–62.317%，含GUI、worker、动画和测试驱动，不是GUI线程CPU、更不是产品空闲值。外部200ms资源观察器运行在另一进程，启动阶段会采到包装脚本/准备进程，因此不把整个启动窗口的CPU/RSS平均值当产品稳态。本专项不改变原动画成本，原正式入口空闲短测/完整资源预算另行保留。

### 原综合修正、失败保留与可复现证据

原测试`navigationClick()`注入QWidget事件、导航和信号同步完成；`waitUntil()`在条件已成立时立即返回。原50对循环没有事件循环间隔，故100次测试动作串在同一调用栈里，timer/绘制等待该栈结束。历史100次导航记录时长之和1401.224ms，与1556.009ms处于同一量级，属于静态线索；没有旧阶段标签，不能据此给旧峰值作精确归因。本轮三组纯burst、两种附加开销burst及paced对照把这个机制动态复现出来。

修正仅在原循环每次操作后加`qWait(10)`，给排队绘制、动画和timer运行机会，不添加产品`processEvents()`、不改线程/弹窗/布局。修正原用例已用同一冻结产品源码重新构建成功；两次WSLg实际执行均在科室中文`paste`处报`QXcbClipboard::setMimeData: Cannot set X11 selection owner`后失败（医院名称已输入成功），约1.263/1.257秒，尚未进入百次导航。两次均2 passed/1 failed，原日志保留，**本轮没有把修正原综合用例登记为通过**；专项paced三轮通过也不能替代其全部业务回归。

为避免占用整合窗口WSLg，另尝试仅在`temp/`中`apt download`并`dpkg-deb`解包独立Xvfb及缺失依赖，Ubuntu索引版本/大小/SHA和`ldd`核对通过。未执行安装、sudo、ldconfig或配置写入，无需配置备份。独立`:89`服务因固定调用缺失的`/usr/bin/xkbcomp`而在键盘初始化阶段退出1，另报UNIX socket权限提示；没有运行任何综合用例、没有新增通过结果。不修改系统路径或WSLg socket权限，未触及`:0`；自有server已回收，临时认证文件已删除。来源、包SHA、动态库及失败日志在归档`xvfb_tool/`，临时deb/解包工具留`temp/`，不是环境定版。

初次诊断构建因QtTest内部默认延迟函数的块作用域声明不可见而失败，补准确前向声明后重新构建；第二次冻结捕获并行protocol文件变化并主动拒绝，随后收紧到实际依赖132文件，在全新`final_03`目录冻结、构建成功。失败尝试原样保留，没有运行失败构建的旧产物。图表首轮Windows离屏渲染未加载字体，显式加载字体并修复刻度缓存裁切后重渲染，PNG/SVG均已打开检查文字/坐标/单位；图表渲染不是WSL产品验证。

公开证据在[offline_shell_latency_20261023](../04_references/sn100_host/offline_shell_latency_20261023/archive_policy.json)：18份逐次结果、每进程日志/构建绑定、汇总CSV/JSON、只读独立复核、修正原测试与失败日志、独立X11补验阻碍、132份公开源及生成helper的冻结压缩包、SHA清单和原始账户截图。源清单SHA为`7f56e19e98cb6d62ce528854a7bef9e38992d5211b8ca9f45a2be30f5adb30b3`，诊断ELF SHA为`44c74f465d0b8e474f568a80572fc980f107fd9e493405abbe9fa01488f157d6`；本轮WSL Release编译通过，不据此增加Windows/ARM产品或板端运行通过结论。压缩源包使用专用Git LFS规则并保留SHA；旧O03-2归档40份payload重新校验未变。[独立交付复核](../04_references/sn100_host/offline_shell_latency_20261023/independent_delivery_review.json)已核对归档清单、源包全部132源+helper、5图和新记录链接，未发现错误。原[账户空密码框截图](../03_photos/20261023_sn100_latency_account_01.png)未覆盖，数据图另存PNG及可编辑SVG。DB/tag、临时密码及认证派生DB哈希不进入正式证据。

计划与根/工程README同步到本节：机制排查和开销分离已完成，原综合完整重跑、WSLg中文输入可靠性、原Windows完整视觉/行为、RK屏幕/输入/资源、仪器/同板控制及长稳继续按条件分别核验。复现入口与新目录命令见[工程README](../06_programs/sn100_host/README.md#o03-2-账户接入权限与注销)，不重置旧库、不用旧无标签样本替代阶段证据。

<a id="o04-2-offline"></a>
## O04-2：复测均值/CV、原模板报告与PDF/预览（2026-10-23）

本轮承接O04-1，按用户“先均值/CV，再报告模板、PDF和打印预览”的要求推进。开发只在正式`history/`和必要的`thirdparty/qtrpt/`，参考工程只读；原报告页、明细四份`.ui`、配色、原打印按钮和五列表格保持。与O03/O06串行交接`requestLeave`接口和GUI测试时段，未关闭他人窗口，未启用账户、仪器、网络或生产数据。

### 业务入口与原版对照

| 入口 | 规则和输出 | 失败与恢复 |
| --- | --- | --- |
| 选中报告 → `HistoryPage::displaySelectedResults` → `HistoryResultStatistics::groups` | 参考`FrmReportMaterial::slot_showSingleRowResult`，同一报告按项目/结果类型分组；保留全部原任务及异常星号。仅Calibration/Rate乘显式单位倍率。样本SD分母n−1，CV=SD/mean×100，原有符号、两位百分数和近零显示规则保留。默认平均值开关true | 缺失/非法数值计无效，不补写任务；少于两有效值、零均值、溢出、配置冲突明确给不可用原因，不伪造0。只生成显示快照，不改变库 |
| 原主页面/明细`btnPrinter` → `reportPreviewRequested` → `HistoryReportController::preview` | 未保存患者先保存/取消；生成不可变值快照，使用原内嵌XML及QtRPT预览工具栏、翻页/缩放 | 不读取参考数据库/INI，不自动保存草稿；重复进入和离页有忙态守卫 |
| 原预览“Save as PDF” → 非原生文件对话框 → `QProcess`独立渲染器 | 单在途、1MiB快照、384结果行、30秒超时、32MiB输出检查；匿名标记目录`exports/`内新PDF，成功校验后发布，既有文件不覆盖 | 取消不写文件，渲染失败清自己的临时目录，保留预览可重试；不自动打开桌面阅读器 |

均值计算保留double到最终格式化，不保留原中间`QString::number`默认6位有效数字带来的精度损失；这是明确的数值差异，不改变原结果。列只来自报告中已出现的显式类型，同项目其他任务缺该类型时保留空值和task/time/abnormal信息，不编造从未出现的列。

### 模板、依赖与必要差异

原`tool/qtrpt_module/qtreport.cpp`绑定拼音变量，而交付`resource/report_template/report.xml`使用英语变量，直接照搬会留白。本轮显式绑定13个报告头字段及5个结果字段；医院/项目/单位/精度均为新的匿名契约，默认不编造临床参考区间。`sample_sn`按模板“样本序号”绑定当日`sn`；原包装层“标本号”绑定条码的歧义保留为待原运行核实。送检、检验、审核分别对应sender、examiner、auditor。

模板SHA-256为`673788540ab34ef214e513d6241e983586b98d0256166a6c01439aaee954efc2`，原XML逐字节提取；`pageWidth=840/pageHeight=592`经原引擎÷4，实际210×148mm，保留该尺寸，不把`pageFormat=A4`标签当真实A4。报告打印原配置的各任务值，不加入页面均值/CV。原模板固定单行字段遇换行显式拒绝，以免输出截断内容；数据库原文保持。原字体缺失时只作字体替代，实际视觉核对另记。

QtRPT3.0.0最小依赖36个原文件及Apache2许可进入正式`thirdparty/qtrpt/`，未修改的34文件仍与参考SHA一致。`qtrpt.h/.cpp`补丁单列非阻塞预览、输出请求信号、明确打印设备所有权、绘制失败检测及脱机物理打印禁用；来源/原现SHA/理由见该目录`origins.json/NOTICE`。与原`.pro`相同定义NO_BARCODE、关闭QXLSX，保留原依赖代码，未按“搜索无引用”删业务。QtCharts的GPL3/商业许可与QtRPT Apache2分别保留说明，正式分发沿用项目许可核对。

原引擎实际依赖Qml/Charts/Xml/PrintSupport。Native仅新增确定缺失的9个包，零升级/删除，来源/包/SHA/前后状态归档在`07_backups/qt_environment/sn100_report_amd64/`；ARM21包及54个SONAME闭包归档在`sn100_report_arm64/`并只解入临时sysroot。未替换系统Qt、未安装ARM包入WSL、未连接/修改RK。Windows继续原Qt5.14.2/MinGW32。

### 实际输入、输出与失败恢复

**本卡WSL离线功能与累计回归通过；完整O04仍未关闭。** 统计17组手写金样本覆盖样本SD、有符号CV、近零、倍率、缺失类型、非法/非有限、零均值、单有效值、溢出及配置冲突。界面样本3保留8个原始结果，增加两行原均值/CV；关闭平均开关恢复8行，再开回10行。PT的12.3/13.5得到12.9及6.14%，INR的1.24/1.11得到1.17及7.53%；样本4异常值仍为`****`及星号。患者、结果、列配置的四表独立快照前后相等，`integrity_check=ok`。

| 验证 | 实际结果 |
| --- | --- |
| 原按钮/患者草稿/离页 | 真实鼠标点击主页面`btnPrinter`进入原预览；患者未保存草稿不发送打印快照，确认放弃后恢复；预览/导出忙态拒绝离页。脱机物理打印及快捷键不启用 |
| 真实工具条/文件选择器 | 原Save as PDF → 非原生对话框，Cancel不生成文件，立即再次点击可重开并保存。首轮发现仅等destroyed释放对话框导致快速重试被挡，改为finished即释放、旧destroyed只清相同实例后复测通过 |
| 报告头/数值 | 13个英语头字段及5个结果字段独立核对；0.00合法；未知性别沿原包装层输出`Unknown`。原样本3含换行被拒绝，只在测试副本中将name/remark设为单行后打印7个已配置任务值，原库不变 |
| 单页金样本 | PT12.3、INR1.24、PT13.5、INR1.11、APTT35.68、FIB2.35、D-D123.456各一次；RAWX仅按显式禁用配置不进入报告；均值/CV不进入打印。合成区间8–12单独验证7↓/10/13↑/非法`****`，不是临床区间 |
| 分页 | 64条PT原任务8000.1至8063.1，实际5页，每个数值独立解析恰一次；首项患者ReportTitle只在第一页，原表头/报告页脚及页码1/5至5/5保留 |
| 渲染异常/恢复 | 新文件限制、目录外/相对路径/空报告、重复导出、缺渲染器、输入模板控制符及U+10FFFF缺字均明确失败，无目标覆盖；可恢复正确报告。QtRPT会再次解释数据内`<>[]`，故该切片明确拒绝并报字段，不执行为表达式 |
| 真实进程失败/超时 | 可执行但格式错误文件产生`execve: Exec format error`；fake helper用`exec sleep 60`复现真实30秒上限，收到“PDF渲染超过30秒”并终止该进程，临时job/目标清理、忙态解除、重试成功。导出中关闭预览后仍完成原值快照PDF；未留下sleep子进程 |

无窗口规则/所有权首轮7 passed/0 failed；完整首轮8 passed/1 failed定位Cancel重试，第二轮文本断言失败属于抽取器映射而非缺字；最终04的documentContract/previewAndPdf/errorLifecycle为5 passed/0 failed、34431ms。信号枚举注册警告属于测试辅助，补`QProcess::ProcessError`注册后，最终05单独生命周期3 passed/0 failed、30413ms，无该警告。旧O04的write/read/pagination分别3 passed/0 failed、6885/306/641ms；O01的write/read分别3 passed/0 failed、8766/227ms，均为独立新目录/进程、退出0，未把旧缓存产物当本轮结果。

### 原界面与PDF逐页核对

四份`.ui`与O04-1归档指纹一致，本轮未改；原版坐标/布局沿此前结构核对保留。O04-1既有的Material/mask自定义控件替换为原生Qt、图标文字替代、动画/阴影差异仍待完整视觉对照，不能把当前控件实现称为原件逐字节一致。页面深灰右栏/灰按钮、五列表格、原均值行、打印入口及QtRPT工具栏保持。未标注原图见[统计界面](../03_photos/20261023_sn100_report_statistics_01.png)、[打印预览](../03_photos/20261023_sn100_report_preview_01.png)、[报告值图](../03_photos/20261023_sn100_report_pdf_values_01.png)。原报告XML逐字节一致；原字体缺失时，仅在DOM中同字号/位置替换Noto；实际输出每个Unicode码点检查字形，缺字明确失败，XML原件未修改。

[统计标注](../03_photos/20261023_sn100_report_statistics_01_marked_01.png)与[预览标注](../03_photos/20261023_sn100_report_preview_01_marked_01.png)另存，配套同名SVG内嵌原图且可编辑；标记不进入产品页面。原业务画布1751×968、预览原图2549×1872，截图像素保留，本轮未单独采集显示服务器DPI，不把该缺项或WSLg画面当RK实屏证据。

独立pypdf核真实PDF头/页数/595×420pt纸型/字段/数值；QtPDF+Noto的ToUnicode会将部分同形汉字映为康熙部首，文本断言采用NFKC及空白规范化，原提取文本同时保留，未因抽取问题改模板。根任务用Poppler120dpi逐页渲染查看预览1页、7行1页、64行5页全部页面：无可见缺字、裁切或重叠。日期按原字段自然折为两行，日期和时分秒完整，与下一备注仍分隔，未改几何/字号。对照及允许差异见[visual_comparison](../04_references/sn100_host/offline_report_20261023/visual_comparison.json)，源文件原样不等于原发行版运行视觉验收。

**PDF文本复制/检索的修复前限制（后续修复见[本轮复测](#o04-pdf-text)）。** 后续严格Windows/WSL对照发现，Linux/Noto中的“页”提取为U+2EDA“⻚”，NFKC也不折叠该字符；例如64行样本姓名“匿名分页”及页脚全文精确Unicode对照为false。视觉为正确字形，输入快照、数值与页数正确；保留原文和严格失败结果，不以自定义映射掩盖，不宣称全部中文可复制/检索等价。该项当时在模块卡单列待完善。Windows系统黑体/宋体7行和64行PDF的头字段/逐行数值/单位/页脚核对通过，根任务另将其全部6页渲染目检正常，字体差异单列，不把移植版Windows当原发行版。

Windows默认离屏QPA不自动发现系统字体，先明确注册现有`FontsLocation`黑体/宋体到本进程、支持同字体本地名别名，不安装或修改系统字体。随后默认导出仍超时，实际只读GDB栈定位`WINSPOOL!OpenPrinter2W`，根因是本机Qt5.14源码`QPrinterPrivate::init`先创建Native引擎，再切PDF会先初始化系统打印驱动；未发打印任务、未改打印机配置。独立helper在QApplication/字体注册后用公开`QCoreApplication::setLibraryPaths(empty)`隔离后续打印插件，只用内建PDF引擎，7行1页/64行5页均实际退出0，无需字体环境变量；Qt日志显式进stderr，使QProcess能回收真实错误。隔离只限不需SQL/外部插件的独立helper，未在主GUI改全局插件路径；Windows主GUI预览运行仍待验。Windowsownership测试也仅在该方法RAII暂时隔离无关的打印设备发现并恢复，保留隔离前49秒通过/后续60秒超时证据，不把辅助测试隔离称为完整打印驱动验证。原始失败/栈、最终helper输出与逐字段对照见[Windows证据](../04_references/sn100_host/offline_report_20261023/windows/)。

最终Windows无窗口规则/所有权5个指定方法加init/cleanup共7 passed/0 failed、QtTest142ms、进程退出0。另以只供比较的Noto临时副本运行同一7/64行快照，Windows与WSL的compact NFKC提取全文两例相等，但两边均有上述“⻚”码点，对原输入的严格对照仍false，限制未被消除。Noto来源/许可/文件SHA保留元数据，不把Windows系统字体或临时字体副本随工程分发。独立GDB断点复测helper约2秒退出0，`OpenPrinterW/OpenPrinter2W`命中0且Qt打印插件未加载，印证该进程未再进入原生打印发现；不外推到主GUI或物理打印。

### 响应、资源与负载口径

WSL Ubuntu-24.04/qyq、x86_64、Qt5.15.13、GCC13.3、Release、Fusion/Noto、XCB/WSLg。仍保留其他聊天/用户窗口，其他模块编译可能并行，未隔离整机负载。操作时间从鼠标注入到信号/内容就绪，不含合成器像素呈现；10ms心跳为事件处理间隔，包含截图、独立SQL与pypdf采证开销。父进程与独立renderer分列，不把子进程资源遗漏后称全链低资源。

| 观察项 | 本轮实际值 |
| --- | --- |
| 首次按钮到有内容预览 | 38.852ms；随后20次再开为18.866–48.906ms，21次预览/关闭无崩溃 |
| 实际Save到PDF完成 | 136.721ms；64任务5页完成335.209ms，完成时长与按钮反馈分别登记 |
| 10ms心跳，181样本 | p95 37.036ms、p99 135.368ms、最大219.200ms；不代表硬实时或RK门槛放行 |
| QtTest父进程约2.914秒 | 单核CPU25.065%；RSS观察峰165524KiB、PSS144524KiB、最多7线程/23FD，包含测试/PNG/SQL开销。开始RSS108372、结束146808KiB，有缓存/分配增长，短测不能证明长期无泄漏 |
| 64行renderer PID1270 | 3个约100ms采样，RSS观察max41824KiB、PSS22307KiB、1线程/7FD；最大观察累计CPU21/100=0.21秒。短进程峰值可能漏采，null保留为缺测，不能记0或将该max当全生命周期峰值 |
| 正式入口5秒、首秒后空闲 | 52个100ms采样，退出0；RSS/HWM观察max108988KiB、PSS99679KiB、4线程/11FD；单核CPU按10ms ticks量化得到0%，仅该短空闲窗口，未测正式入口预览负载 |

原始资源/操作和SQL快照保留在[reports测量](../04_references/sn100_host/offline_report_20261023/reports/report_measurements.json)、[生命周期测量](../04_references/sn100_host/offline_report_20261023/lifecycle/error_lifecycle.json)、[正式入口采样](../04_references/sn100_host/offline_report_20261023/formal_app/measurement.json)及[汇总](../04_references/sn100_host/offline_report_20261023/measurement_summary.json)。RK预算、温度/降频、同板deadline和长稳继续待验，不用WSL短测推断目标板达标。

### 构建、归档、回退与交接

主程序和独立`sn100_report_renderer`分别在WSL x86_64、Windows Qt5.14.2/MinGW32、ARM64构建；ARM检查AArch64，两候选均成功后才发布，未部署或执行。ARM脚本首轮因WSL无rg漏moc、随后RCC缺`-name`导致同名符号链接失败，均未发布失败产物；改为已有grep和三个独立资源名后通过。编译日志、每个实际产物SHA/架构/大小/动态依赖、源码指纹及失败复测保留在[正式证据目录](../04_references/sn100_host/offline_report_20261023/)，安装输入仍在正式`07_backups`，未移动或清理原参考件。

最终8个实际产物及当前源码的[构建清单](../04_references/sn100_host/offline_report_20261023/logs/o04_2_build_manifest.json)SHA-256为`c1cb6e71b3c012338174fe7fd6c7c78326b5e188c9cf195b5648873709a058d0`；native动态库无not found，两个ARM ELF递归DT_NEEDED均`missing=[]`。Linux helper最终SHA仍为`59e2221f978eb17cd77bc11b0eea0461bf62c0a7df6eae71149edb538b1b4a74`，与最终04/05实际测试副本一致；后续仅Windows分支/测试辅助改动，未用不同Linux逻辑替代已验版本。完整证据与照片分别有SHA清单，正式归档和匿名夹具未被Git忽略。

运行仍在WSL的`06_programs/`中执行`bash sn100_host/history/wsl.sh run`；无需设备参数，新的匿名数据在原`temp/sn100_host/history/runtime/`。`build`构建成对可执行文件，`report-test-build`只编译、`report-test`执行新报告用例；测试需带pypdf的Python。ARM`history/build.sh`仍只收一个绝对主程序路径，helper发布在同目录，报告依赖恢复只读归档、不联网。维护路径和最小复现见[工程README O04-2](../06_programs/sn100_host/README.md#o04-2-均值cv与原模板报告)。

交接给O03/O06的`history_core.pri`仍仅Core/Gui/Widgets/SQL；`requestLeave`与原busy/dirty守卫保留。页面打印能力默认关闭，只有独立入口绑定可选的`reportPreviewRequested`和busy信号才启用；统一主壳的重报告能力接线不在本切片冒充已完成。2026-10-23 17:26释放GUI时段，后续仅离屏校验/归档，不操作他人预览。回退可继续只include轻量core、禁用打印接线，匿名SQLite/旧O04-1证据保持，不迁移生产数据，不自动提交/推送。

实体打印机、纸张/驱动、RK实屏/插件、仪器、原Windows发行版完整行为/活动主题、长稳和同板控制继续待验，完整O04不关闭。

<a id="o03-3-offline"></a>
## 2026-10-23：O03-3 医院、账户权限与历史查询统一入口

本轮按用户要求串行整合已交付的O01/O02/O04切片，读取根/局部规则、当前计划/工程说明、两个项目skill及验收参考；用户明确允许向O03和O04聊天协调文件/GUI时段。O03交接账户主壳后本窗口接管`shell/`接线，O04保留历史业务/报表所有权并冻结core，GUI验证依次进行。保留所有旧入口、匿名库及既有未提交工作；参考原件只读，没有连接、上传或修改RK/仪器，没有系统配置覆盖、额外环境备份或自动提交/推送。

### 业务和维护入口

| 入口 | 结果与边界 |
| --- | --- |
| `shell/main.cpp --accounts --history` | 一套主壳接医院、账户、历史；三个store在各自worker创建/使用/关闭SQLite，入口负责close→quit/wait。原无参数O03-1和`--accounts`O03-2仍可运行 |
| 主菜单“结果报告”(2) | `history_shell.cpp::attachHistoryPage()`复用一个HistoryPage，日期/样本/条码查询、明细患者信息保存和GBK CSV。页面归主壳，store/线程归入口；切页不重复初始化 |
| “用户设置”(6) | 账户(0)/医院(1)保留原容器、坐标、菜单与规则。唯一身份来自AccountStore；Guest和五类匿名角色均核对实际可见姓名/角色集合。原报告查询/患者保存/CSV没有角色门禁，保留该行为，未从角色大小发明全局ACL |
| 离页/注销/退出 | 检查在途操作，医院/历史草稿默认保留；确认放弃恢复最后成功快照，放弃本身不写库。标题栏X/Esc也走明细守卫；接受离页才取消查询日历/导出范围/文件选择器。账户提交触发的自动注销仍立即撤回身份，不被草稿阻挡 |
| 就绪与失败 | 首轮历史query成功后才输出HISTORY_INITIAL_QUERY_READY，再汇总SHELL_READY。发现并修复initialize信号先于自动query导致的提前报就绪。初始化失败直接exit3并关闭三个worker，不等待not-ready页面的离页许可 |
| 依赖/资源 | `shell.pri`只含轻量`history_core.pri`；历史fixture由shell qrc提供，避免两个resources.qrc同名生成冲突。不链接重报表、QtPrintSupport/QtNetwork或生产登录、设备TCP/LIS/校时链路 |
| 数据/构建 | 新产物和运行数据在`temp/sn100_host/integration/`；`hospital/`、`account/`、`history/`相互隔离，不移动/重置旧库。账户认证DB/临时secret只留temp，不进入长期证据；历史和医院匿名数据仅明确列查询/完整性检查 |

实际运行入口和复测方法已同步[工程README](../06_programs/sn100_host/README.md#o03-3-统一脱机主界面医院账户与历史查询)，来源/有意差异在`shell/origins.json`，当前卡见[O03-3](../01_docs/plans/sn100_host_migration_plan.md#o03-3-card)。

### 原版界面保持及必要差异

沿用O03/O02/O04已有的原`.ui`、QSS、动态样式、图标和来源审查；本轮没有重画表格、改菜单顺序/主题或新增实验横幅。原主画布1920×1080、账户1471×881、历史1751×968保持。历史画布略大于主内容栈，只增加无边框内部滚动；1600窗口沿已有外部滚动宿主访问原画布。1600历史图刻意滚到右侧以取证导出按钮，左导航通过底部横向滚动返回。窗口标题、现有顶栏状态/tooltip明确脱机和匿名模拟身份，设备始终未知。

六张移植版原图和两张独立SVG标注已保留；这不是原Windows运行像素对照或RK实屏验收：

- [1600历史原图](../03_photos/20261023_sn100_integrated_1600_history_01.png)、[独立标注](../03_photos/20261023_sn100_integrated_1600_history_marked_01.svg)。
- [1920账户原图](../03_photos/20261023_sn100_integrated_1920_account_01.png)、[独立标注](../03_photos/20261023_sn100_integrated_1920_account_marked_01.svg)。
- 其余医院/账户/历史图及SHA、尺寸、6/6逐字节一致性见公开`screenshot_manifest.json`；1600×900及1920×1080逐控件核对完整可见、实际widgetAt命中、Tab、详情Esc关闭和返回医院编辑框强焦点恢复。

### 固定输入与功能累计回归

最终三页写/读/失败/视口使用新`experiment_04`，读回是不同进程；匿名夹具仍是6份报告，指定2026-10-23筛出3份，样本3及`ANON,"003`筛出1份。仅将报告id3年龄从0保存42再锁库失败/解锁重试为43，其余患者字段逐项相同；含中文、引号、逗号和换行。医院名保存`三页集成匿名医院 O'Brien`；医院和患者草稿取消/放弃均对照数据库字节不变。全部/选中CSV分别4/2条结果记录（不含表头），GBK解码/引号/换行/重复导出拒绝均核对。三个匿名库integrity_check均ok，认证列未导出。

| 最终测试 | 实际结果 |
| --- | --- |
| 历史离页专项 | 7 passed / 0 failed，覆盖初始化/忙态、草稿保留放弃、X/Esc和临时弹窗收尾 |
| 三页write/read/init-failure/viewport | 各独立进程3 passed / 0 failed；包含Guest/五角色实际集合、100次三页往返、两类库锁等待拒绝导航/注销/close、重试与另进程读回；未标记历史目录哨兵不变，exit3和三worker正常收尾 |
| 原O03-1累计 | write/read/applicationExit三个新进程均3 passed / 0 failed |
| 原O03-2累计 | write/read/applicationExit/navigation四个新进程均3 passed / 0 failed；该次为后续节奏修正前测试版本，修正后的因果诊断及原用例复测由O03聊天另节登记，不抹掉原峰值 |
| 无GUI累计 | quality 19、coagulation 19、protocol 63 passed，均0 failed/skipped；新隔离构建，源前后SHA相同，不运行通信/设备/GUI |

表中Totals包含每进程init/cleanup，不把它们说成同等数量独立业务用例。成功日志、公开输入/输出、两个匿名CSV、三库完整性和图片清单归档在[统一入口证据目录](../04_references/sn100_host/integrated_shell_20261023/)。原Windows行为、生产登录/完整权限策略仍待验。

### 失败、纠正和证据边界

失败日志单独保存，不混入通过：测试辅助器误用QWidget::isAncestorOf识别独立QMessageBox造成90秒超时，改QObject父链且10秒明确失败/安全取消；旧账户测试强制o03_2数据根，改用该根的新integration_regression目录；真实命中检查补窗口exposed/active和原生映射等待，先交付排队焦点恢复再按目标滚动；最终医院焦点沿旧shell_test已记录的WSLg前提先激活顶层，仍保留名称编辑框强断言。一次追加焦点记录lambda的context寿命错误引发bad_alloc，改为先于记录容器析构的局部QObject，失败原件保留。

第一轮资源驱动标签含空格被采样器拒绝，驱动40秒等待不足又中止了60秒测试；已改合法标签/75秒等待。随后发生X11连接error1、Qt测试exit1；该轮没有measurement_result，外部采样还因只认PID/start_ticks误把僵尸留存期计入55秒，整轮明确无效。采样器补Z/X状态终止和不完整返回非零；不把外部采样间隔当Qt心跳。失败后WSLg现存weston日志从17:43:21的新启动段开始，未保留失败前日志，不能据此独断根因；本窗口没有重启WSL、改变显示或系统环境。后续复测单独登记，不覆写失败。

### 构建与测量

Windows Qt5.14.2/MinGW32、WSL Qt5.15.13/x86_64 Release、ARM64 Qt5.15.13交叉构建通过；正式应用SHA：WSL `5fb7ee2f7c7f2bdf2861e986d3c6de2db4d6bcc07ce0a1fb36e2e7a2bd675b33`，Windows `deedaaad460d989589ff07c7b18e7801d0ff74131b72cfc34f039aeaba2af720`，ARM64 `c22f8830ab652eac7455a70426d1b55db18078fe26848b690cf026e4fe263bf1`。WSL ldd无缺库，ARM readelf为AArch64，直接Qt依赖Widgets/Gui/Sql/Core；ARM只构建不部署。实际编译日志、输入清单与产物元数据分别保留，时间点SHA不单独证明构建或硬件通过。

最终有效复测在WSL Ubuntu-24.04/qyq、Qt5.15.13/x86_64、Release、XCB/WSLg进行，保持原布局与动画，不改优先级/调频。混合输入限定ASCII按键，避免与现有WSLg剪贴板故障混淆；中文/复杂文本仍依据前述独立功能结果，后续O03中文clipboard复测失败另列环境限制，不推定此问题已修好。此轮与失败轮的输入方式/显示会话不同，不据此独断X11断开的原因。

| 观察项 | 实际结果与口径 |
| --- | --- |
| 混合操作 | 60.041秒，332轮，996导航、332查询、34保存，失败0；不截图/PNG/外部SQL采证 |
| 导航信号反馈 | p95 73.931ms、p99 86.995ms、max150.017ms；起点为UI注入，终点为matching navigationChanged，不是像素完成 |
| 查询/提交完成 | query p95 18.437/max46.193ms；save p95 19.221/max20.586ms |
| 10ms Qt心跳 | 3239样本，间隔p95 33.201/p99 37.183/max76.243ms；事件循环代理指标，不是硬实时或真实输入绘制放行 |
| 混合QtTest进程资源 | 外部100ms采样55.000秒/551样本，PSS约1秒；单逻辑核口径CPU93.02%，RSS最大146936KiB、PSS134864KiB、10线程/20FD。包含测试驱动/spy等开销，不冒称正式产品混合峰值 |
| 正式程序静置 | 独立PID验证后的12秒静置，外部完整10.000秒/101样本；CPU17.20%单核、RSS122276KiB、观测VmHWM130044KiB、PSS103622KiB、9线程/18FD。CPU不能称低占用，预算/原动画影响留待明确负载后评估 |
| 数据/退出 | 混合前后既有DB/INI/两个CSV文件字节数增长0；最后三库integrity_check均ok。正式入口history首query仅报一次且先于SHELL_READY，WM_DELETE_WINDOW正常关闭，三个STORE_CLOSED/THREADS_STOPPED true/exit0 |

测量与正式退出各3 passed/0 failed（含init/cleanup）。外部采样完整有效标记为true，退出/僵尸样本不会补零；其采样间隔不等于Qt心跳。实际原始JSON、disk_sizes、正式输出和采样器自检均归档。没有把候选100/500ms或未确认RK预算作为放行门槛；WSLg/X11/剪贴板异常、正式产品持续混合负载、像素响应、RK/温度/同板控制与长稳仍未关闭。O03对旧同步测试饥饿的独立因果诊断见[旧峰值定位](#o03-2-latency)，不改写此前失败/峰值。

### 交付与未关闭项

已用新的`user_preview_01`匿名数据根打开统一WSL主界面，显式offlineEngineer，启动日志HISTORY_INITIAL_QUERY_READY→SHELL_READY，临时播种文件在就绪后删除。通过computer-use正常激活并实际点击账户、医院和结果报告三个入口，最后停在“结果报告”供用户操作；[原始交付截图](../03_photos/20261023_sn100_integrated_user_handoff_01.jpg)由Windows窗口捕获，保留原JPEG（1331×785逻辑像素），与Qt内部1920×1080截图的缩放/边框口径不同，不能混用作原版视觉对照。该窗口保持运行，不是退出测试对象；没有结束他人窗口。检查当时没有此前旧预览可关闭，未强杀进程。

仅关闭有证据的离线整合切片；原Windows完整运行视觉/行为、正式密码登录/通用权限策略、真实SN100曲线及算法结果、RK显示/输入/仪器、资源预算、温度/降频、同板控制和长期稳定性保持未验。不把短测/构建或未知候选阈值作为完整系统放行。回退范围为新增主壳历史接线及相应构建参数，可继续运行保留的独立O01/O02/历史入口；既有数据未迁移，无需系统环境恢复。

<a id="progress-review-20261023"></a>
## 2026-10-23：进度盘点、计划整理与下一轮上机边界

本轮用户要求核对“已到哪一步、下一步做什么、能否上机”，据现有计划、记录、公开证据及构建/运行入口进行只读盘点并整理Markdown。保留已有未提交修改、历史失败/待验及正在运行的任务；没有重新编译、部署、连接或操作RK/仪器，不产生新的功能/硬件通过结论。

### 当前到达的阶段

- **RK基础已具备。** RT内核启动与短测、WSL交叉部署、Qt最小显示/USB键鼠及GDB链路有历史证据。HDMI/USB的原始运行证据本就位于RT内核下，不能写成RT后从未验证；但当前SN100业务实屏及最终跨平台demo的HDMI对照仍待补。[Qt环境记录](20261022_qt_environment.md) · [RT记录](20261022_preempt_rt.md)
- **SN100已从独立切片进入主壳整合。** O03-3接入医院、账户及历史查询/患者保存/CSV；WSL三页、两尺寸、数据重开/错误恢复与Windows32/ARM64构建通过。[原结果](#o03-3-offline)
- **其它离线成果仍有清楚边界。** O04-2模板/PDF仅在独立历史入口，中文复制/检索仍有缺陷；O05仅回放/本机TCP，尚无外部仪器Transport；O06是统计和自主凝血点研究原型，真实曲线、认可误差与完整AL4A业务未验。对应模块的原始输入/输出和限制分别保留在上文各节。
- **长稳仍在进行。** 2026-10-23北京时间18:07只读查看既有`long_run_01/status.json`：`state=running`、`active_stage=continuous_2h`、`completed_stages=[]`，阶段经过约784秒；8h恢复阶段尚未开始。这只是过程快照，不算小时级通过。正式索引仍为`hours_passed_at_publication=false`；结束/失败通知沿用原任务，不重启、不另建监控。[原长稳合同与记录](#o05-3-offline) · [正式证据索引](../04_references/sn100_host/protocol_stability_20261023/index.json)

### 上机判断与下一步

**可以安排RK离线实屏实验。** 无需等全部离线业务、完整算法或RT最终长测完成；设备到场后先核对既有环境，再从O01单页逐模块到O03-3三页，检查真实显示/输入、匿名数据保存重开、失败恢复/退出及资源。该判断表示具备开始实验的基础，不表示当前SN100已在RK运行通过；设备是否到场本轮未确认。

上板入口审查发现：根`build.sh`/`rk.sh sn100_host all`生成医院/账户独立页，默认医院；统一壳另用`shell/build.sh`。现有`shell/wsl.sh`固定x86_64/XCB，尚缺统一壳的RK部署/EGLFS启动/退出回收封装。需要显式启用账户/历史及新匿名数据根，首次匿名账户走临时初始化；不能拿独立页当三页产物，或搬含认证字段的WSL账户库。上述为源码入口静态核查，尚未实现/验证新封装。

**仪器联机须再满足V01条件。** 先核对SN100型号/主控固件、连接角色/地址端口、协议/编码、只读查询和单/多客户端限制，再实现/预验证显式外部Transport。人工连接/查询并隔离自动校时/首次收包联动，回收真实TX/RX与断线/重连结果；回环不能当仪器响应。V01通过后逐模块补实物，真实算法/校准/质控按各自依赖推进。

可执行步骤、前置条件、输入/输出和停止条件统一落在[RK离线首验卡](../01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)；总体排序见[开发计划](../01_docs/plans/development_plan.md)。原Windows同状态对照、WSLg中文/PDF缺陷、O05结果回收与O06真实曲线准备可按条件继续，不重复建立计划或主纪要。

### 本次整理范围

收敛根README的重复旧进度，更新总体/详细计划的当前状态和上机顺序；工程README说明独立页/统一壳入口差异；Qt环境记录把断点练习降为学习选项，并链接现有SN100交付；工程说明修正“O01首版仍待纠正”等过时当前态。历史参数、测量、失败、图片/证据、待实物勾选均保留。仅修改文档，不改源码/构建、系统环境或既有运行数据，不自动提交/推送。

<a id="rk-prepare-20261023"></a>
## 2026-10-23：统一界面RK部署入口、ARM64与上机前准备

**本轮结果：软件准备完成，实板与仪器验收继续待执行。** 用户要求补统一界面的RK部署/图形启动入口，完成ARM64交叉编译和依赖检查；本轮没有SSH/SCP连接RK、上传、启动图形服务、安装板端包或连接仪器。使用既有Windows/WSL同份源码，保留原主壳颜色/布局和已有未提交成果，不改参考原件，不提交/推送。

[正式证据索引](../04_references/sn100_host/rk_prepare_20261023/index.json) · [证据SHA清单](../04_references/sn100_host/rk_prepare_20261023/SHA256SUMS) · [构建/上机使用方法](../06_programs/sn100_host/README.md#rk-prepare) · [当前首验卡](../01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)

### 入口、行为与修改范围

- **电脑入口：**`shell/rk.sh prepare`先冻结输入，再构建新ARM64、检查主程序/SQLite闭包与符号版本、生成带完整文件清单的发布包；只在成功后登记最新包。`deploy`会重新准备，上传SHA及目标主程序/SQLite实际ldd均核对后才切换版本，拒绝覆盖运行中的程序；部署和启动分开。原根`rk.sh sn100_host all`仍保持独立页含义。
- **目标入口：**`rk_board.sh check/start/status/stop/collect`负责实际Qt/插件/字体/显示输入预检、瞬态VT分配、普通用户/EGLFS启动、监督与白名单采集。每次执行目标helper前先核发布清单；新/既有release都检查清单摘要、完整文件集合/无符号链接。旧release及前一指针保留，不迁移或重置数据，不装常驻服务/自启。
- **正常关闭：**新增`shutdown_request.h/.cpp`并接入main/qmake/ARM构建。Linux信号处理器只向nonblock/CLOEXEC管道写请求，GUI调用原`viewport.close()`，经过原草稿/忙态守卫；不直接quit或绕过确认。Windows分支不安装POSIX处理。监督核对boot ID/PID/start_ticks/UID/exe，真实wait取得退出码，原子发布结果；停止/软截止拒绝时保留进程，不发SIGKILL。
- **运行数据与观测：**默认Guest，模拟身份显式选择。新匿名根含医院/账户/历史独立子目录，首次secret只在本次700运行目录内临时600文件，就绪或退出即删，部分初始化不自动重播种。原始资源TSV记录CPU ticks、RSS/PSS、线程/FD、温度/频率，缺项为unavailable；不做实时优先级/调频改动。日志收集固定白名单，先复制私有快照再归档，排除数据库、secret与完整环境变量，也未开启原始键盘追踪。
- **维护入口：**`check_arm64.py`只在电脑解析readelf，不执行ARM ELF、不用主机ldd替代目标、不回退主机库；`package_arm64.py`保存源码/资源指纹及包清单。`prepare_runtime.sh`只重建隔离检查sysroot和私有SQLite输入，未部署loader/系统图形库。

### 构建、依赖与冻结包

| 项目 | 实际结果 |
| --- | --- |
| 统一壳ARM64 Release | ELF64小端AArch64，解释器`/lib/ld-linux-aarch64.so.1`，18,927,456字节；SHA256 `333bbe6a2b765b76c185d8b8f98524333f319b1a3343009e36c449a5f85ca5bb` |
| O01独立基线ARM64 | 重新构建及相同隔离sysroot检查通过；SHA256 `a9ec662cd9f4d27c3f3b82c4b4a9fba94086e070395c25aac607c157a1e6b805`，仍非统一三页产物 |
| Windows32 Release | qmake/make均0，x86 PE32，854,528字节；SHA256 `57559c63f7b7c0f4321881ca78fafe3a1d9a8d71f117abb9c9dee4e0b2e738b3`。只构建当前移植工程，不是原Windows发行版运行对照 |
| 主程序/SQLite离线闭包 | 38个ELF、0错误；递归DT_NEEDED、SONAME、解释器及各provider定义的版本集合核对通过，含GLIBC/GLIBCXX/CXXABI、Qt private及其它实际需求。对最终发布路径另复核，输入/报告SHA再次通过 |
| Release ID | `e0420b6be0f6c981000b07f6e63dda4a641af8e9df9ff27d97fb51a44f0c3a09` |
| 包SHA256 | `99e44c5310a3ab484d027de9bc772e9d0cb2e99465cc272940ba43fb109acf05`；包在`temp/sn100_host/rk_prepare/packages/`，完整路径见证据索引 |
| 真实tar核验 | 包内8个普通文件和3个目录；主程序/三项私有运行库均AArch64，所有payload/内部清单/release ID/外部包SHA一致，独立只读复核通过 |
| 适用边界 | 状态为`offline_closure_passed_board_runtime_pending`，`board_runtime_verified=false`。EGLFS/KMS/GPU/输入/图像支持与Noto字体未完整归档，只保留目标预检与现场验收要求，不称全部板端依赖已验证 |

[最终依赖报告](../04_references/sn100_host/rk_prepare_20261023/dependencies/dependency_report.json)、[实际包核验](../04_references/sn100_host/rk_prepare_20261023/windows32/package_audit.json)、[发布清单与源码指纹](../04_references/sn100_host/rk_prepare_20261023/release/release.json)记录完整输入与结果。

原检查输入确有三类缺口：历史运行库归档缺loader及部分Qt SONAME别名，新增qsqlite还缺SQLite3 provider。已将2026-10-22成功远程调试保留的板端loader按原SHA最小归档（仅检查用）；SQLite3取Ubuntu noble官方包并核索引SHA；Qt别名只按实际ELF SONAME在本机sysroot重建。正式来源、版本、恢复范围和SHA见[sn100_rk_runtime/source.json](../07_backups/qt_environment/sn100_rk_runtime/source.json)，新增.gz/.deb均匹配Git LFS。部署包仅带QtSql、qsqlite与SQLite3，不带loader、系统QtCore/Gui/Widgets或GPU驱动，不覆盖系统Qt。

### 离线实际回归、失败与复测

| 验证 | 实际结果与边界 |
| --- | --- |
| 关闭桥QtTest | 5 passed/0 failed（含init/cleanup，3个测试函数）；脏数据/忙态拒绝、嵌套重复信号合并、10000请求满管道不阻塞、20次安装销毁/原handler与FD属性恢复均核对 |
| 实际统一壳offscreen | SIGTERM→SHUTDOWN_ACCEPTED→三个STORE_CLOSED→THREADS_STOPPED true→EXIT code=0；是电脑本机运行，不是RK EGLFS/显示验收 |
| 依赖检查器 | 12/12通过；缺库/loader/传递SQLite、错误架构、provider版本集合缺项、符号链接边界/循环闭包及真实x86拒绝等负向检查可复现 |
| 发布/监督回归 | 冻结代码完整17/17、退出0；真实/proc身份、防PID误停、真实0/23/29/未ready即0、secret清理、软截止/15秒守卫拒绝不KILL、资源原值、数据保护与采集白名单均核对 |
| 本地mock发布 | A首次→B切换并保留previous=A；同ID自洽篡改、额外文件/符号链接污染拒绝，current=B保持；失败后上传/暂存清理。SSH/SCP均为本机mock，不冒充板端发布 |
| 构建/冻结防护 | prepare/deploy遇模拟build退出37时零ssh/scp/curl/wget且旧产物保持。前两轮真实ARM构建/检查已通过，但因封装审查后脚本变化被源码指纹拒绝打包；保留日志，最终冻结重跑prepare退出0，未发布旧包 |
| 活跃采集 | 1MiB日志持续写入时私有快照打包成功；不带DB/secret/额外文件，不把采集期间才完成的结果补成快照已完成 |

[关闭桥原始日志](../04_references/sn100_host/rk_prepare_20261023/shutdown/test_result.txt)、[实际主壳退出](../04_references/sn100_host/rk_prepare_20261023/shutdown/application_signal.log)、[17项结果](../04_references/sn100_host/rk_prepare_20261023/tests/workflow_results.tsv)、[12项结果](../04_references/sn100_host/rk_prepare_20261023/tests/dependency_tests.log)和构建/失败日志共47份非敏感证据已归档。本机最多2个统一构建任务加1个O01基线编译与既有O05长稳重叠；未停止/重启该任务，此窗口不称空载，也不从本轮准备推导其长稳结论。

### 下一步与仍未验收的范围

首轮匿名输入、预期字段/CSV、部署/预检/启动/退出与取证步骤已写入[RK离线首验卡](../01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)。设备到场后先B0核当前RT/Qt/插件/字体/权限，再O01、账户/历史逐模块到三页累计回归，保存实际屏幕/输入/数据及资源。RK实际SSH上传SHA、ldd/插件发现、sudo/VT/DRM、Noto字形/中文IME、模态/滚动/焦点、重启及实板关闭仍未验；软件入口存在不等于这些条件已通过。

原Windows完整视觉/行为、生产认证、真实SN100协议/外部Transport、实际凝血曲线/认可误差、仪器动作、资源预算/温度降频/同板deadline、产品长稳与整机验收继续开放。没有业务删减、参考清理、系统环境覆盖或额外整机备份；新归档仅为本机恢复/检查所需最小依赖，旧运行数据与已有构建/证据保留。

## 2026-10-23：明确 HDMI 鼠标测试与后续触控小屏两个阶段

按用户要求，在[详细移植计划](../01_docs/plans/sn100_host_migration_plan.md)开头明确两阶段目标：第一阶段先在 RK3576 外接 HDMI 屏幕上使用 USB 鼠标测试操作，文字输入可配合键盘；第二阶段在 HDMI 版形成稳定基线、目标屏规格明确后，再适配可触控的小屏幕。同步细化计划 4.2 节的小屏显示、触摸和软键盘待办，并在根 README 补充路线入口。

本次仅修改文档并核对阶段表述与链接，保留已有未提交修改及各模块验收状态；小屏型号、接口、分辨率和触摸控制器仍待确认，两个阶段均未因此标记完成。

## 2026-10-23：简化计划说明与初学操作入口核查

按用户要求，用普通表述重写计划开头和已有成果，解释三页共用窗口、基础名单、单独报告程序及电脑模拟通信，保留 RK/仪器未验和 8 小时恢复测试失败的边界。核对 `shell/wsl.sh integrated-run --test-session offlineEngineer --viewport 1600x900` 为当前 WSL 图形入口，建议先用测试历史报告熟悉操作，再沿一次按钮操作阅读相关代码。本轮仅核查和改文档，没有启动、编译或部署程序。

当时静态核查将医院名称框误判为只读，并误称此前自动测试通过代码赋值，因而在计划与根README登记待修。这两项判断均已由下节的XML节点核查、测试代码审查和真实窗口操作纠正；保留此处作为误判及复核经过，不再把它作为产品缺陷依据。

<a id="hospital-manual-20261023"></a>
## 2026-10-23：医院名称手动输入核查、添加保存与重开演示

用户要求先处理医院名称不能手动输入，再打开软件并简单演示添加保存。本轮读取根/局部规则、当前计划/记录、工程说明、两个项目skill及验收参考，并由独立子代理只读复核。保留所有已有未提交源码与运行数据，参考原件只读。**实测没有复现医院名称无法输入，因此没有修改业务源码或宣称修复了只读属性。** 未连接/上传RK或仪器，未安装依赖、修改系统配置或自动提交/推送Git。

**误判原因与实际调用链：**原`.ui`将多个控件压在同一行，`readOnly=true`属于装饰分隔线`lineEdit_5`和`lineEdit_4`。逐XML节点核对`ldtHospName`没有只读属性，Qt默认可编辑，焦点策略为`Qt::ClickFocus`；原版参考和既有生成头文件也相同。`tests/offline_test.cpp::paste()`实际先检查可编辑，再鼠标点击、Ctrl+A/Ctrl+V，`writeExperiment()`从新空库输入医院名，不能称为`setText()`绕过。业务入口仍为`frmHospInfoSetting::on_btnSave_clicked()`读取医院名/行数据→非空校验→排队`HospitalStore::save()`→SQLite事务→INI兼容导出→重读界面；业务规则和原主题/几何位置均未改变。

**真实窗口输入与结果：**使用Windows computer-use的`sky`鼠标/文本/按键操作现有WSLg三页软件（Ubuntu-24.04/qyq、Qt5.15.13、XCB、原1920×1080逻辑画布）。沿“用户设置→医院信息设置”进入原先空名称/空表的匿名`integration/user_preview_01/hospital/`，直接输入`演示医院`；点“添加”，新行输入`检验科`，助记符自动为`JYK`，备注输入`操作演示`，点击“保存”。独立只读SQLite查询得到医院名`演示医院`和一行`id=1,column_one=检验科,column_two=JYK,column_three=操作演示,type=keshi`，`PRAGMA integrity_check=ok`。全过程未通过产品setter或直接写库代替界面输入。[保存后原图](../03_photos/20261023_sn100_hospital_manual_saved_01.png)。

点击标题栏X正常关闭，原运行日志实际返回三个store关闭、`THREADS_STOPPED true`、`EXIT code=0`。随后通过现有`shell/wsl.sh integrated-build`重新构建当前WSL Release，成功后重开同一匿名数据目录，无重复seed或数据库迁移；新产物SHA256为`8eaf9e912006eb7c4dc685895e4e42e78837aacce76d7625664fa66a3dd54a66`。再次进入医院页，医院名、科室、助记符、备注均仍在；另用实际`a`键和BackSpace验证`演示医院→演示医院a→演示医院`，保存恢复后的值，最终只读查询再次一致。软件保持打开并停在医院页，供用户继续操作。[重开后原图](../03_photos/20261023_sn100_hospital_manual_reopened_01.png)。本轮重开脚本位于`temp/sn100_host/hospital_manual_20261023/reopen.sh`，未覆盖旧启动日志。

**证据与边界：**[结构化结果/源码及图片SHA](../04_references/sn100_host/hospital_manual_20261023/result.json)、[WSL构建日志](../04_references/sn100_host/hospital_manual_20261023/build.log)、[首次正常退出](../04_references/sn100_host/hospital_manual_20261023/saved_session_exit.log)、[重开启动](../04_references/sn100_host/hospital_manual_20261023/reopened_session.log)和[归档校验](../04_references/sn100_host/hospital_manual_20261023/SHA256SUMS)已保存，仅读取医院库，不归档账户库/认证信息。操作后WSL单次约1秒静置样本：CPU26.989%（单核口径）、RSS128376KiB、峰值RSS136672KiB、PSS117783KiB、6线程/18FD；只是环境快照，不构成性能或稳定性放行。未改产品代码，未新增或全量重跑自动测试，也未重建ARM64；本次只确认WSLg自动化文本填入/普通字母键/保存/重开及当前WSL构建。Unicode文本输入和普通字母按键成功不代表中文IME组字、原Windows完整运行等价、RK实屏/键鼠、仪器、长稳/同板控制通过，这些仍待分别验收。同步纠正计划及两个README的缺陷状态，并加入简单操作说明。

<a id="hospital-ime-20261023"></a>
## 2026-10-23：用户实际键盘中文输入失败与WSL输入法诊断

用户随后说明：自己在医院名称框使用键盘无法输入中文，询问是否Windows原因。上一轮自动化Unicode文本填入和普通字母按键并未覆盖拼音组字/候选选字，面向用户说“正常手动输入中文”不够准确；本轮纠正交付说明，保存/重开证据仍有效，中文IME仍未通过。

只读检查运行中的WSL `sn100_shell`（PID474）：`QT_QPA_PLATFORM=xcb`，`QT_IM_MODULE/XMODIFIERS/GTK_IM_MODULE`均未设置，`LANG=C.UTF-8`，会话DBus地址存在；进程只加载`libcomposeplatforminputcontextplugin.so`。系统Qt目录有compose及IBus桥接插件，但没有发现Fcitx/IBus服务进程，`fcitx5/fcitx/ibus-daemon/im-config`命令均不存在。由此定位到当前Ubuntu缺少可用的中文输入法链路；现有事实不能归因为Windows键盘故障、中文字体缺失或医院字段拒绝中文。

Microsoft WSLg官方[IME支持议题](https://github.com/microsoft/wslg/issues/9)说明Linux输入法可用但需配置/启动，Windows IME直接传递到Linux窗口仍有支持缺口；Fcitx官方[应用与输入法通信说明](https://fcitx-im.org/wiki/How_does_an_application_talk_to_Fcitx)说明Qt需输入模块与输入法服务衔接。软件虽显示在Windows桌面，实际运行于Ubuntu；需在该环境补齐中文拼音输入法、Qt桥接及启动环境，再用用户实际键盘完成候选选字、保存和重开核对。本轮仅诊断和说明，没有安装软件、改环境变量/用户设置、重启WSL或操作RK/仪器；待办已回到计划当前第一项，两个README同步明确“自动化文本填入通过”与“实际拼音输入待修”的区别。

<a id="o04-pdf-text"></a>
## 2026-10-23：O04 PDF 中文复制/检索编码映射修复

本轮按用户要求继续修复中文映射，读取两个项目skill、PDF skill、模块卡与原失败证据；仅修改独立PDF输出链和所需QtRPT通知接口。保留其他窗口未提交成果与现有预览，没有打开可见GUI、安装依赖、修改参考原件/系统配置或连接RK、仪器、打印机；无不可恢复数据覆盖，不额外备份，不提交/推送。正式证据在[本轮目录](../04_references/sn100_host/pdf_text_20261023/summary.json)，沿用同一事项记录。

### 根因、维护入口与发布规则

本机只读Qt5源码`qfontsubset.cpp::getReverseMap()`按BMP顺序反查glyph，多个Unicode共用字形时取较小码点；Noto的“页”U+9875/“⻚”U+2EDA、“生/⽣、人/⼈、血/⾎、目/⽬、日/⽇”等因此混淆，非BMP也不能靠该反查恢复。固定替换或NFKC会误改实际输入的部首；只加ActualText又不足以兼容本次两种提取器，故直接修正字体编码映射。

调用链为原按钮/控制器→不可变JSON快照→`report_renderer.cpp`→原QtRPT绘制→`report_pdf_mapping.cpp::rewriteHistoryPdfTextMapping()`→新文件发布。QtRPT仅增加最终label文字绘制前后信号；helper用纸外微小矩形界定每段绘制，不改画笔/字体/透明度/变换。mapper严格核对classic xref、页/字体、标记顺序和覆盖，用完整标签的`QTextLayout`上下文验证来源glyph；为`原CID＋精确来源文字簇`分配新CID，更新ToUnicode、CIDToGIDMap、宽度W及CIDSet，仍引用原subset glyph，最后删除标记并保持Flate压缩。原字体程序、坐标与纸型保留；未加隐藏正确文字绕过错误映射。

该模块仅处理**本进程自产的Qt5单版本PDF**，不是外部PDF导入器。输入/最终输出仍≤32MiB、快照≤1MiB、≤384行、一项在途、30秒超时；额外限制4096绘制段、单段4096 UTF-16单元、单glyph来源簇≤8单元。无法唯一对应、RTL或复杂多glyph语义簇明确失败，原数据不变，不发布目标；已有文件不覆盖。`qUncompress`的64KiB前缀只是初始分配提示，可能自增长，32MiB解压检查在返回后执行，不宣称对任意恶意PDF有硬内存限额。它只在隔离helper中运行，主壳轻量core不链接此文件。

### 实际验证、失败与纠正

- WSL Qt5.15.13严格文本QtTest **8 passed/0 failed**：10行1页、64行5页，八组汉字与部首同时出现、𠀋(U+2000B)、ASCII/单双弯引号、Office/ffi/fi/ﬃ/ﬁ、e+U+0301与é均输出成功。pypdf6.10.0和pdfplumber0.11.9独立提取，逐页重复字段/列名/页脚、整行顺序/次数、全部数值/单位/纸型核对；仅忽略排版空白，不正规化或换字。Windows Qt5.14.2同Noto六份报告及系统黑体/宋体原1/5页共八份PDF也全部通过。原WSL错误PDF仍作阴性、旧Windows正确PDF作阳性，原始提取文字完整保留。
- mapper QtTest **16 passed/0 failed/388ms**：原真实标记PDF与独立重压缩/重建xref正例；坏/截断PDF、越界/溢出/全free xref、循环Length、标记缺失/重复/越界、来源段缺失/重复、错误源汉字及既有文件保护共13个负例。失败均有原因、无新输出，输入字节及既有sentinel保持。正式匿名[夹具和来源文字](../04_references/sn100_host/pdf_text_20261023/fixture/raw_offpage_values.pdf.spans.json)可重放；上述QtTest totals均含初始化/清理。
- 原7行及64行同源报告，Linux与Windows分别用同一Poppler120dpi渲染，**12页全部零像素差异**。Linux六页嵌入字体字节、逐glyph GID和全部文字定位操作也完全相同（[字形/坐标证据](../04_references/sn100_host/pdf_text_20261023/metadata/rows64_glyphs.json)）。最终16份报告共32页已渲染并逐页联系图检查，无新增方块、裁切或重叠；日期仍按原字段自然换两行。原模板SHA仍为`673788540ab34ef214e513d6241e983586b98d0256166a6c01439aaee954efc2`，四份UI、history_core/page/store/statistics指纹本轮均不变；这不替代原发行版活动主题或RK视觉验收。
- 适用的原报告headless累计回归 **8 passed/0 failed/30734ms**：统计金样本/边界/分组、快照契约、printer/painter所有权、启动失败、真实30秒超时、恢复、关闭后完成及数据库前后不变。首次误以offscreen运行整组旧用例，两个明确要求WSLg/XCB的GUI用例按设计拒绝；保留8通过/2失败日志，随后只执行适用项。未修改测试门槛，也未把它们算成本轮GUI通过；既有WSLg证据继续保留，本轮不重开用户窗口。

本轮保留三项纠正过程：初版CMap尾随空白导致拒绝；完整上下文中的Noto数字alternate glyph不能用逐scalar独立shape，改为整段上下文并检查SplitLigature；透明标记触发Qt5后续线条的stroke emulation，导致2643像素差，改为不改任何state的纸外标记后归零。另pdfplumber默认展开真实ﬃ/ﬁ，验证器显式设`expand_ligatures=False`、`unicode_norm=None`，同一PDF无需重导出即通过；此为提取器配置纠正，未放宽来源Unicode要求。相关失败、原文、CMap及最终复测都归档。

### 同负载资源、构建与边界

Ubuntu-24.04/qyq，x86_64 Qt5.15.13 Release/offscreen，同一匿名64行5页输入，各重复3次；每5ms采本次child的RSS/PSS/HWM/线程/FD，CPU用`getrusage`，启动至退出计时。其他窗口和编译未隔离，观察峰值不等于精确峰值；父进程/测试解释器未算入child。详细样本在[资源证据](../04_references/sn100_host/pdf_text_20261023/resources/new_measure_tuned/measurement.json)。

| 项目 | 修复前 | 最终修复后 |
| --- | --- | --- |
| 导出耗时 | 270.5–294.9ms | 363.1–473.1ms |
| child user+system CPU | 207.4–211.0ms | 270.2–371.9ms |
| RSS观察峰 | 41932–42068KiB | 80628–80772KiB |
| PSS观察峰 | 26397–26451KiB | 64769–64909KiB |
| 线程/FD | 1线程，6–7FD | 1线程，7FD |
| 五页PDF大小 | 25918B | 27231B（约+5.1%） |

映射有明确时间与内存增量，约79MiB的helper观察RSS不能称低资源通过。初版展开内容为125KiB，恢复Flate后为27KiB；解压初始提示从32MiB缩至64KiB后RSS未显著下降，未据此声称内存优化成功。当前仍保持进程隔离、一次一份、完成退出，RK内存/CPU预算、温度、长稳及同板deadline影响待实测，不用WSL短测放行。

WSL x86_64、Windows Qt5.14.2/MinGW32、WSL ARM64的主程序/helper六个产物均构建通过，native `ldd`无not found，ARM递归47个ELF SONAME闭包无缺失（不包含实际板端插件加载验收）。最终[构建/源码/来源清单](../04_references/sn100_host/pdf_text_20261023/metadata/build_manifest.json)记录每个产物SHA/架构/大小、34个未改第三方文件及两处QtRPT补丁。native helper SHA为`cc1c0d4562f6a56670a7884a2875ecede77407ae5ee83e7db4587375b8001d3b`，ARM helper为`b51526adbec114d5e231a4e0eede133a7296d1948e0cce99dfb93ec009a6a274`；均未部署设备。

最小复现仍从`06_programs/`运行`bash sn100_host/history/wsl.sh build`或`run`，main/helper成对构建。严格文本使用新的绝对证据目录，例如：

```bash
SN100_PDF_TEXT_TEST_DIR="$PWD/../temp/sn100_host/history/new_pdf_text_run" \
SN100_REPORT_PDF_PYTHON=/absolute/python-with-pypdf-and-pdfplumber \
bash sn100_host/history/wsl.sh pdf-text-test
```

独立mapper测试在新的`temp/sn100_host/history/<build>/`中用Qt5 qmake构建正式`06_programs/sn100_host/tests/history_pdf_mapping_test.pro`，运行时设置绝对`SN100_PDF_MAPPING_TEST_DIR`为新目录、`SN100_PDF_MAPPING_FIXTURE`为本轮正式`fixture/raw_offpage_values.pdf`（同目录trace须保留）；程序自己使用offscreen。依赖安装输入仍复用既有`07_backups`，本轮无新增生产依赖或环境安装。仅回退本轮helper映射及QtRPT通知即可恢复旧PDF行为，匿名库/核心页面/原参考不变；旧错误证据不删除。

逐页原图与[Linux文本样例联系图](../03_photos/20261023_sn100_pdf_text_native_exact_contact.png)、[Windows联系图](../03_photos/20261023_sn100_pdf_text_windows_exact_contact.png)均已归档。收尾指纹检查发现另一窗口在`history/wsl.sh`的run/ide分支接入`wsl_ime.sh`；保留其修改，build/pdf-text-test分支未变。新脚本`bash -n`退出0、wrapper文件存在，构建时/当前SHA分别登记，不把本轮PDF测试冒充其实际WSLg输入法验收。

本轮关闭的是独立helper中文编码映射这一离线缺陷。指定桌面阅读器实际Ctrl+C/Ctrl+F、统一入口报告整合、原Windows发行版完整行为/主题、Windows主GUI预览、RK实屏/插件/资源、仪器、实体打印/纸张/驱动、XLSX及长稳/同板控制均继续待验，完整O04不关闭。已向原协调聊天交接源码、资源增量与边界。

<a id="integration-plan-20261023"></a>
## 2026-10-23：确认完整统一上位机目标，先整合现有成果再补业务

用户在询问试剂/样本页面“尚未接入（脱机）”及原版实现后，明确希望最终上位机菜单、功能都齐全，并要求把下一步先整合及后续工作写进现有计划。本轮按项目模块/文件管理规范只读核对源码、更新文档，保留已有未提交修改及其他聊天新补的PDF映射修复结果。

**源码核查与术语边界：**

- 正式主程序的[shell/frmmain.cpp](../06_programs/sn100_host/shell/frmmain.cpp)对试剂/样本等尚未迁入的业务创建文字占位，历史页再替换结果报告占位；当前统一入口实际是医院、账户与历史查询/统计/CSV。模板/PDF有独立实现，试剂/样本并不是已有完整模块只差挂菜单。
- 原版[frmmain/frmmain.cpp](../06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC/frmmain/frmmain.cpp)统一创建八个业务页并连接菜单、通信发送和状态/结果回调。原版已按模块组织；移植后的交付也应让用户在同一主窗口完成业务，独立入口用于测试和排错。
- 原版[试剂保存](../06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC/frmreagent/maskdlgreagentsetting.cpp)含资料/瓶型、批号/容量/警戒量及新旧协议分支；[样本保存与任务](../06_programs/sn100_host_reference/Seerain450_PC/Seerain450_PC/frmsample/frmsamplematerial.cpp)含架/舱盖、试剂/缓冲液校验、任务对象更新和后续下发/结果链。因此“页面接入”包含真实业务与数据/状态协作，“仪器联机”另指实际控制器通信；两者分别实施和验收。

**本轮计划调整：**[详细计划](../01_docs/plans/sn100_host_migration_plan.md#o03-4-card)新增O03-4：先登记原版8个主菜单、5个用户设置子菜单的实现缺口，优先把已有原模板预览/PDF及辅助程序接入统一“结果报告”；沿用单一账户、医院配置和历史接口，纳入报告忙态/未保存、取消/失败恢复、全局退出、累计回归、资源测量和三目标构建/新冻结包。O03-3原结果和冻结包继续保留，新增功能不沿用旧包冒充已发布。PDF映射修复已在独立helper有离线证据，统一入口和指定阅读器仍需复测；实际拼音输入问题也保留，不用自动化填字代替。

整合后的[业务顺序](../01_docs/plans/sn100_host_migration_plan.md#integration-next-steps)为项目/单位与试剂基础配置→样本/任务→统一设备状态/通信→算法/校准/质控及结果链→其余原版功能与完整交付；各切片通过自身离线用例后及时并入统一程序。接口/数据依赖先确认，设备同步、任务草稿/下发/执行、模拟与真实状态分开，原版规则不因脱机而改成假成功。O05恢复失败只约束相关稳定性验收，O06研究原型须真实曲线/参数/认可误差后才能进入相应结果业务。

**与既有硬件计划的关系：**设备具备时可先按[RK首验卡](../01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)验O03-3冻结基线，新整合版回归、重冻后补验新增功能；无需等待全部业务完成才做实屏。V01及后续仪器验收继续按协议/参数和动作范围逐模块执行，HDMI版稳定后再适配触控小屏。

同步更新总体计划、根/工程README和知识页入口，不新增平行计划。**本轮只完成目标与可执行计划登记，O03-4实现仍全部未勾选。**没有改业务源码、运行库、配置或已有运行数据，没有编译、安装、部署或连接RK/仪器；既有离线/RK/仪器/长稳结论未因此扩大。

文档核查：六份文档可读，26处本轮新增/状态链接解析到实际文件和锚点，四个相关锚点无重复；O03-4的I0–I5及后续五步均保持未完成，两个计划的四部分结构保留，Git差异空白检查通过。独立只读复核后同步了O05前置的措辞：恢复长测失败约束相关稳定性验收，不阻断无关报告、本地配置或接口分析。

<a id="hospital-ime-setup-20261023"></a>
## 2026-10-23：配置Ubuntu拼音输入、用户实际选字与医院名称保存

用户明确要求配置Ubuntu中文输入法，使软件直接拼音输入，并打开软件指导医院名称输入/保存。依据根/局部规则、当前卡、项目skill及独立只读复核，操作范围限定Ubuntu24.04 amd64输入法依赖、qyq的Fcitx5用户配置、SN100正常WSL图形启动入口和既有匿名实验库。未改医院C++/.ui/原主题布局/保存规则、系统全局环境、Windows输入设置、ARM/RK脚本或仪器；保留已有修改及其它窗口新登记的O03-4计划，不提交/推送Git。

**安装与恢复输入：**安装前模拟确认26个新增包、无升级/删除；明确请求`fcitx5`、`fcitx5-pinyin`、`fcitx5-frontend-qt5`，关闭Recommends，不安装完整桌面/配置GUI/云拼音。qyq从Ubuntu官方源下载完整精确版本闭包，归档至[07_backups/qt_environment/wsl_ime](../07_backups/qt_environment/wsl_ime/README.md)：26个`.deb`、来源/版本/安装前后清单、日志、最小配置及38项SHA校验；`.deb`确认Git LFS，归档未被ignore。`sudo -n`需密码，故仅包安装使用WSL root；下载/配置/GUI和输入法均用qyq。首轮apt `--no-download`拒绝获取本地归档、未安装；改为`dpkg --install`完整闭包后成功，前后清单确认新增26、变更0、移除0。不保存密码或认证材料。

**配置与启动：**原`~/.config/fcitx5/profile/config`不存在，无需备份其它用户环境；创建US键盘+pinyin组及`Ctrl+Space`切换键。新增正式`wsl_ime.sh`，接根/统一壳/独立历史的正常run/account/ide入口；构建和自动测试不加载。适配器复用同一用户DBus，用`fcitx5-remote --check`查服务，flock限时串行启动，不用`--replace`；缺包明确提示，无DBus或启动失败报错。仅向目标GUI导出`QT_IM_MODULE=fcitx`和`XMODIFIERS=@im=fcitx`；不改全局Qt路径。新daemon关闭继承锁FD。`SN100_WSL_IME=off`可单次对照。

默认Wayland前端实测因WSLg拒绝`input_method`绑定退出，日志有`permission to bind input_method denied`及错误71。因此仅检测到`/mnt/wslg`时，为适配器**新启动的共享用户Fcitx5 daemon**加`--disable=wayland`，以X11/DBus接Qt XCB。此开关作用于新daemon，不能称只影响单个窗口；已有daemon复用、不替换。portal缺服务提示未阻止X11拼音；首次无拼音历史文件提示在初始化后消失。依据[Fcitx官方配置说明](https://fcitx-im.org/wiki/Setup_Fcitx_5)及5.1.7实现核对；失败/成功日志均保留。

**实际操作：**正常`shell/wsl.sh integrated-run`打开既有匿名`integration/user_preview_01`。先1600×900，再正常关闭（3个store关闭、`THREADS_STOPPED true`、退出0），重开1920×1080画布，完整显示右侧医院导航。qmake/make检查成功，首轮make有clock-skew警告，第二轮无该警告且无需重新编译；业务二进制沿用既有SHA，本轮仅改WSL脚本，不称新业务/ARM编译验收。运行环境及maps确认`QT_IM_MODULE=fcitx`、已加载`libfcitx5platforminputcontextplugin.so`。

Windows自动化在试键期间反复报告用户输入活动，遂停止自动按键，改为指导用户实体键盘操作，未以剪贴板填中文或直接写库替代拼音验证。用户明确回复“出现中文候选字了”；继续指导空格选“测试”，再输入`yiyuan`/空格选“医院”后保存。用户询问名称显示位置时，实际窗口已显示`测试医院`，独立只读SQLite确认`hospital/name=测试医院`；原一条科室仍为`检验科/JYK/操作演示/keshi`、id=1，`integrity_check=ok`。解释医院名称是下方独立设置，“添加”新增的是上方字典行。用户后来添加的一条空白科室草稿保持原样，未替其放弃/提交/关闭；不把医院名保存说成空白行也已保存。软件留在医院页。[实际页面](../03_photos/20261023_sn100_hospital_pinyin_saved_01.png)。为保留该草稿，本轮新拼音输入后的完整关闭重开未执行；此前保存重开证据独立保留。

**验证与边界：**四入口脚本语法、正常GUI启动、Qt插件/DBus衔接、用户键盘候选及医院名保存均核对，见[结构化结果/源码SHA](../04_references/sn100_host/wsl_ime_20261023/result.json)、[Fcitx日志](../04_references/sn100_host/wsl_ime_20261023/fcitx5.log)、[正常退出](../04_references/sn100_host/wsl_ime_20261023/user_demo_01.log)、[当前启动](../04_references/sn100_host/wsl_ime_20261023/user_demo_02.log)。两进程均uid1000；逐进程约1秒且有GUI观察活动的快照：软件CPU29.988%单核/RSS129760KiB/峰值138468KiB/PSS114097KiB/9线程/21FD；Fcitx CPU0%/RSS与峰值67672KiB/PSS57404KiB/3线程/19FD。不是同负载前后比较，不据此推断性能、预算或长稳。原Windows完整对照、RK/ARM运行输入法、整机重启后启动、仪器/同板控制及长稳仍待验。恢复范围与输入保存复测分开登记，Qt环境记录仅链接本节；同步计划及两个README，保留O03-4等其它任务的优先级。

<a id="docs-simplify-20261023"></a>
## 2026-10-23：精简进度文档，等待并行任务结束后统一收尾

用户要求先简化MD、生成下一步清单，等3个窗口完成后整合、清理并commit/push。本轮仅调整文档及准备只读清理审计，不改业务源码、环境、数据或参考原件；此次明确提交/推送授权适用于后续收尾，不扩大到生产数据或设备动作。

- 新增[日常待办](../01_docs/plans/next_steps.md)，34行；[README](../README.md)精简为84行、[总体计划](../01_docs/plans/development_plan.md)91行、[移植计划](../01_docs/plans/sn100_host_migration_plan.md)138行。计划保留四部分、勾选、当前O03-4完整任务卡和既有锚点；旧模块测试细节链接原记录，代码风险/依赖/F01–F18/仪器批次表移入已有[工程说明](../01_docs/knowledge/sn100_host_project_guide.md#sn100-code-risks)，不新增另一套技术日志。
- PDF中文映射窗口已结束；医院实际键盘候选/测试医院保存及SQLite核对通过，输入后重开因保留空白科室草稿未执行。O03-4报告整合窗口仍运行，未提前勾选完成。原Windows、RK、仪器、资源预算及长稳边界继续保留。
- 只读审查形成临时候选清单：21个候选、165个文件、17533161字节，均为旧纯构建/生成头、已完成一次性文档脚本或与正式归档SHA一致的副本。尚未执行删除；执行前必须重核文件成员/SHA、进程和绝对范围。运行中的统一界面、运行数据/草稿、secret、sysroot/依赖、原参考及正式证据均排除。
- 六份入口文档126处本地文件/锚点链接通过，Git差异空白检查通过。本项不要求无关业务构建/上板；没有把既有编译或离线结果扩大为硬件验收。

后续在本聊天跟进3个窗口的实际终态，全部结束后核对新增实现/测试结果并回填简短进度；再执行有证据的清理、Git/LFS/敏感信息与受影响目标核查，完成一次提交与推送并核对远端。此处记录的是文档交付和收尾安排，删除、提交、推送尚未执行。

<a id="o03-4-offline"></a>
## 2026-10-23至24日：O03-4 原模板报告预览与PDF接入统一主界面

用户明确要求先做O03-4、保持原布局、测试后打开查看。本轮在正式工程接线和验证，保留既有未提交修改、独立入口、O03-3冻结基线及其它窗口的输入法/PDF映射成果；未修改、构建或运行参考工程，未连接RK或仪器。正式证据集中在[本轮索引](../04_references/sn100_host/integrated_report_20261023/index.json)，可变数据库、secret、全部临时产物仍留`temp/sn100_host/`。

### 入口、业务和数据边界

- **入口与实现：**统一主壳创建原`HistoryPage`后调用`frmMain::enableHistoryReports()`；原“结果报告→打印”发出值快照，经`HistoryReportController`进入既有QtRPT预览，预览原PDF工具调用同目录`sn100_report_renderer`导出新文件。没有另启独立历史演示程序。主要维护点为`shell/history_shell.cpp`、`shell/frmmain.cpp`、`shell/main.cpp`及`history/report_controller.cpp`。
- **医院配置：**从同一`HospitalStore`成功的`loaded/saved/allLoaded`信号交接医院名称值；SQL继续只在所属worker使用。预览不读取医院编辑框草稿，空的已提交医院名明确报错；保存失败不替换成功快照。现有预览及PDF任务使用创建时的不可变副本。项目列/单位仍沿用显式匿名O04配置，真实项目模块完成前不能宣称已接生产项目配置。
- **患者与生命周期：**患者未保存时沿用原页面守卫。预览、文件选择和辅助导出忙态纳入历史页与统一主壳的切页、注销和关闭守卫；取消不产生文件，缺helper/渲染失败可重试。关闭预览后保留红色错误或成功PDF路径，避免忙态结束覆盖结果；新操作会刷新状态。主壳析构先回收controller/helper；配置接线失败走正常三worker关闭/等待路径并退出3，不报告就绪。
- **权限与设备：**仍为默认Guest或显式匿名模拟身份，沿用既有账户规则，未发明生产报告权限。脱机表示仪器状态未知；报告接线不会让试剂/样本等未移植业务变为可用，也不触发设备、LIS、校时或实体打印。

### 原菜单与真实功能盘点

以下是8个主菜单与5个用户设置子菜单的当前切片状态，逐按钮规则随相应业务模块继续细化。[F01–F18范围](../01_docs/knowledge/sn100_host_project_guide.md#sn100-functional-scope)继续保留。

| 原入口 | 当前真实状态与缺口 |
| --- | --- |
| 试剂设置 | 未移植、占位；资料/批号/孔位/余量及设备动作链待实现和协议确认 |
| 样本设置 | 未移植、占位；普通/急诊/重复/稀释、任务下发/执行/恢复链待实现 |
| 结果报告 | 已整合离线查询、分页、明细/患者保存、均值/CV、CSV、原模板预览/PDF；曲线/重算/LIS、删除、XLSX及实体打印未开放 |
| 校准曲线 | 未移植；真实拟合/逆算、参数/参考结果和容差待确认 |
| 质量控制 | 页面未移植；`quality/`统计工具独立可用，靶值/规则/图表及完整流程待实现 |
| 分析项目 | 未移植；项目、单位/倍率/精度、动作与算法配置待确认 |
| 用户设置 | 容器已整合，医院/账户两个真实子页，其余三个占位 |
| 仪器操作 | 未移植；`protocol/`回放/本机TCP独立工具不等于真实仪器页面或外部Transport |
| 账户设置（子） | 已整合匿名账户、既有角色规则与注销；生产密码登录/通用权限策略另验 |
| 医院信息（子） | 已整合名称、五类字典CRUD、保存/重开和错误恢复 |
| 语言设置（子） | 未移植；参考仅列简体中文且应用槽为空，实际发行版启用范围待确认 |
| 打印机设置（子） | 未移植；PDF通过不代表打印机配置、纸张/驱动与实体输出通过 |
| LIS设置（子） | 未移植；配置、传输及业务收发链待接入 |

13入口中结果报告、用户设置容器和医院/账户子页有真实实现，其余9入口仍是占位。凝血研究工具不能替代校准或真实检测结果链。下一业务切片仍按项目/单位与试剂基础→样本/任务→设备/结果链推进，不把菜单可见算作功能完成。

### 原布局与实际窗口

本轮未更改`.ui`、QSS、图标或报告XML；主壳1920×1080、历史1751×968逻辑画布及原按钮位置保留，1600×900仍由外部滚动访问。报告XML与参考原件逐字节一致，SHA-256为`673788540ab34ef214e513d6241e983586b98d0256166a6c01439aaee954efc2`；已有Linux宋体/黑体→Noto CJK字体适配继续使用，未重新设计模板。

已用正式WSL Release程序重开既有标记匿名根`integration/user_preview_01`，未播种或重置数据，从原“打印”按钮打开实际预览，标题为已保存的“测试医院检验报告单”，日志记录`REPORT_PREVIEW_READY`。另一旧窗口中新增空白科室草稿保持原样。新版预览留给用户检查：[原始截图](../03_photos/20261023_sn100_o03_4_preview_01.png)、[外部标注图](../03_photos/20261023_sn100_o03_4_preview_marked_01.png)。桌面自动化尝试未弹出PDF文件选择器，不把该次手工操作算导出通过；真实工具栏导出由下述QtTest完成。

独立渲染并检查实际PDF：医院/患者/项目/数值和页脚可见，1页；包含单引号的长医院名在原模板中末字换行、文字完整，保留原模板换行规则。实际桌面短医院名为一行。原Windows发行版完整活动主题/同输入视觉对照、Windows主GUI、指定PDF阅读器Ctrl+C/Ctrl+F仍待验，不以源码几何或WSL截图替代。

### 功能回归、失败和复测

| 实验 | 实际结果与边界 |
| --- | --- |
| 报告专项offscreen_02 | 4 passed/0 failed/0 skipped，3005ms，退出0；其中2个业务方法，另含初始化/清理 |
| 报告专项xcb_02 | 4 passed/0 failed/0 skipped，2920ms，实际wrapper退出0；原页面按钮→预览→PDF工具栏→新文件 |
| 已提交医院/患者与PDF | 精确保留中文及`O'Brien`；未提交医院/患者草稿不进入报告；pypdf和pdfplumber独立打开PDF核对医院、患者、APTT 0.00、页数等 |
| 报告失败/守卫 | 重复挂接拒绝、空医院拒绝、文件选择取消、缺helper失败/重试、预览关闭后失败状态保留、导出未结束时关闭预览仍忙、离页/注销/退出拒绝、成功后恢复均通过 |
| 三页累计core_xcb_02 | write/read/viewport/initializationFailure/measure/applicationExit六个方法各自新进程，均3 passed/0 failed/0 skipped、退出0；不是18个独立业务用例 |
| 数据/原功能 | 五角色及Guest、医院事务锁库失败/重试、患者保存和草稿保留/放弃、独立进程重开、查询/均值/CV、2/4条GBK CSV、100轮切页均通过 |
| 两尺寸与退出 | 1600×900/1920×1080控件、弹窗、焦点、滚动和Escape通过；未标记历史根初始化拒绝/文件不变/退出3，正常窗口退出0，均三个store关闭且`THREADS_STOPPED true` |
| 60秒报告混合负载 | 60轮原预览/PDF导出/三页切换，60份PDF逐份被两解析器复核，0失败；辅助进程残留0，3 passed/0 failed/0 skipped、退出0 |

保留失败而不改写历史：offscreen_01为3 passed/1 failed，固定标题提取含排版空格；只修测试对固定标题忽略空格，医院/患者值仍逐字核对，未据此改业务模板。xcb_01虽然4项通过，但外层退出码引用错误，不作为正式退出证据，重跑xcb_02。core_xcb_01把六方法放同进程导致6 passed/2 failed、退出2，违反既有read/measure独立进程合同；改驱动为六进程后通过，无产品修复归因于该次驱动纠正。早期缺少`QTimer/QPrintPreviewDialog`完整头的构建错误已修；ARM链接/复测见本节构建结果。

### 响应与资源：建立基线，预算仍待验

条件为WSL Ubuntu24.04/qyq、Qt5.15.13、x86_64 Release/Noto CJK；ARM编译和另一个桌面程序并存。报告资源的parent是QtTest进程内承载的真实Widgets/主壳，包含测试驱动与采证开销，不是独立正式程序PID的纯产品占用；本次打开给用户的正式程序未作等条件资源测量。时间是UI注入到匹配信号/helper空闲，心跳是10ms事件循环代理，不是合成器像素或RK实时期限。资源按`/proc`100ms采样，短峰值及helper生灭尾部可能遗漏；单CPU核满载记100%。

| 负载与指标 | 本轮实际值 |
| --- | --- |
| core_xcb_02，60130ms/314轮 | 导航p95 80.732/max109.882ms；查询p95 21.902/max26.804ms；保存p95 24.178/max24.428ms；心跳p99 35.874/max44.883ms；0失败 |
| report offscreen，60004ms/60轮 | 预览p95 51.298/max91.692ms；PDF工具栏至helper空闲p95 499.420/max642.299ms；心跳p99 98.081/max149.041ms |
| parent峰值/平均 | RSS94164KiB、PSS65162KiB、9线程、23FD；单核口径平均CPU40.9998% |
| helper采样峰值/平均 | RSS80624KiB、PSS54782KiB、1线程、8FD；60个PID、残留0；观测区间平均CPU6.0747% |
| 文件增长 | 60份20818字节PDF，共1249080字节；计入临时job采样峰值1269328字节；本轮选择首/末PDF归档，其余可变输出留temp |

既有O03-3三页基线导航p95 73.931/max150.017ms、查询p95 18.437ms、保存p95 19.221ms、心跳p99 37.183/max76.243ms保留。两轮共载、采样和时刻不同，表中只是可追溯原值，不能据此量化报告接入开销或推断性能改善/回退。固定最大数据/并发与预算、同板控制deadline、RK温度/降频、2h/8h及24–72h完整混合长稳另验。

### 构建、发布输入及回退范围

WSL、Windows32与ARM64均实际完成主壳和helper构建，正式脚本仅在两者成功后发布一对产物。WSL日志见[构建日志](../04_references/sn100_host/integrated_report_20261023/logs/native_build_02.log)，Windows日志见同证据目录；没有部署旧产物。当前产物指纹：

| 目标 | 主程序SHA-256 | helper SHA-256 |
| --- | --- | --- |
| WSL Qt5.15.13/x86_64 | `9ab048ce42930cdf18db454afdae6e6852673432dec30132b8a1d346e57d4fe7` | `cc1c0d4562f6a56670a7884a2875ecede77407ae5ee83e7db4587375b8001d3b` |
| Windows Qt5.14.2/MinGW32 | `321286e119aa6b821889feb6e21f2f474d500d299e2a46735f3a38547c40ac50` | `00aaeeb34e9faf6ed6700eeafaa34d4c2e0d3990113f71b3377fb61c3f0f2fdb` |
| ARM64 Qt5.15.13/GCC | `e9a46e2f5a68e647f6cab683821bf43ee09fdcef15603a062e360be37c01d143` | `6908223fdb3d9b22abea04cd0bae6c003e734b27003807491851555531ce653a` |

**ARM64正式终态：**`shell/rk.sh prepare`本轮使用独立`temp/sn100_host/o03_4/rk_prepare/`，20261023_235408轮真实退出0；54个ELF/370项provider符号版本检查、errors=0，状态`offline_closure_passed_board_runtime_pending`。主/辅位于其`build/`；包为`packages/sn100_shell_f1f08caa0a5914badd6009e5710842cb3f867a29c2d8985d938b7c7ec414614b.tar.gz`，56780368字节，SHA-256 `68b6d0d31d26ffff1f3e27ebb59ec6265ac486ee7b4616b32a012c9a78c454e4`。Release ID即文件名中的完整64位值；冻结清单SHA为`a0d014d2eb4f99112b20799c065c4205843e9a5c7f5960ad22fa23d9f17eddb4`。包内30个普通文件逐项SHA、全部ELF AArch64、release ID及当前输入与冻结一致均通过；`READY_FOR_TRANSFER=1`、`BOARD_RUNTIME_VERIFIED=0`。准确路径/日志/依赖和包审计见[ARM结果](../04_references/sn100_host/integrated_report_20261023/arm64/result.json)。

ARM失败/修复分别保留：早期输入变化使冻结拒绝发布；首次完整80对象编译后，手写moc命令缺Qt include根导致Qt版本条件内`QtRPT::setChart`信号未生成、主壳链接失败。补moc include根后，核验同一轮全部编译输入、编译器、精确flags/源顺序及每个source/object SHA，仅重编内容变化的moc；最终轮显式合同验证80对象再重链接，helper两个源重新编译，默认构建入口仍全量编译。随后严格闭包暴露旧最小板端检查输入缺`libresolv.so.2`，从Ubuntu官方索引取得与既有板端相同`libc6 2.39-0ubuntu8.9`，只提取resolver到本机检查sysroot，不部署整包。新增[检查输入归档](../07_backups/qt_environment/sn100_report_inspection_arm64/source.json)保留来源/SHA与范围；最终包确认没有libc、loader、libresolv或QtCore/Gui/Widgets。没有通过放宽检查或复用旧发布产物绕过失败。

统一构建已纳入既有`history_report.pri`及renderer；RK准备纳入helper、模板QRC、CJK字体/许可、offscreen插件和实际闭包。新增3个Ubuntu官方ARM输入归档至[sn100_report_runtime_arm64](../07_backups/qt_environment/sn100_report_runtime_arm64/)，保留索引SHA、来源、版本、许可与恢复范围，`.deb`使用既有Git LFS规则；只提取到隔离sysroot/包，不安装或替换系统Qt/库，不写板端环境。预检字体缓存改到独立`preflight_cache`，避免首次check创建未标记runtime后start拒绝；未标记已有runtime仍拒绝。16项依赖检查与6项发布/兼容回归通过。

本轮只添加必要接线、测试和构建依赖，无业务删减或数据/schema迁移。回退时保留数据与证据，回退报告接线/controller配置和相关构建入口，按已保留O03-3指纹重建/验证；新导出PDF留存，不复位医院/账户/历史数据。O03-4的I0–I5仅按上述离线切片勾选，Windows完整行为/视觉、指定阅读器、RK加载/实屏/键鼠、仪器、实体打印、XLSX、资源预算及长稳继续作为未关闭子项。日常下一步为项目/单位与试剂基础的本地字段/规则/保存重开切片，设备同步另验。

2026-10-24跨日收口：七份文档426个本地链接/锚点、三份计划的四部分勾选结构、证据清单63项SHA均通过独立只读复核；当前正式编译输入仍与最终ARM冻结清单一致。代码接线复核未发现新的P1/P2。实际报告预览保持打开，未关闭旧医院窗口或替用户提交/放弃草稿。本聊天未清理其它事项文件、自动提交或推送Git。

<a id="cleanup-commit-20261024"></a>
## 2026-10-24：三窗口成果核对、文件审计与提交收口

三个指定聊天均已完成，未发现同仓库其它活动写入任务。已汇总O03-4统一报告、PDF中文映射及医院实际拼音输入成果，更新README、总体计划、移植计划和短待办；下一开发切片是项目/单位与试剂基础。原Windows完整行为/视觉、指定阅读器、RK/仪器、XLSX/实体打印、资源预算及长稳继续待验。新版已读回已提交医院名称，保留旧窗口草稿，不替用户提交/放弃或关闭软件。本次不再重复已通过的业务构建或操作仪器。

**文件清理的实际结果：删除0个。**原21项165文件加正式工程2个Python缓存，共22项167文件/17569114字节（约16.76MiB），逐项重核成员、大小、SHA、归档副本、绝对路径/重解析点及Windows/WSL进程路径。递归删除在进程创建前被工具自动审批以`blocked by policy`拒绝；改用逐文件、非递归且逐项SHA核验的较安全方案仍被同样拒绝，未提供更具体原因。两次均未执行，未尝试绕过，候选原地保留。加入06_programs局部`__pycache__/`、`*.pyc`忽略，根`/temp/`继续排除缓存与临时凭据；不是把忽略当成实际删除。[审计结果/167项指纹](../04_references/sn100_host/cleanup_20261024/result.json)、[结果SHA](../04_references/sn100_host/cleanup_20261024/SHA256SUMS)。源码、参考原件、全部运行数据/草稿、secret、永久证据、sysroot/依赖及新旧冻结包均保留。

**提交前核查：**当前源码197个正式构建输入与最终ARM冻结清单逐项SHA一致；原O03-4证据63项SHA、三目标主/辅及ARM包7项实际产物SHA、60份PDF指纹由独立只读复核通过。27份Shell脚本语法与8份Python辅助脚本AST检查通过（不写Python缓存），六份入口文档136处本地文件/锚点链接通过，Git差异空白检查通过。首次Shell检查的命令传参有误，改用临时脚本后27份全通过，不据此声称新增业务回归。14份恢复输入SHA清单262条一致，241个待加二进制/压缩归档有LFS规则；7个公开数据库的匿名来源/schema与常见凭据格式核查未见新增问题，临时账户库/凭据不入Git。

本次仅调整进度/说明/忽略规则及新增公开审计，不改变业务源、UI/模板或系统配置；清理因策略受限保留为待办。按用户明确授权，在审查暂存内容后提交并正常推送当前main，禁止force；最终提交号及远端一致性在交付回复中报告，不为回填hash再产生重复提交。等候窗口结束的sn100自动跟进已删除，避免后续重复执行。

暂存进一步核查：241个LFS指针的OID/大小与本机归档逐一匹配，temp/参考工程/pyc/secret均不在暂存区。发现4个冻结输入会被Git自动转码换行，已为history/origins.json、shell/resources.qrc、thirdparty/qtrpt/origins.json和ui/frmhospinfosetting.ui增加精确字节保留属性并重新暂存；197个索引blob与冻结SHA全部一致，工作源码字节未改。全暂存空白检查提示了保真SVG/证据/供应商及原UI的原始空白，未为了消除提示改写校验原件；本轮编辑的文档、规则和入口说明专项空白检查通过。所有167个候选仍实际存在，删除计数保持0。

<a id="catalog-sample-offline-20261024"></a>
## 2026-10-24：继续项目/单位、试剂基础与样本任务离线移植

用户明确要求按“项目/单位和试剂基础 → 离线验证通过接统一程序 → 样本和任务”继续，并保持原版界面颜色和布局。开始时 Git 工作树干净；读取根/局部约定、当前计划/工程说明、模块与文件技能及业务验收参考。项目/单位、试剂按独立文件范围并行实施，统一入口由同一整合方修改，样本先只读准备原规则与接口，前置通过接入后再实现。

本轮只使用正式源码和新的显式匿名运行目录，不运行/修改参考工程，不读取参考生产数据库、密码或连接配置，不连接 RK/仪器。开始登记时尚无新增构建、功能或视觉通过结论；各实际结果在本节继续追加。数据、配置/环境无覆盖时不额外备份，不自动提交或推送。

### O07：项目/单位与试剂基础独立通过，再接统一程序

项目业务入口为`projects/project_page.cpp`，校验/事务在`projects/projects_store.cpp`；按原基本页保留名称/全称/通道号/启用、10列结果、精度1–6、Calibration/Rate单位与唯一主显示、复制和固定/外部引用删除保护。原`.ui`逐字节一致；原版单位字典仅有读取入口，本轮不发明单位CRUD。四字段sampleRates候选/默认按原表只读交给样本，演示标签无物理比例含义。独立offscreen与XCB各9/0，真实启动/退出及ARM64构建通过；初次种子NULL绑定和ARM链接依赖路径失败修复后重新验证，原失败日志保留。[来源/差异](../06_programs/sn100_host/projects/origins.json)与[独立证据](../04_references/sn100_host/projects_20261024/verification.json)维护原字段与实际SHA。

试剂入口为`reagents/reagents_page.cpp`，存储在`reagents/reagent_store.cpp`；实现资料、瓶型、条码、项目批号、20个试剂位和4个稀释位的本地配置、容量/手工余量、保存重开/引用保护/权限/草稿/锁库恢复。最终独立QtTest11/0，XCB及本轮统一图形验证有对应证据，独立ARM64构建通过。7份原UI的控件次序/几何/文字/样式/字体均对照；资料UI只去掉与共享mask冲突的布局包装，961×995资料卡在1751×968宿主按928/995等比缩放；本地瓶型管理与余量编辑为离线新增弹窗，原业务区域不重排。10张原位图SHA相同，孔位显示仍为原ml格式；WSLg抓图全黑会拒用并回退真实有效画面，抓图导致的绘制警告不当成功能失败或视觉通过。[来源](../06_programs/sn100_host/reagents/origins.json)与[独立证据](../04_references/sn100_host/reagents_20261024/verification.json)保留差异和失败/复验。

独立通过后才将两模块加入`shell.pri`与原主导航0/5；统一入口持有worker，页面归stack/scroll，项目快照按稳定id交给试剂，停用项目保留引用名称、写入再核启用状态。同一显式`catalog/catalog.db`由项目先提交schema/marker，再开试剂连接，FK在事务前开启并读回1，未标记目录/未知库/参考路径拒绝。既有医院/账户/历史目录不覆盖。原1751×968内容画布使用内部滚动，外层保留1920×1080逻辑布局；离页守卫、全局注销/关闭和账户回落覆盖隐藏草稿。

O07阶段统一offscreen/XCB各**5 passed/0 failed**：匿名输入复制LOCAL_A、试剂/瓶型/项目批号、R_1装载5000µL/本地余量4000µL，跨worker删项目/被引试剂拒绝，Guest和注销后拒写；项目草稿取消保留、放弃恢复，两尺寸截图；真实产品遇未标记catalog目录退出3且sentinel字节不变。复核修复原启动身份门槛：初始化Guest不代表请求actor成功，不存在的模拟身份退出3、无SHELL_READY，首次账户读取失败也不继续空等；实际线程停止/退出均核对。[统一证据](../04_references/sn100_host/catalog_integration_20261024/o07/verification.json)包含204个源码/资源输入SHA、主辅/测试程序SHA及23份公开证据，未归档账户库、认证字段或任何可变运行DB。

资源负载为WSL Qt5.15.13/x86_64 Release/XCB，测试驱动在60.025秒中完成374次真实项目提交并往返项目/试剂/用户页：保存完成p95 **127.580ms**、最大179.261ms；10ms心跳3665样本，p99 **50.589ms**、最大64.717ms。外部100ms采样590点/60.912秒，直接QtTest子进程CPU **68.02%单核口径**、RSS/HWM273088KiB、PSS260183KiB、线程40/FD53观测峰值；包括测试驱动及库线程，不冒称正式产品预算。运行文件前后均131156字节；临时journal峰值未测。没有把候选100ms当已约定门槛，也不据不同负载与旧三页数值推断开销改善；RK/温度/同板控制/长稳仍待验。

统一ARM构建输入与freeze白名单原漏新目录，已补项目/试剂及真实UI/moc/QRC，报告helper仍只带报告闭包；新增输入核查7/0。最初两条Windows→WSL内联构建调度丢失状态/路径变量，未作为通过结论；改用保存的严格退出脚本，正式native/test构建退出0，并在模块冻结后强制重编新增输入才运行本轮用例。O07阶段统一Windows32构建继续执行，最终样本版的修后构建结果见下文。没有部署或连接RK/仪器。

**范围与下一阶段：**生产项目/单位导入、测试动作方案、逐项目试剂消耗/足料、实测余量、ISI/校准、设备同步/探测/换位/清盘、原Windows活动主题/完整行为、RK、仪器及长稳未关闭。O07统一门槛通过后已启动O08样本本地预设/任务实施，实际结果继续在本节追加；本地草稿不伪装成已下发或执行结果。

### O08：样本与本地任务独立门槛

O07独立及统一门槛通过后才实施样本。入口`samples/sample_page.cpp`沿原样本架/设置/明细/组合/批量/架号界面进入，规则与事务在`samples/sample_store.cpp`，值对象在`sample_types.h`。普通架S01–S98、每架8孔及急诊3孔来自原默认配置，真实机型孔位仍待确认；保留容器、0–4位字母数字样本号、项目任务、0–99次重复、延长测试标志、原sampleRates候选与组合成员顺序/重复。缺默认或候选失效明确拒绝，不猜生产1:1比例。

**有意差异：**原普通架预设在内存中，本轮把已提交预设保存到隔离catalog中的`sample_local_*`，未提交内容仍只在页面；原组合`sample_combination`字段保留，另用有序FK成员保护项目引用。局部id不是设备任务/报告id。Guest只读，显式匿名角色1–5可本地编辑；原版普通预设没有全局running禁止，故模拟`actionReady=false`仍可准备本地预设。原急诊更新在Testing/Prepared/Pause会自动调用下发，本轮隔离该副作用；没有Transport/LIS、TX、报告生成、试剂扣减或真实执行状态。试剂快照仅作本地候选来源，无逐项目消耗映射时不声称足料或可测试次数。

最终冻结源码独立**offscreen 14/0、XCB 14/0、均无跳过**（含init/cleanup与12个业务方法），覆盖未知schema/链接路径拒绝、空字段重开、重复0/99/越界回滚、组合空/重复/引号名、FK、急诊删除保护、锁库失败与重试、Guest及运行态、跨弹窗草稿守卫/关闭状态撤回。初轮页面把store ready当首load完成导致测试读取空表崩溃，已分开initialized/ready并复测；XCB人审又发现延迟删除的旧列表控件引用，已限定当前可见itemWidget并断言99次选中状态一致。mask动画初始缓存图不作输入证据，最终等待动画可交互后取图；[来源/差异](../06_programs/sn100_host/samples/origins.json)、[独立证据](../04_references/sn100_host/samples_20261024/validation_summary.json)及`03_photos/20261024_sn100_samples_*_03.png`保留实际状态。7份原UI/原图、Material控件、颜色与布局保留，Linux字体/资源路径/脱离全局mainWinPtr的Popup所有权与屏幕定位逐项登记；Material瞬态QPainter警告仍保留、原因待定位，不能声称无警告。原Windows发行版完整像素/主题一致性仍待验。普通8孔末两孔批量、容器保存、组合应用重复数及真实task引用FK另有补充探针exit0，不重复计入14项。

架定位修复前的独立XCB历史基线：固定3孔本地样本号连续保存60.037秒、660次提交、0失败：保存至读回p95 85.834ms、最大96.816ms；10ms心跳2966点，p95 37.475ms、最大48.296ms。外部100ms观察约60.47秒/592点，CPU约72.60%单逻辑核、RSS峰136708KiB、PSS124593KiB、6线程/16FD；属于QtTest驱动+GUI+存储代理负载，主机另有构建，不能当完整产品或RK达标结论。[固定负载](../04_references/sn100_host/samples_20261024/mixed_measurement.json)与完整资源摘要保留测量范围。

独立门槛后接原导航索引1，统一壳沿用原逻辑尺寸/配色；项目→试剂→样本顺序提交schema，每个worker单独SQL连接。会话与已提交项目/试剂值快照进入页面，草稿/过期回复不覆盖编辑；全局离页/注销/关闭守卫覆盖新页与隐藏草稿，启动必须等全部首读。统一集成及最终构建结果在下文追加，尚不以独立门槛替代累计回归。

### O08联调发现与原版定位修复

统一窗口人审发现普通S01架虽显示8孔，左侧当前卡仍露出急诊3孔卡标题。对照原`frmsamplematerial.cpp:367–386,5613`，补回编辑时当前item按页面高度展开、`scrollToItem(PositionAtTop)`及禁止滚动，返回总览恢复136像素/允许滚动，重新编辑和前后架切换再次定位；首末架前后按钮沿原规则显隐。新增未保存普通架使用仅在UI存在的临时卡，明确标记“未保存/未下发”，不写入已提交快照或数据库；取消保留、放弃撤回、保存以真实本地id替换。

只修改`sample_page.cpp`，最终SHA-256为`6e8fb2b95eec1160a49d7c9499147c781e5cb44faca8c342e0db97853e5977ce`；样本其余62项输入未变。先在影子副本验证再更新正式源码，正式offscreen/XCB各14/0，额外可视状态探针两平台退出0，覆盖8/3孔当前卡、总览/重进、前后架与新草稿取消/放弃/保存替换。[最终样本清单](../04_references/sn100_host/samples_20261024/SOURCE_SHA256SUMS) SHA为`af03d902e27056f508735ec5890b5ef88358b2b1b03e9e0227e9174d84254db7`；三张`03_photos/20261024_sn100_samples_*_visibility_04.png`已目视核对。上文660次/60秒数据属于修复前源码历史基线，修后独立短负载与本轮统一60秒数据分别登记，不混为同一次测量。

Windows完整构建在修复前已通过；修后核对260/50项主辅输入仅该CPP变化、9项编译/链接参数不变，实际仅重编1个对象并重链，辅助程序输入/对象/产物SHA不变。首次临时调度脚本把链接目标斜杠写错导致make退出2，改用Makefile实际目标后链接退出0且未额外重编；失败与恢复日志均保留。[修后Windows证据](../04_references/sn100_host/catalog_integration_20261024/o08/windows_after_visibility/verification_visibility.json)是编译证据，未运行Windows程序或完成原版活动主题对照。

### 修后统一与累计回归

修后统一offscreen/XCB各**7 passed/0 failed/0 skipped**，包括O07引用/草稿/权限、O08本地任务保存重开、当前普通/急诊卡定位、总览/重进与新架草稿撤回、真实六store产品就绪/关闭、未标记catalog及无效身份拒绝。统一截图已目视确认S01/8孔蓝色架与表格对应。原441px高表格配60px表头及至少51px行高，原本就不能首屏显示全部8行；当前Material滚动条可淡出，末行用常规滚轮/滑块访问。原QScroller左键拖动和ScrollPerPixel两句尚未接入，触屏拖拽一致性明确待验，没有把截图截断视作缺孔或声称触摸通过。

既有核心六组`write/read/measure/applicationExit/viewport/initializationFailure`各**3/0**（各含init/cleanup）。报告累计首次`committedHospitalAndReportLifecycle`通过，但`persistedHospitalAndLegacyEntry`在`legacy.query()`失败；同一冻结可执行文件/源码、新匿名目录复测两方法合计**4/0**。首轮3/1日志与复测4/0均保留，原因尚未定位，未宣称问题已修复或首轮全绿；该旧入口只创建医院/账户/历史，不实例化新样本页。后续长稳需继续观察这项UI用例。offscreen不支持raise/propagateSizeHints及样本自定义加号的MousePress/Release提示亦保留，业务状态断言按实际结果判断。

新增接线经独立只读审阅未发现新P1/P2：worker线程归属、共享catalog串行schema初始化、会话和值快照、ready前首读失败退出、反向关闭及隐藏草稿守卫均核查；没有据此替代运行验证或扩大设备范围。

### 修后响应与资源基线

统一Release/XCB固定负载60.147秒，真实项目保存**268次**、样本保存**268次**，均成功，并往返项目/样本/试剂/用户页。项目保存至完成p95 **127.041ms**、p99 146.415ms、最大170.567ms；样本p95 **73.415ms**、p99 80.951ms、最大101.396ms。10ms心跳3773点，p95 45.474ms、p99 47.998ms、最大**56.262ms**；运行文件前后均176212字节。外部100ms采样598点/60.977秒，CPU **74.60%单逻辑核**、RSS/HWM280260KiB、PSS264287KiB、线程41/FD55观测峰值。该驱动创建五个store（无历史），包含QtTest与GUI/库线程开销。

另用实际**六store**产品进程、Guest、1600×900/XCB观察60.178秒/590点，普通SIGTERM后退出0，SHELL_READY与全部关闭/THREADS_STOPPED核对通过。CPU **18.93%单逻辑核**、RSS260204KiB、HWM263444KiB、PSS246374KiB、线程41/FD55观测峰值。它是空载显示基线，不能与上述混合驱动相减推断新业务开销。主机同期有构建，时钟心跳不是输入/合成器像素时延；文件前后字节不覆盖journal瞬时峰值。资源预算、RK温度/降频、RT同板共载与长稳均未验收。

### 跨平台依赖与发布核对

样本沿用原SVG图标，需要与现有ARM Qt5.15.13匹配的QtSvg库及`imageformats/libqsvg.so`。从Ubuntu官方noble/universe获取`libqt5svg5`与dev 5.15.13-1 ARM64，验证Ubuntu签名InRelease、完整Packages.xz摘要及包SHA，归档到[恢复输入](../07_backups/qt_environment/sn100_samples_arm64/README.md)，`.deb`沿用Git LFS。仅恢复隔离sysroot/私有包，未安装系统包或修改板端Qt；来源、许可与恢复范围可追溯。闭包检查新增SVG插件合同，发布主包能力仅声明本地预设/任务，不声明仪器执行。

构建输入测试**8/0**、依赖检查**18/0**、部署发布模拟**7/0**；后者仅WSL本机模拟，不代表板端执行。单CPP复用工具新增显式允许路径，默认仍拒绝业务输入变化；要求唯一源、无路径逃逸、全部其他输入/flags/工具链/源顺序与逐对象SHA匹配，且26个生成UI头键/SHA前后相等。只允许本次`sample_page.cpp`失配重编，未把旧sample对象列为复用；检测到直接/计算include CPP或其他失配即拒绝。工具针对性用例**13/0**，默认正式`build.sh`仍全量编译。完整构建及修前包保留为历史，再按严格合同复用119个未变对象、重编当前样本并重链；helper重新构建，最终闭包/冻结与实际SHA见本轮公开结果。

最终主/辅实际产物SHA-256如下；Windows仅编译，ARM仅构建/静态闭包，没有上板执行：

| 平台 | 统一主程序 | 报告helper |
| --- | --- | --- |
| WSL Qt5.15.13/x86_64 Release | `c20cf47339599833a5591dd0ada18c3f18886f1c984cbdcea628b700b7af9f48` | `cc1c0d4562f6a56670a7884a2875ecede77407ae5ee83e7db4587375b8001d3b` |
| Windows Qt5.14.2/MinGW32 PE32 | `e54d5c00573aff9613df97682682880ecc8aa6111e2ec7eafc387ca41482235a` | `eb17a55a5a2829d19792e8b3935ee9dd281035f087ebe0cb88ce45807b3e4b10` |
| ARM64 Qt5.15.13/GCC Release | `9f8c31679bc0e339110841e1fb674f0241c71e8708cc383afdc46a18d1e2c394` | `7699cd6d864896a0fc245d90ce01487ace6300d575486b2300cbd3045591fa64` |

ARM最终prepare退出0，检查56个ELF、393项provider符号版本、errors=0；新release ID `91674b00d3762269aabba26596a054393313ef357144033be31057c54a16952c`，包位于`temp/sn100_host/catalog_integration/arm64_final/packages/sn100_shell_<release-id>.tar.gz`，64355182字节，SHA `c0d5aff81d2de8a06723c55656bbffe51c93e2efa9bd759d9f52ed1e1b9a74e8`。包内33个普通文件，其中SHA清单31项全部匹配，另核release ID及清单本身；全部ELF AArch64、QtSvg库/插件/许可及316项当前源码快照均核对。修后审计SHA `fc65e0ba5d8dfd40828eb67c6d5c0efeaafe05baf665bdd34f85a0afd9b9a4a9`，状态仍为`offline_closure_passed_board_runtime_pending`。

[本轮统一结果](../04_references/sn100_host/catalog_integration_20261024/o08/verification.json)及同目录SHA清单归档当前输入、两QPA截图/日志、首次报告失败/复测、累计与资源、Windows修后证据和ARM最终审计；不归档运行DB、临时认证字段或可执行文件。修前完整ARM和Windows产物只作历史，未以它们代表修后交付。没有业务删减、系统安装/配置覆盖、自动提交/推送或设备连接；回退只撤接线/新模块及相应构建项，保留运行数据、草稿和全部新旧证据，不降级覆盖当前数据库。

收口只读复核：七份文档451个本地链接/锚点、三计划四部分与勾选结构通过；O08公开107项、O07及其Windows历史清单、修后Windows15项、项目/试剂/样本独立证据与源码清单均逐项SHA匹配。统一265项直接构建/测试输入、316项运行程序冻结源码及五个WSL主辅/测试产物与当前一致；Git专项差异空白检查通过。复核指出的工程README残留“ARM审计进行中”及包内33文件/31项SHA清单表述已修正，未改链接或源码。原有窗口/用户草稿、参考原件与167项旧清理候选均保留，没有自动提交或推送。

<a id="followup-20261024"></a>
## 2026-10-24：另一窗口完成核对与O05离线接续

用户要求检查另一窗口是否完成，完成后更新计划并直接执行下一步。目标聊天“查明试剂样本业务未移植原因”本轮已真实结束（turnCompleted，无错误），同仓库未发现其他活动写入方；保留全部O07/O08未提交成果，未提交或推送Git。根README、三计划及上一节已登记O07/O08结果，本轮据证据独立复核后增加当前可执行的O05诊断卡，真实任务下发仍等待协议、参数与设备，不重复移植已通过的本地页面。

独立核对O07 23项及其Windows 11项、O08公开107项、修后Windows 15项、ARM最终13项、样本57项及其源码63项、统一直接输入265项和ARM源码快照316项，SHA均一致；WSL、Windows和ARM最终主辅实际SHA与上节一致。最终ARM包64,355,182字节、33个普通文件、31项SHA及release ID核对通过，包SHA仍为`c0d5aff81d2de8a06723c55656bbffe51c93e2efa9bd759d9f52ed1e1b9a74e8`；本次审计没有重新构建或新增RK/仪器通过结论。独立样本摘要中的“交整合方完成”是历史交接口径，现由O08统一最终证据覆盖，不改写旧证据。

### 旧报告偶发查询：原程序五轮复测，未定位原因

使用上轮冻结`integrated_report_test`及同一helper、WSL Ubuntu24.04/qyq、Qt5.15.13/x86_64 Release/XCB，在五个新的显式匿名目录依次运行`committedHospitalAndReportLifecycle`和`persistedHospitalAndLegacyEntry`。五轮分别为2875、2708、2514、2718、3013ms，均**4 passed/0 failed/0 skipped，退出0**；4项含初始化/清理，不计为四个独立业务用例。测试和产品源码均未因本次报告复测修改，没有读取原生产库或操作仪器。

[五轮原始日志、退出码和程序SHA](../04_references/sn100_host/followup_20261024/report_frozen/result.json)已归档。原首轮`legacy.query()`失败保留；本次未复现不构成根因定位或修复，缺陷保持待定位，不增加超时或放宽断言。

### 当前接续：O05有界时序与短受控实验

依据原1150.178秒失败，先补默认关闭的client/transport单调时标及专用本机回环诊断：固定容量内存采集、结束后落盘，区分deadline建立/tick/observe、入队/pump/write/bytesWritten、peer实际读取。仍使用120ms与一次重试；正常、预算内短停顿、超预算停顿分别验证，失败后只在测试驱动有界排空取证，不能将迟到观测改成成功。超预算注入只能说明可发生的机制，不能据此倒推旧长测根因；同步progress/log落盘仍为待证候选。

本段是本轮实施与合同登记，O05诊断/短回归/跨平台实际结果待后续追加。运行和日志放`temp/sn100_host/protocol_timing_20261024/`，必要证据放`04_references/sn100_host/protocol_timing_20261024/`；没有重新启动8h、修改系统配置、部署RK或连接仪器。当前改动无不可恢复配置/数据覆盖，不额外备份。每30分钟跟进已更新为接续当前工作，避免反复轮询已完成窗口或重复构建。

<a id="o05-timing-20261024"></a>
### O05诊断切片实际交付与下一轮运行

正式维护入口为`protocol/read_only_client.{h,cpp}`、`tcp_transport.{h,cpp}`的默认关闭`setTimingEnabled()`及`timingEvent`，以及`soak_main.cpp --timing-trace`。采集槽只收集、不重入控制或写盘；原120ms/一次重试、迟到观测、连接代次及收发策略保留。client/transport各自源时钟分别登记，统一collector单调时钟用于跨源排序，不相减不同源的起点。Soak用8192容量的最近事件环，公开覆盖计数，终态一次保存`timing.json`；包括日志write/flush、progress commit前后及peer实际读取，文件保存失败明确退出3。监督脚本冻结同名开关、独立核对环容量/序号/时间顺序，非默认时长使用实际秒数命名阶段，避免把缩短实验写成2h/8h。

WSL Ubuntu24.04/qyq、Qt5.15.13/x86_64 Release实测：

| 检查 | 实际结果 |
| --- | --- |
| 专用受控时序 | 四场景＋init/cleanup共6 passed/0 failed；默认关闭零timing事件，无停顿/40ms停顿成功，240ms超预算仍单一失败终态，两次Qk均与独立13字节金样本一致 |
| 旧客户端/传输/异常/协议累计 | 分别19/27/14/63 passed，均0 failed/0 skipped，退出0；含init/cleanup，迟到观测不回转失败语义保留 |
| 长测工具CLI | 关闭/开启各12秒退出0；50条/秒24秒覆盖环16253事件、保留8192、覆盖8061，序号及采集时钟顺序一致；日志预算耗尽、timing保存目标为目录两项均按预期退出3，outputs_saved=false |
| 监督链 | 最终脚本两个2秒阶段均退出0，冻结开关与独立wire/timing复核通过；仅监督入口冒烟，不计小时级通过 |
| 跨平台 | 同一长测工具WSL实际构建运行、Windows Qt5.14.2/MinGW32及ARM64构建通过；Windows诊断QtTest另构建通过。未运行Windows目标、未部署ARM |

240ms驱动故意在attempt2的`queryQueued`直连回调停顿，异步pump已排队但尚未执行。统一采集时钟实际顺序为第二次`write.returned(13)`约361.456ms → 失败终态361.461ms → `bytesWritten`361.501ms → peer实际读取361.514ms → 迟到有效观测361.559ms；失败未回转成功。**这证明一种事件线程饥饿机制，不证明旧1150.178秒故障由同步文件IO、TCP丢包或transport阻塞引起。**Soak原正常结束/失败立即关闭策略保留，只有专用短测试驱动在终态后继续250ms排空取证。

同一12秒、10条Qq/秒、2秒重连、每2次请求故意不回首答的Release负载，用现有host-only观察器100ms采样各121点，关闭/开启诊断均退出0：CPU为单逻辑核口径1.077%/1.243%，观察RSS/HWM峰12816/16640KiB、PSS9595/13445KiB，线程均1、FD均8。仅是该轮短基线，未隔离操作系统共载，终态JSON的瞬时峰值可能遗漏；不是RK资源预算或同板实时性放行。

长测工具实际SHA：WSL `5837009027c610e46ed6c8312fde24b6a0848ddb63154f2a13b79b230553b5eb`，Windows `af4cfbe1069e5cf05714c814f80024be401707138efd7eec3b4404525f8264a0`，ARM64 `4d9efcd523fd0dfb57e76cf1f54ff71217e3ed30231aff20dd94050023e79ff6`。原始用例/线日志、负例、资源、构建和当前源码SHA见[诊断证据](../04_references/sn100_host/protocol_timing_20261024/verification.json)；O07/O08只读复核见[正式审计](../04_references/sn100_host/followup_20261024/o07_o08_audit.json)。本次protocol不在统一壳316项编译/发布快照内，没有以协议诊断冒充新增统一页面或重跑其构建。

保留驱动纠正：首个12秒环覆盖实验实际8134事件、未满8192，覆盖断言失败；改为24秒固定负载后只重跑未完成覆盖/负例，产品未因该失败修改。首次内联Windows→bash ARM命令参数丢失、mkdir被拒，改绝对脚本后构建通过；监督首次因证据父目录不存在拒绝，建立本轮父目录后冒烟通过。原始失败输出/说明保留，不将其算产品修复。独立只读代码审查未发现新P1/P2；启用诊断的直连槽须遵守只采集合同。

**已实际执行下一项：**在同一工作目录冻结新binary/源码/监督器并后台启动`recovery_probe_25m`，先12秒连续、再**1500秒恢复**，10条Qq/秒、每秒Qj/Qk/QH、60秒重连、每10请求故意不回首答、120ms与一次重试，开启时序，1秒采CPU/RSS/PSS/线程/FD。监督进程PID371，监督器启动UTC为2026-10-24T04:53:03.266786（北京时间12:53:03），精确时标与SHA以[冻结合同](../04_references/sn100_host/protocol_timing_20261024/recovery_probe_25m/job.json)和[运行状态](../04_references/sn100_host/protocol_timing_20261024/recovery_probe_25m/job_status.json)为准；当前仅登记启动，不勾完成。此轮覆盖旧19分10秒附近用于诊断，不是8h重测或长期通过。后台只写专属日志/证据，不覆盖共用计划；每30分钟跟进回收终态、失败前后时序后再决定下一步，不重启重复任务。

交付核查：诊断静态证据154项SHA（排除仍变化的25分钟任务目录）、O07/O08审计及五轮报告8项、诊断32项当前源码、原统一壳316项冻结输入均一致；本轮六份文档417个本地文件链接、三计划四部分结构和两个Python辅助程序AST检查通过，专项Git差异空白检查通过。25分钟任务已进入`recovery_1500s`，完成的只是前置12秒阶段，恢复终态仍待回收；没有自动提交或推送。

<a id="probe-result-and-status-replay-20261024"></a>
## 2026-10-24：回收25分钟恢复诊断，继续O09-1状态文件回放

用户要求确认诊断是否完成，核对后更新计划并继续下一步。`recovery_probe_25m`已在UTC05:18:18（北京时间13:18:18）结束，两个阶段真实退出0；1500秒阶段目标实际1500009ms，14975条Qq连续收齐、1510次逻辑查询、1661次计划发送对应peer实际21593字节、151次故意不回首答、24次重连及25次连接。终态无待写/在途、关闭状态清空，原始wire/时序/冻结SHA正独立重核，最终审计在本段追加。当前没有重启该任务或改120ms/一次重试规则，旧8h失败及报告偶发原因继续保留。

下一可执行离线切片为O09-1：只把原Qj状态回放接到统一窗口已有状态区。读取根/局部约定、模块与文件技能、原`frmmain.cpp::slotTimeTickerOut()`文字/配色和正式`StatusSession/FrameDecoder`；新增纯QtCore逐步文件回放，不引入socket/线程、旧连接配置/生产库或仪器动作。单一主壳仍负责接线，独立核心/边界用例与本次25分钟审计分开文件范围并行。

明确输入→输出边界：显式绝对匿名timeline，schema/动作/整数时间/hex白名单及容量上限；记录时间是虚拟协议时间，250ms仅为展示步频，不作为响应实测。仅仪器操作第7页的原81px状态文字呈现“回放/…”和来源tooltip，其余页保留当前业务状态；型号/耗材实际状态未知，原开始/暂停/告警/维护仍禁用。回放值绝不写入`AccountSession.actionReady`或发送任何物理命令；EOF/stop/断开/超时清空有效显示。新增编译输入须进入新冻结，旧O07/O08包及所有证据/草稿保留；源码、运行数据及系统配置无不可恢复覆盖，不额外备份，不自动提交或推送。

### 25分钟终态独立核对通过

[终态审计](../04_references/sn100_host/protocol_timing_20261024/recovery_probe_25m/terminal_audit.json)及单独SHA清单已归档；原有阶段清单不改。公开与temp原件字节一致，27项冻结源码、原binary和私有冻结binary一致，95份证据指纹一致。两阶段wire共**38334条事件**逐项重读，独立核对查询literal金字节、所有应答帧头/长度/分片/载荷/XOR/尾、解析字段、Qq连续序号、字节守恒、重连及关闭；恢复阶段实测**1500.009秒、退出0**，24次重连、25次连接、14975条Qq全收、1510次查询全部完成，1661次发送的peer实际RX为21593字节。没有待写/在途残留或不明取消字节，12.022秒前置阶段也退出0。

资源按1秒独立复算1492个样本，无采样错误：RSS/HWM峰18960KiB、PSS峰15790KiB、线程1、FD峰9、CPU约1.123877%单逻辑核。首/末600样本平均RSS为18788.607/18960KiB，PSS为15612.902/15789.763KiB；只能说明本轮观察值，不据25分钟否定长期增长或放行RK预算。日志7902727字节、2段，全部保存。

时序环共420812事件，保留sequence412621..420812，collector约1470.823125479..1500.009955006秒，**仅尾部29.18683秒**，覆盖412620事件。因此旧1150秒附近现场已经不在环里。尾部log write/flush/progress commit最大分别0.069168/3.189259/44.907382ms；不能外推全程IO上界。全程摘要心跳最大403.389088ms，但没有该心跳样本的时间戳；全wire在277.327..277.723秒有396ms间隔、同处一次查询约410ms，详细时序已覆盖，不能归因为同步IO、TCP丢包或pump阻塞。查询总时延含调度和有限重试，不等于单次入队后的120ms预算。

结论仅关闭本轮25分钟诊断与证据核对子项，**旧1150.178秒失败根因仍未知，8h未通过**，未修改超时/重试以求通过。PID371/372/373在后来O09构建中已复用，审计按启动时间和实际命令区分，没有把旧PID存在误判为长测仍运行或停止新进程。job_status的`target_progress`保留的是终态前缓存，判断结果以两个completed_stages、各自summary/verified/原始日志和实际退出为准。

### O09-1当前实施

纯QtCore回放核心已实际**30 passed/0 failed/0 skipped、506ms、退出0**，包括init/cleanup；≤1MiB/256步/每RX8192字节/总RX128KiB/24h整数记录时间、拆粘包/坏包恢复、120ms一次重试、静默、重连清半帧及EOF/stop等均有用例。[独立核心结果](../04_references/sn100_host/status_replay_20261024/core/verification.json)与14步匿名fixture可重放。最新合法Qj是模拟观测，不是旧请求成功或仪器ACK；迟到后新观测可fresh，但不改变真实业务操作条件。统一UI、累计回归与三目标交付仍在本轮执行，后续结果继续追加。

<a id="o09-status-replay-result-20261024"></a>
### O09-1最终离线交付：状态文件回放接统一窗口

25分钟恢复诊断已按上一节收口，本轮继续实际完成独立可做的统一状态接线。源码/计划由主窗口统一写入；Windows构建、六store实际产品资源验证和最终发布审计分别交子代理，构建目录互相隔离，资源段等待本轮构建、QtTest及审计IO结束。保留已有未提交成果、数据和草稿，没有提交或推送Git、部署RK、连接仪器或运行参考原工程。

正式维护入口为`protocol/status_replay.{h,cpp}`、`shell/device_status_shell.cpp`及`main.cpp`的显式`--status-replay`。全部文件在建库/建窗口前校验；六个存储就绪后每250ms推进一条，协议时间严格取文件`at_ms`，不是实测响应时间。第7页原状态区显示“回放/…”及来源/虚拟时间tooltip，其他页面恢复各自业务状态，后台医院状态不覆盖回放。日志分别标记`STATUS_REPLAY_PLANNED_TX`和`STATUS_REPLAY_FILE_RX`，计划TX不发送，没有接入socket/client/transport或原首次收包的同步/机械联动。断开、超时、静默、EOF和stop使旧快照失效；迟到合法帧只作最新模拟观测，不回转旧请求超时，不更新账户`actionReady`或启用设备、告警、耗材按钮。

**界面依据与差异：**按原`slotTimeTickerOut()`及状态枚举对照文字/颜色，保留原状态区81px几何、字体、导航、1920画布和滚动。仅增加回放前缀及来源提示；文本过长按原宽度省略，全文放tooltip，避免把模拟状态显示为真实仪器就绪。暂停颜色核对后由初稿红色修为既有非敏感默认主题蓝色`rgb(42,118,242)`，并加专项断言；绿/黄/错误/未知色按源码映射保留。原Windows活动主题与同状态截图仍缺，不能据源码/WSL宣布完整原版视觉通过。仪器操作页主体仍是脱机占位，并未开发设备动作。

| 验证 | 实际结果与范围 |
| --- | --- |
| 核心 | 30/0/0、506ms、退出0；输入边界、半帧/坏帧、断开/重连/迟到、EOF/stop均有用例 |
| 最终GUI05 | offscreen 5/0/0、4627ms；XCB 5/0/0、4461ms，均退出0，运行前后最终native/测试/fixture SHA一致；3个业务方法及init/cleanup |
| 项目/试剂/样本累计 | offscreen 7/0/0、7074ms；XCB 7/0/0、7044ms，均退出0；引用/权限、错误启动、样本预设与完整启动关闭，未重复旧60秒mixedLoad |
| 原三页累计 | 写入、重开、实际产品退出、两尺寸、初始化失败五项分别独立进程，各3/0/0、退出0；本轮未重复旧60秒measureExperiment |
| 报告累计 | 原模板/医院名快照/PDF生命周期与legacy入口两方法共4/0/0、2392ms、退出0；未重复60轮长负载。旧`legacy.query()`偶发原因仍未知，未因本次通过关闭 |
| 构建输入合同 | 9项Python检查通过、退出0；实际ARM编译及冻结覆盖回放/session/frame codec，排除未链接transport/client。累计QtTest的定义、CXXFLAGS、include集合和750条头文件解析均核对一致后链接当前对象 |

原始图为[1600×900](../03_photos/20261024_sn100_status_replay_1600.png)、[1920×1080](../03_photos/20261024_sn100_status_replay_1920.png)及[结束失效](../03_photos/20261024_sn100_status_replay_finished.png)，另有[独立SVG标注](../03_photos/20261024_sn100_status_replay_1600_annotated.svg)，未改写原图像素。截图来自使用实际shell类的匿名QtTest宿主；下面的六store验证来自最终真实产品进程，二者不混同。

首轮核心测试的helper命名/QByteRef编译错误、GUI01错误地在GUI线程初始化测试store触发线程守卫、GUI02用QPushButton查找原自定义QWidget按钮均已保留原日志，修正的是测试驱动。产品线程校验及按钮禁用门槛未放宽。GUI04 offscreen启动早于最终native发布结束，不能据其成功确认最终产品；随后已在最终构建退出0后，用前后SHA绑定的GUI05收齐两QPA结果。

**三平台主辅与冻结：**

| 平台 | 最终main SHA-256 | 最终helper SHA-256 |
| --- | --- | --- |
| WSL Qt5.15.13 / x86_64 Release | `150c9f0335e564cc0cef367c82a876908afa1177657e3944864e01d12fd54bbc` | `cc1c0d4562f6a56670a7884a2875ecede77407ae5ee83e7db4587375b8001d3b` |
| Windows Qt5.14.2 / MinGW32 PE i386 | `3b02911a39613d3069c750063853c45f5d9b2a6c34f3e3824009d7c6deccd9d6` | `1bfd18364d86289a0fdd2532bf3060837d335be16a8820ad5374b03fe7a60060` |
| ARM64 Release | `8d195502b7c49270239abcc1e676434b8272ea503ee78fe1830e73af322ef508` | `22ee7dd87c3e12e3874314f4f8448d1c84ec277d7433d653126967828e565308` |

三个平台均先完成隔离全量构建；暂停颜色修正后，Windows严格核271项实际输入仅该CPP变化、main仅重编该对象、helper make=0且无需重编，初版pair完整保留。ARM按既有显式复用合同核编译器/flags/源码顺序/对象/生成源/26个UI头SHA，仅重编该独立CPP的obj62，123个对象复用后重新链接主辅；helper按正式脚本重编。最终prepare真实退出0、56项ELF离线闭包errors=[]。native最终前后323项冻结输入与当前源码一致。

[独立发布审计](../04_references/sn100_host/status_replay_20261024/final_audit.json)逐项重核570项指纹通过，包含Windows新旧pair及13项证据、native和ARM对象、包内容及当前源码。最终ARM包65,090,617字节、33普通文件/7目录、31项内部SHA；release ID `8bc2619940aaff1c8909b99acf4e93a487353e262725694b29332dfeae7baf6f`，包SHA `ecf43c4f600ab130a6e9f9f5b707d94e26a69a1f9481d9970c57978da52d9cb1`。正式压缩包、SHA与恢复范围归档[07_backups](../07_backups/sn100_host/status_replay_20261024/README.md)，受现有Git LFS规则管理；旧包保留。`offline_status_replay`只表示编译能力，默认启动仍不自动回放，外部输入文件不随运行包播种。

**实际六store与资源：**最终native以`--offline --accounts --history --catalog --samples --status-replay`和新的匿名数据根/XCB1600×900运行60.121秒，591个100ms样本，正常SIGTERM、退出0。回放14步/EOF fresh=false、六store全部关闭、`THREADS_STOPPED true`与`STATUS_REPLAY_STOPPED true`均真实核对，主辅/fixture前后SHA不变。临时bootstrap凭据已删除且未归档；匿名DB保留temp，正式证据仅有行数/文件大小，无字段值和凭据。

全程CPU为单逻辑核17.117%，包含冷启动/匿名播种、六store就绪后名义约3.5秒回放及其后空载；RSS/HWM峰260180/263728KiB、PSS峰246520KiB、线程峰41、FD峰56。最后29.916秒295样本CPU仍为单核16.045%，线程9–41、FD23–55，来源未据此归因。驱动最后目录采样229496字节/9文件；EOF日志前缀在独立驱动5.217秒采样可见，只是1秒采样的可见性上界。另测GUI14次同步状态到控件更新最大offscreen0.156907ms/XCB0.170094ms，不含排队、绘制、网络或仪器响应。

[60秒原始验证](../04_references/sn100_host/status_replay_20261024/resource/verification.json)和[尾段独立复算](../04_references/sn100_host/status_replay_20261024/resource/idle_tail_analysis.json)均保留。该负载不是持续60秒通信/业务负载，没有认可预算、同负载旧版对照或同板RT测量，不能宣称资源已放行；尾段持续CPU另做同版本有/无回放参数的短对照，结果继续追加，不盲改计时器或减少必要告警。

本轮只关闭O09-1离线功能/构建/基线子项。原Windows完整行为/活动主题及Windows GUI、RK实屏/实际依赖/输入、仪器/真实协议与动作、资源预算/同板实时性和长稳均待验；旧O05恢复根因及旧报告偶发保留未知。[统一最终索引](../04_references/sn100_host/status_replay_20261024/verification.json)汇总本轮证据。下一可执行切片为既有计划O09-2：显式自建本机回环后台只读状态，先核worker/协议/失败与退出，不猜真实设备参数或重启8h长测。

### 空载CPU观测的短对照（保留限制）

基于上述尾段16.045%单核观测，追加一次成对短实验，各12秒正常SIGTERM，没有重启25分钟/8h或修改产品。两组从已关闭的匿名runtime复制9个文件，源/两副本初始SHA一致，不再播种账号/项目或生成凭据；原目录及冻结主辅/fixture未变。无回放/有回放实际12.170/12.177秒、各118样本、退出0，六store与线程均正常停；前者没有`STATUS_REPLAY`，后者14步EOF失效并停止。

6–12秒观测尾段，无回放CPU19.449%单核、有回放24.606%；两组线程41、FD55。按100Hz内核计数，主要增量分别在主线程112/143ticks，XCB事件线程各2ticks，其他采到的线程为0。这里只识别CPU计数所在的线程，不定位具体回调/绘制栈或根因；短10Hz采样可能漏掉瞬态线程，不能据0ticks认定工作线程永远不运行。

[成对正式验证](../04_references/sn100_host/status_replay_20261024/resource_pair/verification.json)明确`visible_page_controlled=false`：回放就绪自动切第7页，无回放沿用默认页。因此这不是同页面的纯回放开销对照，不能把5.157个百分点差值归因于回放、主线程缺陷或统计显著性。无回放也观察到持续CPU，说明该现象不以打开回放参数为必要条件；仍不能据此排除本轮修改的其他影响。源码确认EOF执行`statusReplayTimer.stop()`，本轮未盲停其他计时器/动画或删代码求低数值。

下一卡O09-2已把固定同页面/同数据、主GUI周期回调/绘制栈取证列为首项，再接后台本机回环只读状态；预算/同板实时性仍待验。没有第三次重复资源运行。三份现有计划及README已同步本轮实际结果和该待定位项。

<a id="o09-loopback-cpu-20261024"></a>
## 2026-10-24：GUI空载CPU定位与统一本机回环后台只读状态

用户继续授权按“先定位GUI空载CPU → 本机回环后台只读状态接统一程序 → 离线验证通过后继续依赖满足的后续模块”实施，并保持原版颜色和布局。开始时保留O07/O08、O05时标及O09-1全部已有未提交成果；未启动原版、未连接RK/仪器、不猜外部端点，不重复8h或25分钟长测。

CPU阶段先固定同一匿名数据副本、Guest、第7页、1600×900/XCB及Release二进制，以事件/栈证据定位周期绘制，再比较相同条件下修前/候选的CPU、Paint/Timer及交互状态；诊断插桩开销不计作产品CPU基线。已确认原Material按钮840ms双向无限halo动画会逐帧请求update，即halo透明/不可见时仍可能重绘；这只是静态候选，须实测后才修复，保留原动画时相、颜色、布局及可见焦点/按压/勾选效果，不盲停计时器或告警。素材/业务源与构建产物分开，临时诊断和私有匿名运行副本位于`temp/sn100_host/o09_loopback/`，不归档运行DB或认证字段。

本机回环阶段仅接本轮显式自建127.0.0.1模拟端和实际分配端口，复用只读client/transport；socket/timer在所属worker创建和关闭，GUI通过queued值快照更新原状态区。保持120ms/一次重试及断线/迟到/代次/退出语义，不触发校时、配置/试剂同步、机械动作、任务下发或actionReady解禁。CPU定位门槛通过后才实施接线，实际功能、测量与构建结果继续追加。

### CPU定位、最小修复与同条件对照

正式Material按钮继承原版840ms双向无限halo动画；即使光晕透明、尺寸0、按钮禁用，`setHaloScaleFactor()`仍每帧调用`update()`。独立测试在修前`haloOpacity=0`时600ms实际37次Paint，按预定检查失败2/1，原日志保留。完整产品第7页诊断的10秒窗口，可见Guest注销按钮分别634/635次Paint，均透明、尺寸0且disabled；动画驱动各625次Timer、主时钟各10次。GDB实际调用栈为`QUnifiedTimer → QPropertyAnimation → qt_metacall → setHaloScaleFactor`，另一聊天独立取证亦捕获父阴影渲染路径；没有用线程名字代替具体原因。

仅为`setHaloScaleFactor()`增加可见/启用/halo可见/opacity和size均大于0的重绘门控，持续保存原scale值与动画时相。其他属性setter、焦点/按压/勾选/ripple及主时钟不改。修后独立offscreen/XCB各3/0：透明/隐藏halo、禁用及点击渐出后600ms均0 Paint，可见焦点仍38次/600ms。完整产品同10秒窗口的注销按钮从635次降至10次，动画驱动仍625次，主时钟仍10次；剩余1Hz刷新保留，未宣称整机0绘制。

无插桩实际六store产品采用同Guest、同第7页、1600×900/XCB、同已关闭匿名数据副本及同回放结束状态，各约20秒后正常SIGTERM/退出0。筛6–20秒稳定窗口实际跨度13.9888/13.8770秒，CPU为单逻辑核口径**18.51482% → 0.86474%**；RSS峰260892/258112KiB、PSS247611/244856KiB。因同目录另一个聊天接仪器页期间曾出现引用先于文件落盘，当前共享壳不能作为纯CPU对照；本次使用旧已验124个对象，仅替换Material一个对象并核其余SHA、flags相同，候选`bdb07703…`只作CPU因果实验，不冒充最终综合产物。主机非资源隔离，单次短对照不外推RK、预算上界或长稳。修前/修后原图除明确时钟矩形外0差异像素，原图未改写。

[CPU证据](../04_references/sn100_host/gui_idle_cpu_20261024/validation_summary.json)含74项SHA、原始计数/图/栈、独立测试、源/对象/flags与无插桩样本。首次notify拦截无调用、smoke误选原隐藏同名按钮、构建撞到尚未落盘的仪器头，以及分析脚本误把最后disconnect当作EOF等失败均保留/说明；分析脚本仅对已结束的原样本修正断言重算，没有重复before采样或伪造原始stderr。

用户明确授权向“梳理原版仪器操作界面功能”和“梳理计划并确定下一步”两个聊天协调共享源码和GUI时段。仪器聊天保留instrument模块、先完成接线后把公共主壳交本轮追加O09-2；自动跟进聊天停止重复Material/主壳/worker实施，保留其独立只读证据。GUI测试串行；最终共同冻结源的主辅native/Windows/ARM构建集中一次核验，模块功能和实物待验分别登记，不覆盖彼此成果。

### O09-2后台只读核心与统一入口接线

`protocol/status_loopback.{h,cpp,pri}`中worker在所属线程创建`QTcpServer(LocalHost:0)`，只连接本轮自己拥有的实际临时端口，控制器不接受外部host/port。复用既有`ReadOnlyClient(120ms,1次重试)`、`TcpTransport`和只读消息解析器，正常场景仅每1000ms发Qj、在途不叠加；匿名应答`Qj[4][SIMULATED][2][1][0][0]`不代表真实机型或就绪。原client/transport终态规则未改，迟到合法状态可以成为最新观测，但不能反转已失败请求。

状态按runId/revision/连接代次排队交付，未消费时只保留最新值；wire队列128条/65536字节，另有一个在途批次最多16条/16384字节，超限明确失败并停止，不静默丢原始证据。实际TX定义为本机peer实际read，RX为client实际read，每块≤4096字节立即以`STATUS_LOOPBACK_WIRE`记录hex；GUI转发日志另标TX/RX，不能把write入队当收到。停止先停轮询、关闭client/peer/server、撤回fresh，再退出线程；app.exec返回后可不依赖GUI ack回收，最终计数仍留存。

[独立核心验证](../04_references/sn100_host/status_loopback_20261024/core/verification.json)最终QCore **13/0、2835ms、退出0**，覆盖金字节/1s轮询、全部socket/timer线程归属、半包/坏包、120ms/一次重试、迟到终态、重连旧半包、在途取消、stop/start、worker延迟时主线程心跳、有界trace超限、无GUI ack退出与实际wire计数；最终构建/运行无QObject线程或编译警告，首轮clock-skew提示另留。独立证据不代替统一窗口与最终跨平台验收。

主程序新增显式`--status-loopback`，与`--status-replay`互斥、在建库前拒绝冲突；存储SHELL_READY后才启动后台回环，SHELL_READY不表示模拟或仪器已就绪。GUI以“模拟/…”在原第7页状态区显示已确认字段和本机单调时标，回放继续用“回放/…”及虚拟记录时间，两者共享原主状态/LED文字和颜色映射；旧回放公开接口/属性保留。仪器页通过通用只读值接口展示已确认四类字段，故障/记录超限/停止清旧值，不猜传感器，不改变账户actionReady或解禁设备动作。其他页面后台更新不覆盖该页业务提示。

首次综合链接漏列`read_only_messages.cpp/.h`，出现真实undefined reference；已补齐native/ARM/冻结/输入合同，原失败日志保留，未用旧产物冒充。随后348项正式程序输入前后相等的native主辅构建退出0，主`0ac098cb…`、helper`cc1c0d45…`；这是本轮中间门槛版本，最终共同源另冻结。该真实程序offscreen新匿名副本、七store、只读回环5秒启动检查退出0，实际wire10条/260字节、七store关闭、`STATUS_LOOPBACK_STOPPED true`及`THREADS_STOPPED true`通过。首对真实字节为TX `02 03 00 06 00 00 00 01 51 6a 3a 04 05`，RX业务为上述SIMULATED状态；完整帧/log保留temp，统一窗口专项继续执行。

### O09-2统一门槛通过与后续版本边界

统一专项[正式证据](../04_references/sn100_host/status_loopback_20261024/shell/validation_summary.json)：offscreen **5/0、3311ms**，XCB **5/0、3562ms**，均退出0。覆盖原色/几何及两尺寸、fresh/error/overflow清旧值、四类已知仪器字段/传感器未知、真实后台wire和重连/stop-start、切页不覆盖业务状态、实际七store与正常SIGTERM回收、互斥参数建库前退出2。初轮驱动把原CusBadgeButton当QAbstractButton查找而失败，修测试类型后复验；未放宽产品禁用或状态断言。测试驱动/MOC按Release编译，链接该次实际136个生产对象（仅排除main.o），137对象/348源及主辅SHA前后不变；中途停止的重复全量测试编译不记通过。正式40项SHA只收源/日志/图/flags/分析，不收DB、凭据、对象和可执行文件。

O09离线门槛通过后，仪器聊天复制了相同348项程序快照及完整468项私有源码检查点，独立做Windows/ARM/累计和资源收口；本聊天随后开始O02-2本地登录。该检查点包含CPU/O09/仪器页，但不含后续登录；后续新源必须另外构建和验证，不能拿检查点产物宣称登录版本通过。两个版本均保留，GUI时段继续串行。

<a id="o02-password-login-20261024"></a>
## 2026-10-24：原版本地密码登录卡接统一入口

按用户“离线验证通过后继续后续模块”授权，在O09-2核心/统一门槛通过后推进现有账户设置尚缺的本地登录。原`maskdlglogin.ui`逐字节提取，SHA `588813687e12b880abe7f4e83b625a2a2a1b951c6af425446db07aa888d336ab`，保留白卡/蓝按钮、字段/顺序/遮罩动画；公开主题颜色复用已有账户页，原动态公司名/活动Windows主题未知，未读取参考配置或生产库。

入口为原用户头像→`shell/login_shell.cpp::requestLogin()`→`MaskDlgLogin`异步卡→账户worker的`loadLoginDefault()/authenticate()`→专用request-id回执和`sessionChanged`。SQL绑定用户名与密码，角色0–5；空密码忽略用户名/记住勾选、回Guest；错误保持旧身份和掩码输入。角色≤4且勾选记住才按原用户名范围设置默认，管理员不改；默认更新改为事务，失败回滚默认标记，但认证成功以独立警告告知。相同凭据多行明确拒绝，避免原无ORDER BY任意身份；默认用户名预填固定id顺序，是有意差异。完整来源/差异见`ui/login_origins.json`。

专用登录信号不触发隐藏账户页CRUD回执，SQL/工作线程归属不变。对话框及提示卡用parent拥有的异步open，避免新嵌套exec；旧CusMessageBox四个同步helper不改。请求号跨弹窗不复用；加载/认证/提示/关闭动画期间禁止重复提交/关闭，Esc沿用原版不退出。主壳在打开前先检查全部页忙态，再询问隐藏草稿；取消保留草稿和身份，登录失败不预先注销。登录卡可见期间切页/注销/主窗关闭拒绝，结束后恢复原导航。

显式`--password-login`需要`--accounts`，建库前拒绝缺依赖；全部模块首读成功后启用头像，默认仍Guest，旧未启用模式保留。集成启动器和后续新包显式启用该卡。身份新增Guest/Simulated/Password来源供提示区区分，认证只改身份，不改变actionReady，不迁移原首次登录的校时/试剂/配置自动联动或设备动作。原`pwd VARCHAR`兼容仍为直接文本，生产凭据安全/导入策略未验。

独立两QPA首轮各12/0；首编Makefile未来0.27秒提示保留，另force全量小目标重编，二进制与首轮相同且无尾clock-skew警告。最终offscreen **12/0、6418ms**，XCB **12/0、6581ms**，退出0；[独立18项SHA证据](../04_references/sn100_host/password_login_20261024/independent/verification.json)包含十个业务方法、33个源/资源输入、原图及两个后端的短资源观察，不含认证DB/凭据。XCB约6.67秒QtTest含fixture/交互/受控锁等待，CPU13.26%单核、RSS141912/PSS127319KiB，不是统一产品预算或空载对照。

新native采用独立缓存目录，qmake按新头依赖实际编译33个对象，再链接主程序；353项源前后相同，main `2ce1df6cb0be38085e1e5834fd0037a43bd906c6e87d451a06e4e25cbbfc61c9`，helper `cc1c0d4562f6a56670a7884a2875ecede77407ae5ee83e7db4587375b8001d3b`。独立审计核140个明确对象目标make-q=0，全部19个AccountSession头依赖已真实重编且mtime晚于头，22个变化依赖对象都有编译命令，其余107个缓存对象SHA相同；不把旧ABI对象直接混用。原clock-skew提示保留，默认make-q=1仅因脚本已把staging产物发布移走，make-n无CPP/MOC/UI待生成，并非忽略潜在漏编。ARM输入合同11项通过。

[统一登录专项](../04_references/sn100_host/password_login_20261024/shell/verification.json)offscreen **5/0、8863ms**，XCB **5/0、8797ms**，退出0；驱动只新编QtTest源/MOC，链接139个新生产对象、排除唯一main.o，140对象/APP前后SHA未变。旧模式头像禁用、显式模拟来源；启用后原卡两尺寸、空密码Guest不改默认、Esc和空闲关窗；正确/错误重登录、role4→2、actionReady前后均false、隐藏医院草稿取消/放弃、数据库忙拒打开、提交期重复/切页/注销/关闭拒绝、隐藏账户CRUD不被登录回执污染通过。真实新产品七store+回环+密码登录正常退出0，缺accounts负例退出2且不建目录。原始session_guards.json的`action_ready_preserved:false`存的是保留后的动作状态值false，不是失败结论，前后不变的断言已通过。登录截图为对话框自身grab，不用于证明整桌面背景合成或原Windows主题。

当前新源[累计验证](../04_references/sn100_host/password_login_20261024/cumulative/verification.json)十组全部退出0：五个核心方法（写入、独立进程重开、viewport、初始化失败恢复、真实applicationExit）各3/0；项目/样本7/0、文件回放5/0、本机回环5/0、仪器页10/0、报告两方法4/0，无跳过。六个driver只编自身CPP/MOC，139个当前native公共对象及各实际源前后SHA相同，QtTest增加的宏/头/库与生产flags区别明确登记，运行后353源仍与native冻结一致。本轮累计用XCB执行含物理屏幕命中的viewport；未将offscreen不可用命中算通过，未重复measure60s、60轮报告或长测。旧报告偶发与O05恢复根因继续未知，不因本次通过关闭。

新源[实际产品资源](../04_references/sn100_host/password_login_20261024/resource/verification.json)采用Release/XCB1600×900、Guest、第7页、七个重开匿名store、1Hz自建回环Qj及已启用但空闲的登录入口，30.113秒/295个100ms样本、正常SIGTERM退出0。暂停本组所有编译、其他GUI及大文件复制/哈希后才观测；正常系统后台仍存在，不称WSL完全隔离。主辅/353源前后不变，七store、loopback与线程全部停；实际wire58条/1508字节，即29对Qj实际收发，不将write排队当收到。原匿名副本11文件/262292字节，正式只留行数/大小，不归档DB或凭据摘要。

全程含冷启动的单核CPU **6.3328%**，RSS/HWM峰274972/282384KiB、PSS峰257935KiB，线程/FD峰43/61。另固定筛`10 <= elapsed_seconds < 30`的195点、实际19.8078秒段，CPU **2.6757%**，RSS274972、PSS257924–257935KiB，线程43/FD61。本组是七store+仪器页+持续1Hz模拟通信的新版本负载；前述CPU因果实验是六store固定回放终态，两者不能作单模块开销差或直接效果对照。预算、GUI排队/合成响应、RK温频/同板控制和长稳未放行，不因这次短测再调低必要刷新。

初始计数脚本只匹配`.db`，遗漏`history/history.sqlite`；保留原5份raw和verification字节，另在exit0后只读该精确匿名文件的结构/行数，未读字段值或hash/copy库。补充[分析](../04_references/sn100_host/password_login_20261024/resource/history_rows_analysis.json)为offline_metadata=1、report_columns=6、report_data=6、report_results=11、report_values=16，资源安全清单从8追加为10项并全部读回通过；这是扩展名过滤的取证补充，不是重跑、数据增长或新功能结论。

### 新353项版本的跨平台构建、冻结与恢复

两组串行资源观测结束后才启动Windows/ARM构建；[平台证据](../04_references/sn100_host/password_login_20261024/platforms/verification.json)已封存85项SHA。Windows fresh全量140个主对象/31个helper对象、0复用；ARM fresh全量75个正式源+57个moc+8个qrc，即140个主对象，另编2个helper专用对象，原正式prepare及最终路径离线闭包均真实退出0。353项源与native前后相同，工具链/外部输入核对通过，56项ELF递归闭包errors=[]。Windows仅验证PE32/i386构建/imports，ARM仅交叉构建/静态闭包，均不代替对应GUI/板端执行。

| 平台 | 主程序SHA-256 | helper SHA-256 |
| --- | --- | --- |
| WSL x86_64 Qt5.15.13 Release | `2ce1df6cb0be38085e1e5834fd0037a43bd906c6e87d451a06e4e25cbbfc61c9` | `cc1c0d4562f6a56670a7884a2875ecede77407ae5ee83e7db4587375b8001d3b` |
| Windows32 Qt5.14.2 MinGW | `56e3857aedf816335fa5ab16bb822cac6f52e25c475f75e6abe3380331709df4` | `0001cee00a743ec3772bce81933b3cc199da5b6aebde3480b2ba33eade38ea82` |
| ARM64 Qt5.15.13 Release | `1be8c669c3b7c6be3b169728cf29da953383606cccf67ca908d1caa4dbb56332` | `0c49bad1c91d8b875cb4799dc9162e18c7deb74edeef984882238e4307c9b71a` |

ARM后验首轮要求源码与对象参数位于同一xtrace行，obj_20遇并行交错，wrapper真实退出1；编译、prepare和closure已各自通过。保留原日志/脚本/Traceback，不修原始输出；改用142个既存对象的ELF64 REL/AArch64及DWARF真实源名、GNU C++14 13.3.0、-O2/-g和fresh目录时间独立配对，再核原日志全部输出路径、源/对象SHA。第二轮后验摘要因临时变量遮蔽又退出1，提前写出的0字段未被接受。修临时脚本后同批只读复算退出0，再原子发布退出0，实际阶段为`[1,1,0,0]`；未改业务源、对象或包，未重编。原同行命令不完整明确留false，不伪称已恢复完整原命令。

Windows保留22条既有警告：原仪器ui重复layoutWidget17、主辅QtRPT未用band变量4、原MaterialScrollBar Q_D1；ARM19条为同ui17及QtRPT2。对应源码与旧检查点相同，不为消警告改原UI或清理第三方；不称零警告。native的clock-skew及对象新鲜度独立核验亦如前保留。

正式[07恢复归档](../07_backups/sn100_host/password_login_20261024/README.md)含新ARM包68,534,566字节，release ID `f558ff278eb332b1c304ac8f69843bd6951830136ee43ce2c2a5c91702cc48a2`，包SHA `b8fa6f4c8728c7b03ef8aa76ad29560fe6fa0e65b4e82662784f55f66e6f1660`；包内33普通文件、31项校验。另存严格353成员构建输入源码包910,175字节，SHA `55a511242e9c6f3ba85056d6b89e60eef833b6d0064c292583fba8c0127cec28`，及source_snapshot.json，逐成员SHA等于当前冻结。目录6文件/5项SHA全过，两个gz均核Git LFS规则；未自动提交/推送。恢复范围是当前应用/私有依赖/字体/模板和该版构建输入，既有业务库/系统配置不在包内，不替代运行数据备份。

[最终总索引](../04_references/sn100_host/password_login_20261024/verification.json)已独立复核并真实退出0，stderr为空；根186项清单再次读回退出0。五子目录18/32/35/10/85项，加五个子清单和本索引共186；另核七个交付物、353成员源码恢复包、139个累计生产对象/8个测试驱动，以及历史CPU/O09的74/8/40项安全证据。总索引SHA `9f753985afdbe5efd873ff29db2a7a98d47f9d74f1800d263f949135c06bdac0`，根清单SHA `f3088d619d536125c632c48d042ebbf5679a20acb9a5ed38e05aad3007c53eb5`。真实命令、stdout/stderr/退出码在`temp/sn100_host/o02_login/final_collection_01/`；未把提前写的JSON当成功依据。

旧348项仪器检查点保持原位置，不当作新353项登录版本；后续修改源码需另冻。当前只关闭CPU修复、O09-2与O02-2已验离线子项；原Windows完整行为/活动主题、生产凭据安全、RK实屏/输入/实际依赖、仪器与真实任务、预算/同板控制和长稳继续待验。本轮不连接RK或仪器，不覆盖已有匿名目录，不归档运行账户DB、密码或密码派生摘要，不自动提交/推送。

另一协调聊天完成了[全局静态盘点](../04_references/sn100_host/global_coverage_20261024/review_summary.json)，覆盖F01–F18、43项操作事实，61个读取路径SHA及旧348项检查点；只是源码覆盖依据，不冒充运行验收。剩余打印机/语言/LIS设置、项目高级方案、项目配置→结果/报告列、报告曲线/修改/重算/上传、校准/质控和告警等缺口及前置条件已并回同一四段计划。核查当前CusRackListWidget已接原QScroller左键手势/ScrollPerPixel，README原“尚未接入”描述已更正；这次只纠正文档，不称新做或触屏已通过。

<a id="monitor-checkpoints-20261024"></a>
### 2026-10-24：本监控独立核验两个完成批次及全局差异

本轮先核聊天实际终态、同仓库活动方和真实构建/GUI进程；两个交付窗口确认本轮无活动写入，未因idle直接推定业务完成。保留所有未提交成果，不重复运行既有测试、构建或资源观测。仪器348检查点独立审计见[固定证据](../04_references/sn100_host/monitor_audits_20261024/instrument_checkpoint/verification.json)：232项公开清单、8项备份、468源成员/348编译输入、六个主辅和56项闭包一致；真实store11/0、两QPA各10/0及七store正常关闭/30.134秒295点资源留证。旧外层驱动exit1和早期失败保留，累计退出证据按实际协调器粒度登记，不凭后写JSON补造独立OS收据。

新登录353检查点随后另行完成[独立审计](../04_references/sn100_host/monitor_audits_20261024/password_checkpoint/verification.json)，索引SHA `74f36bd7c6f743392705a95e5fdf6b5d970ce01b3e38d79c55b84f9746dd95b4`，四份审计文件清单读回通过。186项公开清单、正式备份、六个三平台主辅、353成员恢复源码包、ARM33文件/31内部SHA及56项闭包与当前源一致；142个ARM对象的DWARF另行核对。登录独立两QPA各12/0、统一各5/0有原始QtTest文本与真实退出0；本批没有XML，十组累计按严格协调器支持的通过登记，未声称有每调用独立退出文件。重算资源为30.113秒295点，10–30秒CPU2.6757%单核，RSS274972KiB/PSS257935KiB、43线程/FD61，58条真实本机wire、七store/回环/线程正常停止。仅离线兼容行为，不关闭生产凭据安全、Windows活动主题/完整行为、RK/仪器、预算/RT或长稳。

对照原F01–F18的43项操作事实，本轮353与348源清单差异为新增5项、改变12项、删除0；新增是本地登录及其接线/构建闭包，其他功能缺口没有因本批编译通过而关闭。原计划已补齐报告删除/列排序/HTML、样本ResetErr/ResetFin本地草稿、试剂ISI/normalValue、应用启动/单实例、生产导入/schema/多store运行数据恢复及分类轮转/崩溃诊断，并将校准/质控本地页面CRUD与真实计算前置拆开。七组漏项修订已复读，无新的实质偏差；首次旧文档审核JSON仅作修订历史，不重复报已解决遗漏。

<a id="o10-printer-settings-20261024"></a>
### O10-1开始：原打印机本地设置

在新353审计收束且原开发窗口明确交接后，本监控认领正式源/主壳/同一记录与计划单写，其他窗口只读。按全局缺口选择设置子页3，保留353主/源码包与全部历史证据原字节。原`frmuserset/frmprintersetting.{cpp,h,ui}`为只读名称、设备列表与Apply，原键`printer/name`、UTF-8；原无角色门槛、刷新或取消按钮，不虚构管理员权限。原业务框`(390,20,681,821)`和各控件几何保留；字体沿现有Linux必要替代、活动Windows主题待验。

本切片使用显式匿名根的`printer.ini`及专用脱机schema，worker枚举/读写，保持名称原文。原页未检查QSettings同步状态，本移植改为实际字节读取/唯一临时INI解析、零等待协作锁、提交前源字节复核和QSaveFile原子提交，成功后才更新提交快照，并保留无关键。离页继续编辑/放弃为统一程序安全补全，不加业务区域。原报告仅`printExec(true)`未传该名称，当前仍是脱机预览/PDF；本切片不修改CUPS/系统默认设备或执行打印，勾选见[当前卡](../01_docs/plans/sn100_host_migration_plan.md#o10-printer-settings-card)。

### O10-1实际结果、原失败与冻结

[最终总索引](../04_references/sn100_host/printer_settings_20261024/verification.json)SHA为`6d1c814dab7fba5147d584177ddbf4fb9fd14df8f3e131b2fd27cf20199d9874`；361项最终源清单SHA `119cfab52d54bd61d4fb765a7666a8dddf74d2e110baf1a4207eab1d68cef83f`，基础309项公开清单已读回。源/入口、原Qt页面、实际测试及退出、对象/driver、三平台主辅与ARM主/源码包分层核对；不是完整SN100或实物放行。

| 已验切片 | 实际证据及边界 |
| --- | --- |
| 独立设置页/worker | offscreen/XCB各14/0、真实退出0；保存重开、空列表/失联原值、非法/不可用名称、原路径/锁失败与重试、祖先链接替换、同size/mtime外改、枚举期间冲突、无关键保留及GUI完成屏障通过。QtTest数字含init/cleanup。 |
| 统一最终入口 | 两QPA各5/0；最终子页3真实替换，六角色、原设备动作条件不变、草稿保留/放弃、busy时切页/登录/注销/关闭守卫、独立重开、带/不带参数及坏配置真实退出通过。1600×900外层滚动后Apply中心可达、1920×1080及两档截图留证，没有重排原业务框。 |
| 原功能累计 | 新361源登录两QPA各5/0；五项核心各3/0、项目/样本7/0、文件回放5/0、本机回环5/0、仪器10/0、报告4/0，每调用独立OS退出0。绑定144个实际生产公共对象，driver新增QtTest宏/库不重编业务变体；原失败未抹除。 |
| WSL构建 | 初次145主对象/31 helper全量构建退出0，随后为无关键保留修一个独立CPP、强制重编该对象并重链；其余144主对象/flags SHA相同。最终主`598ca581…`、helper`cc1c0d45…`，不把初轮旧CPP指纹冒充最终源。 |
| Windows32 | 主145 fresh对象、helper31 fresh对象及PE32/i386通过，主`f172a67d…`、辅`5d6e7aa7…`。主编译后慢PS5解析获准中断，PS7只读接续、make-q真实0且244主产物字节未变，没有重复主编译；helper及后验真实0。原主fresh make单独退出文件缺失，仅严格原控制流证明；主外部头原prebuild哈希未恢复，不冒充全量预后相同。顶层环境失败1/取消-1/接续0均保留。 |
| ARM64/恢复 | fresh147对象（78正式源、59 moc、8 qrc、2 helper）逐项DWARF/flags对应；构建、56 ELF离线闭包和打包真实0。主`9fae2c17…`、辅`e818a52f…`，旧sysroot只读复用；新应用33文件/31项SHA及361成员源包逐成员一致，已归档[07恢复目录](../07_backups/sn100_host/printer_settings_20261024/README.md)，两个gzip核LFS且无忽略。未执行目标ELF或连接RK。 |
| 输入/生命周期合同 | ARM输入合同12/0。native-only生命周期原19项通过；新增打印capability用例首轮仅测试局部staging未定义，修后单项1/0，未重复原19。旧包不传新参数，新cap包会传；只用本机C夹具和mock，不是RK/网络验收。 |

首轮独立INI失败定位到QSettings原子替换临时路径后旧描述符仍为空；首次修补误选open位置的失败也保留。进一步只读审查指出路径替换、同mtime缓存及无关键丢失，最终以实际字节/协作锁/原子提交和保留原键值修复。独立run06的一次重试未更新同样留档：确定存在worker QSignalSpy DirectConnection早于GUI恢复的观察缺口，测试改等GUI `operationFinished`并核两次`requestSave`，没有放宽生产busy语义；失败轮缺时序，单独-vs复测通过不作为唯一事故根因证明。

统一首轮90秒超时由受控阶段/信号确认是测试夹具在已有账户库再次给bootstrap，触发原账户防重播种弹窗；修为仅新库给临时随机值并等待身份后的GUI重载。后续CLI新库用例遗漏一次性bootstrap也按原规则退出3，补齐夹具后通过。一次诊断与driver重编重叠且GDB附加不可用，已明确排除验收，未提权或改系统。全部原文/真实退出与限制在[失败记录](../04_references/sn100_host/printer_settings_20261024/failures/verification.json)，认证运行库/秘密及其摘要不归档。

### O10-1响应、资源与全局接续

在本轮所有构建、GUI及大文件处理结束后，单独一次40次匿名交替保存：XCB实际生产对象驱动、固定两名枚举无人工延迟，QtTest3/0、退出0。从GUI排队click到busy信号p95/max为**0.028662/0.046227ms**；到GUI已保存数据状态p95/p99/max为**123.364365/133.080784/133.080784ms**。546个5ms timer间隔p95/p99/max为10.597076/10.771369/12.927292ms，仅是QtTest事件循环代理；不包含OS输入、实际paint或CUPS枚举耗时，也不把候选100ms可见反馈门槛套给保存完成。[原始响应](../04_references/sn100_host/printer_settings_20261024/response/verification.json)

随后一次实际整程序Release/XCB1600×900、Guest、第7页、七SQLstore加设置worker、1Hz自建Qj及空闲登录入口，显式新匿名catalog夹具，**30.157927925秒/297个100ms样本**，普通SIGTERM/真实退出0。七store和设置各闭一次、线程/回环停、临时bootstrap删除；实际29TX+29RX共58事件/1508B，序号及金字节已独立核验。10–30秒尾197点跨度19.88607秒，CPU **2.313177%单逻辑核**，RSS286628KiB、PSS269307–269318KiB、44线程/FD62；全程CPU5.45808%包含冷启动，HWM293240KiB。只统计匿名表行数/文件大小，不读认证值或归档运行库。[原始资源](../04_references/sn100_host/printer_settings_20261024/resource/verification.json) · [独立复算](../04_references/sn100_host/printer_settings_20261024/resource/final_metrics_audit.json)。不与旧353不同输入直接相减推断纯打印开销，不算预算、RT或长稳通过。

本批再次做F01–F18[全局差异复核](../04_references/sn100_host/printer_settings_20261024/global_delta/verification.json)：353→361新增8文件、改11、删0，既有所有UI字节及各原业务目录输入未变；打印子页3已接，语言2、LIS4及校准/质控顶层3/4仍为最终占位，原业务具体缺口继续留在同一计划。修复前printer wiring审查只作历史依据，其中无关键问题已修，不能机械当作新TODO。

下一已核源、依赖满足的切片为O10-2 LIS本地设置：原9个可编辑键、12隐藏键必须保留；原Save→signalConnectLis→openComm会打开TCP/串口，离线迁移须完全隔离，自动模式只保存意图，串口缺失/切模式草稿不得自动猜选。33份源码事实已归入[同一下一卡](../01_docs/plans/sn100_host_migration_plan.md#o10-lis-settings-card)，未声称LIS页面/HL7已开发。不自动提交/推送；原Windows活动主题/完整行为、生产安全、RK实屏/输入/实际打印插件及服务、真实打印/仪器、磁盘满/断电、资源预算/同板实时性与长稳继续待验，CUPS挂起枚举的有界退出仍待验。旧报告偶发与O05旧恢复根因保留未知，未重启长测。

<a id="instrument-offline-20261024"></a>
## 2026-10-24：原版仪器操作页面与人工维护离线接入

用户在本聊天要求将仪器操作界面先按原版接入，能离线做的先做，颜色和布局保持原样，真正控制仪器的部分等有设备再验证。本轮按根/局部AGENTS及模块/文件Skill，核对当前计划/相关记录并保留全部已有未提交成果。只读参考`frmdriverset`及必要资源/三维护表，未运行/构建参考原件、未读旧连接配置/生产库，未连接RK或仪器。

范围：逐字节提取原1751×968的`frmdriverset.ui`及7张必要原图；原8个子页与角色入口不重排，沿用既有壳的原动态主题/灰色侧栏及按钮样式，原Windows实际活动主题与同状态截图待验。未知仪器字段清成未知，软件版本明确为离线移植版；只呈现已核实的状态回放字段/来源，温度/液位/机型子系统映射不猜测。所有真实连接、洗针、泵阀/针/抓手/扫码器、通道开关、标定、自测/任务与设备打印动作保持禁用。

人工维护入口→确认人工已完成→worker内身份/动作/当天日期白名单校验→事务提交→按月重新读取→显示勾选；6项分别为日维护waste_cup/waste_water/waste_cup_box、周维护clean_pool/clean_dirver、月维护clean_needle，保留原拼写/表结构。long_clean和shut_down仅留设备流程待验，离线不能登记完成。独立显式数据根下`instrument/maintenance.db`，QtSQL连接归维护worker，读写/关闭不跨线程；未知动作/Guest/坏库/锁库返回真实错误，业务区不新增实验横幅。

新增范围为正式`instrument/`源码/UI/资源、`shell/instrument_shell.cpp`与专项测试；主壳`--instrument`及集成启动脚本接第7页、维护store首读就绪/退出、会话/忙态守卫与回放值转发，ARM显式输入/快照清单同步。未删除已有业务代码/原件、未更改系统环境或迁移既有配置数据，因此无额外环境备份；不自动提交/推送。构建、两QPA运行、视觉/资源和待验结果在本节继续追加。

<a id="instrument-offline-result-20261024"></a>
## 2026-10-24：仪器操作离线检查点交付与验收收口

**结论：原仪器操作已接统一程序，本轮离线切片通过；完整实物模块未关闭。**保留原8个子页/控件坐标、颜色/主题和必要原图，六项人工维护可确认登记、按月查询、保存重开；真实连接、开始/暂停、洗针、泵阀/针/抓手/探测、通道开关及工程动作仍禁用。原维护打印未见完整业务槽，保留待原发行版对照；不发明打印或SN100传感器映射。

### 版本和入口

本轮与O09-2聊天协调单写共享源码和GUI时段，在其只读状态统一验证通过后冻结独立检查点：468项完整源码/测试/UI/资源，348项实际包输入逐项等于已验O09-2指纹；旧460项首版完整保留，23项增改按内容差异同步、445项未改mtime。之后正式目录继续开发密码登录等新源，本节三平台及资源只对应本检查点，不能冒充后续最新整机包。

入口是`--instrument`、`shell/instrument_shell.cpp`和`instrument/instrument_page.cpp`；worker启动/首读/退出在冻结的`shell/main.cpp`，规则/事务在`instrument/maintenance_store.cpp::record()`和`loadMonth()`。原三表名/日期列及`clean_dirver`拼写保留，人工登记只接受当天和六个白名单动作，存储层再次检查有效角色；`long_clean/shut_down`不允许人工伪造完成。月份查询用同一事务读取三表；有效脱机tag留下的空库可在首次建表失败后安全重试，已有不符结构不转换。

统一入口沿用默认Guest，显式匿名身份仅作脱机模拟；原页面从`frmdriverset.ui`逐字节提取，SHA-256仍为`4e0f24fa7e99e459a123bced7855d25399b2ce7f6e23cfb3f8cd2f36fb879f4e`。已补仪器UI/来源清单的Git字节保持属性。必要差异为QRC资源路径、Linux字体近似、未知仪器值和“离线移植版”版本文案；原Windows实际主题/字体及同状态截图尚缺，不据此宣称完整视觉等价。小屏保留原1751×968业务画布和1920×1080主壳，通过外部滚动访问，没有重画/重排。

回放和自建本机模拟共用只读值接口，来源前缀分别为“回放/”“模拟/”，动态文字按PlainText显示；只映射已核实的型号、主状态、灯状态、错误类型，失效先清旧值。未核实的温度/液位/子系统全部未知，不改变账户actionReady或解禁设备动作；维护反馈合并原状态区提示，不增加业务横幅。

### 功能、实际输入输出和原失败

- 存储Release **11 passed/0 failed**：六项人工维护、重复幂等、重开/跨月、Guest/坏角色/未知和机器动作/非法日期拒绝、锁失败恢复、空库初始化恢复；100份整月快照与11次独立连接三表原子提交无混表。253ms锁等待期间主QCore事件循环24次心跳，不能替代GUI绘制/仪器响应。[存储证据](../04_references/sn100_host/instrument_20261024/store/verification.json)
- 最终仪器界面 **offscreen 10/0、XCB 10/0**，连接136个最终生产对象且前后SHA不变：原UI/8子页、6角色、全部物理按钮禁用、6项确认/取消/持久化、月查询失败不留旧勾选、锁恢复、回放→模拟→失效来源替换、未知值及主壳忙态切页/注销/关闭守卫。[界面证据](../04_references/sn100_host/instrument_20261024/final_gui/verification.json)
- 本检查点累计：两QPA分别独立进程写入→重开读回、初始化失败恢复各3/0，状态回放各5/0；XCB实际viewport与产品关闭各3/0。产品关闭用例就绪后空载12017ms，再正常WM_DELETE退出0，该子进程仅3store；回放子进程仅医院store，不混称七store。未重复原60秒measure/8小时通信。[累计证据](../04_references/sn100_host/instrument_20261024/cumulative/verification.json)
- 原首版仪器offscreen为8/1：注销清身份后账户页仍异步刷新，诊断实际捕获account_busy=true及“账户数据正在处理，请稍后再退出”。只补测试等待各页真实idle，原忙态拒绝断言全部保留，产品守卫未修改；修后首版两QPA各9/0，最终新增通用来源场景后各10/0。原失败、诊断和driver差异保留在[首版证据](../04_references/sn100_host/instrument_20261024/initial_gui/verification.json)。
- 累计runner曾将物理屏幕命中用例用于offscreen，原2/1显示screenAt=0×0、widgetAt=none和raise不支持。修正临时运行合同为该用例仅XCB，原失败保留、产品及断言不改；XCB按原断言3/0通过，不将offscreen此项记通过。

### 三平台构建和离线发布

| 平台 | 最终主程序SHA-256 | 最终helper SHA-256 |
| --- | --- | --- |
| WSL Qt5.15.13 / x86_64 Release | `c5da853069071eedc12a601cb5cab8be0529e726196b8ce4d79667b7c232aeb3` | `a73acc08b9747a4d36d3fae7ecf2034a1880e750936e53fdbecac793c5603c89` |
| Windows Qt5.14.2 / MinGW32 PE i386 | `a5c2241ef62dd253d84ccbc07388a8ab6b9919f1dc91933ef41fb2f68e31c3a8` | `a0d8f5afd878c113c7f48ad4f340b175302762563cbfb335b58d7a1571feda05` |
| ARM64 Qt5.15.13 / Release | `2278c5fc769fb5b9f60a6f7ca54c3d8dfa0ea4e4564bbe34bf2424610fc692e4` | `6558bf9c791f3b78e88440e183a4cedef5e7102841fd4f1c8665490c050877f1` |

WSL/Windows先独立全量首版，再核相同flags、实际依赖和工具后增量17对象；两平台各151对象未重编，另有4个重编后SHA相同，155仅是同SHA数量，不能误写成实际复用数。WSL原Makefile比WSL时钟快0.19秒警告保留，传递依赖应重编集合与17条真实命令恰好一致、missing=[]。Windows295项实际正式输入、963项外部输入和工具均稳定，PE/imports核验通过。[native审计](../04_references/sn100_host/instrument_20261024/native/verification.json) · [Windows最终审计](../04_references/sn100_host/instrument_20261024/windows/final_update/windows_result.json)

ARM是单次全量137主对象加2helper对象，build.sh真实退出0、1362秒；56项ELF离线闭包/check真实0、errors=[]，SQLite/SVG满足。原UI重复layoutWidget、QtRPT未用变量及GCC对大UI调试变量追踪自动降级提示保留。外层PowerShell stdin driver末行CR导致其退出1，但真实build已0并发布成对产物，后续源/架构/SHA/check/package均独立通过；不抹掉外层失败，不重编有效结果，不部署旧产物。[ARM全部证据](../04_references/sn100_host/instrument_20261024/arm64/verification.json)

正式恢复输入已在[07备份](../07_backups/sn100_host/instrument_offline_20261024/README.md)：ARM包68,077,183字节，33普通文件/7目录/31内部SHA，release_id=`00414bde726a3dd1c2460caf413c7240f56b2b907bdee512962d989c201b7354`，包SHA=`22b7579395af3f95afcb2bc88ff931bb687fba611ea87762637cfb026e6b423b`；另有匹配468项源码压缩包1,189,664字节，SHA=`b0c9b7fe13fcea8f0ca82098caeb31ff06302e206b2b24d823bb83ac11690f30`，已读回全部成员/安全路径/字节。两压缩包使用现有Git LFS规则，清单/恢复范围随仓库保存；未部署或运行目标ELF，未覆盖系统/配置/旧运行数据。

### 30秒七store主机基线及边界

固定native Release、WSLg/XCB1600×900、Guest、新匿名数据根与显式demo项目、14步文件回放后停在仪器页空载。实际30.1339765秒、295个100ms样本，正常SIGTERM/exit0，七store各关闭一次、THREADS_STOPPED/回放停止true，EOF fresh=false；主辅SHA不变，临时bootstrap秘密文件已删除。原fixture记录时间为虚拟时间，不作为实际响应。

整个观察段CPU为单逻辑核**4.662934%**；采样RSS峰279416KiB、内核HWM280624KiB、PSS峰262533KiB，线程峰42/FD峰59，PSS无缺失。运行目录从新建到最终11文件/262292字节；按SQLite签名只读计数补全5库（原glob*.db漏掉history.sqlite，原始记录不覆盖），不读取/归档字段值。相同样本10–30秒尾段195点约1.006620%单核，仅是同一次回放后空载分析，不作回放前后因果比较。

界面导航只测7次同步click入口，最大offscreen1.408266ms/XCB2.980348ms，不含排队/绘制/物理输入/仪器延迟；真实完整交互响应预算仍待验。观测前Windows编译器进程0、WSL无项目编译/GUI，和另一聊天资源观测串行，也避开压缩/大文件哈希复制；其他系统服务存在，不声称整机后台绝对空闲。[原始样本/独立复算](../04_references/sn100_host/instrument_20261024/resource/verification.json)

本轮未改系统环境、不删原代码或参考原件，不自动Git提交/推送；维护和其他匿名运行数据库留temp，不归档密码/私钥/认证材料。原Windows活动主题及完整行为、Windows GUI、RK实屏/输入/实际依赖、仪器真实协议/维护动作/互锁与失败恢复、认可资源预算、同板控制/温度/长稳继续待验；只关闭本轮离线检查点。

### O10-1最终独立审计登记

打印机本地设置切片的[最终独立审计](../04_references/sn100_host/printer_settings_20261024/audits/final_release_audit.json)通过，SHA-256为`7c198583791f34f1a7b6392577eb049aa37358d9ff48153ac978173ff58ffd9c`。原309项证据基础保持不变，加入此审计后外层[SHA256SUMS](../04_references/sn100_host/printer_settings_20261024/SHA256SUMS)共310项，全部读回匹配，清单SHA为`e935750d144c4e30879717cce3ba48032730c0ad77c3165a5b111c5b628676cc`；根结果索引仍为`6d1c814dab7fba5147d584177ddbf4fb9fd14df8f3e131b2fd27cf20199d9874`。最终361项源码、主辅产物、测试实际退出、发布包与资源证据的关联已核，保留Windows原主make独立收据及外部输入prehash缺口、ARM并行命令行交错等原有限制。

最终XCB的1600顶部/滚动后及1920页面截图已同步至`03_photos/20261024_sn100_printer_settings_*.png`，[来源与SHA](../03_photos/20261024_sn100_printer_settings_sources.json)对应正式测试原图，不能作为原Windows或RK实屏证据。下一切片按[既有LIS本地设置卡](../01_docs/plans/sn100_host_migration_plan.md#o10-lis-settings-card)执行，尚未实现；每批完成后继续按F01–F18复查新增全局差异，不重复旧测试或长测。

<a id="o10-lis-settings-20261024"></a>
## 2026-10-24 O10-2 原LIS本地设置与统一接入

### 入口、规则与本地数据边界

确认前两聊天交接及本仓库无其他活动写入后，本聊天单写完成本切片；隔离构建及只读审查并行，未重复旧页面或长测。原`frmuserset/frmlissetting.ui`按字节迁入，SHA-256为`158ff240e1586db7ec1bfbb408cc27d04ade2fb30c2756c0aa44a9684b46666e`，业务框`(400,40,611,781)`、Save`(420,690,161,61)`及原TCP/串口分组/Material控件保持。原主题键及必要Noto CJK替换沿用已查来源，完整Windows活动主题及RK视觉另验。

显式`--lis-settings`启用，统一`integrated-run`已带参数；不带参数不创建LIS配置或worker。`shell/main.cpp`管理`O10LisSettings`线程、首读与正常关闭，`shell/lis_shell.cpp`→`frmUserSet::attachLisPage`替换最终用户子页4，busy、草稿、登录/注销/关闭守卫接入原壳。原版所查入口无角色门槛，本地六种角色保持可达，登录不改变设备动作条件。

维护者从`ui/frmlissetting.cpp::on_btnSave_clicked()`追到`data/lis_settings_store.cpp::save()`。原9个可见键`lis/type,ip,port,commname,baudrate,samMode,autoget,autosend,link`存于显式数据根`settings/lis.ini`，增加`sn100/offline_lis_schema=1`，只在真实原子提交后更新GUI快照。保留12个隐藏键、非活动模式值和无关键；`samMode`缺省0，布尔值保持原精确字符串`true`语义。端口保留原键入0..99999的本地范围，它不代表真实TCP端口可用；实际连接时仍需确认角色/地址及1..65535等合同。本片不推测TCP client/server、串口/波特率、8N1或任何生产参数。

必要行为差异明确登记：模式切换不覆盖另一套草稿，一次Save有意提交两套草稿的9键（原版只更新活动模式端点）；空配置/缺失已存串口或波特率不会落首设备/1200，失效原名保留并有tooltip；未知样本模式及坏字段/控制字符/无效UTF-8明确拒绝且原字节不变；统一离页提供保留/放弃原版未设的草稿保护，不增加业务区或Cancel按钮。原`LisCommSetting::setConfig`不更新静态缓存，原版靠Save后连接流程重读，本地已用成功提交快照替代该副作用。

原Save发`signalConnectLis`，主壳随后`openComm`会打开TCP或串口；本切片没有移入/实例化这些活动通信类，不发连接信号。自动获取/上传/应答只存意图；HL7/MLLP、ACK、任务/结果收发、去重/重连及真实设备均不随配置保存关闭。

### 串口候选及文件失败恢复

Qt5 Linux `QSerialPortInfo::availablePorts`的8250检查可能用`open(O_RDWR)`探测；native和实际ARM sysroot也没有QtSerialPort/libudev。Windows Qt5 QSettings显式注册表路径可能请求写权限或创建键。因此新增`data/serial_port_catalog.cpp`：Linux仅查`/sys/class/tty`与`/dev`的stat元数据，Windows仅`RegOpenKeyExW(KEY_QUERY_VALUE)`→`RegEnumValueW`→`RegCloseKey`查询固定SERIALCOMM，链接系统Advapi32，不引入QtSerialPort/DLL、不打开线路、不验证端口可用性。[平台依据](../04_references/sn100_host/lis_settings_20261024/review/platform_dependency_audit.json)

INI处理在所属worker进行：64KiB实际字节上限、严格UTF-8、唯一临时INI避开Qt同size/mtime缓存、原字节复制后只改本模块键、QSaveFile禁直接覆盖回退、提交前复核实际字节及合作锁，失败保留草稿/已提交值。配置与锁目录及祖先符号链接拒绝，加载不使用旧生产配置。

受控目录0500失败曾暴露真实恢复问题：临时INI创建被拒绝后，Qt5的QLockFile析构无法unlink，但仍清除对象所有权状态；恢复父目录权限后下一次保存仍报告占锁。原`run_02`16/1及单用例2/1失败保留，未删未知锁或加等待掩盖。改为预先建立的私有`.lis-locks/settings.lock`子目录，设置父目录暂时只读时仍可清理合作锁，所有init/save使用同一路径且不改已有目录权限；最终同一失败/恢复用例通过。原独立入口漏复选图标QRC也已补齐，最终日志无该缺图警告。[实现审查及Qt依据](../04_references/sn100_host/lis_settings_20261024/review/implementation_review.json)

### 已核离线门槛及构建阶段

最终独立两QPA各17/0，实际退出0，覆盖九字段保存重开、12隐藏/无关键保留、空配置/缺失串口、双模式草稿、非法输入、保留/放弃、busy、锁冲突、真实临时文件创建失败恢复、外部字节变化、坏格式/UTF-8/超限和配置/锁目录符号链接。统一专项两QPA各5/0，实际测试最终子页4、六角色、草稿/busy/登录/注销/退出、同根重开、1600×900外滚访问Save及1920原布局，并启动实际产品核全功能/旧无参数/坏配置三条退出路径。[独立](../04_references/sn100_host/lis_settings_20261024/independent/verification.json) · [统一及累计](../04_references/sn100_host/lis_settings_20261024/unified/verification.json)

原打印机/登录两QPA、核心五项、catalog、回放、回环、仪器及报告累计：本批16次Qt调用共76项通过、个别Popen退出均0，Qt XML消息计数保留，不以`errors`属性等同断言失败。10个driver绑定最终150个共用正式对象，只有driver加QtTest宏/库，未重编/修改业务对象供测试。静态输入合同13/0；只运行新LIS capability部署模拟1/0，旧已通过19+打印1不重复跑；模拟不是RK部署。

native新目录全量151主对象+31辅对象，实际构建0；最终371源快照SHA`e00508fe595f49c50413856422522fb00e8ddcd21b2080f15b50e636d8093fb5`前后一致，主SHA`d3d5fb4c50e387b0f0128a240d27a9e7617b4b4583bdb9d55ac4e008adc16ea2`、辅SHA`cc1c0d4562f6a56670a7884a2875ecede77407ae5ee83e7db4587375b8001d3b`。初次exit2为编辑时CRLF使bash拒绝pipefail，原失败保留，修正三份改动脚本LF后才进行全量成功构建。

Windows本批从PS7全量主151/辅31，12份子进程实际退出收据和顶层均0；371源、11工具链、Makefile实际外部依赖预后匹配，182 COFF/298构建产物核准。主PE32/i386 SHA`d47598a555d324507ac1275f16ce1f45138d6299148a58162773267b055cf9cb`，辅SHA`5f70bbff88e217aeb02794d133118785a1035f78aba254b8269de2826cdc8dac`，直接导入三项Advapi32查询API、未导入QtSerialPort。本批有真实独立收据，不能反向补齐旧361主构建的历史收据缺口。首次仅私有driver WSL参数转义失败、快照127/顶层1且未编译，原证据保留；修正ArgumentList后从新目录fresh执行。新增NOMINMAX重定义及原警告保留，未因非阻断警告重编。Windows GUI/注册表实际枚举及部署未执行。[Windows](../04_references/sn100_host/lis_settings_20261024/platforms/windows/verification.json)

ARM64全量主辅、离线依赖/成对恢复冻结及最终响应/资源观测此处仍待终态核验，不凭后台进度记通过；最终结果后续追加同节，当前正式源码冻结不变。

### 本批全局差异及后续切片

对照唯一F01–F18/43操作事实，本批361→371新增10个LIS文件、改变11个壳接线/构建入口、无删除，350旧输入一致；原data/history/projects/reagents/samples/instrument/protocol旧业务输入未变。最终LIS子页4已实现本地配置，语言2、校准主3、质控主4仍为最终占位；真实LIS通信并未实现，不能把本地设置当F11全部通过。[全局差异](../04_references/sn100_host/lis_settings_20261024/review/global_delta_review.json)

下一依赖满足的小片选择F08控制品/项目/批次本地CRUD：已只读核33份原输入、5表/5窗体，尚未实施。须保持全表唯一批号、项目规则按item_proj_id而非批次的原关联、历史字符串引用保护、稳定ID与事务默认批次轮换；显式改选默认批次可立即排队事务，渲染/打开不得隐式写库。原Cancel顶X解绑、过滤后row错配、ckxValid未持久化等问题逐项有依据处理；主Print原隐藏，不盲恢复。真实曲线/AL4A/失控规则与本地CRUD分开，缺材料不阻塞本地页。仍按现有计划其他全局缺口排序，无平行计划。


### O10-2最终ARM接续、冻结与测量收口

ARM初轮原日志明确记录obj126/127两条Hangup，128份原调用收据中126完成0、2未终结，原整体退出收据缺失。核无存活编译器及371源、原sysroot闭包指纹、工具版本和全部生成UI/moc/qrc内容后，受控接续用完整argv/cwd、对象SHA/生成源/头快照校验，只复用本批126对象，27个缺失编译和2个链接实际完成0；153个最终对象逐个ELF64 REL AArch64/DWARF源/flags/原始SHA及每次完整调用收据通过，未使用旧361对象。原中断原因仍未知，不能补造原终态退出；最终接续真实0与闭包/打包0分别有收据。

ARM主SHA`6449642ecdce25f8a4bd5318ad639131b48b0a4423dc3d0d5ac9a88d7a1b518e`、辅SHA`1c7acee3205acd95acd6cef9fcb4ce416a5bbd52330bc26b9130fe50ae4598bd`；56项ELF离线闭包errors=[]，源码首尾/包均371一致，旧sysroot仅只读复用、未恢复/下载/安装。正式[成对恢复目录](../07_backups/sn100_host/lis_settings_20261024/README.md)中应用包70090942字节、SHA`78b2ee85c8592c977a3bd23ab7c1df5016ebca7d7d663dc76a19aab44022de57`，匹配371成员源码包928063字节、SHA`2c7943f4c0f6ff1ccbcbadc8c01150cd9423260956828cc85e7b8c27e65050c4`；应用33普通文件/7目录、31内部SHA、21个AArch64 ELF已逐成员回读。仅恢复本应用/匹配源码，不覆盖运行库、系统或认证材料。[ARM完整证据](../04_references/sn100_host/lis_settings_20261024/platforms/arm64/verification.json)

最终构建/GUI/大文件哈希均停止后，仅做一次40保存响应与一次整程序30秒取证，不重复长测。固定两匿名端口provider、无人工延时，queued click→busy p95=0.286544ms、p99/max=0.327510/0.327510ms；queued click→GUI保存数据状态p95=125.634198ms、p99/max=134.108695/134.108695ms。5ms心跳592间隔p95/p99/max=11.287213/12.956783/14.180806ms，Qt3/0真实0；这是事件循环/GUI数据状态代理，不含OS物理输入、paint完成、实际枚举/通信或认可预算。[响应原样本](../04_references/sn100_host/lis_settings_20261024/response/metrics.json)

实际整程序Release/XCB1600×900/Guest/原仪器页7、七SQLstore+打印/LIS两INI worker、空闲密码入口与1Hz自建Qj，独立匿名demo数据、30.152998050秒/296个100ms样本，SIGTERM正常退出0。固定10≤t<30段196样本、19.851647258秒，CPU=5.138113%单逻辑核；RSS峰286344KiB、HWM288436KiB、PSS258937–270500KiB、线程45/FD63，PSS缺样0。全wire 30TX/30RX/1560字节、连续sequence及逐帧金字节全部匹配，九存储关闭各一次、threads/loopback停止、临时bootstrap已删；只在退出后匿名库计数，不读/存认证值或运行库哈希。当前整程序单次基线不能与旧361/353不同数据相减推断纯LIS开销，亦非RK/预算/同板RT/长稳验收。[资源及原样本](../04_references/sn100_host/lis_settings_20261024/resource/verification.json) · [算术/wire复核](../04_references/sn100_host/lis_settings_20261024/resource/final_metrics_audit.json)

[本批总索引](../04_references/sn100_host/lis_settings_20261024/verification.json)SHA`d93510e53a364464bf9a605b46d10fed20aa55c02e1c8ed58bfe87872b166ca9`；外层清单606项全部读回匹配。最终18次Qt调用共110通过含init/cleanup，另响应3/0，个别实际退出及XML断言均核；静态13/0和新部署mock1/0单列。Windows主/辅本批完整收据与原先361历史缺口分别保留。后续证据聚合由本聊天回读，未把代理未交付的最后审计冒充独立结论。

三张最终XCB统一截图已归档03_photos，来源/SHA对应本批371测试；原Windows活动主题/完整行为、Windows实际注册表查询、RK实屏/输入/设备名/依赖、真实LIS/仪器/任务/打印、认可预算/实时性/长稳继续待验。旧O05 1150.178秒与报告偶发根因未关闭。下一按既有[O11-1卡](../01_docs/plans/sn100_host_migration_plan.md#o11-qc-local-card)推进质控控制品/项目/批次本地业务；本轮只完成原合同准备，尚未实施。

### 用户安排：本轮结束后停止定时检查

用户随后明确要求“这一轮工作做完之后，后面就不要去检查这个10分钟一次了。这个工作我后续再做”。已通过应用原生工具删除`sn100`每10分钟检查任务，工具返回`deleteStatus=deleted`；本轮LIS只完成剩余验收、归档和同一计划更新，不启动O11或其他开发/长测。未完成条目和QC原合同留在现有计划供用户后续安排，不另建提醒、自动任务或平行计划。

本轮最终[交付回读审计](../04_references/sn100_host/lis_settings_20261024/audits/final_delivery_audit.json)通过，SHA`8ec9d7dd416857e4f64db59fe0d8ce0e667a51d7528ec5305f4a0046332bc188`；606项基础清单加入审计后为607项，[最终清单](../04_references/sn100_host/lis_settings_20261024/SHA256SUMS)SHA`4c953e6815247372f14965492793b4555db64547655e4fd65f35e693a04aa80e`，根索引仍为`d93510e53a364464bf9a605b46d10fed20aa55c02e1c8ed58bfe87872b166ca9`。371当前源、六个主辅产物、153个ARM对象/完整编译收据及两个中断收据、双恢复包、三图和七份文档均回读；所有本轮构建/GUI/测量已终止，无后台开发，自动任务配置文件亦已不存在。保留所有已有未提交修改/运行数据/原失败，未自动提交或推送Git。

<a id="offline-four-windows-20261025"></a>
## 2026-10-25：授权四窗口继续离线移植

用户确认RK暂不可用，要求创建4个聊天分别推进质控页面、校准页面、项目剩余业务、报告剩余功能。本轮授权替代上一轮“等待后续安排”的开发暂停范围，但不恢复每10分钟自动检查，不扩大到RK/仪器连接、真实动作或未经认可的计算效力。

先核对当前工作区与已有聊天：同项目原计划聊天为idle，其余旧开发聊天未加载；保留全部未提交成果和371项输入的LIS已验检查点。本窗口先更新既有计划/README和当前事项记录，随后派发4个本地同目录聊天；不自动创建工作树或提交/推送。

分工与门槛见[现有计划四窗口卡](../01_docs/plans/sn100_host_migration_plan.md#offline-four-windows-20261025)。校准、项目、报告各自负责模块源码、独立构建/匿名数据/功能证据；质控窗口完成自身模块后，兼任统一整合负责人，回读其他窗口实际交付再串行改公共主壳、累计回归、三平台最终产物与离线闭包、资源和匹配恢复归档。各窗口只写自身证据中的交付清单，不另建移植主记录，总文档由整合窗口集中维护；当前仅登记授权/分工，任何新功能均未提前记为通过。

4个聊天已通过应用原生`create_thread`创建在当前本地项目，未指定模型或新工作树；逐个派发了实际实施范围、模块文件边界、匿名测试/证据目录、原色布局要求和交付清单。随后一次`wait_threads`快照确认4个均为active/inProgress、各自已给出开工说明、turn error均为空；这只证明已开始，不是功能通过。

| 聊天名称 | 本机聊天ID | 职责 |
| --- | --- | --- |
| SN100质控页面与统一整合 | `01a1732c-83b1-7bf2-8efb-ce6b8e252208` | 质控本地管理及最终统一整合，已提供其余3个真实聊天ID |
| SN100校准页面离线移植 | `01a1732b-9c52-7180-9464-28a0dc526a76` | 本地记录/点表/曲线及关联 |
| SN100项目剩余业务移植 | `01a1732b-3481-7d80-a7de-d861dbb28e19` | 高级参数、试剂批组关联及共用catalog结构兼容 |
| SN100报告剩余功能移植 | `01a1732b-d6b9-7df1-b2ba-ec1133072d5b` | 原版已启用的报告剩余本地功能与文件导出 |

所有聊天hostId为`local`，实际cwd均为本仓库。当前派发窗口到此完成创建/初始登记，不另建自动监控；用户后续可直接进入各聊天跟进，最终接入与总文档由质控/整合聊天负责。派发文档的差异空白检查通过，保留此前所有源码/证据/运行数据及已知失败。

### 质控独立实现和当前门槛

质控/整合窗口实际启动后，按模块与文件Skill回读工程/计划及原合同，33份只读原件SHA再次逐一相等，未运行参考工程或读取其生产库/连接配置。[原件复核](../04_references/sn100_host/qc_local_20261025/original_contract_recheck.json)。正式新增`quality_control/`，保留原`quality/`统计接口；原五份`.ui`逐字节保留，原Print隐藏，原入口无角色门槛。必要差异及原函数来源见[origins](../06_programs/sn100_host/quality_control/origins.json)。

存储使用显式匿名`quality_control.db`，SQL连接在所属工作线程创建/使用/关闭。原五表旁增加数据集UUID和项目结果单位/倍率/精度快照；规则仍按`item_proj_id`关联，绝非按批次保存靶值。全表批号唯一；已有历史的控制品/批号改名及删除、项目解绑拒绝，当前/上一批次删除拒绝。默认批次仅在明确动作中整组事务轮换，同选无写入，加载不修复历史标志；ckxValid仅回显禁用，不冒称原有持久化已完成。

项目关联先在已有catalog引用表保留带数据集UUID的预约，再提交质控库；失败仅回收本次新增预约，成功解除关联后才释放。释放失败保守保留并报告，跨库不是SQLite外键或一次原子事务。原版只认`Original/Calibration/Rate/INR`类型1至4，匿名seed中的`Time`不能猜成`Original`；未知类型不创建质控规则。尚需真实ProjectsStore接线联测。

WSL Ubuntu-24.04/qyq、Qt5.15.13、GCC13.2 Release独立存储首轮14/0，增加初始化拒写、重开/只读SHA不变后复测16/0，9.735秒，qmake/make/test退出均0；真实锁、COMMIT失败、触发器中途失败及恢复、双数据集同ID引用隔离均有原始输入/回执。[结果与原始日志](../04_references/sn100_host/qc_local_20261025/store/verification.json)。这些测试用匿名最小catalog协议夹具，不能替代统一跨模块联测。页面已编译并1800ms正常启动退出0；早先一次类型编译错误和非空目录启动拒绝均保留，完整GUI CRUD、1600/1920视觉、统一累计及三目标仍待本轮后续证据。

当前三个用户开发窗仍在独立收口，整合窗口只读核对其状态，未向其他聊天发消息。最终源码冻结、Windows/ARM整程序构建与资源测量尚未开始；RK/仪器不可用，不部署，不新增自动检查或长稳任务。

### 其他三窗交付回读与整合开始

随后通过`wait_threads`分别确认项目、报告、校准三窗均`completed/idle`且turn error为空；未发送跨聊天消息。项目63份完整模块源码、报告48份相关源码、校准55份输入逐一核SHA均相等，项目6个独立产物也逐项相等；校准12份原始调用日志SHA全部相等。[项目/报告回读](../04_references/sn100_host/qc_local_20261025/incoming_source_review.json) · [校准回读](../04_references/sn100_host/qc_local_20261025/calibration_incoming_review.json)。

独立交付实际范围：项目高级方案11/0、项目基础9/0、试剂专项11/0和基础11/0，两个独立程序退出0；报告本地删除/显示12/0、独立进程重开3/0、HTML7/0及原CSV/PDF/患者草稿回归，保留原失败与XLSX未启用依据；校准存储50/0、offscreen/XCB页面各21/0，原始点/复制/软隐藏与两尺寸可达性有证据。各独立结果仍以原交付清单及对应源码为准，不能算统一程序或本轮最终三目标通过。

公共接线现在集中进行：项目补ARM显式高级编辑器清单、试剂提交后的项目候选刷新；报告接实际会话、新显示合同QRC别名、HTML结果状态与报告helper依赖；质控/校准接原主4/3、独立worker、就绪门闩、退出顺序与全局草稿/busy守卫。构建清单只读检查15/0、真实退出0；这仅证明输入闭包清单，不代替编译或业务测试。

校准独立版本尚无跨库保护，因此先补预约与含隐藏历史的启动对账，再验共享catalog写入。选用模块worker自有第二连接、仅写既有预约表的实现，项目/批组业务及schema仍由其owner维护；这是对独立审查中“仅经owner API”建议的明确实现调整，原因是原QC可Guest操作、校准role1–5可操作，而项目编辑API只允许工程师/管理员，不能伪造身份或新加管理员门槛。预约与本地提交保持分步，失败/崩溃残留保守保留并闭锁核查，不声称跨库一次事务。受影响的原校准两存储文件和三匿名夹具精准保留到`07_backups/sn100_host/offline_four_windows_20261025/pre_integration_sources/`，SHA与独立交付清单一致；不复制运行库/认证资料。保护后的实际联测与原回归仍待后续收据。

### 用户晚间新授权：继续分工、每10分钟查看

用户随后明确要求其他三个窗口继续分担，并要求今晚每10分钟汇总真实进度、核对下一步、向原窗口分发工作，本聊天持续推进进入真实RK前的离线程序验证。该新要求取代本轮开头沿用的停止定时检查安排。已向原三个真实聊天派发项目引用、校准guard/历史语义、报告主壳/HTML/权限的独立复核，只允许各自新增审查JSON、不改公共或业务源码、不跑额外构建/GUI/资源；`wait_threads`确认三窗重新active/inProgress。实际修复与统一回归仍由本聊天集中执行。

通过应用原生工具创建heartbeat自动计划`sn100-10`，状态ACTIVE，目标为本聊天`01a1732c-83b1-7bf2-8efb-ce6b8e252208`、每10分钟。工具view卡及本机automation.toml字段已核对；未创建重复聊天或cron工作。计划保存了用户授权分发的三个聊天ID、只读回读/源码SHA/退出证据门槛、共享dirty写入边界、最多-j2、最终构建/资源串行及不连接硬件/真实LIS、不猜生产参数、不安装依赖/启动长测的限制。按用户要求每轮简要汇总；可执行离线门槛和归档完成、或仅余硬件/材料前置时停止空转并保留清单。尚未发生的后续检查不记为执行通过。

### 晚间独立核查结果与修复中的门槛

原三窗只读审查现已交付并停写：[项目引用审查](../04_references/sn100_host/projects_remaining_20261025/integration_reference_review.json)、[校准保护审查](../04_references/sn100_host/calibration_local_20261025/integration_guard_review.json)、[报告接线审查](../04_references/sn100_host/report_remaining_20261025/integration_shell_review.json)。发现质控启动漏对账/并发回收、校准自动回滚后失锁回收、停用项目仍可隐藏、同UUID异文件副本及报告后台导出导航守卫等具体问题；这些为静态审查，未把提出的交错步骤冒充实际复现。已分给当前聊天的三个独立代理分别修复质控引用、校准保护及补报告统一测试，文件边界互斥；主代理负责公共壳及统一终态。用户授权的多窗口分担持续有效。

主代理已将报告实际busy检查放到两导航入口，覆盖控制器从非报告页面发起导出的情况；native构建脚本默认/可选并行数限1或2。质控引用回收警告保留到后续列表读取后的状态，避免本地删除成功遮住残留保护登记。以上新改动仍须实际统一回归，不提前标通过。四份受影响源码精准保存于正式恢复目录的`review_navigation_preimage/`，有逐份SHA清单。

此前质控边界实际复现数字末尾换行误接收（2通过/1失败、退出1），正则改为绝对结尾后完整存储16/0、退出0；原失败未覆盖。页面通过真实键鼠和SQLite回读发现原表单八个编辑框readOnly、批次新增重入，已在页面代码解锁同一控件并用信号屏蔽修复，不改原五份UI。原事件过滤器跳过readOnly，因此不能称解锁只是还原原版虚拟键盘。修正测试弹出列表/快捷键焦点后，gate05 offscreen6/0、gate06 XCB6/0，后者留存五窗真实截图和1600可达断言；这些产物先于随后引用修复，终态必须再跑。

校准加入保护后原存储首轮47/3暴露新合同差异：合法预约会改变catalog整文件，历史原始单位/倍率应保留。改为核项目/试剂业务行不变及真实预约、明确两种历史单位保留断言后复测50/0、24.286秒、退出0；首轮失败保留。页面offscreen21/0、10.147秒、退出0。当前追加修复可能改变这些源码，最终需重新核验；ARM/Windows主辅统一构建、离线闭包和最终测量尚未通过，原Windows、RK及仪器均未执行。

### 引用修复独立终态与426输入统一构建启动

质控修复已正式交付：[清单](../04_references/sn100_host/qc_local_20261025/qc_reference_repair_handoff.json)，20/0、8.654秒、qmake/make/test真实0。真实ProjectsStore与双QcStore在明确屏障处复现解除/重加、ABORT和自动ROLLBACK的失败/成功交错，回收先重新取本地写锁核真实关联；只释放确定无引用的新键。启动补齐缺失匹配hold，校验含停用项的全部历史/快照，未知身份、孤立自身hold、同UUID异物理路径闭锁；历史单位/倍率/精度不覆盖。18份匿名SQL回读及两轮日志留证，第一轮仅测试缩进告警修后同20例再次通过；源码四份SHA由主代理回读相等。

校准修复已正式交付：[清单](../04_references/sn100_host/calibration_local_20261025/calibration_guard_repair_handoff.json)，旧store加停用删除拒绝后51/0；引用22/0，真实退出均0。操作锁跨两库动作，自动回滚后重建本地事务核含隐藏记录，清理失败保留保护和诊断；同规范文件多worker可用，同UUID异路径副本拒绝。其私有引用构建消费的QC源未预先冻结，只留后验样本，明确不能冒称最终联测；主代理将用同批统一对象重跑。两模块在catalog_metadata各用独立路径键，同路径恢复，跨平台/移位需停写后显式核对迁移，不能直接清UUID或hold。

两修复代理均已停写/停构建，驱动审计也停写后，主代理开始新目录native全量主/报告helper构建；426项输入的初始快照SHA为`e65d77308b35b89dd6394ed6844e9a2482b32be9da2763f2b0fd5e268098de19`。此处只记录启动，不凭进度记编译通过。最终驱动已补源码完整集合、实际主辅/Makefile/对象SHA绑定及原始退出收据；最多2并行，GUI和资源尚未开始。

报告原窗对新统一测试再审，提出Guest初始role=0不能确认异步actionReady变化。主代理已将等待完整状态后再断言身份/来源；并补实际HTML工具按钮、文件框Cancel/重开/键盘保存、导出期间关闭预览、后台直接导出两路径，逐字段/精确行数与APTT `0.00 s`回读。新增测试仍待绑定执行，[原只读审查](../04_references/sn100_host/report_remaining_20261025/new_shell_test_review.json)仅对修前SHA有效，未覆盖或改写。

### 426项原生构建终态与累计首轮失败

native全量真实退出0，426项首尾相等，主168/辅32个对象均本次新建。主SHA`013eb363c6cd7a928f62f860c5cbd6755421a81808a4e6ad347cc6db0887f176`，辅SHA`2de67df76a6871884e52584cefb613fa89daa657a59938435f61f4bd875fe7eb`；Makefile SHA`df6bbee134af56a57cac5da631502188c07ab7c62e934eb3b06c62423e7527d0`。随后21个QtTest驱动实际编译/链接退出0，业务链接使用同一167个主对象（排除main.o），主对象未加QtTest宏。

累计` suite_final_01`前11次调用实际通过；catalog调用6/1、退出1，失败在旧测试直接setText后期待草稿。切入项目页现在会异步刷新已提交候选，ProjectPage实际已把编辑框设readOnly且busy；旧setText绕过键盘保护，旧等待只查isEnabled不能确认可编辑。测试改为等待busy结束/非只读，再真实鼠标和键盘输入，截图也等读取结束；产品源码未改。精确旧测试已备份`07_backups/.../review_test_preimage/`，SHA`acd04609e8c14482cb8ef044cf85332cbd488ea25e1c8db6ea60df3f7a46448c`。原21驱动/证明/原执行脚本整体保留为`cumulative_build_01/`，实际原路径及迁移依据在同层`cumulative_build_01_relocation.json`，原失败日志未覆盖。

测试绑定工具新增显式可选复用：逐一核同一426冻结、同Makefile、全部167对象/其源SHA、测试源及二进制/moc/私有对象SHA相同后，20个未变测试产物可复制复用并单记reuse.json；其原三段编译退出收据原样保留，未伪造新调用。唯一改动的catalog测试新编译，整体绑定真实0。未复用旧371检查点程序。现执行新目录`suite_final_02`共34调用，终态尚待核对；Windows/ARM、独立新入口、资源和恢复归档仍待实际执行。

### 累计测试输入与窗口焦点修正

`suite_final_02`前16个旧调用全部实际通过，catalog已7/0，原报告两个方法4/0；本次通过不能关闭既有偶发查询根因。新QC page实际6/1：主代理新增警告测试把控制品名写成19字符，超过原1至16上限，输入被正确限制；精确失败测试源SHA`535c14030a2a98b9f2465b4bcf69e70d5601a4cc7eea1c6b8f2933f587fcd502`保留在正式`review_test_preimage/`。改为13字符`QC_REF_NOTICE`后，`suite_gate_03`QC page两QPA各7/0、QC shell各5/0；新增真实catalog删除触发器证明本地删除已提交、保守hold仍1、警告经过成功reload仍保留。旧60秒项目benchmark也补相同页面就绪等待，但本轮未执行该长用例，不记新性能通过。

gate03校准统一测试初轮3/3，产品完整/旧路径/失败/依赖启动退出方法已过。独立任务只修QtTest活动窗口、下拉框及点表delegate的真实键鼠流程；中间attempt01仍3/3，证据显示modal关闭后headless宿主未重新激活，Insert没打开对话框、focusWidget为空，未延长超时或改变业务断言。attempt02 offscreen6/0（9.47秒）/xcb6/0（10.01秒），实际均0，准确中间源按真实prebuild SHA重建并标注，生产426不变。[正式交付](../04_references/sn100_host/calibration_local_20261025/calibration_shell_repair_handoff.json)。主代理再次绑定当前源后，`suite_gate_04`校准两QPA各6/0，均真实0。

gate04报告统一测试实际4/1：六角色与actionReady=false、四入口草稿Cancel/Discard、显示保存/软删/重开均过；HTML在实际工具栏按钮尚不可见断言失败，未进入导出成功段。该原始日志保留，已分配单一GUI/构建槽核对预览异步布局、原工具栏溢出菜单和文件框焦点，禁止弱化字段/忙态断言或改生产冻结。当前gate是显式suffix预验，未执行的前缀逐项记录omitted，不能算完整34次通过。最终完整累计、三平台收口、资源与恢复包仍待后续实际收据。

### 报告真实操作修复终态与项目事务终审新发现

报告测试私有attempt01/02各4/1、真实退出1，分别保留布局可见区判定与扩展工具栏路径的失败。attempt03终于按真实Qt5布局操作：offscreen扩展后点击第二行原按钮、Cancel后重试direct，xcb两次direct；无action.trigger、改预览尺寸、加超时或弱化断言。两QPA各5/0、2963/2976ms、实际0，真实Save/fileSelected路径、前后台各1行APTT `0.00 s` HTML字段与busy守卫均过。[正式交付](../04_references/sn100_host/qc_local_20261025/report_shell_repair_handoff.json)。新cpp SHA`be49e9384af399d01d891e9ccdce63868e7649d69e8b9cb943d9aa11afe9b822`，主代理当前已换绑到同一167原生对象，20个未变测试产物带SHA复用。83份formal证据逐项核SHA，原失败与源像未覆盖；这仍不是最终完整累计。

按用户持续分工授权，已再次给原项目/校准/报告三个聊天派只读修复终审，wait_threads确认三者active；各自只写post_repair_review.json。项目窗指出新的具体并发路径：`ProjectsStore::save`在事务外read得到旧名称，之后别的写者改名并新建QC引用，旧保存比较缓存旧名会跳过引用检查，再写回旧名。主代理已逐行回读确认代码结构，尚未以实测冒称复现；试剂saveBatch一开始begin(op)，同类检查已在事务中，无需扩大。

已将实际修复交回项目源码owner，独占构建/GUILESS槽：窄备份projects_store.cpp，BEGIN IMMEDIATE先于当前记录/单位/身份读取和引用判定，所有早退rollback，保留权限/不可变类型及原名/全名合同；新增root/tests确定性原子引用回归并保留修前实际失败、修后成功。可用SQLite原生事务prepare回调屏障而不添加生产测试接口，API须有准确依据。此后e65/主013eb/辅2de只作为修前检查点，最终35调用、新native/Windows/ARM与资源/配对恢复须修后新冻结再做；当前没有生成最终发布包或进入RK。

### 项目原子保护实测与修后重新冻结（2026-10-26凌晨）

项目窗口实际完成窄修复，正式交付见`04_references/sn100_host/projects_remaining_20261025/project_reference_atomic_handoff.json`。原`projects_store.cpp`的SHA为`5342e5dbded0eda1f07cdd0bebb3971dac5d3222dd3fdeea61e793fb98219721`，修后为`b234dd9d2609fc1b424396a2d9244f4dc2a9b4929fb0f9dae46a12685141702f`；先BEGIN IMMEDIATE再读当前记录、单位及引用，全部早退回滚，不改接口、结构或权限。现有SQLite授权回调只用于测试屏障，未加生产测试钩子、安装依赖或改工具链。

同一真实双ProjectsStore与QcStore交错，修前实际5通过/2失败、退出2，项目名和全名两条竞态均覆盖；新源独立构建两轮各7通过/0失败、qmake/make/test均真实0，含锁、COMMIT/写入/读取失败与后续重试。主代理回读实际原始日志、源码SHA与仅一份生产源码变化；修前源码精准备份，原审查/失败收据原样保留。此处独立成功仍不替代统一对象累计验证。

修前native及测试对象迁存`native_pre_atomic_01`、`cumulative_build_pre_atomic_01`，关联原执行路径的relocation另存，不改写原命令/退出。修后重新从空目录全量WSL Release主/辅编译，2026-10-26 00:12本地时间完成：426输入首尾相等，快照SHA`57f3b03290cc3635a5772a7169d22c1713f164186ab3b368028a0f35f8b710ed`；主168与辅32对象均本次新建、实际退出0。主程序SHA`054bffa7847881cd084f4792f81c796313396f3e07e72195aa20b89e9417b347`，报告helper SHA`2de67df76a6871884e52584cefb613fa89daa657a59938435f61f4bd875fe7eb`，Makefile SHA`df6bbee134af56a57cac5da631502188c07ab7c62e934eb3b06c62423e7527d0`。helper内容相等是重新鲜编后的实际结果，未复制旧产物。

现开始22个QtTest驱动全部鲜编绑定，不复用修前e65对象；完整计划35次调用增加项目原子保护，未用suffix门槛代替全量。Windows/ARM主辅、独立入口、短响应/资源与匹配恢复包仍待后续真实结果，不连接RK、仪器或真实LIS。

### 修后完整累计首次崩溃门槛

22个驱动以57f3b032冻结的167个统一业务对象全部鲜编/链接，实际退出0、复用0。完整`suite_final_05`前24次调用均通过：原16次累计、QC page两QPA各7/0、QC shell各5/0、校准shell各6/0、报告剩余shell各5/0。随后`calibration_reference_offscreen`在`destroyedTypesOrHistoricalUnitCannotBeCopied(missing-calibration-type)`实际收到SIGSEGV11，Qt记录18通过/1失败并ABRT，父进程回收真实exit -6；累计驱动退出1，后续11项未执行，未产生完整通过结论。原日志、XML、实际命令/退出和匿名runtime均保留在原目录。

已将独占调试槽交还校准测试负责人，先用现有gdb作为父进程抓实际线程栈，不因Qt自附加受YAMA限制去改系统参数；仅允许测试源窄修并保留精确前像，若属生产代码须先报告证据。此时根因未明，不能以私有先前22/0或重试成功关闭。主窗口继续整理操作说明；其他构建/GUI/资源暂停，之后按实际根因决定重新绑定或重新冻结。

### 校准引用测试线程风险取证与重绑

同一修前bound driver在gdb父进程中跑精确数据行及完整22序列均正常退出0，没有重现原SIGSEGV，不能以此关闭根因；ThreadSanitizer私有单body诊断实际exit66，unexpected memory mapping，未产出race结论，未改ASLR/sysctl或安装环境。随后有界gdb实际停在ProjectsStore worker发ready时的`QSignalSpy::qt_metacall`：Thread2/LWP372直接写主栈receiver，同时Thread1/LWP369在同一数据行waitFor/qWait读取信号记录。本机Qt5头文件亦确认DirectConnection、appendArgs无锁；这是测试跨线程风险的实际栈证据，仍不是原SIGSEGV调用栈。

仅测试`calibration_reference_test.cpp`改用QObject上下文的QueuedSignalRecords，完整typed参数通过Qt::QueuedConnection交主测试线程后append/clear/read；Worker与临时读回/hold/bind观察器全部替换，实际worker no-op队列屏障等待原slot及自动load返回后再取新观察器。22条业务数据行/断言及5000ms等待未减弱，生产426/57f不变。修后首次真实22/0、13.64秒、exit0，285次主线程投递有自检。直接崩溃原因仍标未确证，不宣称旧崩溃或长稳已修好。

主代理核对实现后，将原57f的22-driver完整绑定迁存`cumulative_build_pre_calibration_reference_01`，22份精确旧cpp均依原proof SHA留`*_executed.cpp`，另存执行脚本后验副本；原路径/argv/退出不改，relocation保存对应关系。当前新绑定只重编校准引用测试，其余21驱动须同57f/167对象/源/moc/二进制SHA相等才允许带原真实编译收据复用。之后完整35从首项新目录重跑，不把已有24次及私有22/0拼成最终通过。

### 当前57f完整35次累计实际通过

修正测试线程风险后，主代理新绑定真实exit0：1个校准引用测试重新moc/编译/链接，21个未变测试仅在同57f源/Makefile/167对象/测试源/moc/私有对象/二进制SHA逐一相等后带原始编译收据复用。`suite_final_06`从首项执行，35次actual exits全0、full_suite=true、omitted=[]、无失败或跳过；426输入未变，主054bffa7/辅2de67df7相等。原suite05崩溃及所有旧e65失败仍保留，不以本次通过关闭原SIGSEGV直接原因、旧legacy.query或8h恢复根因。

实际当前结果：原16次累计通过；QC page两QPA各7/0、QC shell各5/0；校准shell各6/0、报告剩余shell各5/0；校准引用22/0、QC引用20/0；报告剩余核心两QPA各12/0、HTML各7/0；QC原存储16/0、校准原存储51/0、原页面两QPA各21/0；新增项目原子保护7/0、4.165秒。原始命令/实际退出/Qt txt+junitxml见`temp/sn100_host/qc_local_20261025/suite_final_06/`，正式总收集仍待所有后续门槛完成。

随后启动新目录独立QC/校准qmake、make-j2、真实启动/1800ms正常关闭验证。另核到项目高级11/0、基础9/0、试剂批组11/0/基础11/0及项目/试剂两个独立程序原交付早于最新projects_store原子修复，因此分给原项目窗口只准备新目录补充驱动，不改源、不覆盖旧产物、当前不执行。主窗口在当前阶段后串行补这4组专项和2独立入口，才能把本轮功能与当前源码对应；原35次完整计划自身仍保持35，不把追加专项混记为已执行。

### 独立QC/校准终态与项目专项补验进度

`standalone_final`当前源426/57f全部7个实际阶段退出0：QC和校准各qmake/make-j2鲜编；QC实际已加载、1800ms正常退出0；校准先由同批真实主壳创建项目/试剂/样本catalog，观察SHELL_READY再经SIGTERM正常关闭全部5worker，临时初始化文件已删；校准独立实际加载后正常退出0并输出worker/database关闭标记。首尾source、主辅、native verification和两独立binary SHA相等。此为实际启动/关闭验证，不代替独立CRUD或RK/原Windows。

原项目窗口补充驱动`run_projects_modules.py`已AST0并停写，主代理逐段回读（source合同、真实CLI、fresh目录/对象/退出/QtXML、环境隔离及只读匿名catalog检查）后串行执行，脚本SHA`c0b67f95fd7b9e70bfd59383e0b8e74e2008c590ae7dd7b3d424fd6150621d55`。当前项目高级11/0、项目基础9/0、试剂批组11/0实际均0，使用修后b234项目存储新对象；试剂基础及两个独立入口继续执行，未提前记6目标完成。总collector已增加该补验为必需门槛，35累计、4专项和2补充独立入口分开登记。

从同批`suite_final_06`复制7份匿名截图到`03_photos/20261026_sn100_*.png`，全部SHA核对，清单与已实际审图范围在`04_references/sn100_host/qc_local_20261025/current_visual_review.json`。质控管理1600、校准统一1600和报告软删后空表已人工视图回读；完整原Windows同状态/活动主题、HTML浏览器渲染及RK实屏仍未验，不由截图存在推断视觉等价。

### 项目/试剂补验终态与Windows首轮真实失败

`projects_modules_final`六个目标全部实际qmake/make/run退出0，426/57f源码、8测试输入、主辅与全部200原生对象首尾相等；四组专项分别11/0、9/0、11/0、11/0，两个独立程序实际加载后1800ms正常退出0（1.94/2.00秒）。项目有唯一原成功日志；试剂没有伪造success标记，采用原main中readyChanged(true)后才生成的1751×968新PNG及匿名catalog完整性摘要核对。六个私有构建目录由空开始，全部实际对象/生成文件/Makefile/二进制SHA贯穿运行和末核相等；不把QT_TESTLIB定义参与的私有qmake产物冒称统一167个对象本体，主35计划仍精确绑定正式Release对象。本地业务源码也只读核到没有QT_TESTLIB/QTEST条件分支，原搜索空结果exit1表示未命中。

随后普通PS7执行Windows全量fresh主/报告helper构建，Qt5.14.2/MinGW7.3，主qmake实际0、首轮make实际2，driver实际1、published=false；只生成27个主对象，未发布或部署任何旧产物。`history/report_remaining_store.cpp`报QVariant不完整与addBindValue无法转换，日志明确指向缺少显式QVariant头；Qt5.15的间接包含不能代表Qt5.14可编译。原make.log/exit.json及verification不覆盖。

已将报告源码窄修交回原报告聊天，独占Windows诊断槽：按57f条目保存旧像，先补真实所需头文件；在原失败树继续make-j2仅作兼容诊断，另存实际命令/退出，发现别模块问题先回报，不改业务/接口/权限/布局，不安装或改全局环境。待平台缺口收齐并停写后统一新冻结及最终对应验证；57f已通过结果保留原版本范围，不冒充后续修后源码结果。

资源驱动尚未运行，已补结束后解析真实STATUS_LOOPBACK_WIRE报文的只读核验：按正式frame_codec的帧头/长度/单片序号/XOR/帧尾核对Qj请求和SIMULATED应答、连续事件序号及至少20条实际请求/应答，区分正常关闭最多1条在途请求。仍只做一次30秒观测，未增加硬件/长测或预算结论；AST0不等于该未来观测通过。

### 两处显式头文件修复收口与5123新冻结

第一轮报告头补齐后，Windows continued make实际2，随后准确报校准存储QVariant不完整。主窗口按57f中的ccff38b3…精准备份`calibration_store.cpp`至正式`windows_include_preimage/`，只插入`#include <QVariant>`、保留其他原始字节/行尾，新SHA`3c398cde7c968153d7d33c7400cd33fe055a5ad14d7177935a5dd09f01a121c3`；报告窗口自身单行include的新SHA`13709d4d4054c5d03e5a3a361f4a0998063f22026bf89e18d119ecb38142ed6c`。这两处不改业务、API、权限、schema、布局，也未改全局Qt/编译器。

原报告窗口第二轮continued shell make0，独立新目录renderer qmake/make0，驱动0、两份PE32架构核对通过；准确源码426首尾不变、对旧冻结只差两份上述cpp。正式交接`04_references/sn100_host/report_remaining_20261025/windows_include_continuation_02_handoff.json`，SHA`8c84126ad2fdbd17c48297015fdbbb8475cae7322c1754f30efeae684af7fc3d2`。本次仅诊断构建，未伪称fresh最终发布/WindowsGUI，原首轮/续编失败分别原样保留。wait_threads确认该窗口idle/completed且error=null，源码及构建进程已停。

新完整快照仍426项，实际只差上述两项，SHA`5123bf1442472450bbde23a59fc47287dc85ead34e006c523a96a016ac76cf29`。主窗口执行显式RootGo保留脚本：核原57f主辅/200对象/22测试binary与每份实际test source后，把native、cumulative_build、standalone_final、projects_modules_final及Windows失败树迁存对应pre_windows_include_01目录；22份准确cpp与执行脚本副本保留，原proof/argv/退出不改。5个绝对移动目标均先核在本任务temp根内，未删除文件。完整映射已精准复制`04_references/sn100_host/qc_local_20261025/pre_windows_include_relocation.json`。

迁存目录中的匿名运行库仅保留历史，不改其canonical-path绑定；原路径记录不能指向后续新同名产物，也不直接重开迁存库或清UUID/hold。重新实验用全新匿名目录，必要原路径恢复须停写核对。历史正式collector按e65/57f各自归属保存，不计为新5123的通过。

随后从全新native目录开始5123整套WSL Release构建，最多-j2；此处只记启动及前快照SHA，不提前记成功。项目补验driver仅在保留旧c0b67f…执行源像后，把EXPECTED_FREEZE改为5123，新driver SHA`b2710670991dcc53e87549488bced897f8f670294220b66edc5ed3ffb71a3bf2`；后续22驱动/35全量、独立入口、6项目补验、Windows/ARM、短响应资源和配对恢复仍需实际新收据。另分原校准窗口仅做测量脚本只读预核，不执行任何构建/GUI/benchmark，不改源码。

### 5123整套原生全量编译实际终态

2026-10-26 01:38本地时间，新5123/426项输入的WSL Release主/辅全量构建实际退出0，主168/辅32对象均在本轮开始后从空目录创建；源码首尾完整集合及SHA相等。主SHA`054bffa7847881cd084f4792f81c796313396f3e07e72195aa20b89e9417b347`、辅SHA`2de67df76a6871884e52584cefb613fa89daa657a59938435f61f4bd875fe7eb`，Makefile SHA`df6bbee134af56a57cac5da631502188c07ab7c62e934eb3b06c62423e7527d0`。主辅字节与57f一致是本次鲜编的实际比对结果；新源码冻结仍是5123，未拿旧scope替代本轮。

现从新cumulative_build目录鲜编22个驱动，无跨cohort复用；随后完整35次将使用新的独立attempt并从第一项执行。资源脚本两处预核缺口正在仅temp工具中收口；源/业务继续冻结，后续独立入口、6项目补验、最终Windows/ARM及正式短观测尚未写通过。

### 5123累计登录1920断言失败与测试输入准备修正

5123的22驱动全部鲜编0后，`suite_final_07`前5调用实际通过，第6个login_xcb实际4/1、exit1：原`legacyOptInAndGuestLogin`第329行available.contains(cardRect)为false；登录其他两业务方法仍过。该失败目录仅有login_1600.png而没有1920截图，可推断失败在1920循环；原精确矩形没有日志，不能凭后续通过确定直接原因。完整计划当轮停止，后续未执行，原raw/command/exit/XML保留。

界面测试负责人独占GUI槽，先保存原5accedc3…测试cpp/pro到正式login_geometry前像，再用原bound新匿名单方法、私有几何/事件诊断及3次有界缓冲完整XCB观察，均通过但未抓到原false现场。实际记录到resize/ensureVisible前后宿主inactive，open/show后XCB才产生Move/Resize/Exposed/Active事件，原loginReady仅看动画和默认账户加载，没有准备原头像点击所需的native宿主状态。MousePress/Release警告在通过轮也存在，源Badge事件过滤器释放时发clicked后继续默认处理，不能单凭警告归因。

仅测试cpp新增prepareHostForInput：resize/show/raise/activate、sync窗口系统，在原15秒内核请求尺寸/exposed/active、头像可见可用且完整落在viewport，连续事件轮次宿主/滚动几何稳定后才做原点击。没有把contains放进等待条件；原450×571、立即contains、登录业务和密钥扫描全部保留。首轮过宽地给全局loginReady加active条件导致offscreen4/1，原失败与1192684…执行源原样保留；第二轮恢复原loginReady，只限制合法宿主输入准备，完整offscreen5/0（8.10秒）、XCB5/0（8.17秒）、实际退出均0。128条原断言源码行逐条保留，新增1条宿主准备断言，生产5123源码完全未改。

正式交接`04_references/sn100_host/password_login_20261024/login_geometry_repair_handoff.json`，SHA`d346e5f5c44cc153fe496d705654001fab1bcabb9abdb7dd5662fb4ff392524e`；新cpp SHA`8374aeea26e9a670a7fc27c88538db615ad31cf02c3bc19ecbc6d95ce5984afe`。原false直接原因仍未确证，不记生产布局缺陷已修。主代理回读新准备函数和未变contains/尺寸断言后，将原5123主22绑定迁存`cumulative_build_pre_login_geometry_01`并核22份准确旧cpp/binary SHA保存源像；原路径/退出不改，relocation对应关系另存。新绑定只鲜编这一测试，其他21必须同5123/167对象/源/moc/binary逐项相等才带真实原收据复用；之后从第一项重跑完整35，不把私有5/0拼成新整轮通过。

### 5123最终完整35次与两个独立入口通过

`cumulative_build`同5123绑定真实exit0：仅password_login_shell_test按8374aeea…新源鲜编，21未变测试在同167正式对象/源/Makefile及自身源/moc/object/binary SHA完全一致后带原编译收据复用；未借旧cohort产物冒充本轮。`suite_final_08`从首项完整执行35调用，实际全部0，无失败/跳过、full_suite=true、omitted=[]，426源未改，主054b/辅2de SHA相等。两显示模式登录均5/0，之前失败位置通过；QC page各7/0/shell各5/0，校准shell各6/0，报告剩余shell各5/0，cal引用22/0、QC引用20/0，报告核心各12/0、HTML各7/0，QC store16/0、cal store51/0/page各21/0，项目原子保护7/0（4.015秒）。原suite05 SIG和suite07几何失败直接原因仍未确证，保留原始失败，不以本轮通过关闭长期稳定性。

随后当前5123的独立QC/校准在全新`standalone_final`目录完成7阶段实际0，源码/主辅/native verification/两独立binary首尾相等。两页实际loaded，1800ms正常关闭；校准catalog由同批真实主壳owner初始化并观察ready/正常5worker关闭，临时初始化输入已删，未手造catalog或复用旧运行库。

现继续当前5123的6目标`projects_modules_final`鲜编与实际运行；新source freeze guard5123，父进程输出与原始阶段收据继续保留。Windows首轮兼容问题诊断已过，但最终对应新cohort的Windows/ARM、短观测与恢复包仍待实际完成。

### 5123六个项目/试剂目标与Windows最终构建通过

`projects_modules_final`六个目标均在全新目录实际qmake/make/运行，共18阶段退出0；项目高级11/0、项目基础9/0、试剂批组11/0、试剂基础11/0，未失败或跳过。项目与试剂两个独立程序均实际loaded后1800ms正常退出0，试剂已加载图由主代理实际查看；426输入及统一主辅/200对象首尾核对一致。此4组专项及2独立入口单列，不算进35次主回归。

`platforms/windows`使用PowerShell7全新构建，主168对象、辅助32对象，两个qmake/make均实际0；主SHA`7ed55948503fb838ac511641275605f084d776941e0b4344d685dafa511f7841`，辅SHA`9c9f26f6cdba94e7220bed0267404a7b9dcbdb35d8a35144f87e45fce6831ca9`，均PE32/i386。426源码与5123冻结及构建前后工具/外部依赖一致，完整每个子进程收据在解析前落盘。本轮只编译/链接，未启动WindowsGUI或实际枚举注册表、打开设备，不等于原Windows行为/主题通过。

当前ARM全量构建继续运行，现有sysroot只读核对，未安装、恢复或连接RK；响应/资源与配对归档仍待此构建及依赖/包检查完成。最终文档两个写入脚本经独立只读审查修正6点（顶端段落定位、恢复链接、校准原件依据、归档冻结绑定、受影响文档原字节/精确preimage及独占新文件、日常deploy与旧包恢复区分），未提前执行或勾选。质控/校准当前7张图已实际查看，1600保留外层滚动，不冒称全部控件同时适屏或完整原Windows视觉等价。
