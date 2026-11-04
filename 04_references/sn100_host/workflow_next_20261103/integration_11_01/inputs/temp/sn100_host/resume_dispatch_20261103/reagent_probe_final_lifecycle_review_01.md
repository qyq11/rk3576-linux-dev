# 单孔Mg最终候选的独立异步与收尾审查

本审查没有发现能从实际代码确认的新增异步、取消或重入阻断。结论仅针对 freeze_03 的稀疏候选：它仍基于 b688，不能直接覆盖当前含 B1 的正式源码。不得把本结论写成已合入本批、编译通过、RK通过、仪器通过或资源通过。

本轮只用 Windows 读取候选、必要正式源码及摘要；只新增本报告。没有执行候选、产品、SQL、报文、Git补丁、编译、网络或板卡动作，也没有把作者提到的 Windows 后续核对当成运行授权。本批正式687的构建由 Root 串行负责，本报告不影响其冻结来源。

## 实际检查的版本和范围

实际摘要与 Root 指定值一致：

|原件|实际SHA-256|
|---|---|
|`reagent_single_probe_20261103_01/handoff.md`|`e2f9e234cfd10aa22afc2cf6b14e65e8fcb0ba85af2171b79717d4aa7854ffe3`|
|`delivery_manifest.json`|`4b834e28dd3ee71bd3759b016b34944194498fe442515474086d5e292097e93a`|
|`freeze_03.json`|`1f4b9034f1820a1df127a2b6555bcd845fbd33aac40b87b77268afd9b6795678`|
|`packaging_02/single_probe_on_b688.patch`|`2c3723630dd749cf684d5efacdd852726a6a92526bf9403c65cb24927ab61e4d`|

逐份计算13份 candidate 源码摘要，13/13等于 freeze_03；补丁有且只有对应13个文件：12修改、新增纯值头 `reagents/original_reagent_probe.h`。本轮没有重放作者的补丁或整树复制。作者 packaging_02 保存的检查/应用退出0属于作者证据，本轮没有重新执行或认领。

实际读取了候选的 Request/Prepared/NGrant、Store许可和来源锁、Root准备/发送/回包/取消/结束、Page请求及专用N展示；还读取当前正式的 `original_reagent_level_worker.cpp`、`task_reagent_shell.cpp`、关闭顺序、TCP入队及B1公共入口必要部分。以下结论据这些代码路径形成，没有采用作者审查报告作为结论依据。

前像点核范围为候选声明的12份既有源，而非重做完整687表。实际只有两份因B1漂移，其余10份仍等于 `base_selected_sources.json`：

|重叠文件|候选b688前像SHA|当前正式实际SHA|
|---|---|---|
|`shell/frmmain.h`|`efae044127aecd82e7953e461e742b9537f624556c2e729fabe7ac5ff0803352`|`a84c57bcd0f98979510caca2823ea271c8d8add7f90724395aebbcf7d5d4f068`|
|`shell/device_control_shell.cpp`|`eadccb771fc651cac7e6d26f52386ce8b570cbfc092910745356b129d7a331d4`|`d7bb3b6c05ed6cc576935eb67e4b9fdd528583b3959c4e131434114bcc523ecc`|

本轮摘要/范围点核工具块为 `ee2dbc`、`9a9458`。这些是只读核对，不是产品测试结果。

## 准备许可、真正后台返回和来源锁

Root 的 `beginReagentProbe`（候选 `device_control_shell.cpp:1300–1325`）先登记唯一 owner，绑定页面请求号、连接代次、账户、原资料根、孔位与10秒 `steady_clock` 截止点；Page自己的原请求不对应即拒绝。Root 同时持有一份 owner、一个100毫秒检查器，不建立新线程。

Store 的 `authorizeOriginalProbe` / `prepareOriginalProbe`（`original_reagent_store.cpp:132–257`）只有一个许可槽。领取通过原请求全等和原子比较交换，不按请求号大小猜归属；重复token、未撤旧许可、仍可用或已Claimed的旧来源都会拒绝。准备槽重新核账户、actionReady、资料根、截止点、ready及仪器忙；读取结束、发布proof后还复核当前许可，覆盖GUI恰在发布前撤销的窗口。

工作线程按项目锁→试剂锁顺序 `tryLock(0)`，拒绝 pending 项目复制，不在等锁时阻塞GUI。成功来源证明绑定原 Request、完整payload、typed孔行、versions、不可变初始化对象、盘面批号视图；GUI领取只改proof的原子状态，不能让GUI副本析构去释放 `QLockFile`。只有Store线程的 lease持有两锁。

Root 只在收到对应 `originalProbePrepared` 后设置 `prepareReturned=true`，取消准备不会将这个位提前置真；唯许可/排队根本没建立成功时才直接结清。已排入的准备即使过了10秒也等待真实结果，不假退后台工作。10秒是继续准备和发送的准入期限，不能称后台读操作一定10秒内返回；代码没有强杀线程或中断SQL。

`preparedReagentProbe` 第一条构造 `ProbeSourceRelease`（`device_control_shell.cpp:27–38,1328–1370`），因此旧结果、不匹配、失败、到期、取消、发前拒绝、send返回等路径都在栈退出时按prepared原token撤销并排队归还。Store `releaseOriginalProbeSource` 只释放相同token，不会清另一份来源。若领取与撤销/到期相撞，Claimed不会被期限回收提前解锁；实际send栈返回后才显式归还。未领取来源到期或关闭可由worker清理。归还排队失败只记录并保留占用，未假称释放成功；这仍需真实关闭排空验证。

## 发送重入和取消不改写真实事实

发送前 Root 再核初始化、批号、10秒截止、账户、主题来源、页面孔身份和 setting版本，并一次领取Store proof。随后先将阶段置 `Queuing`，再调用专用 `DeviceControl::sendReagentProbe`；两把合作锁覆盖该调用返回。没有把普通准备接口重新接为Mg发送入口。

DeviceControl（`device_control.cpp:477–503`）检查当前连接、deadline、实际最新状态和舱盖，限定 `Mg[1]`、九个括号字段、目标孔/区域、128000字节正文及4096字节字段。入队前再次检查deadline，然后先登记 `probeOwner_` 和 `probeMayHaveQueued_=true` 才调用transport。它不把入队当动作完成，也没有自动重发。

实际transport `queueMessage:247–279` 先受131072字节待写预算限制，再登记整批帧，`wireQueued` 可同步重入，通知后检查socket/代次；泵送另排队。Root的cancel在 `Queuing` 时不排立即释放来源锁，而由Prepared栈的finally负责。cancel还先读取DeviceControl的may-have-queued事实，可能已尝试即保留独立unknown，停止新N grant。

若同步回调已使owner退休，send返回后的 `reagentProbe_!=owner` 检查会阻止旧栈覆盖新状态；若已有N必须排空而owner仍保留，cancelled位没有被清除，后续N准入仍拒绝。没有因为send返回false就统一报告从未发送。最终Page通知前先置 `retiring=true`，自身 `operationFinished(false)` 不会再次取消同一退休动作。

普通开始、资料更新及部分设备业务接收增加探测门；DeviceControl的通用发送例外只保留Qj/Qk/QH、消音Mz和MP，没有借ownProbe布尔值开放其他公开入口。探测unknown在Root和DeviceControl均单向保留；连接换代、Qj和通用确认没有清除路径。它只是本进程状态，不具备跨进程恢复证明。

## NGrant的自身忙例外是精确的

Root在N解码之后，先按当前单孔prepared数据核孔、试剂、区域、瓶型、针、发送序号和连接；已有一条N未收尾时不再接纳第二条。它只在此 `ownProbe` 为真时免除Page自身busy，仍检查液面worker忙、未确认运行量、草稿、任务准备、其他资料回读和队列。

Page的 `grantOriginalProbeN`（`reagents_page.cpp:1630–1649`）同时要求原 Request/token、原页面pending request、当前连接、账户、资料根、孔typed身份、盘面批号视图及一个非零具体packet。grant只占单槽。`originalProbeNMatches` 的忙例外还要求该packet、connection、root、hole及owner账户完全对应；不是“只要是N就忽略忙”，也没有清其他等待。

取消只把 `probeAcceptingN_` 关掉，已派发grant仍可以收到真实保存/展示结果；换账户或连接后的页面来源检查仍会拒绝采用旧展示。取消没有撤销已提交N、补Us/Ur、重发或退量。

Root执行期 `reagentProbeBlocker:1249–1261` 对任务收尾门的例外仅限当前 `owner.nPacket` / 原connection的唯一 `nReaders` 项，而且所有action已processed、无transaction或overflow。其他reader、未processed动作和事务未知仍保留原门，没有为了自身reader清空Bridge。

## 保存、页面采用和真实reader返回分别结清

当前正式 `ObservationWorker::submit:329–376` 以连接、许可、实际Qj新鲜期限、任务门、单个pending和递增packet约束后台提交。`process:431–526` 在实际原资料读取、保存、提交后重读、项目/主题和版本核对结束后才发布不可变结果；提交后失败保留committed事实，不声称回滚。pending复位后发presentationReady/processed，作用域内数据库和锁已收尾；关闭队列不会中途杀正在运行的槽。

Root对processed要求当前N Context、packet、permission全匹配，分别保存 committed/loaded；Page接受展示另核当前permission、新鲜期限、root、savedFingerprint、同一grant及无草稿，不用普通loaded直接宣布设备完成。

Page先把 `loadOriginalLevelObservation` 排到原Store线程；该槽在同样两把合作锁下拒pending并做专用真实读取。Root随后在同一Store事件队列追加内存哨兵（`reagent_level_shell.cpp:366–417`），哨兵只有在读取槽实际返回后才能投回GUI。Root先把 `nReaderReturned=false`，因此Page较早发出“采用/拒绝”信号也不能退休正在返回的reader。哨兵按具体packet/connection移除对应Bridge项，不清其他reader。

`finishReagentProbeIfReady:1518–1551` 同时要求准备已返回；若曾派N，还要求N processed、展示已有结果、reader真正返回，才退休具体grant。成功另外要求Mg正回报、见过N、实际committed/loaded/shown且无unknown。Mg动作回报不能代替N提交或页面采用。关闭入口先取消探测并等待其owner消失，再按原液面worker条件继续关闭；没有用timer到期冒充worker结束。

## 期限、迟到包和资源的实际边界

Root准备期限为不可变10秒；DeviceControl发送前也核实际steady clock，不能只靠100毫秒timer。执行期到期在N入口、grant前、Mg入口和最终收尾均按实际elapsed检查，避免先采用迟到包再续时。Page在登记探测owner时设PreciseTimer；Root实际发送后、以及合格N且原灯态为2时都先重启自己的clock再续Page60秒等待。

60秒是可按原有效N/操作中条件续时的等待窗，不是整次操作不可延长的硬上限。取消后会继续等已经派发的后台工作；长期I/O挂起可以让关闭继续等待，代码不提供强制释放成功保证。

- 超时、取消、断线或来源变化且可能已入队：保留unknown，停止新grant；已有N继续记真实结果。owner收尾后unknown仍禁止新的N写入。
- 非当前连接、准备阶段、取消后的Mg只作未归属日志；相同等待内相同Mg幂等，不同Mg结果取消并保留未知。
- 正Mg先到仍等待N；N先完成仍等待Mg。负Mg先到且没有已派N时可以结束失败；已派N则等它真实排空。此后到达的N不再归属该失败探测；若没有unknown，它可能进入原被动N入口，不能把这种后续被动保存倒填为该探测成功。
- 原协议没有请求号：同连接唯一等待窗内完全相同的旧N/Mg无法凭本地token绝对识别。代码没有把token写上线或声称设备回显。该限制需要真实正/负Mg与N两种先后顺序留证，不能由静态审查消除。

新增常驻规模为一个Root owner、一个Store许可/proof/lease、一个Page grant、两个计时对象；复用现有工作线程和单条N pending/专读。Mg发送正文128000字节、字段4096字节；Root Mg回包另限4096字段。原设备队列32包/1MiB、transport131072待写预算仍存在。候选的完整项目versions/批号快照规模随真实原资料集合，不能据“只有一份owner”声称内存字节预算已测过。

100毫秒tick会检查原初始化当前性；worker还会核全部项目文件集合、文件摘要和原资料。CPU、文件I/O、UI最长停顿、锁占用、日志增长及同板控制影响没有RK测量结果。单槽和长度门只证明代码结构有界，不能签资源验收通过。

## 当前B1必须保留的冲突，留待后续串行整合

本次不合入。候选两份主窗文件仍是b688起点，直接替换会删除当前正式B1行为：

1. 当前 `frmmain.h:286–300` 有第三个manualRequest参数、共享暂停/消音入口、manual nonce/epoch、身份/连接/页面session和手动请求状态；Mg候选仍是旧两参数开始和单参数 `deviceUserControlBlocker`。以后须保留当前签名及所有manual状态，只增加Mg成员/守卫，不能退回旧头。
2. 当前 `device_control_shell.cpp:122–295` 有手动请求挂接、精确六条payload分派、撤销、终态通知、共享确认与发送前重核；候选没有这些代码。以后在这些真实新入口补Mg门，不能只把Mg门贴回旧按钮路径；共享暂停/消音的参数和行为也须保留，不用旧签名覆盖。
3. 当前 `requestDeviceStart:298–375` 在dispatch前把manual id映射到原deviceTaskRequest，updateActions后再核，再走 `queueUserTaskPreparation`。候选同样保留User04无写开始链，但没有B1映射。以后只能在当前链增加Mg阻挡，不改回旧 `prepareDeviceTasks`，也不改变QB v3状态选择。
4. 当前唯一 `finishDeviceTaskPreparation:377–403` 在真实User finished/Bridge退休后才接Ts，并记录manual attempted/结果。候选旧helper缺B1结果链；不能用候选整段替换。唯一 `deviceTasksPrepared` connect继续保持唯一，不另加第二发送lambda。
5. 当前 `cancelManualCommunication:198–215` 会按保存的manual taskRequest取消真实User准备，通知终态不提前清User/worker/N-C门。Mg的上下文取消、updateActions/忙计算与原接收门需要与这些现存逻辑窄合，不能漏掉任一方或把整个b688文件盖回。

以后整合必须以这两份真实b688 baseline为共同base、当前f741正式为ours、freeze_03候选为theirs，保留三侧和真实冲突，生成相对当前正式的新窄patch，再独立审新增量。其他10份也须再次核前像；新增头要由Root登记构建输入并据真实整合结果生成新全表，不能预填688或沿用本批687终态。

本片下一阶段尚缺的是：上述B1窄合与复审、正式来源登记、完整ARM64构建、已核身份板卡上的原配置软件复核、真实仪器Mg/N/提交/展示/读者顺序与异常收尾、资源测量。按用户授权在RK与仪器核对，不安排Windows/WSL产品、模拟报文或脱机补验。
