# 主控重连候选独立复核

结论：建议先修正下列1项，再将本候选作为下一批基线。重连计时、首连立即失败后的晚注入、显式取消、旧socket隔离、工作线程查询及干净连接同步，未发现其他确定阻断问题。本次仅源码和指纹检查，没有修改候选或正式源码，没有重编、运行、读业务库、联网或连接RK/主控。

审查固定输入为`device_reconnect/device_reconnect.patch`，SHA-256 `686f73c5164ab90fc8cd024f7d37aad02d5ce2b52b00ba417c92383651180d86`；`candidate_manifest.json` SHA-256 `67eb20eb2c55a3deeb83f18718c56c7b481a6f7b9d56dcc0000a82b64d37c8db`。以下行号均指本候选，不是正式04代码。

## 需要修正：优先使用断线时，完整待核对来由可能在保存前被清空（P2）

位置：`candidate/shell/device_control_shell.cpp:588–595`的简化后备记录；相关顺序在同文件53、145–158、247–276、602–604和641–658行。

触发条件：仪器原处于可操作状态，`session_.actionReady`为true；优先使用已经可能入队，仍在WaitingReply、SavingReply或WaitingPage阶段；随后连接断开。

实际可达顺序：

1. `DeviceControl::changed`处理器先在53行调用`updateDeviceActionReadiness`，此时新断线状态会把动作就绪变为false。
2. `applyDeviceActionReadiness`在158行立即调用`updateDeviceControlActions`，比同一changed处理器64行的`updateOriginalDeviceSync`早。
3. 此时底层代次已改变，`updateReagentPriorityAccess`发现当前优先使用不再符合条件，在602–604行调用`finishReagentPriority`。
4. `finishReagentPriority`先登记旧代次未知，再在651–654行清掉优先使用请求号、阶段、孔位、发送/回读报文等；658行又同步重算动作状态。
5. 这次重算进入588–595行：旧未知代次与当前代次不同，于是创建只有`priorityUnknownConnection`的`deviceReagentRecoveryContext`，并清除原代次字段。
6. 外层随后才在64行进入`updateOriginalDeviceSync`，再调用`invalidateDeviceTaskContext`。后者256行坚持“只写第一份”，已存在的简化记录阻止完整来由写入；即使改成补写，此时孔位、请求和报文也已经清空。

后果：`deviceReagentRecoveryRequired_`仍为true，业务仍被阻止，本问题没有发现自动重发或误放行。但是最常见的优先使用断线分支，会丢失本来仍可保存的原工作根、孔位、请求号、阶段及报文摘要，不能达到handoff声明的“单份内存来由”范围。它不是跨进程台账缺口；信息在当前进程、清理前本来就存在。

建议：在清除任何可能已入队的优先使用状态之前，统一保存第一份完整、受限的来由。让断线、超时、状态失效和跨代次后备路径共用这一保存步骤；简化后备记录不能先占住只写一次的位置。保留现有未知标记、一次许可撤销及业务阻止，不借修正本问题增加解除流程或重发。Preparing且明确未可能入队时，仍保留现在的取消处理。

复核条件：只读追通上述顺序，确认首次写入发生在请求/孔位/报文清空前；WaitingReply、SavingReply、WaitingPage断线均保留可获得的根、编号、孔位、阶段和摘要；后续断线不覆盖第一份来由；未知仍阻止业务。真实通知重入与界面结果待获准联机验证。

## 已核一致的部分

- 指纹：清单9个正式底稿与`base/`、当前正式文件均逐字节SHA相等；9个候选、9份参考源码和8个既有对象SHA均匹配manifest。Windows普通`git apply --check --whitespace=error`退出0。这里只核对作者已有对象，没有重新编译或把对象编译当功能通过。
- 循环计时：`DeviceControl`新计时器没有设置singleShot；失败后只在计时器未启动时start，保留循环节拍。连接中跳过当前拍，避免并发创建socket。间隔至少2000毫秒。与旧首连1500毫秒、当前底层1000毫秒的差异已在handoff列出，本片未改底层写入期限。
- 首次立即失败：底层清理会增加代次，`beginConnection`对这种同步失败不误清原连接意图；稍后注入已解析配置可安排重试。非法端点会明确撤销意图。配置自身不会复活已经显式close的连接意图。
- 取消和换代：close先增加意图代次、撤销请求并停计时，再关闭传输。两处原连接按钮在连接中或等待重连时均能取消；关闭重连配置后，失败状态显示“连接”。换端点、退出和通知重入继续核对意图及底层代次。
- 旧socket：未改底层`tcp_transport`。它在通知前先失效代次、清旧发送队列、断开旧回调并abort/deleteLater旧socket；新连接新建socket，各回调带原代次。因此不复活旧socket，也不从旧队列重放业务。
- 控制未知：发送控制前先登记pending，避免同步入队通知中断线遗漏未知动作。跨连接的开始/暂停/停止未知不会由新Qj自动清除；仍由既有人工核对入口处理，该入口不清除任务或试剂待核对事实。
- 任务查询：`hasUnfinishedIssuedTasks`只读取当前issuedTasks与finishedTasks，TD9不当终态。原Tr只有完整保存及预设更新后才登记finished；TR按其已提交终态登记。`cancelUnsent`只取消尚未占号的准备，`markConnectionUncertain`保留已发事实和预留，不清集合。新增查询在SampleStore所属线程读取，未就绪或根不一致返回未知。
- 异步回执：恢复查询携带请求号、原连接代次和原数据根；Root同时核pending、当前代次及当前页面根，过期、断线或根变化不会启动新同步。未能排队、10秒未确认、未知或未完任务均保守阻止。后台`beginOriginalDeviceSync`还会再次核未完任务，避免只在GUI检查。
- 干净连接：真实有效状态、原页面及账户就绪、根一致、查询确认干净后，继续既有`Ur → Up → UP`同步。`Up`发送参数，`UP`请求回读，不能把整个同步称为只读。本片没有重发旧Ts、Tp、Us或任务，也没有清除参数版本指纹去放行。
- Us/优先使用守卫：新增恢复阻止同时用于开始、优先使用、异步配置包和架任务下发；保留唯一Ur分流、原代次核验、许可撤销及优先使用真实10秒期限。普通Us等待保守记为待核对，与handoff已列范围一致。上面的P2涉及来由内容丢失，阻止标记本身仍保留。
- Qt队列参数：新增槽和回执只使用quint64、QString、bool等Qt内建类型；当前Qt5头将quint64列为ULongLong别名。既有生成moc有`queryDeviceRecovery`和`deviceRecoveryState`签名，没有发现新增自定义类型漏注册。此次只读核其已有编译证据。
- main：只在既有`enableDeviceControl(true)`后同GUI启动阶段注入原debug重连设置；退出只关闭网络和计时器。没有关闭SQL线程、复制整份main覆盖既有Qq/QC/LIS等接线，正式main底稿SHA仍为`52335c8d9f5048c91a48db71d63c634698114446fed82a6f93f3722189ba759f`。

## 已列范围与仍待验证

当前进程的内存任务集合不能证明上次进程退出前已对账；旧连接未完任务没有已核实的跨代次续传/对账入口。本候选明确保留阻止，未实现跨进程台账或试剂待核对解除。这里不要求本片凭空补造恢复业务，也不把重启视为恢复。

CAL参数、试剂组等本地保存后的“新版本→对应Up回读→重新就绪”统一消费者是另一个切片。本次未把它混入重连，也没有因本地页面已重读就称设备已同步。

尚未验证：真实首连拒绝/超时、循环重连节拍、手动取消、信号重入、断线前后SQL队列顺序、干净连接的参数收发、未完任务/Us未知保护、真实UI、响应资源和同板控制。结论仅为固定候选的源码审查与指纹核对，不代表RK或真实仪器功能通过。
