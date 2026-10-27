# SN100 上位机移植


<a id="raw-inbox-offline-usage"></a>
### 原报文离线导入与查看

在现有账号数据目录的启动命令上增加 `--lis-settings --lis-raw-inbox`，并保留 `--accounts`。首次匿名账号初始化沿本页既有临时密码步骤；重开已有目录不再seed。数据只落在指定数据根的 `lis_raw_inbox/`。

启动后打开辅助窗；之后可从设置→LIS页空白处右键选择“离线原报文收件箱…”，或按Ctrl+Shift+I唤回。选择包含帧边界的单帧UTF-8 DSR原报文文件，核对报文控制号，再确认导入；仅选文件不会保存。列表可查看纯文本原文，始终标明未映射、未应用检测任务。

Guest只读，导入沿用真实已认证角色权限。失败后重试沿用首次读取内容；换文件先明确放弃当前草稿，已保存记录保留。关闭主程序会等待数据库关闭回执和后台线程退出。当前Linux组件与主程序启动/重开/退出有离线证据；主程序认证后完整导入、物理菜单/视觉、Windows安全导入及真实LIS仍待验。
**当前功能补齐（2026-10-27）：**本地资料、样本草稿、单孔质控预设、质控历史、报告本地处理及原值结果后端已有正式代码。项目结果显示配置已在真正主程序从原菜单保存，关闭后同目录重开能读回四类结果、单位、小数位及独立主结果/选用/显示。正常关闭暴露了页面销毁仍通知主窗口已释放成员的问题，原程序自然异常退出及调用栈已保留；仅两页析构已作最小修正，新冻结518（2d45ecd8）正在全新编译，修后关闭仍待验。本次未观察到报告自动刷新来源提示，不登记该项通过。报告使用当前已提交配置，四原值不改，不冒检测时单位快照；新原值文件输出仍关。检测调度、完整算法、真实结果、LIS收发、实时告警、正向打印及一致业务备份恢复仍有实现或具体资料缺口。


**当前实现结论：**尚未完成全套SN100业务。本地资料、样本草稿、单孔质控预设、质控历史和报告本地处理已有正式实现；检测投递与暂停恢复、完整校准拟合逆算审核、真实质控判定、真实结果与单位映射、LIS收发、实时告警互锁、一致业务备份恢复仍有程序或资料缺口。打印正式接线已有电脑缺设备失败链验证，正向确认与实际出纸待验；离线原报文辅助组件及真正主程序的初始化重开/关闭已通过电脑验证，仍非完整LIS。功能实现与验证范围分别登记，不能说代码全齐只等设备。



**用户当前重点（2026-10-26 18:33）：**先逐项确认功能有没有实现，再优先补有依据、能离线完成的缺口。已实现但待验证、确实未实现、缺资料、只待设备验证分别登记；测试次数和源码数量不当作功能完成度。原三个窗口继续分担，每10分钟检查保持ACTIVE；新功能和修复做必要针对验证，已有通过项不反复跑。
本轮447离线结果：WSL主辅210对象鲜编、Windows32与ARM64主辅编译、56项ARM离线依赖、完整35次调用、四组项目试剂回归和四独立程序启动关闭通过；匹配恢复包及短测均留证。保存提交后页面回读p95约107毫秒，高于尚未认可的100毫秒候选，不能称预算达标；历史图卡CPU绘制返回p95约53毫秒，不是屏幕合成延迟。[本批固定索引](../../04_references/sn100_host/qc_history_integration_20261026/checkpoint_index.json)。

**历史426检查点（已冻结）：**质控、校准、项目高级方案/试剂批组与报告剩余本地业务已接统一程序；上一轮426项输入（冻结`5123bf14`）的完整35次离线调用、额外4组项目/试剂测试和4个独立界面启动/正常退出均通过。WSL实际运行、Windows32/ARM64主程序与报告辅助程序全量编译、ARM离线依赖及匹配应用/源码归档通过；单次40保存响应与30秒整程序资源基线已留证。[本轮总索引](../../04_references/sn100_host/qc_local_20261025/final/verification.json)。原Windows完整行为/活动主题、RK实屏/输入、仪器/实际LIS/实体打印、真实结果链、临床校准/质控、认可资源预算/同板实时控制及长稳仍待验；该历史检查点的语言设置仍占位；新中文页状态见上方当前说明，原版未启用的XLSX未开放。旧报告查询、8小时恢复，以及suite05校准引用崩溃和suite07登录1920几何失败的直接原因尚未确证，原失败保留。

O10-2使用`--lis-settings`显式接用户子页4，`integrated-run`已带此参数，无参数保留旧路径。`ui/frmlissetting.cpp::on_btnSave_clicked()`→`data/lis_settings_store.cpp::save()`在独立worker保存显式`settings/lis.ini`，原9键及12隐藏/无关键保留；一次保存两套模式草稿，成功原子提交后更新GUI。私有锁目录支持父目录临时只读后的立即恢复，错误不清草稿；Linux仅sysfs/dev stat、Windows仅固定注册表KEY_QUERY_VALUE候选，不打开端口、自动选项只存意图。原Save的自动连接已隔离，HL7/MLLP/ACK及真实通信仍待做。[当前卡](../../01_docs/plans/sn100_host_migration_plan.md#o10-lis-settings-card)。

根`main.cpp/sn100_host.pro/build.sh`仍是O01医院与O02-1账户独立入口；统一主壳使用`shell/`，已复用`history/`的原模板预览/PDF，独立历史入口继续用于对照。O05协议回放/回环/异常恢复在`protocol/`，O06统计与凝血点研究分别在`quality/`、`coagulation/`。保留Qt5 Widgets/qmake/C++14与原布局/颜色，所有现有入口始终脱机。

O10-1已接原打印机设置子页3，独立两QPA各14/0、统一各5/0与累计通过；361源三平台产物、响应/资源和双恢复包已收口，旧353/348保持独立。`--printer-settings`显式启用，`integrated-run`已带此参数；无参数保持旧入口。配置为显式`--data-dir`下`settings/printer.ini`、原键`printer/name`，只读名称/列表/Apply沿原版，不改系统默认设备；Apply本身只保存配置，不提交打印任务。

维护者从`ui/frmprintersetting.cpp`的Apply追到`data/printer_settings_store.cpp::save()`，worker执行枚举与实际字节配置原子写入，成功后才更新GUI提交快照；保留无关键，空列表/失联原名、错误和草稿可恢复。`shell/printer_shell.cpp`/`frmuserset.cpp`负责最终页位与守卫，`shell/main.cpp`负责首读/正常退出。原页没有角色门槛或Cancel，本地沿用各角色，统一离页确认为安全补全。原`.ui`字节和几何保留；1600×900用现有外层滚动访问底部Apply，原Windows活动主题/RK实屏另验。[当前卡](../../01_docs/plans/sn100_host_migration_plan.md#o10-printer-settings-card)

原报告未把保存的设备名传入`printExec`；本轮正式源码现由`PrinterReportShell`把已提交设置接入报告任务，运行验收以当前功能卡为准。统一入口同时启用`--accounts --history --printer-settings`后，先在“用户设置→打印机设置”选择设备并Apply；收到保存成功后再打开报告预览，点原预览工具栏“Print”才会请求独立确认窗口。预览固定当时的设备名和报告内容，后来保存的新设备只影响下一份预览。缺设备时明确拒绝，不偷偷换系统默认设备；取消时不提交，软件提交只显示“交给本机打印系统，实际出纸尚未确认”。异常后结局不明时不自动重试，需要人工核对。本地设置通过不等于实体打印。目标打印后端/插件及服务、真实设备、服务挂起时枚举/有界退出、磁盘满/断电、认可资源/输入响应与RT长稳另验；本轮不更改系统打印配置或连接仪器。

**最终目标是菜单和约定功能齐全的统一上位机。**项目/单位、试剂基础和样本本地任务已迁入；离线与实物验收边界见[当前卡](../../01_docs/plans/sn100_host_migration_plan.md#catalog-sample-card)。试剂消耗映射、足料/实际测试次数、设备下发与执行仍待实现和验证。

**统一界面使用`shell/rk.sh prepare`构建、离线依赖检查和冻结打包；**显式deploy/check/start/status/stop/collect用于后续上机。根`rk.sh sn100_host all`仍为独立页，`shell/wsl.sh`为WSL界面入口。本轮426项输入的[匹配ARM应用/源码](../../07_backups/sn100_host/offline_four_windows_20261025/README.md)已归档；此前371等包保留，本轮没有连接RK或仪器。[首验卡](../../01_docs/plans/sn100_host_migration_plan.md#rk-offline-card)。

| 文件 | 用途 |
| --- | --- |
| `main.cpp`、`sn100_host.pro` | 脱机入口与本模块依赖 |
| `ui/`、`resources/` | 原业务页面、提示框和实际使用的图标；医院中文助记符 |
| `data/` | 医院/账户SQL及打印/LIS本地INI存储、只读端口候选源码，各归工作线程；不是运行数据目录 |
| `tests/` | 真实窗口操作、锁库、重开读回、响应和目录边界；`fixtures/accounts.json` 仅含匿名公共字段 |
| `setup_wsl.sh`、`wsl.sh` | WSL 环境安装及日常构建/运行/测试/IDE |
| `prepare_arm64.sh`、`build.sh` | 从归档恢复 ARM64 依赖并构建；不自动部署 |
| `AGENTS.md` | 本工程规则 |
| `protocol/` | O05-1帧/Qj回放、O05-2 TCP/诊断/请求、O05-3异常恢复及有界长稳，限定自建127.0.0.1模拟端 |
| `history/` | O04 原报告页/明细、SQLite worker、GBK CSV、均值/CV与原模板预览/PDF，独立入口和三目标构建；`origins.json`记录依据/差异 |
| `coagulation/` | O06-2 自主凝血点研究原型：百分比法和一阶导数最大速率法；真实曲线/项目验证后续执行 |
| `quality/` | O06-1 原质控统计离线切片，含显式有效性标记；不含失控判定 |
| `quality_control/` | 原质控页面及控制品、项目、批次、规则元数据本地管理；独立SQL worker，真实曲线/失控规则未开放 |
| `calibration/` | 原校准页面、本地原始点与历史快照；独立SQL worker，拟合/逆算/审核应用未开放 |
| `projects/` | O07原项目基本页、结果/单位选择、复制/引用删除保护与worker；只读单位字典及sampleRates候选 |
| `reagents/` | O07原试剂盘/资料/孔位/批号/条码与本地瓶型、手工容量/余量；未知物理状态不假成功 |
| `samples/` | O08普通/急诊本地预设、孔位/容器、重复/稀释候选/组合、任务草稿及worker；不投递设备任务 |
| `instrument/` | 原仪器操作8子页、未知状态/只读来源、角色入口及六项人工维护记录；SQL归维护worker，真实动作禁用 |
| `shell/` | 统一脱机主壳与原导航；接项目/单位、试剂、样本/本地任务，保留医院/账户/历史及报告。默认Guest，无设备通信；用绝对`SN100_SHELL_WORK_DIR`隔离构建 |

临时构建、运行数据和日志全部在根 `temp/sn100_host/`，不进入源码目录。参考原件仍在相邻 `sn100_host_reference/`，只读；本工程不加载其数据库或配置。

<a id="offline-four-window-usage"></a>


<a id="local-results-mode"></a>
### 本地原值结果库入口（空库启动/重开离线验证已通过）

显式参数`--local-results`须同时使用`--history --catalog`，而资料库本身要求`--accounts`。此模式从空白本地结果库开始；先完成项目、试剂以及启用时的样本库准备，再由独立历史工作线程打开结果库。不开此参数仍走原匿名历史查询及文件输出入口，不自动替换已有数据。

新模式的历史目录使用`.sn100-local-result-ingest-v1`专属标记，并绑定自己的身份及同运行根下的资料库路径；已有旧匿名库或不符身份会明确拒绝，不自动转换或清除引用保护。当前仅在隔离匿名数据根验证，不导入生产库。

后台已提供显式任务预约和四原值提交接口，区分同任务覆盖、复测留旧、错误复位清旧；主程序现接空库初始化及查询页面，患者编辑和隐藏沿用原权限。**尚没有由操作页面预约任务、提交检测结果的完整流程，也未接检测调度或仪器结果来源。**项目单位、倍率和精度尚未映射，原字符串直接显示，新模式暂不开放CSV、模板预览、PDF/HTML或打印；旧匿名模式的这些已实现功能保留。

本轮存储核心和未知单位导出限制分别有电脑离线证据；统一主辅已全新编译，真实入口启动/重开已通过电脑离线验证；Windows/ARM构建、原版视觉、RK/仪器、资源预算及长稳各自仍待验。

## 质控、校准与本轮剩余本地业务

本节由项目、校准、报告三个窗口的操作说明合并；具体通过范围以[本轮四窗口卡](../../01_docs/plans/sn100_host_migration_plan.md#offline-four-windows-20261025)及同一移植记录为准。统一启动器`bash sn100_host/shell/wsl.sh integrated-run`现已包含`--quality-control --calibration`；直接运行统一可执行文件时，这两项均依赖`--accounts --catalog`。仍使用显式绝对匿名数据目录，保留原主菜单“校准”“质控”位置、原颜色和布局；1600×900通过外层滚动访问原画布。

质控页管理控制品、关联项目和批次。点“质控品管理”打开管理窗，在窗内新增或选中控制品，填写名称、厂家，再明确保存；在项目选择窗勾选后点确认才提交，取消或关闭不改变关联。管理窗可选择结果类型，填写原靶值、范围、标准差和规则选项，但这里只保存元数据，不产生真实质控点、曲线或失控判定。主页面原打印入口继续隐藏。

选中控制品后点“设置批号”，填写批号和有效期；点已有批次会载入编辑框，需新增时使用表格右键“新增批次”或Ctrl+N。批号全局唯一；当前/上一批次及已有历史的批号不能删除，已有历史的控制品不能直接改名或删除。在管理窗改选默认批次会立即提交事务，重复同选不写入；加载列表不会自动修复旧标志。项目关联在选择窗明确确认时提交。管理窗“取消”放弃尚未保存的文字/规则草稿，不撤销已经成功提交的新增、项目关联或批次改选。质控沿原版入口无角色门槛，其本地演练权限不能用于推断生产授权已验。

当前源码增加历史日结果读取：在已核对绑定的匿名本地库中已有结果时，选控制品、点项目，自动读取当前与前一批次；当前批没有点时沿原版保持空图。左右箭头按原顺序查看日期、时间和数值，曲线支持拖动、滚轮缩放、双击复位。旧行没有逐点单位证据，明确显示原值与“单位未知”，Mean/SD/CV为“—”，不套当前单位或叠加阈值线。这里只读现有记录，真实检测写入链仍待接；独立页面已通过，统一新447产物尚在构建。

校准页用于保存、回看原始点，默认访客只读。选择已启用、单位资料完整的项目，在曲线列表右键“新增本地原始点记录”或按Insert，填写校准品批号、可选试剂批次、点数和重复次数。按表头单位输入浓度和原始测试值，程序不自动换算或按稀释标签推算；每条1至32点、每点1至8次，0至99999且最多4位小数，浓度不能重复。“保存修改”确认并读回后生效；失败保留草稿，“取消”放弃未保存修改。

校准图只显示原始重复值均值散点，“反算值”未计算。复制保留已保存原始点和历史单位快照，生成新本地编号；“删除曲线”只隐藏记录，历史和保护登记仍保留。停用项目只能读历史和启动核对，不能新增、保存、复制或隐藏。记录始终未拟合、未审核、未应用，选中列表不会使曲线用于仪器结果。

项目高级方案用于保存时间、主副波长、加样体积、混匀与清洗步骤资料。工程师/管理员选中项目后，在“测试方案”“稀释比例”按列标题和单位填写，点右侧保存；取消恢复已提交值，失败保留草稿。既有稀释标签、选用/默认及未开放系数保持只读。试剂页“试剂组设置”可选择项目、新增组并整理组成批号；PT项目显示的ISI及正常测定值仅本地保存，不计算或应用校准结果。重新打开后核对保存内容，匿名示例参数不能当生产配置。

报告页的“显示”可切换患者信息、在同组内拖动排序；结果列保留，调整只在同项目内生效。保存成功后更新展示，取消或关闭放弃草稿；CSV内容不随展示隐藏而删列。有权限账户可将勾选或当前报告软删除，普通查询不再显示，但原始结果/任务/曲线关联保留。访客不能保存显示配置或删除报告。

报告“打印”先开原模板预览；医院名称和患者草稿须先处理。预览工具栏的“Save as HTML”按钮打开文件框，窄窗时先点右侧展开箭头；在数据根的`history/exports/`填新的`.html`名称保存，同名不能覆盖。取消不生成文件，可再点按钮重试；导出处理中离页、登录/注销和退出受守卫保护。HTML为UTF-8连续网页，源文字含`&`时明确拒绝，可保留资料改用PDF。GBK CSV、原模板及中文PDF继续沿用；XLSX原版未启用，不能把CSV改后缀代替。实体打印、上传和真实结果链分别待验。

完整本地模式现在为9个SQL worker加2个设置INI worker。质控和校准数据位于运行根的`quality_control/`、`calibration/`，项目/试剂/样本仍共用`catalog/catalog.db`。跨库保护使用先预约、后本地提交和启动对账，不能视为一次跨库事务。被质控或校准引用的项目名/全名/删除和试剂批组身份受保护；已保存校准记录及质控项目关联的单位快照不随当前配置改写。原版质控历史日结果没有逐点单位、倍率和精度快照，项目关联快照也不能证明旧结果的单位；本轮只读查询SQL21项已通过，曲线/逐点追踪页面正在验证，历史单位未知时不计算单位统计。出现“待核对项目引用”时，成功刷新列表也不会清掉警告，须核对恢复。

每份质控/校准库的UUID与规范文件路径及catalog保护登记配套。恢复前停止全部写入程序，在原路径恢复匹配库；换目录、换平台或克隆必须显式核对迁移，含隐藏历史，不能直接清UUID、路径键或保护登记。此程序包只归档匹配应用与源码，不包含运行库、认证资料或生产数据。

维护入口：质控`quality_control/quality_control_page.cpp`→`qc_store.cpp`，校准`calibration/calibration_page.cpp`→`calibration_store.cpp`；公共接线为`shell/quality_shell.cpp`、`calibration_shell.cpp`和`main.cpp`。项目事务保护在`projects/projects_store.cpp::save()`，试剂批组在`reagents/reagent_store.cpp`；报告显示/隐藏在`history/report_remaining_store.cpp`，HTML在`report_controller.cpp`。各窗口原始交付及后续修复分开保留，完整统一回归和平台结果只引用最终对应源码的证据。

<a id="instrument"></a>
## 仪器操作与人工维护（离线）

本轮固定检查点已通过：存储11/0、仪器界面offscreen/XCB各10/0、适用累计及WSL/Windows32/ARM64主辅构建，56项ELF离线闭包无错误；30秒七store运行/退出与资源基线已保存。[完整结果与待验](../../02_records/20261022_sn100_host_migration.md#instrument-offline-result-20261024) · [发布包及匹配源码](../../07_backups/sn100_host/instrument_offline_20261024/README.md)。此包固定仪器与已验O09-2检查点，不包含之后继续开发的密码登录新源。

统一壳显式`--instrument`接原仪器操作页（需要`--accounts`）；`shell/wsl.sh integrated-run`默认启用，默认Guest只读，显式匿名身份用于离线操作。页面沿用原`frmdriverset.ui`的1751×968画布、原子页/控件位置及共享主题，较小屏幕用外部滚动；原Windows活动主题与RK实屏完整对照仍待验，来源与必要差异在`instrument/origins.json`。

人工完成后可确认登记六项维护：日维护清空废杯/废液、清洗废杯盒；周维护清洁洗针孔/仪器；月维护清洁加样针。原版按系统当天登记，月份选择仅用于历史查看；重复确认不重复计数，只有事务成功且读回后才显示勾选。数据在运行根`instrument/maintenance.db`，保留原三张维护表/字段；Guest或无效角色、未知动作、非当天日期、错误标记/结构不匹配库会被拒绝。不要将参考库或生产配置放入这个目录。

洗针、关机完成记录及泵阀/针/抓手/扫码器、探测/标定、自测、通道开关与真实连接入口保留禁用，等待设备与型号/协议参数后分项验证。设备数据默认为未知；显式只读来源只显示已核实的型号/主状态/灯状态/错误类型，失效即撤去旧值，不能解禁设备动作。原维护打印按钮未见完整业务接线，保持待对照，不新增打印流程。

维护者从`instrument/instrument_page.cpp`的确认入口追至`maintenance_store.cpp::record()`身份/动作/日期白名单与事务；月份读取在`loadMonth()`，主壳接线在`shell/instrument_shell.cpp`，worker启动/首读/退出归`shell/main.cpp`。界面/存储专项和待验范围见[同一记录](../../02_records/20261022_sn100_host_migration.md#instrument-offline-20261024)；[当前验收卡](../../01_docs/plans/sn100_host_migration_plan.md#instrument-offline-card)分开登记离线、Windows、ARM构建及RK/仪器。

## 本机回环后台只读状态

统一程序显式添加`--status-loopback`，与`--status-replay`互斥；启动器默认不打开。后台worker只连接本轮自己创建的127.0.0.1临时端口，不接受外部地址，每秒只读Qj，不堆积在途请求。第7页沿用原状态颜色/几何，以“模拟/…”和tooltip标明来源；停止/错误/记录超限即清旧值，其他业务页提示不被后台状态覆盖。实际TX/RX以`STATUS_LOOPBACK_WIRE`的hex日志为准，模拟机型SIMULATED与真实仪器未知分开，登录身份及actionReady不受影响。

核心13/0、offscreen/XCB统一各5/0及七store退出已通过；Material透明halo的无效重绘已修复，同页同匿名数据短观测CPU约18.5%→0.86%单核。主时钟/动画时相/可见效果保留。入口为`protocol/status_loopback.cpp`与`shell/device_status_shell.cpp`，证据和限制见[同一记录](../../02_records/20261022_sn100_host_migration.md#o09-loopback-cpu-20261024)，这些数值不代表RK预算或长稳放行。

## 本地密码登录

直接运行统一程序时，`--accounts --password-login`显式启用原用户头像和登录卡；`shell/wsl.sh integrated-run`已添加该参数。默认仍Guest，原来不启用该模式的调用保持原行为。卡片沿用原白色布局、蓝按钮和遮罩动画，空密码按原规则回Guest；非空密码只验证当前独立匿名账户库，失败保留旧身份。勾选默认用户名只在角色不高于工程师时保存；管理员不改默认。

数据处理中或登录提交中不能重复登录、切页、注销和关闭；隐藏草稿须先按原流程取消或放弃。账号应在明确隔离的匿名测试库按账户设置流程准备，不导入参考库/生产凭据。登录不会连接仪器、同步试剂/配置或改变设备动作条件。现有pwd直接文本兼容、生产凭据安全策略以及原Windows活动主题/RK实屏仍待验。

手动核验时，可先在该匿名目录用显式`--test-session offlineEngineer`启动，进入账户设置新增一个由自己填写密码的测试账号；注销后点原用户头像，使用刚新增的账号登录。既有目录不重新播种，密码只留在本地测试库和输入框中。

维护入口为`shell/login_shell.cpp`、`ui/maskdlglogin.cpp`和`data/account_store.cpp::authenticate()`；来源/有意差异见`ui/login_origins.json`。[当前卡](../../01_docs/plans/sn100_host_migration_plan.md#o02-password-login-card)和[验证记录](../../02_records/20261022_sn100_host_migration.md#o02-password-login-20261024)分别登记独立/统一测试与当前版本产物。

## 离线设备状态文件回放

统一程序可显式添加 `--status-replay <绝对JSON文件路径>`；匿名示例在 `tests/fixtures/status_replay.json`。输入先校验，成功且存储就绪后每250ms处理一个事件，文件中的 `at_ms` 是虚拟记录时间，不能用作响应性能实测。原“仪器操作”状态区显示“回放/…”及来源提示，其他业务页保留自身状态；计划TX和文件RX仅写日志，不建立连接、不发报文。真实仪器状态、耗材状态与设备动作仍未知/禁用，回放不会赋予账户操作权限。

JSON根对象只有 `schema: "sn100-status-replay-v1"` 与 `events`；事件使用 `action`、`at_ms`，仅 `rx` 带十六进制 `hex`。动作是 `connect`、`query-status`、`rx`、`advance`、`disconnect`，时间非负整数且不倒退。文件限1MiB、1–256事件、单RX8192字节、总RX128KiB；只接受绝对普通文件，拒绝符号链接、参考目录及未知字段。断开、结束、停止和超时会使旧状态失效；迟到有效帧可作为最新观测，不能把先前超时请求改成成功。不传参数时沿用原有离线路径。

维护入口为 `protocol/status_replay.{h,cpp}`、`shell/device_status_shell.cpp` 与 `shell/main.cpp`；网络客户端/Transport不接入本回放路径。实际验证与未验边界见[同一移植记录](../../02_records/20261022_sn100_host_migration.md#o09-status-replay-result-20261024)。

<a id="o07"></a>
<a id="o08"></a>
## 项目/单位、试剂基础与样本本地任务

在WSL的`06_programs/`目录，用一个**新的绝对匿名目录**演示；`--seed-catalog-demo`只对新库写入DEMO项目、单位标签与非物理比例候选，不导入生产参数：

```bash
bash sn100_host/shell/wsl.sh integrated-run --data-dir "$PWD/../temp/sn100_host/catalog_demo_20261024_01" --seed-catalog-demo --test-session offlineEngineer
```

不带`--test-session`默认Guest只读。统一启动器已添加`--accounts --history --catalog --samples`；直接运行可执行文件时按所需页面显式启用这些参数；仪器、登录、打印与LIS本地页另分别加`--instrument --password-login --printer-settings --lis-settings`及绝对`--data-dir`。`--samples`依赖`--catalog`，后者依赖`--accounts`；旧模式继续保留。已有目录不重置、不补播种；无初始项目时原版仅复制入口没有来源，需新匿名演示目录或后续验证过的生产配置导入。

原“分析项目”页编辑基本字段、10列结果、精度1–6、单位与主显示，复制或删除项目；单位字典原版只有读入口，本轮保持只读。原“试剂设置”页可保存资料、条码、批号与24个本地孔位配置；本地瓶型/余量编辑从原容器/余量值双击进入，明确区别于液面实测。资料编辑弹窗仅因宿主高度等比缩放，原主画布1751×968、颜色、按钮和分组保留。

“样本设置”沿用普通架/急诊架与原孔位编辑、明细、批量和多项测试弹窗。可编辑样本号/容器，选择项目、0–99次重复、延长标志和项目配置中的稀释候选，保存后重开保留；多项测试保留成员重复次数。默认普通8孔/急诊3孔来自原配置，实际机型另验。原表格几何下第8行需用滚轮/滑块访问，Material滚动条会淡出；当前`CusRackListWidget`源码已接原`QScroller`左键手势和`ScrollPerPixel`，完整触屏拖拽/输入仍另验。只有具备对应角色的本地会话可编辑预设，模拟整机running仍可准备预设；未下发、未执行，不生成设备任务或报告id。演示稀释标签`OFFLINE_RATE_A/B`不表示物理比例。

统一启动包含医院、账户、历史、项目、试剂和样本六个store，各自在所属worker管理SQL连接；运行根下保留`hospital/`、`account/`、`history/`，新增业务共用`catalog/catalog.db`及隔离标记。跨模块引用由外键保护，失败保留草稿。维护入口为`projects/project_page.cpp/projects_store.cpp`、`reagents/reagents_page.cpp/reagent_store.cpp`与`samples/sample_page.cpp/sample_store.cpp`。原单位字典保持只读；原内存预设改为已提交内容本地持久化是明确差异。下发、探测、同步、校准/ISI、消耗/足料与剩余测试次数尚未接通，不产生设备TX或检测报告。[测试/资源与边界](../../02_records/20261022_sn100_host_migration.md#catalog-sample-offline-20261024)。

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

2026-10-24接续诊断新增可选`--timing-trace`（默认关闭）：在内存保留最近8192事件，终态写`timing.json`，公开覆盖数。可区分client截止/tick/observe、transport排队/pump/write/bytesWritten、peer读取及同步日志/progress提交；collector时间可跨源排序，各源自己的时钟只在同源内比较。诊断采集会改变负载，`bytesWritten`不是对端ACK；受控停顿及短测不能解释旧长测根因。监督程序接受同名开关并冻结到job合同，下一轮长测须另行核对冻结输入，不自动重跑。

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

选择报告后点击原“打印”进入原QtRPT预览，保留工具栏、缩放、翻页及210×148mm模板尺寸；“Save as PDF”只能在当前匿名目录`exports/`中新建PDF，不能覆盖已有文件。主程序和`sn100_report_renderer`必须同目录，`wsl.sh build/run`和ARM`build.sh`会一起构建。PDF在独立进程生成，支持取消文件选择、30秒超时和失败重试；独立宿主未绑定已提交打印设置时仍禁用物理打印；HTML现已接原模板UTF-8文件输出，XLSX原版未启用。统一入口的受控打印接线见上方当前用法，正向确认与实体输出另验。

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

该兼容模式默认以Guest进入“用户设置 → 账户设置”（账户页index 0，医院页index 1）。Guest可看医院及进入账户页，账户列表隐藏，账户操作沿用原权限规则。保留原八项主菜单、五项用户设置菜单、主题和图标；未传模块参数的页面显示占位，设备操作禁用。新版显式`--password-login`可启用本地登录卡，集成启动器已添加；原1920×1080业务画布和外部滚动保留，可用`--viewport 1600x900`固定宿主尺寸。原O01/O02独立入口继续使用原命令。

需要核对工程师角色时，显式选择匿名模拟身份：

```bash
bash sn100_host/shell/wsl.sh run --test-session offlineEngineer
```

角色来自独立匿名库，`--test-session`不验证生产密码；顶栏保留原姓名/角色区域，悬停区分模拟/密码/Guest来源。`--device-state running`仅切换账户规则使用的模拟动作状态，仪器仍是脱机/未知。未启用密码卡的兼容模式，注销后重新选择模拟身份仍需重启；启用后可在卡中重新认证，不触发原仪器登录联动。

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

**本节保留O03-3三页历史基线及累计回归入口。**O03-3在原主壳接入医院信息、账户设置和历史查询/CSV。当前源码已有报告及项目/试剂/样本，`integrated-run`的范围以[O07/O08用法](#o08)为准；直接运行可执行文件仅带`--accounts --history`时仍是三页历史模式，`--history`也已接入报告。医院与账户位于“用户设置”，历史查询位于“结果报告”；保留主壳1920×1080及历史页1751×968业务画布，通过外部滚动适配较小窗口，不重排原按钮和业务分组。历史实测见[O03-3记录](../../02_records/20261022_sn100_host_migration.md#o03-3-offline)，完整原Windows视觉、RK/仪器及长稳分别验收。

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

O03-3交付时主壳只链接`history/history_core.pri`；当前`shell/shell.pri`已同时链接`history_report.pri`及其QtPrintSupport等依赖，`--history`会接入预览/PDF，规则见下节。原生产登录、自动设备连接、实际LIS收发、校时及仪器动作仍未接入统一入口；本地密码登录及LIS配置页已有注明范围的离线接入。

仅构建统一程序：

```bash
bash sn100_host/shell/wsl.sh integrated-build
```

ARM64构建仍接收一个绝对输出文件路径，只构建、不自动部署：

```bash
bash sn100_host/shell/build.sh "$PWD/../temp/sn100_host/integration/arm64_release/sn100_shell"
```

此命令仅构建。统一部署包使用下节`shell/rk.sh prepare`；当前封装启用医院/账户/历史/项目/试剂/样本/仪器和登录，并按release capability加入本地打印设置，默认不启自建回环。沿显式匿名路径按需首次初始化，不用x86_64/XCB入口代替板端启动。

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
| `shell/main.cpp` | 显式参数、七SQL store及可选本地设置/回环worker的首读和正常退出；每个对象在所属线程，设置存INI，不混作第八SQL库 |
| `shell/frmmain.cpp`、`frmuserset.cpp` | 原导航、单一账户会话、权限刷新、医院/账户离页与人工注销 |
| `shell/history_shell.cpp` | 历史页及报告控制器挂接、状态回传、导航守卫与焦点恢复 |
| `history/history_page.cpp::requestLeave()` | 明细草稿确认/快照恢复，查询及导出弹窗收尾；不替代worker关闭 |
| `history/history_core.pri` | 历史查询/统计/CSV的稳定构建依赖，不含报告打印后端 |
| `tests/integrated_shell_test.cpp` | 上述六项累计实验；守卫专项另见`history_page_guard_test.cpp` |

旧`build/run/test/account-test/hospital-test`仍用`o03_2/`；`integrated-*`使用独立`integration/`，完整run启用医院/账户/历史/项目/试剂/样本/仪器、登录和本地打印设置。构建、运行数据及证据不混目录；状态继续在[原计划](../../01_docs/plans/sn100_host_migration_plan.md)维护，不另建完成清单。

<a id="o03-4"></a>

## O03-4 统一原模板预览与PDF

本节保留O03-4报告整合时的历史用例与结果；命令仍使用当前源码构建，当前全部业务入口及修后结果见[O07/O08](#o08)。

在同一个“结果报告”页面查询并选中报告，点原“打印”打开QtRPT预览，再用原工具栏“Save as PDF”选择当前`history/exports/`下尚不存在的`.pdf`文件。预览、缩放、翻页和210×148mm原模板继续复用；没有重画页面或调整业务分组。主程序`sn100_shell`与`sn100_report_renderer`必须同目录，导出由独立helper完成；文件选择可取消，缺helper或渲染失败会保留清楚状态并允许重试，已有文件不会被覆盖。本段旧检查点当时未启用实体打印和HTML；当前HTML已接入，当前受控打印源码按上方已提交配置链启用，尚待本轮运行与实际设备验收。XLSX原版未启用；无打印配置绑定的独立宿主仍不开放物理打印。

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

本节命令在 **WSL Ubuntu-24.04 / qyq、项目的`06_programs/`目录**执行。O03-3离线准备已有[历史结果](../../02_records/20261022_sn100_host_migration.md#rk-prepare-20261023)，O03-4报告helper、字体与依赖构建/闭包/冻结也已通过；当前准备范围另含项目/单位、试剂、样本及QtSvg，修后ARM最终审计已通过。下面部署、预检、启动、停止及回收的RK命令尚未在实板执行，当前状态以[本轮记录](../../02_records/20261022_sn100_host_migration.md#catalog-sample-offline-20261024)为准。

### 先在电脑完成准备

```bash
bash sn100_host/shell/rk.sh prepare
```

该命令不连接RK。它校验归档输入，在默认`temp/sn100_host/rk_prepare/`隔离构建，以默认2个编译任务生成Release ARM64主程序与helper，检查解释器、递归依赖、provider符号版本及SQLite/offscreen/SVG imageformats插件。可用绝对`SN100_RK_WORK_DIR`隔离新的准备目录。源码在构建中变化、缺库/架构或版本不符均阻止发布；已有旧产物不会绕过失败继续上传。首次缺少SQLite输入时只访问Ubuntu官方HTTPS包源，校验索引SHA后归档，后续复用归档。QtSvg固定5.15.13-1，按[归档来源与恢复范围](../../07_backups/qt_environment/sn100_samples_arm64/README.md)只解包到私有sysroot，不安装进WSL或RK。

| 结果 | 位置与范围 |
| --- | --- |
| 统一ARM产物 | 默认`temp/sn100_host/rk_prepare/build/sn100_shell`及同目录`sn100_report_renderer`；O03-4历史准备目录为`temp/sn100_host/o03_4/rk_prepare/`，本轮隔离路径与修后最终指纹见记录 |
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

当前RK封装启用`--accounts --history --catalog --samples --instrument --password-login`，包具备`local_printer_settings`/`local_lis_settings`时分别加`--printer-settings`/`--lis-settings`，旧包继续兼容。数据在`sn100_shell/runtime/data/{hospital,account,history,catalog,instrument,settings}/`，项目/试剂/样本共享catalog库，维护用maintenance.db，打印选择用settings/printer.ini，LIS意图用settings/lis.ini（两者为独立INI worker，仍为七SQL store）。只按需匿名初始化，不自动播种项目演示配置；已有库不得重播种。首次随机secret只存700目录内600临时文件，就绪或退出即删；不导入WSL账户库、不记秘密。默认Guest，本地登录/模拟身份不代表生产安全；上板实屏/实际插件与打印另验。

`SEC`默认为1800秒，0为不设软截止；到期只请求经过原页面守卫的正常关闭。草稿、忙态或确认框可能使关闭保持待处理，不宣称硬超时或强制成功。SIGTERM/SIGINT经GUI信号桥走原`viewport.close()`；当前七store模式正常关闭应在应用日志出现`HOSPITAL_STORE_CLOSED`、`ACCOUNT_STORE_CLOSED`、`HISTORY_STORE_CLOSED`、`PROJECT_STORE_CLOSED`、`REAGENT_STORE_CLOSED`、`SAMPLE_STORE_CLOSED`、`MAINTENANCE_STORE_CLOSED`，显式启用设置时另有`PRINTER_SETTINGS_CLOSED`/`LIS_SETTINGS_CLOSED`，以及`THREADS_STOPPED true`和真实退出码0。RK监督器现有`CLEAN_SHUTDOWN`判据检查医院/账户/历史三条标记与汇总线程标记；项目/试剂/样本/维护逐条关闭仍需对照应用日志，实板收尾另验。

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
