# 07候选：液面回报处理与退出条件的源码核对

2026-11-03。本次只读候选源码，只新增本说明；未冻结、编译、运行产品、联网或操作板端，未改06、正式源码、模板和公共文档。以下结论是源码审查，不是已复现的板端结果。

实际候选根为 `C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/integration_07_review/stage_from_06/06_programs/sn100_host/`。合并说明中的底稿649项、候选663项属于当前隔离候选，不是最终冻结编号。下面文件和行号均相对这个候选根。

## 先给合同工具的结论

N03是本次液面回报移植切片的名称，实际消费顶层 `N[...]` 报文。新增处理对象复用现有试剂存储线程，没有新建独立N线程。当前候选 `shell/main.cpp` 字节仍为06的 `a0683da0485f71ac2b8af5abda6c7251fc35b65cc19288dd63c9d4ea1193bed7`，原17项启动/退出日志也仍存在。

**原17项只能继续证明其原有范围。07还要保存N对象的归属、正常关闭守卫、逐包处理结果及未收尾情况，不能只扫描未变化的main就宣称N已经排空或功能通过。**

当前候选没有 `REAGENT_LEVEL_STOPPED`、`REAGENT_LEVEL_SHUTDOWN_UNCONFIRMED` 或其他N专属停止/未确认日志。它也没有独立N失败计数或N专属退出码分支。合同中不能把这些不存在的文字写成必需运行日志。

## 处理对象、启用和真实线程归属

[attachDeviceTaskStore](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/integration_07_review/stage_from_06/06_programs/sn100_host/shell/device_control_shell.cpp:376) 在设备控制、样本、试剂和项目页存在时调用 `attachReagentLevels()`。后者在 `shell/reagent_level_shell.cpp:185–192` 要求设备控制、试剂及其Store、样本及其Store均存在；构造 `OriginalLevels::ObservationWorker` 后执行 `moveToThread(reagents_->taskStore()->thread())`，将所属线程的finished接到deleteLater，将ReagentStore.closed接到revoke。

`ObservationWorker` 头文件 `reagents/original_reagent_level_worker.h:27–30` 明确说明复用ReagentStore线程。`prepareSource()` 和 `process()` 在worker.cpp:130、382检查当前线程；普通公开方法用互斥量保护内存、许可和在途状态。没有额外QThread或新的设备连接。

main中的QThread声明仍是以下14个变量：

| 位置 | 实际变量 |
| --- | --- |
| shell/main.cpp:547、552、560 | databaseThread、accountThread、historyThread |
| :568 | projectThread、reagentThread，同一条声明 |
| :581、591、601 | sampleThread、maintenanceThread、originalChannelThread |
| :606、614、627 | printerThread、lisThread、alarmHistoryThread |
| :640、649、688 | qualityThread、calibrationThread、originalAlarmThread |

N的线程所有者是其中的 `reagentThread`。main:1599阻塞调用ReagentStore.close，:1615退出该线程，:1635等待线程结束。main:1636–1637的 `THREADS_STOPPED` 表达式包含 `!reagentThread.isRunning()`；仍未包含原通道线程，所以 `ORIGINAL_CHANNEL_WORKER_STOPPED true` 仍须单独保留。`REAGENT_STORE_CLOSED` 日志来源为main:1185。

这两条已有日志可以证明试剂Store及其所属线程完成了相应收尾；它们没有单独证明N的GUI结果已消费、N保存成功或页面已接受本次值。

## 一条N如何收尾

[接收与逐包回调](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/integration_07_review/stage_from_06/06_programs/sn100_host/shell/reagent_level_shell.cpp:242) 核对到达连接代次，只在实际Qj上更新3秒新鲜期限，再处理顶层N。通过条件后生成本地递增包号，调用worker.submit；成功入队才设 `reagentLevelActive_`。

实际日志如下，必须按本地包号和到达连接关联，不靠“出现过某个单词”判定：

| 日志及源码行 | 能确定什么 |
| --- | --- |
| `REAGENT_LEVEL_ACCEPTED`，shell/reagent_level_shell.cpp:273 | 包号、连接和原payload十六进制；只表示成功入队。 |
| `REAGENT_LEVEL_PROCESSED`，:280 | 包号、连接、committed、loaded、保存指纹和说明；在GUI回调中打印。两个布尔值必须分别解释。 |
| `REAGENT_LEVEL_DISPLAY`，:316 | 包号、连接、permission、shown、指纹和说明；页面已报告接受或拒绝。 |
| `REAGENT_LEVEL`，:29 | 混合阻断、失败、取消和提示文字，不能一律当成功或失败。 |

[worker.process](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/integration_07_review/stage_from_06/06_programs/sn100_host/reagents/original_reagent_level_worker.cpp:372) 在所属线程读取真实资料，保存前后反复核许可、原库及主题/容量/项目版本。worker:465先清除pending，:468按loaded发presentationReady，:469发processed；这些是排给GUI的结果，线程已空闲和GUI已消费结果是两个时点。

保存函数 `reagents/original_reagent_level_repository.cpp:174–269` 在事务及commit前核许可；:243–257更新 `formula_level/amount_uL`，非测试态另更新 `used_ul`，成功commit后把committed置true。提交后许可或重读失败不能写成“已经回滚”。`committed=true, loaded=false` 应记为已写入但结果待核，不凭应用退出0把本次N保存功能判成通过。

也不能把所有false列为退出失败：撤销发生在提交前时，可以正常得到未写入的处理结果；连接或关闭变化可使页面拒绝旧展示。`loaded=true` 只表示后台重读通过，不能直接等同shown=true。

## 真正的关闭路径与已存在的限制

[frmMain.closeEvent](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/integration_07_review/stage_from_06/06_programs/sn100_host/shell/frmmain.cpp:624) 先处理登录、页面、LIS和原告警守卫，:668才调用 `requestReagentLevelShutdown()`。试剂页面正忙时，前面的catalog离开守卫也可能先拒绝退出：catalog_shell.cpp:163，reagents_page.cpp:555–573。

`requestReagentLevelShutdown()` 位于shell/reagent_level_shell.cpp:96–106：

1. 将closing置true，调用invalidateReagentLevels撤许可、上下文和待用来源。
2. 只要GUI的active仍存在或worker.isBusy仍为true，返回false，Close被忽略；不能终止数据库线程或假定已回滚。
3. 条件均空才返回true；此候选在成功分支没有专属运行日志。

processed回调在:281–288核对本条等待后清除active；:294在closing且worker空闲时排队再次close。页面已排队的展示可能由 `ReagentsPage::setOriginalLevelContext()`（reagents_page_level.cpp:147–176）撤销，并发shown=false；尚未入页面的presentation在shell:296–302遇到closing会只记录通用提示而返回。因此**不能机械要求每个loaded=true都必须随后有DISPLAY true**，需要区分关闭取消和本次N功能成功验收。

外层 `ShellViewport::closeEvent()`（main:231–247）先调用shell.close；被拒就忽略外层Close，只有内层守卫接受后才停止原通道Receiver。

**需要保留的具体风险：** N回调:294排的是 `close()`，目标是frmMain子窗；main:216已把它交给滚动宿主。frmmain:615–616明确说明关闭子窗只会隐藏页面，红色退出用 `window()->close()`；既有告警重试alarm_shell.cpp:136同样关闭外层窗口。因此“外层Close遇到在途N被拒 → N回执清空后只关闭内层”的源码路径，没有明确重发外层Close。外层可能仍在，尚未实机复现。QA若反复调用外层close，可能掩盖使用者只点一次退出的分支；不能据此声称一次红色退出及重开已验。本次未改流程。

强制退出也要准确描述：main:685已有aboutToQuit→DeviceControl.close；device_control.cpp:169–177关闭并publish，N的changed槽shell:206会重核并撤许可。它不是完全没有撤销。差别在于：事件循环结束后，main没有为N单独核GUI processed/显示回执、打印未确认日志或设置N失败码；旧通道/告警有各自检查，不能替N补造同名检查。frmMain析构:177再invalidate也不代表GUI曾消费在途结果。

## 07合同建议与根协调的新增决定

合同应绑定最终实际main、frmmain、reagent_level_shell、worker及相关页面文件指纹，保存上述线程归属和关闭条件。仅核main中的QThread声明不会发现新增的N对象。

建议把两类证据分别登记：

- **程序退出：** 原17项继续按真实启用范围核对，特别保留ReagentStore关闭、包含reagentThread的总停止及原通道专属停止。按真实窗口关闭路径核对检查器、应用、封装和case退出码及是否超时；逐包记录已接受N是否都得到匹配PROCESSED。接受后缺少后台回执、停在显示等待或退出保护未完成时，记录未确认，不能从线程已结束反推保存成功。
- **N功能：** 真实收到并接受一条N后，单独核committed、loaded、保存指纹、真实资料指定字段及页面shown。没有N就记“本次未覆盖”，不因原17项齐全而写N通过。正常关闭取消可以单独记录，不能把所有PROCESSED/DISPLAY false一刀切归为程序退出失败。

**2026-11-03根协调已决定、当前所读候选尚未生效的第3窄片：** 仅在 `requestReagentLevelShutdown()` 原成功return true前新增日志 `REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false`，字段来自实际状态；不改变guard、closing、revoke、处理或线程。由其他Agent另建隔离stage及补丁并回演，原候选不改。

这条READY目前是**计划中的日志文字，不是本文已读源码中的现成证据**。07工具应待最终实际snapshot中确认其日志表达式、布尔字段和所在成功分支后才提取，不能提前生成“18项已满足”。实施后可将原17项与实际READY共同核对；READY表示N关闭守卫在那个时点检查为空，不替代外层窗口关闭、所属线程join、逐包结果或N功能验收，也不消除上面的内外层重试风险。若尚未实施，合同只能诚实记录缺少这项可观测证明。

## 被动接收的真实验证边界

本片没有发送N/N03的调用。`decodePacket()`（reagents/original_reagent_level_packet.cpp:17–80）按已确认的Qj LinuxNew字段选择10/9个试剂字段；Sample孔按原分支忽略；针号只收1–3，高度要求有限且在0至0xff00之间。它不根据收到的长度反猜协议。

写门要求当前连接及实际Qj新鲜、原资料就绪、主题/容量文件状态与版本可核、机型一致、20/42试剂位配置与主控上报一致、原0–6状态有效、无同步等待或其他在途操作。容量文件的Present/Missing状态各自有指纹要求，不应把合法的已确认Missing状态说成必有文件。当前42配置/48上报差异在shell:143–146仍会阻断，不能改配置造成功条件。

不能擅自加“必须先管理员登录或先Ur”的前置：N入口没有session认证守卫；RuntimeProvider可从有效初始化瓶型建立来源，只有来源歧义或失效才等待其他真实来源。原同步另有登录和动作就绪守卫。N的processed/display收尾还可能继续已有同步流程，所以“本片不发N”不代表整机全程只有Qj；须保存全程DEVICE_WIRE TX/RX核范围，不用旧连接、模拟报文、探测或机械动作补证。当前没有真实N时保持待联机覆盖。

## 本次候选指纹

| 文件 | SHA-256 |
| --- | --- |
| shell/main.cpp | a0683da0485f71ac2b8af5abda6c7251fc35b65cc19288dd63c9d4ea1193bed7 |
| shell/frmmain.cpp | 1a0b9d0bb173e487c55eea2b9ad08530cc980bac1e40ba9ad6c22670fad91367 |
| shell/reagent_level_shell.cpp | ba5c765f6bf6e535c029a16fc635e3edcba5bcac40567bfdd033103af26b6011 |
| reagents/original_reagent_level_worker.cpp | 2eac51151cdc26af5164ab6400516545c1aa755ea4149c92b696959f7bf21881 |
| reagents/reagents_page_level.cpp | 1a8f63de297a1823c0161248271c780045f764795d5d8d057aa774af7e2dbb76 |

最终源码或第3窄片合并后须重新绑定实际文件和行号，不能把这些候选指纹移作最终验收记录。

## 根协调补充决定：第3、第4窄片均待最终新快照绑定

根协调已明确把下面两项独立纳入后续隔离合并；本文所读的 `stage_from_06` 观察原件保持原样，本次没有修改候选源码：

1. 第3片仅在原N关闭守卫成功return前增加 `REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false`，打印真实状态，不改守卫、closing、revoke、处理或线程。
2. 第4片仅将N processed回调在closing时排队执行的 `close()` 最小改为 `window()->close()`，并说明内层关闭只会隐藏页面。原守卫、状态和回调顺序保持不变。

这两项是已确定的后续变更范围，**不是当前观察原件已具备的行为，也不是实测结果**。最终须用包含两片的实际新快照重新核对日志来源和关闭回调，合同再绑定该最终快照。上文关于旧 `close()` 目标的证据保留为原候选说明；不能因有修订决定就把旧源码记录改写成已经修复并通过。

即使最终快照包含第4片，在途真实N期间使用者只发起一次外层退出、等待收尾后真正退出，仍待获准真实N场景验证；QA定时再次关闭外层窗口不能替代这项实测。本次按根协调要求在此收尾，不再扩大QA或源码检查范围。
