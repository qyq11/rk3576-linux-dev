# 试剂操作与剩余可测次数交接（2026-11-02）

**当前交接状态：05局部9对象编译及214项输入一致性检查通过；候选、补丁和交接已固定，交总窗口审阅整合。本窗口未应用到正式源码。**

## 交付范围

用户在本窗口直接指定试剂页面的设备操作和剩余可测次数。原先UP同步只做了只读核查，结论另见`sync_initial_review.md`，没有修改同步或TR文件。

总窗口要求整个正式工程冻结，本窗口全部工作保存在`reagent_candidate/reagents/`。**目前不是正式源码已合入，也不是设备功能已通过。**未修改shell、构建文件、README、计划、总记录或原参考工程；未连接RK、替换程序或占用板B显示。Git原有修改保留，候选底稿取自当时的工作树，不能用HEAD覆盖。

候选包括10份现有文件：`reagents_page.cpp/.h`、`reagent_store.cpp/.h`、`reagent_types.h`、`original_reagent_store.cpp`、`original_reagent_editor.cpp/.h`、`original_reagent_repository.cpp/.h`；新增`original_reagent_usage.cpp/.h`及逐字节复制原版的`maskdlgreagentusage.ui`。`base/reagents/`保留这10份文件的原字节底稿。最终补丁、指纹和应用检查结果在`reagent_candidate.patch`、`reagent_candidate_manifest.json`、`reagent_patch_check.log`。

## 原版依据与本轮处理

| 使用者操作 | 原Windows实际源码入口 | 候选已经接好的部分 | 仍缺或须总窗口核对 |
| --- | --- | --- | --- |
| 看剩余测试数 | `wgtReagentPanel::getReagentTests_Impl/getReagentCurTests/getProjectReagentUseage`、`frmReagentMaterial::refreshUseageTable` | 原库加载后计算选中孔和项目表的估算次数；未知数据保留原因；原项目变动先使旧次数失效再重读，有草稿时延后 | RK原资料显示、改项目用量后刷新、取消草稿后的重读、原实时回包口径；圆圈的“- tests”仍须另接，未改wgtreagentpanel/cusreagenthole |
| 优先使用 | `wgtReagentPanel::slotUpdateHolePriority/arrangePriority`、`getSingleReagentUpdateCommand` | 新协议只生成priority1、末项0的15字段Us，不先写库；旧协议按原同名试剂递增并分别排两区域，再事务保存 | 实际发送、对应Ur保存与严格字段核对、超时和旧包处置 |
| 单孔和整盘探测 | `on_btnDectOne_clicked→slotDectLVContinue(1)`、`on_btnDect_clicked→slotDectLVTotalContinue(1)`、`dectLV`、`getReagentDectCommand` | 从原项目顺序构造针选择，保留单孔有效Continue分支和整盘状态筛选，准备原Mg报文 | 舱盖实测字段、实际动作就绪、顶层N的标定换算/落库及Mg完成链未接；当前不能开放 |
| 更换 | `on_btnChangeHole_clicked→fullHole→dectLV` | 按原瓶型/区域最大量保存使用者确认充满的资料；新协议Us末项1后Mg，旧协议按原规则排优先级后Mg；不加无关有效期校验 | 仅管理员；先满足探测全部回读条件。已保存的确认量不能称液面实测 |
| 清空盘面 | `on_btn_clearPanel_clicked→clearPanel→clearHoleSet→updateHoleInfo→ReagentSettingTable/TableDal` | 同一事务清本地盘面后准备UC；旧used_ul保留、formula_level写空串，未知厂商列保留；没有复用行为不同的单孔clearHole | 仅工程师/管理员；UC没有专用完成回调，必须另核真实资料回读，不能收到发送成功就报设备已清盘 |
| 默认盘面/恢复 | `on_btn_restore_clicked` | 原函数仅定义未调用lambda，实际无恢复动作。候选不给它新增恢复业务，提示依据仍待现成发行版核对 | 没有点击Windows这个按钮，不能说实际发行版恢复已运行通过 |
| 扫码、转至出口 | `on_btnScan_clicked`、`on_btnChange_clicked` | 不解禁当前默认禁用/隐藏的入口，分别给出待核原因 | 原源码Mh、Mi[id]与其完成字段已有依据，但现成SN450画面可见入口，与构造源码不完全一致；SN500启用分支、扫描A消息写库和移动回执尚未验收 |
| 容量校准模式 | `on_btn_calibrationmode_clicked`、`setPanelFromMap`、原容量标定代码 | 没有把普通探测代替标定流程；明确保持未开放 | 原模式JSON、孔位编排、备份、迭代标定、600模型及完成/恢复整链仍未接 |
| 试剂耗材/用量 | `slotBtnUsagedClick`、`MaskDlgReagentUsage` | 原UI与初始页签/页码状态、关闭重开输入保持、问号统计；QH/QI/顶层J原编码只提交信号；原无回调按钮不增加动作 | 后端完整解包、当前请求归属、J回复条码落库与失败恢复均未接。默认不授予queryUsage/queryDeviceKey/updateUsage能力 |

## 必须保留的报文和数据规则

- 探测请求/完成是**`Mg`（小写g）**；`MG`是其他动作，禁止混用。液位更新是顶层`N[...]`，扫码更新是顶层`A[...]`，不能写成MN/MA。
- Mg依次为孔数，每孔8字段：孔位名、试剂名、区域文字、针号、发送孔号、瓶型、底径、角度。发送孔号取原孔名`split('_')[1]`，不是数据库id。项目按原xuhao和`sendConfig/sendSequence`排序，针号按最后一个同名项目步骤覆盖；原文本“左试剂针”为1、其他为2，缺项目选择时试剂区1、稀释区3。保留Latin1、原几何文字和128000字节上限，数据不全明确拒绝，不造参数。
- Us保留15字段及Qt原默认数值精度；第14项确实为max，不自行改成used。优先使用用priority1、末项0；更换用priority99（新协议）、末项1。`singleReagentSettingPayload`会强制99，不能用于优先使用。
- `priorityReplyMatches(Ur,sentUs)`复用整包Ur校验，比较目标孔、名称/类型/批号/瓶型、日期、priority1、状态及阈值；不把isUpdate=0错误解释为液量必须重置成max。不比较未发送的formula_level。合法无关回包返回false并保留原因，不确认本次操作；调用者不要把它当成功或盲重发。
- 次数使用原显示量`amount_uL`，不是任务预留后的`used_ul`；原初始化显示会按首个同名瓶型限制上限。按原项目顺序取首个非空同名试剂单次用量，每瓶向下取整`(显示量-out_ul)/单次用量`，负数结果为0；同名试剂求和，再取项目各试剂最小值。缺试剂不列该项目，稀释区表保留空。零除、非数和溢出显示未知，不能充作0。此显示估算不处理批组、有效期或任务准入，不修改`assignTask/saveRemaining`和既有扣量。
- 原LinuxNew回包虽包含体积，`levelUpdate`最终仍按高度经标定JSON及几何后备换算。SN450选450模型，其他机型（含SN500）选600；当前容量适配不接受600，不能偷用450或直接保存回包体积。探测/更换能力必须继续关闭。
- 板B原记录还有主题42试剂位、主控上报48的差异。清盘和整盘动作的实际孔位范围仍待核，不能因本地表和候选报文准备成功就开放动作。

## 总窗口需要接的接口

1. 先按当前会话、原账户明确授权/菜单开放条件、连接代次、实际INI和主控状态判断本次哪些动作完整可用，再调用`ReagentsPage::setDeviceOperations(connection, operations)`；不能按角色高低补造原账户未获的权限。初始、断线和结果不明传0或空清单。动作名为`prioritizeHole/detectOne/detectAll/fillHole/clearPanel`。入口默认全不开放；列表不是“按钮有报文就可执行”的证明。
2. 页面确认后签发递增请求号及可立即撤销的一次性许可，发`deviceOperationRequested(request,connection,operation,holeName)`。主窗口重新核设备状态和权限，再排队调用`ReagentStore::prepareOriginalDeviceOperation(...,linuxNew)`。**页面是唯一许可签发者，不要重签旧请求，也不要绕过它直接调用写入准备方法。**
3. 工作线程按请求号/连接/动作原子领取许可，并在版本检查和事务提交前检查同一许可。断线、超时、账户身份变化、占用及关闭会直接撤销；排队旧请求不能仅靠非0连接放行。提交已经发生后再切代会明确报告本地已存、设备未发，不能假说整项自动回滚。
4. 成功准备返回`originalDeviceOperationPrepared(request,connection,operation,packets,localSaved)`，失败返回`originalDevicePreparationFailed(...)`。本地保存成功后已重读；重读失败不交出设备命令。`packets`只是准备结果，不应直接调用`finishDeviceOperation(true)`。新priority等待对应Ur先校验并确实提交；旧priority无设备命令，只报告本地优先级已存。更换必须先处理Us/Ur、再允许Mg；不能一次把所有包盲目入队。清盘必须区分本地已清与设备待核。
5. 当前`DeviceControl::sendOriginalConfiguration`不接受UC，`maintenanceCommand`也不接受参数化Mg。总窗口需做有动作类型、来源和安全条件的正式发送入口；不要把它们伪装成固定维护命令或任意文本发送。
6. 页面以`deviceOperationProgress`分别显示本地已存、已发、待回读。有效当前N消息且原灯状态为操作中时可`renewDeviceOperationWait`续探测等待；不能让无关/坏包续时。最终用匹配的请求号和连接调用`finishDeviceOperation`。单孔/整盘/更换等待60秒，其他10秒；超时禁用能力、结果待核且不重发，重新核实后才重新开放。
7. 用量信号`usageCommandRequested(connection,payload)`默认阻断。真实QH完整校验后才`updateOriginalUsage`，QI后才`updateOriginalDeviceKey`；J回复及条码保存处理完成后才`finishOriginalUsageImport`。QH未返回保持“?次”，真实完整回复缺项才按原显示0次。切代已清旧统计/申请码，保留使用者未提交的添加码。J按UTF-8保留原输入，不由界面校验发明新的授权规则。
8. 统一构建需要添加`original_reagent_usage.cpp/.h`和`maskdlgreagentusage.ui`（sources/headers/forms）；`shell/build.sh`也有独立固定列表，不能只改.pri。沿用已有`QtMaterialScrollBar`实现；独立试剂目标是否已有此依赖须另核。所有公共接线和构建改动由总窗口完成。

## 实际验证与仍待验证

- 第一轮6份业务CPP和3个moc共9个ARM64对象编译通过，只有局部对象，没有整程序链接。第二轮修连接/旧显示/重读后重编；最终轮结果和指纹在下方追加，不能把旧轮对象当最新整程序结果。
- Windows实际运行核对：使用已经打开的`D:\SeeRain450全自动凝血分析仪\AutoCoagulationAnalyzer.exe`，SN450/等待连接/管理员；已进入试剂页、切剩余次数、打开耗材并切项目页、关闭弹窗。六项目显示0，耗材七项目均“?次”。详见`windows_reagents/review.md`及实际截图。没有改资料、发动作或重新构建；没有核实设备命令、真实回包、错误和恢复，不能将这些说成Windows运行通过。
- 原版所有写入、报文、权限和异常分支目前仍以只读源码对照为依据，现成发行版与参考源码的可见入口差异已登记。未把源码浏览冒充Windows动作验证。
- 待总窗口：窄补丁整合、全量ARM64链接、板卡身份与有效INI、上传指纹及动态库、RK原资料联机、每个动作具体TX/RX/资料变化、断线/超时/迟到/重复包、取消草稿重读、资源和同板控制影响、真实仪器验收。没有做模拟、回环、匿名或电脑产品测试。
- 未来同范围切片：批组`is_related`活跃入口/取消、QH/QI/J完整业务、600容量标定和正式N回读、圆圈次数显示。均不因本候选或局部编译打勾；凝血算法未动。

## 最终检查结果

- `reagent_compile_05.log`：Ubuntu-24.04/qyq、现有Qt5.15.13 ARM64头文件、aarch64-linux-gnu-g++、C++14、Release/-O2；6份业务CPP及3个moc全部生成AArch64对象，实际退出0。编译前后214项候选/工程头文件和表单指纹完全一致。没有整程序链接，没有运行产品。
- 03因WSL缺rg使输入记录脚本退出1，采用普通文件枚举后继续；04两份对象编译完成，但全工程头文件检查因总窗口主动合入`original_detection_session.h`而退出1，保留原日志及`phase04_build_inputs_before/after.json`。总窗口确认旧integration_01已停止，随后05按修后正式输入全量重编；不能把04算通过。
- 13份候选编译期间无漂移；10份正式对应文件仍与创建候选时的底稿逐字节一致。补丁只含本模块文件，不含可执行权限变更；`git apply --check --ignore-space-change`实际退出0，没有应用补丁。
- 最终补丁SHA-256：`da7acc5946a71e0046df0c59d3c3f986fb3f110248aa88c028830b332902c589`。每份候选、底稿和9个对象指纹见`reagent_candidate_manifest.json`。
- 独立审查发现的旧请求许可、旧连接耗材显示、项目变化失效/重读已修；最后补齐取消草稿后的延后重读，见`reagent_final_review_03.md`。首轮和第二轮审查的缺陷记录保留其原范围，不改写成当时已经通过。
- 写入分支尚未做Windows/RK同输入前后库对照。特别是原控件到TableDal全列写入带来的空值转0、Qt默认数值文本规范化及旧协议优先级边界，须在开放写动作前继续逐字段核对；当前候选按负责字段事务更新并保留其他原文本，不能据本轮对象编译宣布所有落库字节完全一致。
- 本窗口没有运行中的产品、编译进程或子Agent，没有取得板端部署/显示控制权；Windows只读弹窗已关闭并交出导航。下一步由总窗口按自身冻结安排审阅、接线、统一编译和板B验证，未获安排不继续扩展其他事项。
