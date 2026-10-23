# SN100 上位机移植

**当前推荐的统一脱机入口为O03-4：医院、账户权限、历史查询/保存/统计/CSV及原模板预览/PDF。** 已完成WSL报告专项XCB与offscreen各4项通过、原三页六项累计实验分别3项通过，以及60轮实际报告/PDF混合负载零失败；Windows32/ARM64主程序与helper构建、新ARM包依赖闭包/冻结通过。RK实屏、仪器、实体打印及长稳另验。[运行与维护](#o03-4) · [同一移植记录](../../02_records/20261022_sn100_host_migration.md#o03-4-offline) · [原始证据索引](../../04_references/sn100_host/integrated_report_20261023/index.json)。

根`main.cpp/sn100_host.pro/build.sh`仍是O01医院与O02-1账户独立入口；统一主壳使用`shell/`，已复用`history/`的原模板预览/PDF，独立历史入口继续用于对照。O05协议回放/回环/异常恢复在`protocol/`，O06统计与凝血点研究分别在`quality/`、`coagulation/`。保留Qt5 Widgets/qmake/C++14与原布局/颜色，所有现有入口始终脱机。

**最终交付菜单和约定功能齐全的统一上位机。**O03-4已把已有报告能力接入统一“结果报告”，后续按[业务实施顺序](../../01_docs/plans/sn100_host_migration_plan.md#integration-next-steps)补项目/试剂、样本任务等，每个切片及时接入并累计回归。试剂/样本仍需移植真实业务，当前菜单占位不算功能完成；设备联机另验。任务边界与待办维护在[整合卡](../../01_docs/plans/sn100_host_migration_plan.md#o03-4-card)。

**统一界面的RK准备入口已扩展报告依赖：**在WSL的`06_programs/`执行`bash sn100_host/shell/rk.sh prepare`，流程包含主程序/helper构建、离线依赖检查及冻结打包；显式`deploy/check/start/status/stop/collect`用于后续上机。根`rk.sh sn100_host all`仍为独立页，`shell/wsl.sh`为WSL/XCB入口。[准备与上机用法](#rk-prepare) · [首验卡](../../01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)。本轮O03-4正式prepare退出0，未连接RK/仪器。

| 文件 | 用途 |
| --- | --- |
| `main.cpp`、`sn100_host.pro` | 脱机入口与本模块依赖 |
| `ui/`、`resources/` | 原业务页面、提示框和实际使用的图标；医院中文助记符 |
| `data/` | 医院/账户存储源码，独立工作线程与事务；不是运行数据目录 |
| `tests/` | 真实窗口操作、锁库、重开读回、响应和目录边界；`fixtures/accounts.json` 仅含匿名公共字段 |
| `setup_wsl.sh`、`wsl.sh` | WSL 环境安装及日常构建/运行/测试/IDE |
| `prepare_arm64.sh`、`build.sh` | 从归档恢复 ARM64 依赖并构建；不自动部署 |
| `AGENTS.md` | 本工程规则 |
| `protocol/` | O05-1帧/Qj回放、O05-2 TCP/诊断/请求、O05-3异常恢复及有界长稳，限定自建127.0.0.1模拟端 |
| `history/` | O04 原报告页/明细、SQLite worker、GBK CSV、均值/CV与原模板预览/PDF，独立入口和三目标构建；`origins.json`记录依据/差异 |
| `coagulation/` | O06-2 自主凝血点研究原型：百分比法和一阶导数最大速率法；真实曲线/项目验证后续执行 |
| `quality/` | O06-1 原质控统计离线切片，含显式有效性标记；不含失控判定 |
| `shell/` | O03-4统一脱机主壳、原导航/资源、医院/账户/历史及报告预览/PDF；默认Guest，无设备通信，默认统一构建/运行在`integration/`，可用绝对`SN100_SHELL_WORK_DIR`隔离本轮目录 |

临时构建、运行数据和日志全部在根 `temp/sn100_host/`，不进入源码目录。参考原件仍在相邻 `sn100_host_reference/`，只读；本工程不加载其数据库或配置。

在 **WSL Ubuntu-24.04 / qyq** 的 `06_programs/` 中启动（首次自动编译）：

```bash
bash sn100_host/wsl.sh run
```

打开已登记原生 Qt5 Kit 的 WSL Qt Creator：

```bash
bash sn100_host/wsl.sh ide
```

默认数据目录为根 `temp/sn100_host/runtime/`。首次创建空数据库、`config.ini` 和 `offline-data.tag`；以后打开同目录保留数据。已有未标记的库/配置会被拒绝，避免覆盖原数据。SQLite 是医院名与字典的权威存储；INI 仅兼容导出，失败会显示警告。

医院信息的简单操作：统一窗口左侧点“用户设置”，右侧点“医院信息设置”；下方“医院名称：”右侧长框用于填写医院名称，位置在蓝色保存按钮上方。只改医院名时直接填写后点“保存”，无需点“添加”；“添加”是在上方表格新增科室/病人类型/医生名单，名称填好后助记符自动生成，再保存。医院名称是一项独立设置，保存后仍显示在下方框里，不会变成上方表格的一行。

**WSL拼音已配置：**正常`wsl.sh`、`shell/wsl.sh`及`history/wsl.sh`的GUI入口通过`wsl_ime.sh`自动接入Fcitx5。用`Ctrl+Space`切换拼音，输入`ceshi`后按空格选“测试”，输入`yiyuan`再选“医院”，最后保存。用户已实际确认候选字并保存`测试医院`，数据库一致。[实际结果](../../02_records/20261022_sn100_host_migration.md#hospital-ime-setup-20261023)。Windows侧保持英文键盘可减少快捷键被Windows输入法截获；Linux窗口内的拼音由Ubuntu负责。启动器只对目标GUI设置Qt输入变量；构建/自动测试及ARM/RK脚本不加载。`SN100_WSL_IME=off`可做原环境对照。[安装包、初始配置及恢复范围](../../07_backups/qt_environment/wsl_ime/README.md)已归档。此前“医院名称只读”的记录为误判，未修改业务源码。[早期保存实测](../../02_records/20261022_sn100_host_migration.md#hospital-manual-20261023)。

打开账户设置（默认选择匿名工程师，设备状态明确模拟为 ready）：

```bash
bash sn100_host/wsl.sh account
```

账户运行库单独保存在 `temp/sn100_host/runtime_account/user_Information.db`。首次只建立 `offlineSend`、`offlineTest`、`offlineCheck`、`offlineEngineer`、`offlineAdmin` 五个匿名角色账号，均不是默认账户；不导入原库。临时初始密码保存在 `temp/sn100_host/account_bootstrap.secret`，只供本地实验输入，不写入记录/截图或 Git。原版明文 `pwd` 格式暂时保留在可变临时库，生产认证方案另行验收。

可通过 `--test-session offlineCheck` / `--test-session ""` 选择模拟审核者/Guest，通过 `--device-state running` 覆盖模拟忙态；这是脱机测试入口，不是生产登录。更新成功、删除当前用户会注销，重新选择模拟会话需重启该入口。`--data-dir` 可指定另一个绝对实验目录；首次建立与既有库重开分开处理，不覆盖既有账户。

维护时从 `ui/frmaccountsetting.cpp` 的按钮槽追到 `data/account_store.cpp` 同名操作；原角色选项、列表过滤在页面中，写入前角色/密码/状态核验在 worker 中。源码原有的忙态比较、空新密码、管理员空角色索引、双击填入其他目标等特殊行为均有回归与记录，不把它们当新的安全设计。

重做实验时设置一个**新的绝对目录**，先写入验证，再用另一个进程重开读取：

```bash
SN100_EVIDENCE_DIR="$PWD/../temp/sn100_host/experiment_new" bash sn100_host/wsl.sh test writeExperiment
```

```bash
SN100_EVIDENCE_DIR="$PWD/../temp/sn100_host/experiment_new" bash sn100_host/wsl.sh test readExperiment
```

账户实验同样使用新目录，两个命令必须分别运行：

```bash
SN100_EVIDENCE_DIR="$PWD/../temp/sn100_host/account_experiment_new" bash sn100_host/wsl.sh account-test writeExperiment
```

```bash
SN100_EVIDENCE_DIR="$PWD/../temp/sn100_host/account_experiment_new" bash sn100_host/wsl.sh account-test readExperiment
```

独立数据目录/角色边界检查：

```bash
SN100_BOUNDARY_DIR="$PWD/../temp/sn100_host/account_boundary_new" bash sn100_host/wsl.sh account-boundary-test
```

界面实验依次执行，避免不同测试窗口争用焦点/模态确认。账户实验库含临时认证字段，仅留 `temp/`；正式证据保存去密码的 CSV/JSON、原始/标注截图、来源/校验清单和测量，不复制该库。

O05-1 在 WSL 的 `06_programs/` 下独立运行；输出目录必须是新的绝对路径，已有结果会被保护。以下命令会实际显示模拟 TX/RX、状态和错误，并保存 `events.jsonl`、`tx_rx.txt` 和 `summary.json`：

```bash
bash sn100_host/protocol/wsl.sh run --input "$PWD/sn100_host/tests/fixtures/protocol_replay.json" --output-dir "$PWD/../temp/sn100_host/protocol/my_replay_01"
```

协议测试单独运行，不使用其他模块的构建或数据库：

```bash
bash sn100_host/protocol/wsl.sh test
```

回放可加 `--stress 10000` 做固定负载短测；时间事件是合成单调毫秒，程序不按事件中的200秒等待。只有 `summary.json` 写出、`failed_expectations=0` 且进程退出0才算该次回放通过；状态字段是合成观测，不代表仪器在线。O05-2已复用同一`FrameDecoder`和`parseStatus`接入本机TCP，真实SN100版本和仪器端点仍待验。[O05-1来源清单](protocol/origins.json)与[实际结果](../../02_records/20261022_sn100_host_migration.md#o05-1-offline)保留原切片细节。

## O05-2 诊断消息与本机 TCP

在WSL的`06_programs/`中，以下命令创建本进程的127.0.0.1临时监听端，实际收发Qj/Qk/QH并解析合成诊断，额外10000条Qq分布在60秒内。输出路径须为新的绝对目录；不读取仪器配置，显式NoProxy，有限运行后退出：

```bash
bash sn100_host/protocol/wsl_transport.sh run --output-dir "$PWD/../temp/sn100_host/protocol/my_tcp_01" --stress 10000 --duration-ms 60000
```

请求层有限演示故意不回第一次Qj，触发一次重试，再完成Qk/QH；日志同时显示对端实际RX十六进制、合成TX及客户端解析观测。入队不等于对端收到；同类型观测因无事务ID，不能称仪器ACK。

```bash
bash sn100_host/protocol/wsl_transport.sh client-demo --output-dir "$PWD/../temp/sn100_host/protocol/my_client_01"
```

两种演示保存`wire.jsonl`与`summary.json`；核对`passed=true`、`outputs_saved=true`及退出0。`ReadOnlyClient::request`负责单请求/入队后计时与有限只读重试，`ReadOnlyTcpTransport`负责有界异步I/O/FIN排空，`parseReadOnlyMessage`负责纯值解析。Qq通道值保持原字符串，Qk/Ql与QK/QL大小写区分；措施文字只显示，UTF8为当前离线配置，实际编码待核实。测试分别运行：

```bash
bash sn100_host/protocol/wsl_transport.sh messages-test
```

```bash
bash sn100_host/protocol/wsl_transport.sh tcp-test
```

```bash
bash sn100_host/protocol/wsl_transport.sh client-test
```

ARM64构建复用已归档QtCore，额外7个包由`prepare_transport_arm64.sh`校验后只解入O05专属临时sysroot，不安装入系统或替换共享环境。两个脚本各接收一个绝对输出文件，构建后不部署：

```bash
bash sn100_host/protocol/transport_build.sh "$PWD/../temp/sn100_host/protocol/o05_2/arm64_release/sn100_protocol_loopback"
```

```bash
bash sn100_host/protocol/client_build.sh "$PWD/../temp/sn100_host/protocol/o05_2/arm64_release/sn100_read_only_client_demo"
```

已通过两端63/27/19项、固定负载和O05-1回归。原Windows完整应用、SN100协议/编码/实际连接、RK加载/运行与实屏、长稳及同板控制继续待验，详见[同一记录](../../02_records/20261022_sn100_host_migration.md#o05-2-offline)、[模块卡](../../01_docs/plans/sn100_host_migration_plan.md#o05-2-card)与[原始证据](../../04_references/sn100_host/protocol_transport_20261023/)。本轮无UI布局/主题变更。

## O05-3 异常恢复与长稳

本轮`long_run_01`已结束：2h持续通信通过；8h恢复阶段在19分10.178秒因Qk只读重试耗尽退出1，完整长稳未通过。原始失败证据和首阶段SHA已保存，自动通知跟进已删除，本轮未修改源码或自动重跑。见[实际终态](../../04_references/sn100_host/protocol_stability_20261023/long_run_01/job_status.json)与[失败复核](../../04_references/sn100_host/protocol_stability_20261023/long_run_01/recovery_8h_failed/failure_review.json)。

WSL `06_programs/`下可独立复测异常恢复；第二条会实际等待约202秒：

```bash
bash sn100_host/protocol/wsl_recovery.sh fault-test
```

```bash
bash sn100_host/protocol/wsl_recovery.sh silence-test
```

短时恢复演示及具体收发、序号和分段日志（须选新绝对目录）：

```bash
bash sn100_host/protocol/wsl_recovery.sh run --output-dir "$PWD/../temp/sn100_host/protocol/my_soak_smoke_01" --duration-seconds 60 --reconnect-seconds 2 --retry-every 2
```

`soak_main.cpp`创建自己的TCP模拟端，默认10条Qq/秒、每秒轮询只读Qj/Qk/QH；每10次逻辑查询故意不回第一次，触发一次有限重试。记录对端实际RX、合成TX和客户端解析，查询金字节与诊断连续序号独立核对；取消时未确认的旧字节单独退役，不算已送达。日志默认4MiB分段/512MiB总额，不删旧段；可用较低的`--segment-bytes`、`--log-budget-bytes`复测失败，最多4096段。心跳使用固定容量直方图，p95是1ms桶上界。

小时级新轮次由本地Python监督C++目标，Python不部署到RK；先定位本轮超时再冻结新版本。以下为新轮次的重测方法，本轮失败跟进没有执行它；父目录须存在，两个输出目录都必须全新：

```bash
python3 sn100_host/tests/run_protocol_soak.py --binary "$PWD/../temp/sn100_host/protocol/o05_3/wsl_soak/sn100_protocol_soak" --output-dir "$PWD/../temp/sn100_host/protocol/o05_3/long_run_02" --archive-dir "$PWD/../04_references/sn100_host/protocol_stability_20261023/long_run_02" --background
```

监督程序冻结可执行文件、源码清单和自己的副本，串行7200/28800真实秒，1秒采样RSS/PSS/CPU/线程/FD；阶段结束独立流式复查全部线日志/序号、汇总资源并归档SHA。实际诊断/心跳数须≥标称90%，监督暂停或墙钟与单调钟差异>120秒判失败，不把睡眠算持续负载；不修改电源策略/WSL/其他进程。若中断，仅回收自己启动的目标并保留部分证据。

ARM64只构建，不部署：

```bash
bash sn100_host/protocol/soak_build.sh "$PWD/../temp/sn100_host/protocol/o05_3/arm64_release/sn100_protocol_soak"
```

已完成异常/200秒/短时冒烟、累计回归及2h持续通信；8h恢复长测失败待定位复测。真实SN100/编码/端点、原Windows完整应用、RK/仪器、24–72h业务长稳及同板控制继续待验。[统一结果](../../02_records/20261022_sn100_host_migration.md#o05-3-long-run-01-result)与[当前模块卡](../../01_docs/plans/sn100_host_migration_plan.md#o05-3-card)分别维护实测和待办。

## O04-1 匿名历史查询与 CSV

在 WSL `Ubuntu-24.04 / qyq` 的 `06_programs/` 中运行独立历史页：

```bash
bash sn100_host/history/wsl.sh run
```

首次在 `temp/sn100_host/history/runtime/` 新建匿名数据库，仅含手写夹具：2026-10-22/23/24共6份报告（5份可见）。查询日期可用日历选2026-10-23；当天3份，样本号3的条码含双引号和逗号。参考路径、未标记旧库或链接目录会被拒绝。CSV只能写该目录 `exports/` 下新的 `.csv`，使用原入口的GBK；不会覆盖已有文件。归档证据不能作为继续体验的数据目录。

原业务区1751×968，详情1341×930，独立宿主通过外部滚动保持控件位置并让小屏可达。保留原深灰右栏/灰按钮/垂直结果表；未接入操作保留位置并禁用。输入正则沿用原版，部分患者字段要求ASCII首字符，长度正则疑点已登记待原运行对照；载入长文本/换行不截断，未修改字段原样保留。

复现实验选一个**新的绝对目录**，三个命令依次独立执行；实验全程操作真实WSLg窗口，避免同时运行其他GUI自动化争用焦点：

```bash
SN100_HISTORY_EVIDENCE_DIR="$PWD/../temp/sn100_host/history/my_experiment_01" bash sn100_host/history/wsl.sh test writeExperiment
```

```bash
SN100_HISTORY_EVIDENCE_DIR="$PWD/../temp/sn100_host/history/my_experiment_01" bash sn100_host/history/wsl.sh test readExperiment
```

```bash
SN100_HISTORY_EVIDENCE_DIR="$PWD/../temp/sn100_host/history/my_experiment_01" bash sn100_host/history/wsl.sh test paginationExperiment
```

ARM64单独构建（只接收绝对输出文件，不部署）：

```bash
bash sn100_host/history/build.sh "$PWD/../temp/sn100_host/history/arm64_release/sn100_history"
```

维护入口：`history/main.cpp`管理线程/宿主，`HistoryPage::savePatient()`读取实际修改字段并投递请求，`HistoryStore::savePatient()`事务落库；`exportCsv()`流式写GBK，格式规则集中在`displayValue()`。`tests/fixtures/history_reports.json`定义匿名字段/精度/单位，不代表真实仪器配置。[O04-1实际证据与结果](../../02_records/20261022_sn100_host_migration.md#o04-1-offline)。

## O04-2 均值/CV与原模板报告

同一`history/wsl.sh run`入口默认显示原均值/CV行，选2026-10-23的样本3可核PT均值12.9、CV6.14%，INR均值1.17、CV7.53%。统计只处理值快照，原复测任务、异常标志、缺失/非法值均保留；不可计算时通过原单元格tooltip说明原因，不写回结果。

选择报告后点击原“打印”进入原QtRPT预览，保留工具栏、缩放、翻页及210×148mm模板尺寸；“Save as PDF”只能在当前匿名目录`exports/`中新建PDF，不能覆盖已有文件。主程序和`sn100_report_renderer`必须同目录，`wsl.sh build/run`和ARM`build.sh`会一起构建。PDF在独立进程生成，支持取消文件选择、30秒超时和失败重试；脱机物理打印、HTML、XLSX保持禁用并待独立验收。

默认打印配置仅为手写匿名医院/项目名称，参考范围留空；不会读取O01真实配置或参考生产库。报告只列配置启用的原任务值，不打印均值/CV；RAWX明确禁用。样本3故意含换行，原固定模板会拒绝该报告；可用样本2检查正常预览和0.00。未保存患者草稿须先保存或取消；换行、模板控制符`<>[]`、缺字等不能安全呈现的内容会明确报错，不静默改数据或打印方块。

独立复现实验用新的绝对证据目录；PDF解析测试还需指定带`pypdf`的Python（仅测试工具，板端程序不使用Python）：

```bash
SN100_REPORT_EVIDENCE_DIR="$PWD/../temp/sn100_host/history/my_report_test_01" \
SN100_REPORT_PDF_PYTHON="/absolute/path/to/python-with-pypdf" \
bash sn100_host/history/wsl.sh report-test
```

业务规则在`result_statistics.cpp`和`report_document.cpp`；`report_render.cpp`绑定原内嵌模板，`report_controller.cpp`管理预览/异步导出及失败恢复，`report_renderer.cpp`只读快照生成PDF。`history_core.pri`保留查询/统计/CSV依赖，报告依赖单列`history_report.pri`；当前统一主壳同时链接两者，独立宿主未绑定打印能力时仍保持原按钮禁用。ARM报告依赖可用`history/prepare_report_arm64.sh`从正式归档恢复到临时sysroot，不连接RK。[独立模块结果](../../02_records/20261022_sn100_host_migration.md#o04-2-offline) · [统一报告入口](#o03-4)。

PDF中文映射已修复：独立helper记录原QtRPT最终绘制文字，用完整标签上下文核对字形，为同形不同码点分配独立CID，更新ToUnicode及字体映射；纸外标记在发布前删除。`report_pdf_mapping.cpp`仅处理本helper自产的Qt5 PDF，未知/歧义排版明确失败、不发布文件。保留原模板、嵌入字体及位置；Linux/Windows原样本12页像素零差异，汉字与部首并存、𠀋、连字及组合重音通过两解析器精确对照（仅忽略排版空白，无Unicode正规化/换字）。旧失败证据保留，[修复与资源增量](../../02_records/20261022_sn100_host_migration.md#o04-pdf-text)在同一记录。

严格文本复测：设置新的绝对`SN100_PDF_TEXT_TEST_DIR`及含`pypdf/pdfplumber`的`SN100_REPORT_PDF_PYTHON`，执行`bash sn100_host/history/wsl.sh pdf-text-test`；全程headless。独立解析器负例用`tests/history_pdf_mapping_test.pro`，`SN100_PDF_MAPPING_FIXTURE`指向正式证据`pdf_text_20261023/fixture/raw_offpage_values.pdf`，同时设置新的绝对`SN100_PDF_MAPPING_TEST_DIR`；具体构建/运行命令见记录。既有WSLg预览用例要求XCB，不把offscreen替代当GUI通过。Windows独立PDF helper仍隔离打印插件，使用内建PDF后端；该处理未作用于主GUI，Windows预览运行仍待验。

原Windows发行版完整行为/活动主题、RK屏幕与插件、仪器、实体打印/纸张/驱动、曲线/重算/LIS、条件XLSX及长稳/同板控制继续待验，完整O04不关闭。

## O06-2 凝血曲线凝血点算法研究原型

用户本轮确认需要凝血点判定，先保存两种自主实现，实际曲线测试后续再做：**百分比法**求基线到终末平台指定百分比处的交点；**一阶导数最大速率法**求曲线变化最快的采样时刻。它们是独立候选点，不默认相等，也不作为全部项目的临床PT/APTT。厂商资料、数值实现选择及待测边界见[来源清单](coagulation/origins.json)，完整结果见[同一移植记录](../../02_records/20261022_sn100_host_migration.md#o06-2-offline)。

在WSL `Ubuntu-24.04 / qyq` 的 `06_programs/` 中运行手工匿名示例（输出文件须是新的，父目录由本模块构建建立）：

```bash
bash sn100_host/coagulation/wsl.sh run --input "$PWD/sn100_host/coagulation/tests/fixtures/synthetic_curve.json" --output "$PWD/../temp/sn100_host/coagulation/manual_result_01.json"
```

示例明确为合成不对称曲线；实际百分比结果11.2秒、最大速率结果11秒。结果JSON保留输入、SHA、全部生效参数、各方法结果/失败与研究原型标记；同名输出会拒绝覆盖，再次运行换新文件名。输入为 `[加试剂后秒数, 光学数值]`，显式指定上升/下降、完整采集与基线/平台窗口；未完成、负/重复/倒序时间、平坦/低幅、平台不稳、明显多峰或边缘等均有错误/警告。所有幅度/窗口/稳定性默认值仅为工程试验设置。

维护入口是 `coagulation/clot_endpoint.h/.cpp` 的 `percentageEndpoint()` / `maximumDerivativeEndpoint()`；CLI和双平台构建独立于主壳。百分比交点线性插值；最大速率用奇数点局部线性回归，在实际采样点返回时间。现有合成检查与WSL/Windows32/ARM64构建不能证明AL4A等价、真实SN100或临床通过。后续先固定真实曲线的单位、时间零点、方向、采样/掩蔽期及项目，再做误差/干扰/无凝血等评估，通过后才接入业务界面和结果流程。

## O06-1 质控统计离线切片

`quality/`提取有完整原源码的均值、极差、样本方差、标准差和CV比值；没有迁入有疑点的失控判定规则。单点样本估计和近零均值CV明确标不可用，空输入/非有限/溢出失败。独立运行同样使用新的绝对输出目录：

```bash
bash sn100_host/quality/wsl.sh run --input "$PWD/sn100_host/quality/tests/fixtures/statistics_series.json" --output "$PWD/../temp/sn100_host/quality/manual_statistics_01"
```

六组手算结果、两平台单测/CLI、ARM64构建及短测证据见[同一移植记录](../../02_records/20261022_sn100_host_migration.md#o06-1-offline)。统计结果不代表凝血判点或临床质控规则验证完成。

## O03-1 脱机主框架与医院信息接入

此前O03-1仅接O01，WSL导航、未保存提示、忙态/锁库恢复、独立进程读回及真实退出短测通过，O01累计回归及Windows32/ARM64构建通过。[历史截图、匿名数据与测量](../../02_records/20261022_sn100_host_migration.md#o03-1-offline)保留原结果与边界；旧`temp/sn100_host/o03/`数据不迁移。当前启动方式已转为下述O03-2。

## O03-2 账户接入、权限与注销

主壳接入O02账户页，沿用原角色选项、列表过滤、增改删校验及提示框。WSL角色/越权、100次切页、未保存守卫、锁库、人工/自动注销、重开与正式退出通过；O01/O02/旧O03-1累计回归和Windows32/ARM64构建通过，见[同一移植记录](../../02_records/20261022_sn100_host_migration.md#o03-2-offline)。[心跳专项排查](../../02_records/20261022_sn100_host_migration.md#o03-2-latency)的18个独立进程全部通过：连续100次同步测试点击期间无心跳交付，复现1.669–1.832秒间隔；逐次让出事件循环后50.1–52.7ms，正常操作86.8–117.2ms。布局和原动画未改。旧1.556秒样本缺阶段标签，不能精确追溯；原综合测试只增加两处`qWait(10)`，构建通过但两次中文剪贴板输入失败，尚未完成其整组重跑。完整生产登录、通用权限表ACL、原Windows运行视觉、1600完整交互、RK/仪器、资源预算及长稳分别待验。

在WSL `Ubuntu-24.04 / qyq` 的 `06_programs/` 中运行独立目标：

```bash
bash sn100_host/shell/wsl.sh run
```

默认以Guest进入“用户设置 → 账户设置”（账户页index 0，医院页index 1）。Guest可看医院及进入账户页，账户列表隐藏，账户操作沿用原权限规则。保留原八项主菜单、五项用户设置菜单、主题和图标；其余业务页仍显示“尚未接入（脱机）”，设备操作及正式登录按钮不可用。原1920×1080业务画布保持坐标和比例，小屏通过外部滚动访问；可用`--viewport 1600x900`固定宿主尺寸。原O01/O02独立入口继续使用原命令。

需要核对工程师角色时，显式选择匿名模拟身份：

```bash
bash sn100_host/shell/wsl.sh run --test-session offlineEngineer
```

角色来自独立匿名库，`--test-session`不验证生产密码、不代表正式登录；顶栏保留原姓名/角色区域，悬停可见完整身份及模拟说明。`--device-state running`仅切换账户规则使用的模拟动作状态，仪器仍是脱机/未知。注销后重新选择模拟身份需要重启入口，不启用原仪器登录联动。

本轮构建、测试、日志和可变匿名数据全部放在`temp/sn100_host/o03_2/`下；默认`runtime/hospital/`保存医院库/INI，`runtime/account/`保存账户库。`--data-dir`可指定另一个绝对测试根，仍分别使用其`hospital/`与`account/`子目录。脚本只在账户库不存在时生成临时播种密码，建立五个匿名角色账号，退出时删除临时密码文件；既有库不重播种或重置。沿用两类库的标记、schema及旧数据保护，不读取参考配置/生产库、不打开O02或旧O03运行库，也不将归档证据目录用作体验目录。

切页、退出及手动注销先检查两页忙态；医院未保存内容即使暂时隐藏也要确认，默认“保留更改”。确认“放弃并继续”恢复最后成功读取/提交的医院快照，不把草稿写入数据库；账户离页清空六个密码框，未提交的非敏感用户名可以保留。顶栏手动注销在守卫通过后排队清除账户会话，收到真实Guest状态后才完成注销并清空账户姓名、密码和列表缓存。账户更新成功或删除当前人成功引起的自动注销，直接随提交结果回落Guest，不再弹医院放弃确认，也不丢医院草稿。退出时分别关闭两个库，再退出并等待各自工作线程。

账户接入实验使用同一个**新的绝对目录**，三个命令逐条独立执行；GUI自动化期间不要同时操作其他测试窗口：

```bash
SN100_SHELL_ACCOUNT_EVIDENCE_DIR="$PWD/../temp/sn100_host/o03_2/account_experiment_01" bash sn100_host/shell/wsl.sh account-test writeExperiment
```

```bash
SN100_SHELL_ACCOUNT_EVIDENCE_DIR="$PWD/../temp/sn100_host/o03_2/account_experiment_01" bash sn100_host/shell/wsl.sh account-test readExperiment
```

```bash
SN100_SHELL_ACCOUNT_EVIDENCE_DIR="$PWD/../temp/sn100_host/o03_2/account_experiment_01" bash sn100_host/shell/wsl.sh account-test applicationExitExperiment
```

第三项仅在Linux运行：按测试启动的PID定位窗口，以原生窗口关闭消息经过正常关闭守卫，核对真实退出码及两个库/线程收尾；需要本机已有`xdotool`及X11运行库。测试只把公开字段CSV/JSON、截图、日志和测量保存为长期证据，账户数据库和临时密码仅留`temp/`，不归档或进入Git。

需要单独观测导航绘制负载时，以另一个新目录运行`account-test navigationResponsivenessExperiment`；每次点击后给予10ms事件循环间隔，不做抓图、编辑、外部SQL或弹窗，另验模拟running下人工注销。它不能替代上述综合测试。专项诊断另提供`tests/shell_latency.sh`，冻结当前实际依赖源码后独立构建，逐阶段记录单调时间、每次10ms心跳及区间交集；不关闭动画或改UI，也不复用O02数据。

在WSL的`06_programs/`中，为新实验选一个未使用的`attempts/<小写编号>`，先构建：

```bash
export SN100_LATENCY_WORK_DIR="$PWD/../temp/sn100_host/o03_2_latency/attempts/check_01"
export SN100_LATENCY_WORK_DIR="$(realpath -m "$SN100_LATENCY_WORK_DIR")"
bash sn100_host/tests/shell_latency.sh build
```

取得共享GUI测试时段后，以新目录运行一次无间断导航；`run`核验冻结源码与可执行文件SHA，构建失败不会运行旧产物：

```bash
SN100_LATENCY_EVIDENCE_DIR="$SN100_LATENCY_WORK_DIR/runs/burst_01" bash sn100_host/tests/shell_latency.sh run burstNavigation
```

同一冻结产物再分别以新目录运行`pacedNavigation`、`normalOperations`、`evidenceOverhead`；前者保留100次点击并逐次`qWait(10)`，后两者分离正常业务与抓图/PNG/写盘/公开SQL/资源采样。`burstNavigation`可显式设`SN100_LATENCY_CAPTURE_FIRST=1`或`SN100_LATENCY_PROC_SAMPLER=1`做附加对照。每条件至少三个新进程，GUI与其他聊天串行。账户DB/tag/临时密码留在`temp/`；[本轮原值、冻结源码包与图表](../../04_references/sn100_host/offline_shell_latency_20261023/analysis_summary.json)已归档。ASCII剪贴板断言通过但有X11警告，不能据此判断中文/IME正常；心跳间隔及导航信号也不等于屏幕像素反馈或RK预算通过。

旧O03-1回归仍由`shell/wsl.sh test`运行，使用`SN100_SHELL_EVIDENCE_DIR`和上述`writeExperiment`、`readExperiment`、`applicationExitExperiment`三个方法；O01累计回归仍用`shell/wsl.sh hospital-test`和`SN100_EVIDENCE_DIR`。两者也应选`temp/sn100_host/o03_2/`下新的独立实验目录，构建分别在`wsl_tests/`、`hospital_regression/`；不会使用O02构建或运行数据。构建失败停止后续运行，不用旧产物继续测试。

ARM64只构建、不部署：

```bash
bash sn100_host/shell/build.sh "$PWD/../temp/sn100_host/o03_2/arm64_release/sn100_shell"
```

维护入口：`shell/main.cpp`管理外部宿主和两个worker；`frmMain::selectMainPage()`及`frmUserSet::selectUserPage()`管导航，`frmMain::requestLogout()`管人工注销。`AccountStore::sessionChanged`是会话唯一来源，原账户规则继续在`ui/frmaccountsetting.cpp`及`data/account_store.cpp`；两页`requestLeave()`管离页，`setEmbeddedInShell()`恢复原业务位置。来源与必要差异在`shell/origins.json`，原Windows运行视觉、完整ACL、RK/仪器及长稳单独验收。

模块划分、依赖和验收待办只维护在[移植计划](../../01_docs/plans/sn100_host_migration_plan.md)；实际输入输出、截图、数据、问题与复测只追加[移植记录](../../02_records/20261022_sn100_host_migration.md)。截图在根 `03_photos/`，保留的匿名实验数据在 `04_references/sn100_host/`，安装输入在 `07_backups/qt_environment/`。

## O03-3 统一脱机主界面：医院、账户与历史查询

**本节保留O03-3三页历史基线及累计回归入口；当前源码和下列同名脚本已升级为[O03-4报告整合](#o03-4)。**O03-3在原主壳接入医院信息、账户设置和历史查询/CSV。医院与账户位于“用户设置”，历史查询位于“结果报告”；保留主壳1920×1080及历史页1751×968业务画布，通过外部滚动适配较小窗口，不重排原按钮和业务分组。历史实测见[O03-3记录](../../02_records/20261022_sn100_host_migration.md#o03-3-offline)，完整原Windows视觉、RK/仪器及长稳分别验收。

在 **WSL Ubuntu-24.04 / qyq** 的 `06_programs/` 中运行：

```bash
bash sn100_host/shell/wsl.sh integrated-run --test-session offlineEngineer --viewport 1600x900
```

`--viewport`和`1600x900`是两个参数。上例显式选择匿名工程师；省略`--test-session offlineEngineer`时默认为Guest。会话仅来自`AccountStore::sessionChanged`，`--test-session`只选择脱机匿名身份，不验证生产密码，也不代表正式登录。账户页沿用已移植角色规则；原历史查询、明细患者保存和CSV没有显式按角色门槛，本次不另造权限矩阵，生产访问策略仍待确认。

构建、日志和可变运行数据使用独立的`temp/sn100_host/integration/`。默认运行根为其`runtime/`，其中三个目录分别保存各模块数据：

| 运行子目录 | 内容 |
| --- | --- |
| `hospital/` | 医院/字典SQLite库、兼容INI及离线数据标记 |
| `account/` | 匿名账户库；含临时认证字段，只留本地实验目录 |
| `history/` | 匿名历史库、离线数据标记及`exports/`下的GBK CSV；O03-4在同一导出目录增加PDF |

`--data-dir`可指定另一个绝对实验根，三个子目录仍分别管理；既有未标记库/配置、参考目录等继续按各store边界拒绝。此入口不复用独立O01、O02、O03-2或历史页的旧运行库。首次账户初始化使用临时secret文件，脚本不打印其内容，进程退出时清除；既有账户库不重播种或重置。不要把账户库、secret文件或真实患者资料放入正式证据。

切页、退出及人工注销先检查在途操作；医院或患者明细未保存时默认“保留更改”。确认放弃后恢复最后成功读取或提交的内存快照，放弃动作不写盘。历史页的标题栏关闭、Escape和宿主离页共用守卫；允许离页后关闭查询日历、导出范围及文件选择器，避免留下模态悬窗。账户页继续清除密码输入，会话注销与权限刷新由主壳统一处理。初始化失败向宿主上报，宿主以失败退出路径关闭三个库并等待三个工作线程，不等待不存在的ready。

O03-3交付时主壳只链接`history/history_core.pri`；当前`shell/shell.pri`已同时链接`history_report.pri`及其QtPrintSupport等依赖，`--history`会接入预览/PDF，规则见下节。原生产登录、自动设备连接、LIS、校时及仪器动作仍未接入统一入口。

仅构建统一程序：

```bash
bash sn100_host/shell/wsl.sh integrated-build
```

ARM64构建仍接收一个绝对输出文件路径，只构建、不自动部署：

```bash
bash sn100_host/shell/build.sh "$PWD/../temp/sn100_host/integration/arm64_release/sn100_shell"
```

此命令仅构建。准备部署包使用下节`shell/rk.sh prepare`，板端启动封装会显式加入`--offline --accounts --history`、独立匿名路径及必要的首次初始化。不要用固定x86_64/XCB的`shell/wsl.sh`或独立页`rk.sh sn100_host all`代替三页部署。

### 集成累计回归

以下命令需要当前WSLg/XCB及既有`xdotool`/X11运行环境；GUI测试按约定时段依次运行，避免其他自动化窗口争用焦点。`integrated-test`只构建程序和测试二进制，不启动GUI：

```bash
bash sn100_host/shell/wsl.sh integrated-test
```

选择一个尚未使用的绝对证据目录，下面六个命令分别启动新进程，按顺序执行；不要对已有结果重复运行写入、视口、失败注入、测量或退出实验。`SN100_SHELL_APP`须指向同次构建得到的最终程序。

```bash
export SN100_INTEGRATION_EVIDENCE_DIR="$PWD/../temp/sn100_host/integration/my_experiment_01"
export SN100_SHELL_APP="$PWD/../temp/sn100_host/integration/wsl_release/sn100_shell"
export QT_QPA_PLATFORM=xcb

"$PWD/../temp/sn100_host/integration/wsl_tests/integrated_shell_test" writeExperiment
"$PWD/../temp/sn100_host/integration/wsl_tests/integrated_shell_test" readExperiment
"$PWD/../temp/sn100_host/integration/wsl_tests/integrated_shell_test" viewportExperiment
"$PWD/../temp/sn100_host/integration/wsl_tests/integrated_shell_test" initializationFailureExperiment
"$PWD/../temp/sn100_host/integration/wsl_tests/integrated_shell_test" measureExperiment
"$PWD/../temp/sn100_host/integration/wsl_tests/integrated_shell_test" applicationExitExperiment
```

`writeExperiment`覆盖医院、账户会话、历史明细保存/守卫、锁库恢复和CSV；`readExperiment`另进程核对已提交数据。`viewportExperiment`检查两种宿主尺寸下的控件可达、滚动、命中、焦点与Escape；`initializationFailureExperiment`仅向新建专用目录注入未标记文件，核对拒绝初始化、文件保留、退出码3与三线程收尾。`measureExperiment`使用同一匿名数据至少测60秒，记录操作完成、10ms心跳及资源口径；`applicationExitExperiment`按该测试启动的PID定位窗口，核对就绪后闲置、正常窗口关闭、退出码0及三个库/线程收尾。通过结论以每次实际日志和输出文件为准，构建成功不代替这些实验通过。

### 维护入口

| 位置 | 负责内容 |
| --- | --- |
| `shell/main.cpp` | 显式运行参数、三个store/worker生命周期、就绪汇总与初始化失败退出 |
| `shell/frmmain.cpp`、`frmuserset.cpp` | 原导航、单一账户会话、权限刷新、医院/账户离页与人工注销 |
| `shell/history_shell.cpp` | 历史页及报告控制器挂接、状态回传、导航守卫与焦点恢复 |
| `history/history_page.cpp::requestLeave()` | 明细草稿确认/快照恢复，查询及导出弹窗收尾；不替代worker关闭 |
| `history/history_core.pri` | 历史查询/统计/CSV的稳定构建依赖，不含报告打印后端 |
| `tests/integrated_shell_test.cpp` | 上述六项累计实验；守卫专项另见`history_page_guard_test.cpp` |

旧`build/run/test/account-test/hospital-test`模式仍使用`temp/sn100_host/o03_2/`；新增`integrated-*`使用`integration/`，不要混用构建、实验数据或证据目录。模块依赖与待办继续维护在[移植计划](../../01_docs/plans/sn100_host_migration_plan.md)，不在此另建第二份完成清单。

<a id="o03-4"></a>

## O03-4 统一原模板预览与PDF

在同一个“结果报告”页面查询并选中报告，点原“打印”打开QtRPT预览，再用原工具栏“Save as PDF”选择当前`history/exports/`下尚不存在的`.pdf`文件。预览、缩放、翻页和210×148mm原模板继续复用；没有重画页面或调整业务分组。主程序`sn100_shell`与`sn100_report_renderer`必须同目录，导出由独立helper完成；文件选择可取消，缺helper或渲染失败会保留清楚状态并允许重试，已有文件不会被覆盖。实体打印、HTML和XLSX仍禁用并待验。

报告医院名只取`HospitalStore`成功读取或提交的值。医院名称为空时先到“用户设置 → 医院信息设置”填写并保存，否则拒绝预览；未保存的医院或患者草稿不会进入报告。报告操作在途时拒绝切页、注销及退出，关闭预览后仍等待正在导出的helper完成。项目列、精度和参考范围继续使用明确的匿名测试配置，不能当作真实SN100项目配置或临床参考。

在WSL的`06_programs/`中运行；`SN100_SHELL_WORK_DIR`可覆盖构建/日志/默认运行根，必须是绝对路径：

```bash
export SN100_SHELL_WORK_DIR="$(realpath -m "$PWD/../temp/sn100_host/o03_4")"
bash sn100_host/shell/wsl.sh integrated-run --test-session offlineEngineer --viewport 1600x900 --data-dir "$SN100_SHELL_WORK_DIR/runtime_new_01"
```

首次使用选新的匿名运行根；既有运行库不迁移、重置或清理。省略`--test-session`仍为Guest，模拟身份不代表生产登录。只构建时执行`bash sn100_host/shell/wsl.sh integrated-build`；脚本先在暂存目录构建helper和主程序，两者成功后才发布到`$SN100_SHELL_WORK_DIR/wsl_release/`。本轮WSL Qt5.15.13/x86_64报告与累计回归通过、Windows Qt5.14.2/MinGW32和ARM64 Qt5.15.13主程序/helper构建通过，新ARM包闭包/冻结通过；RK实际运行及原Windows发行版完整对照仍待验。

报告专项QtTest使用`tests/integrated_report_test.pro`，独立于前节六个累计实验。先将`SN100_REPORT_PDF_PYTHON`设为已有、含`pypdf`的Python绝对路径，仅用于电脑上的独立PDF核对。下面命令从`06_programs/`开始，构建目录与证据目录分开：

```bash
export SN100_REPORT_SOURCE="$PWD/sn100_host"
export SN100_REPORT_TEST_BUILD="$(realpath -m "$PWD/../temp/sn100_host/o03_4/report_tests_new_01")"
mkdir -p "$SN100_REPORT_TEST_BUILD"
(
    cd "$SN100_REPORT_TEST_BUILD"
    /usr/lib/qt5/bin/qmake "$SN100_REPORT_SOURCE/tests/integrated_report_test.pro" -spec linux-g++ CONFIG+=release CONFIG-=debug && make -j4
)
```

构建成功后，在协调好的GUI时段运行两个关联用例；`SN100_INTEGRATED_REPORT_EVIDENCE_DIR`必须是绝对路径且首次不存在，第二个用例在同一进程重开第一个保存的数据：

```bash
: "${SN100_REPORT_PDF_PYTHON:?请先指定含pypdf的Python绝对路径}"
export SN100_REPORT_RENDERER="$SN100_SHELL_WORK_DIR/wsl_release/sn100_report_renderer"
QT_QPA_PLATFORM=xcb SN100_INTEGRATED_REPORT_EVIDENCE_DIR="$SN100_SHELL_WORK_DIR/report_experiment_new_01" \
    "$SN100_REPORT_TEST_BUILD/integrated_report_test" committedHospitalAndReportLifecycle persistedHospitalAndLegacyEntry
```

仅测60秒真实报告/导出与三页切换负载时，以另一个新证据目录单独选择方法：

```bash
QT_QPA_PLATFORM=offscreen SN100_INTEGRATED_REPORT_EVIDENCE_DIR="$SN100_SHELL_WORK_DIR/report_load_new_01" \
    "$SN100_REPORT_TEST_BUILD/integrated_report_test" reportLoadExperiment
```

该方法逐轮记录预览/PDF/导航时长，10ms心跳及Linux `/proc`父/子进程CPU、RSS/PSS、线程/FD、导出增长和helper收尾；本轮60轮零失败是在ARM编译等共载下的基线，预算未知，不作时延、资源或长期稳定放行。offscreen结果也不代替WSLg视觉。前节六个三页累计实验继续分别启动新进程、按顺序使用其共同证据目录；不能合并为一个长QtTest进程或并行争用GUI。

维护时从`shell/main.cpp`的`--history`进入`frmMain::attachHistoryPage()`与`enableHistoryReports()`；`frmmain.cpp`监听医院store的成功读取/保存，`history_shell.cpp`传递已提交医院名、报告忙态及结果状态。`report_controller.cpp`接值快照并管理预览/helper，`report_document.cpp`保持匿名项目列合同；独立历史入口保留自身匿名默认值。[本轮结果与边界](../../02_records/20261022_sn100_host_migration.md#o03-4-offline) · [日志、PDF、数据核对与SHA](../../04_references/sn100_host/integrated_report_20261023/index.json)。

<a id="rk-prepare"></a>

## RK统一界面：离线准备与上机入口

本节命令在 **WSL Ubuntu-24.04 / qyq、项目的`06_programs/`目录**执行。O03-3离线准备已有[历史结果](../../02_records/20261022_sn100_host_migration.md#rk-prepare-20261023)；O03-4已增加报告helper、字体与依赖，本轮正式ARM构建/闭包/冻结包通过。下面部署、预检、启动、停止及回收的RK命令尚未在实板执行，实际状态以[本轮记录](../../02_records/20261022_sn100_host_migration.md#o03-4-offline)为准。

### 先在电脑完成准备

```bash
bash sn100_host/shell/rk.sh prepare
```

该命令不连接RK。它校验归档输入，在默认`temp/sn100_host/rk_prepare/`隔离构建，以默认2个编译任务生成Release ARM64主程序与helper，检查解释器、递归依赖、provider符号版本及SQLite/offscreen插件。可用绝对`SN100_RK_WORK_DIR`隔离新的准备目录。源码在构建中变化、缺库/架构或版本不符均阻止发布；已有旧产物不会绕过失败继续上传。首次缺少SQLite输入时只访问Ubuntu官方HTTPS包源，校验索引SHA后归档，后续复用归档。

| 结果 | 位置与范围 |
| --- | --- |
| 统一ARM产物 | 默认`temp/sn100_host/rk_prepare/build/sn100_shell`及同目录`sn100_report_renderer`；本轮使用独立`temp/sn100_host/o03_4/rk_prepare/`，指纹见记录 |
| 离线依赖报告 | `rk_prepare/dependencies/`是发布前临时ELF的检查，`final_dependencies/`为本轮对最终路径的复核；静态通过不等于板端加载通过 |
| 冻结发布包 | `rk_prepare/packages/sn100_shell_<完整release_id>.tar.gz`；`latest_package`记录绝对路径，旁边有包SHA |
| 包内清单 | `SHA256SUMS`、`release_id`、`release.json`记录二进制/脚本/输入指纹；不含数据库、secret或旧配置 |
| 私有运行依赖 | SQLite与报告递归闭包中的私有库（包括fontconfig/expat），SQLite插件及helper的`plugins/platforms/libqoffscreen.so`；准确文件以`release.json`和SHA清单为准 |
| 报告字体 | 随包带Noto CJK Regular/Bold、许可及`fonts.conf`，仅为本次应用设置字体环境，不改系统字体配置 |
| 系统依赖 | Qt基础库、EGLFS/KMS及GPU/输入/图像支持仍须板端预检；`fc-match`需现场可用；不把loader或系统驱动装进应用包 |
| 安装/恢复输入 | `07_backups/qt_environment/sn100_rk_runtime/`保留SQLite包、历史RK loader的检查输入、来源与SHA；loader仅供本机检查 |
| 实验与日志 | `rk_prepare/logs/`、`shutdown/`、`workflow_tests/`和`collected/`各自隔离；正式非敏感证据见本轮记录 |

### 设备到场后按序执行（本轮未执行）

仅上传经过重新构建/离线检查的统一版本；不启动应用、不安装系统包：

```bash
bash sn100_host/shell/rk.sh deploy
```

默认目标为`lckfb@192.168.100.123`，根目录`/home/rkshare/programs/sn100_shell/`；可用`RK_HOST`和`RK_REMOTE_ROOT`改变主机/父目录。发布到`releases/<id>/`，核对上传SHA、完整文件清单、主程序/helper及SQLite/offscreen插件的实际`ldd`后才切`current`。运行锁/图形服务存在时拒绝覆盖；旧release保留，有旧current时保存`previous_release`指针，不迁移或重置运行数据。

只做当前板端环境预检：

```bash
bash sn100_host/shell/rk.sh check
```

预检核对普通用户/aarch64、主程序及helper依赖、Qt SQLite/offscreen/EGLFS/KMS插件、包内字体清单、HDMI连接、DRM/输入权限及Noto CJK字体匹配。缺项报错，保留实际输出，不自动安装字体/驱动、改权限/内核或切换Qt。预检不能替代实际中文字形、IME、弹窗、滚动和键鼠验收。

默认Guest启动统一医院、账户与历史报告入口：

```bash
bash sn100_host/shell/rk.sh start
```

需要验证匿名工程师分支时显式选择：

```bash
bash sn100_host/shell/rk.sh start 1800 offlineEngineer
```

仅显示分配器使用现有`sudo -n systemd-run/openvt/runuser`权限，Qt以SSH普通用户运行；VT7或DRM占用/查询失败则拒绝启动。EGLFS/KMS配置、XDG目录与环境只作用于本次进程，保留1920×1080业务画布，显式1600×900滚动宿主。无自启或常驻服务安装；瞬态服务名为`sn100-shell.service`。等待`SHELL_READY`才报告就绪，PID存活不算就绪。

数据在`sn100_shell/runtime/data/{hospital,account,history}/`，现有未标记根/部分账户初始化会拒绝自动重播种。首次随机secret只存700运行目录内的600临时文件，就绪或退出即删；不打印内容、不导入WSL账户库。默认模拟身份为Guest，显式身份不算生产登录。

`SEC`默认为1800秒，0为不设软截止；到期只请求经过原页面守卫的正常关闭。草稿、忙态或确认框可能使关闭保持待处理，不宣称硬超时或强制成功。SIGTERM/SIGINT经GUI信号桥走原`viewport.close()`；正常关闭应出现三个`STORE_CLOSED`、`THREADS_STOPPED true`及真实退出码0。

查看/请求关闭/回收分别执行：

```bash
bash sn100_host/shell/rk.sh status
```

```bash
bash sn100_host/shell/rk.sh stop
```

```bash
bash sn100_host/shell/rk.sh collect
```

停止前核对boot ID、PID、start_ticks、UID和exe；不匹配不发信号。关闭15秒仍被守卫挡住时返回4并提示在屏幕处理，不发SIGKILL。监督记录真实`wait`退出码，先原子发布结果，再发布退出标记；监督自身收到停止信号也先请求应用关闭并继续收尾。

每次运行在`runs/<id>/`保存应用/监督日志、启动环境摘要、真实退出结果和约1秒资源原值：CPU ticks、RSS/PSS、线程/FD、温度和频率；缺项写`unavailable`。这是观测原值，不是预算或实时性达标结论。采集先复制固定白名单再打包，运行中采集不包含随后才出现的完成结果；不收数据库、secret、home或完整环境变量。

### 离线回归入口

以下均在电脑执行，不连接设备。native fixture与mock SSH只验证生命周期/发布控制流：

```bash
bash sn100_host/tests/rk_deployment_test.sh
```

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s sn100_host/tests -p test_arm64_check.py -v
```

关闭桥的专用QtTest为`tests/shutdown_request_test.pro`，包含实际统一主壳offscreen信号关闭；本轮结果和独立Windows32构建见同一记录。RK EGLFS/Qt插件实装、物理显示/中文输入、部署/服务权限和真实退出仍须现场分别验收。
