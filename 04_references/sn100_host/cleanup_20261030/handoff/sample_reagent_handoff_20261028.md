# 新样本、试剂聊天的接续接口

本聊天：`01a17d3f-a352-72a1-b13e-349ad830cc13`。用户要求先补试剂单孔RK验证，再按依赖接主入口和任务链。旧stop交接保持原样。

**当前停止点（2026-10-28，用户明确要求）：**“把最近一个任务完成之后，就不用跑了，等后续再做测试和相关的”。本窗把当前OTHER模拟回复编号修正和交接收尾；不新增编译、RK复测、EGLFS或日期候选应用。已通知整合：未启动的本窗测试停止排队，已启动的一项只收回结果后停止续跑；不撤销其另有用户授权的其他工作。用户后续明确继续测试前，不自动恢复。

最新代码为测试`08f05da82d2730e717a4ad5826d05292ed18f479fda579f14d9583f3e554c06a`，只修OTHER回复项目名、仪器编号和基础排序xuhao，保留LIS及步骤编号；源码检查完成，尚未重编/RK。11生产源SHA保持，完整清单`reagent_batch_source_sha256.json`。上一轮be882/e42d3129、测试b643为RK6/1、退出1，批组仍在同步前被拒，不能记为通过。报告批d993/cf60此前正式UI录入已取得offscreen6/0和EGLFS5/0、退出0，范围各自保留。日期候选只在temp，QC/CAL资料页由整合接，本窗不实现其任务/算法，不占构建或显示。

## 文件和RK安排

- 本聊天维护`samples/`、`reagents/`、`protocol/`及其独立测试；整合聊天另已明确移交`shell/device_control_shell.cpp`和`frmmain.h`中仅任务链声明。不改其他main/shell、公共构建和公共计划/README/同一移植记录。
- 新整合聊天`01a17d3f-891b-7de3-a2ee-a52b34acbf2b`负责主入口、共享构建、正式部署、启停和显示。RK只在它安排的隔离路径/时段执行，不读现场runtime、不自动连接仪器。编译总并发至多2，本聊天使用单作业。
- 新coordination已明确本聊天可连续运行本轮QCore匿名任务链驱动；GUI仍由整合安排，不能据此占用显示。

## 首项待执行产物

2026-10-27重新核对停止清单50项：全部同SHA，差异0。现成AArch64驱动与停止交接一致，无需为了执行已有产物先重新构建。

- 文件：`temp/sn100_host/button_controls_20261027/formal_detection_drivers/original_detection_rk_test`
- SHA-256：`2852eddfcc823c8d66148a12d57675cf910baf486a5851323fd2792a8fc68826`
- 构建日志：`single_reagent_update_build_01.log`
- RK运行时设置绝对`SN100_BUTTON_TEST_ROOT`，该目录必须全新匿名；QCoreApplication不需要占用显示。核AArch64、SHA、动态库、实际输出和退出码。
- 预期11项含初始化/收尾，其中新增Us金样为15字段，`123.456789`按原Qt默认精度发`123.457`，第14字段仍为最大容量；不能把11当11项完整业务。该驱动未覆盖所有清孔/正式页面/设备同步，须补针对检查。

## 主入口依赖

按`device_sync_handoff.md`：原模式初始化试剂/样本→原项目快照→当前连接与协议标识→后台同步→任务准备→Ts→有界接收Tt/TD/Tr/Ur/UP/Tu→原库及页面重读。

单孔桥接：`ReagentStore::originalHoleSaved(QString)`成功提交后，GUI核当前连接、`LinuxNew`和可配置状态，再排队调用`SampleStore::prepareOriginalReagentUpdate(hole,true,connection)`；`originalConfigurationPacket`回GUI再核连接并经`DeviceControl::sendOriginalConfiguration`发送。原完整协议只做原本地保存，不因RK平台发送Us。失败/断线不可显示同步成功。

有效role0已补，匿名原试剂后台11/0退出0验证；样本role0在新Store串联检查已通过，完整页面继续待验；高级维护按钮原role4/5限制保留。

## 首项实际结果与后续切片

旧冻结驱动已在RK运行：11通过、0失败、退出0、4689毫秒。证据为`04_references/sn100_host/original_flow_20261027/single_reagent_01`。首轮缺测试库退出127，第二轮上传前测试库符号链接不可用，均未执行业务；第三轮用匹配5.15.13实体库、双端核SHA后通过。没有占显示或连接设备。

新增只读发现：原`MaskDlgReagentSetting::on_btnSave_clicked`在新协议发送前对保存和清孔均执行`setPriority(99)`。基础`clearHoleSet`仍归零。通用Us金样正确不代表此按钮分支已正确；本次新增`singleReagentSettingPayload`保留该差别，`prepareOriginalReagentUpdate`改用它，旧协议在后台也不发送Us。页面正常保存同样将优先级设99。

已补有效会话role0可编辑原试剂和样本（未认证Guest仍拒绝，高级维护按钮仍沿用原限制）。清孔针对检查已扩展r/s两区域、批号/瓶型/日期/容量/阈值、未知字段与公式级别保留、通用Us优先级0及按钮Us优先级99。最终驱动55893027在RK11/0、退出0、5327毫秒，日志`reagent_role_clear_build_03.log`及证据`04_references/sn100_host/original_flow_20261027/reagent_role_clear_01`。前轮10/1为连续改两孔未重读触发真实防覆盖，修正测试按正式Store重读流程后通过，保留失败日志。

## 正式壳与下一组检查（2026-10-28继续）

`device_control_shell.cpp`现已接当前连接同步、单孔Us及对应Ur回读、异步准备后Ts、暂停恢复、有界32包/1MiB收包和Tt/TD/Tr/Ur/UP/Tu，回调均比较连接。`frmmain.h`只新增该链状态/声明。7个AArch64对象编译通过，日志`resume_task_objects_02.log`；首轮测试构建缺试剂头搜索目录的失败保留。完整主窗未运行，不能登记正式业务通过。

新后台Store串联驱动`original_store_task_chain_rk_test`首轮RK4/2退出2：一项是Ur后未按正式壳重读试剂Editor，另一项是匿名夹具用了不存在的AfterR/AfterS清洗液，真实业务校验正确拒绝。修正测试衔接及夹具后，驱动10c18aea在RK6/0、退出0、4658毫秒；4个业务场景加初始化收尾。最终构建日志`store_chain_build_04.log`，证据`04_references/sn100_host/original_flow_20261027/store_task_chain_01`；没有改正式业务去放宽检查。

正式主窗GUI驱动源码及.pro为`samples/tests/original_main_task_chain_rk_test.*`，共享匿名夹具`original_task_flow_fixture.h`。覆盖实际Configured role0、原项目同步、正式单孔页清孔后Ur、正式开始/暂停/恢复、Tt/TD/Tr及原结果待计算。最初测试对象曾AArch64编译成功，日志`main_chain_test_compile_01.log`；其后增加QProcess真正启动新main并关闭重开两次，当前测试cpp为6ee0ed02，**旧f5b96498测试对象不包含新场景，最终链接须重新编译测试cpp**。运行时`SN100_FORMAL_MAIN_BINARY`指向同批新主程序；子程序使用原库、accounts/catalog/samples，不开instrument-control。当前仍待最终链接和RKGUI。整合负责main原模式初始化和catalog_shell原项目传递，下一批全量重编，不复用旧frmMain对象。正式业务9份源已冻结，机器清单`resume_production_source_sha256.json`；整合正在独占2个作业，本聊天无构建，等待新slot。

## 当前范围

两组独立RK分别11/0、Store任务链6/0已通过。正式main接线及清孔、开始/暂停/恢复/原曲线返回已取得下方限定范围的RK模拟结果；正式样本页面录入、普通孔保存/取消新场景仍待本批RK。物理输入、真实仪器、资源门槛与长稳待验。算法沿用此前用户暂缓要求，原始曲线保存为待计算。

本次GUI驱动的任务范围是一个已保存普通样本、一个原项目、一次检测。它即使通过，也不覆盖质控/校准任务、真实仪器、跨日期结果、程序重启后继续识别在途任务、未知任务结果留存或全部错误恢复；这些仍沿用原计划未完成项。不得把三组后台和本次GUI的数量相加当作全功能完成度。

## 正式页面结果收回及截图/耗时（2026-10-28）

整合实际运行的`resume_flow_01/task_chain_02`已收回核对：f5514952主程序、3a655836驱动在RK offscreen为5/0、退出0、20796毫秒；三个业务场景包含从原配置/角色字典建立有效role0，正式试剂页清孔Us99并保存对应Ur，开始/暂停/恢复与任务41的Tt/TD/Tr保存待计算，实际main两次启动/关闭重开均正常。245份同批生产对象重链接与主程序字节一致，current前后保持f8c，现场runtime和真实仪器未动。长期证据`04_references/sn100_host/original_flow_20261027/formal_main_chain_01`，不据此称实屏/键鼠或完整仪器通过。

整合曾因额度中止，本窗已向用户询问GUI接管；其后整合恢复并给出上述实际结果，仍按恢复后的整合安排运行，接管问题不再是前置。下一步由整合跑EGLFS两页面场景，actualmain子程序不在EGLFS重复以免争用显示。

按整合新安排仅改测试cpp，当前6184d167，单作业AArch64对象编译通过，日志`main_chain_test_compile_02.log`，对象cca40f00。生产9份源不变。`SN100_GUI_EVIDENCE_DIR`为绝对目录时保存两张逻辑主窗PNG及各自JSON；单孔记录正式Save事件到本地提交、模拟器收到Us、Ur保存回执，任务记录正式Start前到Ts/Tt/Tr保存，包含自动确认和暂停/恢复，n=1不算p95。截图是1920×1080逻辑主窗，不能冒充屏幕合成或物理输入；资源、实际显示布局由整合另测。

### EGLFS首轮真实失败与最小修复

整合上板`task_chain_eglfs_01`，驱动1b821b91、同批主对象f551，清孔场景PASS，第二场景初始化时SIGSEGV、退出134（2/1含初始化）；完整日志/资源和首张PNG固定在`04_references/sn100_host/original_flow_20261027/formal_main_chain_eglfs_failure_01`。首孔本地提交回执67ms、模拟器收到Us312ms、对应Ur成功保存333ms，n=1，只说明该已通过场景，不能扩为实屏整组通过。

Thread1堆栈落在QOpenGLContext::handle、QEglFSIntegration::createPlatformOpenGLContext、QEglFSWindow::create和测试init，在第二个顶层host显示时发生；第二场景后台尚未启动。先仅修测试生命周期：外层QScrollArea在initTestCase创建并跨场景保留，各例仍新建原页面/线程/匿名库，cleanup只删业务主窗，cleanupTestCase才释放host。当前测试cpp a82927db，AArch64编译日志`main_chain_test_compile_03.log`退出0，待整合新一轮RK；没有据推断改生产代码。

目检首PNG：清孔r_1显示none/0/空日期，r_2 Cleaner保持2.0ml，原盘面保留。页头仍显示“脱机／就绪”，与已连接本机模拟器的状态不一致，已交整合登记catalog_shell状态文字覆盖问题；视觉不全部通过。样本录入完整页面操作、正常试剂保存/取消和写失败未被本次清孔/开始链覆盖，后续专项仍须补。

### EGLFS第三轮结果及正式录入补测

`task_chain_eglfs_03`为driver6c83708a、测试cpp17d44748、f551同批生产对象，实际4/0、退出0、13352毫秒。清孔Save→本地回执69ms→模拟器收到Us304ms→对应Ur保存325ms；曲线报文发送2959ms→保存3155ms，间隔196ms。各n=1，不是响应门槛或真实仪器时延。图片、资源、原日志、对应测试源及校验清单已归档`04_references/sn100_host/original_flow_20261027/formal_main_chain_eglfs_03`。

第二轮单独Start未崩溃，但点击前按钮禁用，退出2；旧测试无阻断原因诊断，确切根因未证。第三轮改测试host生命周期、增加等待和第一失败诊断后双场景通过，不能称某个未取证生产缺陷已修复。两失败归档继续保留。截图表明旧样本为后台预设，当前画面仍在默认急诊架，故不能把这轮算正式样本录入通过。

新测试cppd715bae5已交整合：原savedSample替换成正式页面添加S01→双击样本号17→选择原项目7/PT→更新设置→返回；核快照、倍率1/2、没有提前Ts或报告。新增`formalNormalHoleSaveCancelAndReopen`，先取消有效期不写/不发，再保留Default批组正常保存有效期，核瓶容量5000、优先级99、精确Us十五字段、对应Ur、未知列和编辑器重开读回。只是测试源码完成，尚未编译或RK通过。

生产9份源SHA全部保持；新批`resume_report_01`改变HistoryResult/ProjectUnit布局，必须用该批冻结cache头和配套全量对象重编测试并链接，不混用当前头与f551旧对象。整合负责构建和RK显示安排，本窗当前不占编译/显示。完整程序保存后重启、错误恢复、物理输入、真实仪器、算法、资源门槛与长稳仍有待验项；编辑器重开不等同程序重启。

### 后续原版差异和依赖，只读准备

原`frmreagent/maskdlgreagentsetting.cpp`构造时明确批组下拉框不可编辑；候选来自使用当前试剂的项目的`reagent_batch_group.batch_group`，排除`PTFbg`、按`|`拆分、去重。条码候选代码是注释。当前正式页原模式却允许输入并从条码构造候选，手工输入也没接草稿标记，登记为待修差异；本批不改冻结生产源。normal补测因此收窄为Default批组和有效期，不认可任意新批号作为金样。修复该差异后要单独验证选择、取消、Us、对应Ur与重开。

整合明确QC/CAL暂只读整理合同，等原质控/校准页面与权限接入后再实现任务。当前能使用的原质控资料入口是`QcStore::initializeOriginal/load`和`QcControls`：质控品ID/名称、批次ID/名称/有效期/当前批次、关联ID/项目名及规则。`recordOriginalResult(QcRawResult)`写原`quality_data`，但它要求实际结果串，不能拿待计算空值当质控计算完成。现有`loadSampleChoices/reserveSampleChange/finishSampleChange/reconcileSampleOwner`仍使用早期catalog持久引用合同：UUID、catalog project_id和qc_sample_hold等表；原质控使用`original-quality:<根>`、原关联按项目名，不能直接套这个合同或在原表补早期列。

后续原质控样本需要的稳定合同是：同一原工作根和源指纹、质控品/批次/项目关联真实ID与名称、原项目编号和启用状态、已提交选择、准备/下发时重核、防删除或变化后的明确拒绝。原Windows `slotResultReceive`对QC先保存质控结果再保存样本报告和原始文件，对CAL走校准结果及原始文件，不套普通样本报告。两者不能只把`Task.type`改成QC/CAL。校准还需项目、校准ID、点序号/稀释、重复次数、试剂批组、原任务号及点标志的对应；当前`CalibrationRecord/Point`已有原行ID和原字段，尚无样本任务预约/原曲线回写专用API。具体接口由整合在后续切片明确，不修改其quality/calibration源码，不用普通SAM通过覆盖。

### 新报告批首次页面录入检查失败

WSL意外重启中断测试入口编译时尚未在RK运行；恢复后整合以最终245份对象重链接，主d9931ff6、驱动c2d42df6、测试源d715bae5。`resume_report_01/task_chain_01`实际5/2、退出2、22792ms。清孔、普通孔有效期取消/保存/Us/Ur/编辑器重开、实际main两次启动关闭均PASS。样本录入在`Add存在且可见可用`断言失败，helper和调用者各记一次，实际是一个业务场景失败，没有点击Add或Start。

已只读回报整合：初始化只等页面ready和同步状态，UP之后项目快照还会触发SamplePage::setProjects/reload，可能暂忙；但日志没拆开三个条件，根因未确认。建议仅在测试分别核控件存在、有界等待页面ready且不busy/控件可见可用，失败记录会话、状态、全部同名控件/父链和截图；调用者直接return避免重复FAIL。不调用show/setEnabled、不后台预置样本绕过。按整合要求当前生产及测试源均不修改，等其安排下一轮。完整失败与对应源/对象身份已归档`04_references/sn100_host/original_flow_20261027/formal_sample_entry_failure_01`。

其后整合明确授权只改测试等待和诊断。最新测试7f4cfe23：分别核Add存在，记录before_wait的页面状态/会话及全部同名控件/父链，正常等待ready且非busy/可见/启用至多15秒，成功记录等待时间，失败记录wait_timeout及可选sample_add_blocked.png，调用者只返回避免重复FAIL。未强制解禁或后台预置；生产9源SHA保持，测试入口由整合按最终同批245对象重编和RK。报告专项由整合另报RK14/0、设置同批11/0，此处不合并为样本检查数。

### 正式页面录入已通过及下一批批组选项

新测试7f4cfe23、drivercf60b22e、主d9931ff6在`task_chain_02`实际6/0、退出0、26055ms。before_wait日志明确页面ready=true/busy=true，Add可见但禁用，232ms自然就绪；正式UI添加S01、录入17、选原项目7/PT、保存后开始/暂停/恢复与原曲线待计算通过。只改测试等待时机，没有后台预置或放宽业务规则。驱动结束后整合运行中修改收取脚本导致本地语法错误，随后只补收同轮原日志，没有重跑；归档`formal_sample_entry_02`。

同批EGLFS三业务场景`task_chain_eglfs_01`已核5/0、退出0、20481ms，Add等待283ms，三张PNG/JSON齐。样本图为S01/17/PT，普通孔为R1/B1/Default/5000/2032-02-03；页头状态文字已按模拟连接显示准备就绪/正在测试。软件事件和逻辑主窗截图不等于物理键鼠、完整视觉或真实仪器。整合资源摘要79样/21.01秒，RSS302068KiB、PSS296782KiB、15线程、41FD、58.23℃，不能据此判纯产品预算或长稳通过；长期EGLFS归档待收完摘要。

整合其后明确解冻候选三生产文件及相关测试。本窗核`reagent_batch_candidate/base_sources.json`旧SHA全部匹配后应用，仅`reagent_types.h`新增原项目试剂名、`original_reagent_editor.cpp`填该列表、`reagents_page.cpp`按关联批组只读选择/拆|去重/排PTFbg/切换刷新，不再直接用条码或自由输入。本批涉及ReagentProject内存布局，必须全量重编。完整11生产SHA和测试SHA见`reagent_batch_source_sha256.json`，已重新冻结，无本窗构建或RK执行。

最新GUI测试f004e641新增`formalHoleBatchChoicesFollowOriginalProjects`：仅此场景增加OTHER实际项目只用Cleaner及对应UP模拟；PT批组Default、LOT-A|LOT-B、重复LOT-A和PTFbg，OTHER批组UNRELATED，R1条码BARCODE-ONLY。核R1只读候选恰3项，键盘选LOT-B取消无Us/不写，再保存精确十五字段Us并等Ur/重开；切Cleaner只见UNRELATED、none清空候选，取消保持原库。其余GUI场景加原PT Default批组；公共夹具c182和.pro b061未改。完整offscreen预期7项、EGLFS加该场景预期6项，均含初始化/收尾，尚无新布局编译或RK结果。

整合已按新11生产SHA冻结到`resume_quality_01`，占总2作业全量构建；主入口同时接原质控/校准资料初始化与权限，仍未RK。此后本窗不构建/不显示/不修改生产。只读补查：原试剂名称查询明确valid=1，当前筛选正确；原顺序为none→Cleaner→本区域试剂，当前表顺序混排仍待修。原有效期文本框readOnly=true，直接显示孔位原值（空值不加一年），日历按钮真正打开CusMaskCalendar并输出yyyy/MM/dd；当前允许手输、空值默认一年、按钮只聚焦，因此普通孔日期保存通过不代表原日历功能迁移通过。下一日历切片需补等价窗口、格式和取消/选中/保存，当前冻结批不顺带改。

### 批组选项模拟夹具失败与只改测试修复

`resume_quality_01/task_chain_01`主be882bf3、driver4e3a8b70、测试f004，实际5/2，Qt报告71214ms，最终QThread仍运行而退出134。三个既有业务场景PASS；新增batch场景在初始化同步OTHER时返回project_lis_code不匹配，尚未操作批组。明确原因是本窗模拟器全量replace[PT]→[OTHER]，连复制原库仍为PT的项目和结果LIS代码也变了；生产校验正确拒绝。初始化失败后清理未完成，后一个actualMain用例再次listen失败，最后线程仍在运行，保留连锁失败，不把Qt小计当实际exit0。

整合明确授权仅改两点测试。当前b643ec22：OTHER仅替换前缀UP[t][PT][9]→UP[t][OTHER][10]及R1→Cleaner，保留两个原LIS代码；init入口加本地自动清理守卫，QVERIFY/QTRY提前失败返回时调用现有cleanup关闭peer/server/业务窗和worker，成功初始化不动作。11生产SHA保持、未放宽校验、未改匿名DB业务。整合只重编测试入口并按be882同批245对象RK；当前尚无该修复的实际结果。旧失败完整归档`formal_batch_fixture_failure_01`。

### 日期下一候选，仅临时准备

经整合授权在`reagent_date_candidate`准备业务页和测试候选，原正式源不变。候选CPP内私有日历窗口复用现有CusCalendar/cusmaskcalendar.ui/mask，日期只读、原空值保留、选yyyy/MM/dd显式草稿标记、取消不改；无新Q_OBJECT，不改告警业务。normal候选测试走实际按钮、取消、月箭头和绘制格点击，清孔另核空日期取消仍空。已将上述b643测试修复同步带入候选，并更新base_sources和SHA清单；旧040ddd32候选测试SHA不再作为当前值。review.md列原版函数、文件及独立构建依赖；主build本来已有日历，独立reagents.pri/build.sh须由整合补依赖并防与告警重复编译。没有应用、构建或RK，不用当前normal手输通过覆盖它。

### 最后一次编号修正与延期测试

第二轮`resume_quality_01/task_chain_02`主be882、driver e42d3129、测试b643，实际6/1、退出1、78455ms。OTHER的项目/结果LIS代码已正确，但基础xuhao仍返回7，与PT重复，原OTHER匿名库为8；原同步校验正确拒绝。初始化自动清理在这次实际失败路径有效，后续actualMain两次启动关闭PASS，无残留监听或线程中止。完整原件归档`formal_batch_fixture_failure_02`；整合另有自己的固定归档，均保留。

按整合授权仅改测试回复：先校验唯一十字段前缀`UP[t][PT][9][1][5][120][0][110][none][7]`，再替换为`UP[t][OTHER][10][1][5][120][0][110][none][8]`，不改采样/试剂步骤的序号、LIS代码及其他字段。最新正式测试08f05da8，11生产源不变，diff-check无错误；只完成源码修正，未重编或RK。曾在聊天中误写一个未核准短SHA，已立即更正，唯一有效完整值以上方08f05da8及清单为准。日期临时候选同步同一修正，仍不应用。用户此时要求后续再测，本窗以此代码/证据/交接收尾。

## 最终停止指纹（2026-10-28）

整合已确认同样收到用户停止要求：只收尾其已启动的质控资料页驱动编译/记录，本窗不新增编译或运行。最新08测试未构建/RK；生产保持be882批，正式current最后核对仍f8c。以后不能把这两项候选当已验，恢复前核当前状态和全部SHA。

正式最新测试：
- `samples/tests/original_main_task_chain_rk_test.cpp`：`08f05da82d2730e717a4ad5826d05292ed18f479fda579f14d9583f3e554c06a`

日期仅临时保存，未应用、未构建、未运行：
- `reagent_date_candidate/reagents_page.cpp`：`1f888ac2ab509e6da74556863161cfd99871ea2933e4d171fc34cf870003b6d3`
- `reagent_date_candidate/original_main_task_chain_rk_test.cpp`：`ce737ef5e31313cdd6551c6f1d05177148896a24f865480d1a0e06f686e2378b`
- `reagent_date_candidate/review.md`：`f119d5c4d179da5c79362e7aca26ef4b3a061d72d3705684eb89837b9027f26c`
- `reagent_date_candidate/base_sources.json`：`6f76ecffd3aa2973f211661ab39955dcc5d6bbb59d1fee05df993a8905d7e4a7`
- `reagent_date_candidate/SHA256SUMS`：`86016da86216006a6d410dc600f5dba2a40d792d228f2c794f66366d15761a27`

整合最终回告：08f05da8未编译、上传或RK；其仅已启动的质控资料页驱动编译已收尾，未执行新轮RK。双方后续产品/测试均不再启动；生产be882/包bb9a保持，current仍f8c，日期临时候选未应用。

