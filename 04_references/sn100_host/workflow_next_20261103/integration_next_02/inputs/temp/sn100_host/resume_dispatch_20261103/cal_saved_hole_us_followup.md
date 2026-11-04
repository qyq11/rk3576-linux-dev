# CAL候选：明确保存/清孔后的真实Us消费者续审

2026-11-03。独立只读检查冻结候选 `temp/task_reagent_cal_metadata_candidate_01/candidate/06_programs/sn100_host/` 的本条调用链，参考既有窄审更正和作者 `temp/task_reagent_cal_resume_20261103/root_coordination_note.md`，结论依据实际源码。下文未特别说明的行号均为该候选，注意 `reagent_store.cpp` 因新增构造初始化比旧正式相应位置顺延一行。只新增本报告，未改正式/候选，没有构建、运行产品、执行SQL、联网或联板，没有新开子Agent。当前总控已合入ASCII两文件，本报告不核整套前像，也不声称现场全部684份仍等于b066。

**本条所审范围内，未见CAL片新增的确定批号回盖或失败仍发Us阻点。** 明确保存/清孔会先提交目标行，再按原孔范围覆盖同一批号模型，成功后才发保存信号；发布失败虽本地已提交，但不会进入本条Us消费者。Us读取的是保存后的真实原库，不是旧CAL的临时视图。这个结论只覆盖已追通的源码顺序，不是整片、全部消费者、并发恢复、编译或RK通过。

## 目标批号怎样保存及覆盖盘面来源

页面 `reagents/reagents_page.cpp:1002–1003` 以有效盘面批号选编辑初值，保存时 `:1059–1063` 把实际选择文字放入 `originalBatch` 并排保存；清孔另走 `requestClearHole`（`:1043–1048`）。页面短横线只用于显示，不从详情标签读回并写成批号。

Editor `reagents/original_reagent_editor.cpp:589–606` 先核目标原行、试剂/瓶型/区域和输入，`:600–605` 明确把所保存的批号加入更新列：非空用 `h.originalBatch`，空选择按原规则为精确 `Default`。`saveRow:492–507` 使用目标id更新本页负责的列，不另建孔，也不从整个模型批号集合批量写库。清孔 `:609–619` 明确写目标行空批号、none和清空容量等原字段。原Windows `frmreagent/maskdlgreagentsetting.cpp:232–247` 同样是明确保存时空选择改Default、选择none则清孔；这不是CAL遍历本身在保存。

本片给saveHole传入 `before->hole_name` 原文（`:606`），给clearHole传入实际匹配行的 `originalName`（`:611–619`），没有用界面大写名冒充覆盖孔范围。两者均进入 `mutate:431–466`：

1. 按项目→试剂顺序取两锁并检查原文件版本，在锁内取模型的来源票据。
2. 执行目标行更新并COMMIT；写连接所在局部块结束后，两个文件锁仍持有。
3. 重读真实原试剂状态，再对目标原孔名调用 `overwrite(..., rebuild=false, 原票据)`。
4. 新头 `original_reagent_page_metadata.h:40–78` 先检查实例、盘面及修订；只采用names指定孔的真实行身份和批号，其他孔保留原有效值。同值覆盖仍推进修订。读取真实行而非复用保存前模型，可以处理实际数据库返回的类型/值；本轮未执行SQL，不据源码代替实际类型转换验收。

所以显式清孔确实会把该孔先前的内存Default替换为空；其他没有明确覆盖的孔不会跟着清掉。明确保存的新批号也会替代该孔的旧CAL值，不靠普通load把整个盘面重置。票据过期或发布失败不拿新票据给旧保存重新签名。

## 提交后来源发布失败会怎样

Editor `mutate:454–466` 在COMMIT返回成功之后才重读/发布。发布失败时使原票据失效，并明确返回“本地资料已提交，但盘面批号来源未重建”，没有在这条后置失败分支调用rollback。

本条saveHole/clearHole没有把 `databaseCommitted` 输出参数传到外层，仍使用原bool接口；本地已提交的事实保存在明确错误文字中，不能据false说成已经回滚。候选 `reagents/reagent_store.cpp:815–817,888–890` 仅在Editor返回success时发 `originalHoleSaved`；`finishOriginal:362–366` 的失败分支只发错误。因此上述提交成功但来源发布失败，不会发保存信号，不会启动 `prepareOriginalReagentUpdate`，不发送本条Us。这里也没有解除N/C或任务事务未知保护的调用。

另须区分成功后普通页面重读失败：`finishOriginal` 的成功分支会调用 `loadOriginal`，但函数无返回值，Store仍可能继续发保存信号。后面的Us会另行重读真实库。这个原有接口不能被描述为“保存信号保证所有页面重读均成功”；它不是CAL片新增的发布失败仍发信号路径。

## 真正的保存信号到Us链，包含字符串调用

候选真实调用为：

`ReagentStore::saveHole/clearHole` → `originalHoleSaved(孔名)` → `shell/device_control_shell.cpp:660–687` → 字符串形式 `QMetaObject::invokeMethod(store,"prepareOriginalReagentUpdate",Qt::QueuedConnection,...)` → `samples/original_sample_store.cpp:524–538` → `OriginalReagentRepository::singleReagentSettingPayload`。

这不是只用于测试。shell先检查上一条更新、优先级动作和恢复未知状态（`:663–673`）；只在当前连接且原状态第19字段精确为 `LinuxNew` 时继续（`:675–676`），再核状态新鲜、无未知/在途控制、主状态和当前配置连接（`:677–682`）。随后先登记该孔Ur回读等待，再排SampleStore请求。

SampleStore `:526–530` 再核所属线程、就绪、当前同步连接及账户，并只接受当前同步对象的简化协议。`OriginalDeviceSync::begin:32–59` 从本次连接保存linuxNew分支；`usesSimplifiedProtocol` 要求该连接仍存在。它随后新建只读Repository，从实际原库加载目标行（`:531–537`），没有采用先前Editor的缓存行，也没有把旧CAL视图中的批号重新投影进此包。

`reagents/original_reagent_repository.cpp:723–732` 的 `singleReagentSettingPayload` 只在发送副本中将目标优先级设99，再交 `singleReagentUpdatePayload:689–720`。Us的第4项批号直接取所读目标行，清孔的空批号仍为空；没有再把空批号替换为Default，也没有写回数据库。第14项仍按原编码发送max，第15项是updateAmount；本条shell传true。对保存/清孔后的这条链，正常顺序下原库批号已经与明确覆盖后的模型一致，不能因这里没有“有效批号投影”便认定漏接，更不能额外把清孔刚清掉的旧Default复活。

原Windows `maskdlgreagentsetting.cpp:251–267` 的明确设置仅在新协议发送Us，另一分支按原保存通知处理。当前候选也保留“旧协议CAL空批号setter”与“仅LinuxNew明确保存后单孔Us”的区别：CAL setter在Repository `:441–450` 明确受 `!cached.linuxNew` 条件约束；本条Us有上述两层新协议条件。不能假设同一已核协议请求中，旧CAL刚修改的空值会无条件经这条新协议Us直接发送。先前CAL的内存副作用可在以后被明确保存或清孔覆盖，这是本片预期生命周期，不是错误回盖。

## 排队、重读与未知边界：本片没有关闭什么

这条原有信号只带孔名，字符串请求另带updateAmount和连接号；它没有携带该次保存的文件后像指纹、原root、批号模型修订或独立保存请求身份。SampleStore重读的是处理时的真实库，不是被冻结的那次保存后像；也未在此处持两文件锁把该次保存、重读、生成Us和最终入队连成同一个原子步骤。Repository `load:158–202` 只检查自身读取前后的三文件指纹。

因此，若在两个排队阶段之间又发生其他真实覆盖/保存、协议或身份变更，不能仅凭“先保存后发信号”就证明Us恰属第一次保存。这个接口和缺少上述身份绑定的结构在CAL片之前已经存在；本片没有改写 `prepareOriginalReagentUpdate`、字符串排队或最终发送入口。本次未构造出本片新增的确定回盖触发条件，也不把这个既有非任务异步边界宣称已经安全关闭。

为核实新增归属，本轮只读比较了当前正式与候选的 `prepareOriginalReagentUpdate`、`singleReagentSettingPayload` 两个完整函数，以及 `originalHoleSaved` 的shell连接块，三处文本均相同。这只是这三处窄比较，不代表其他文件或全684份前像核验。

最终原配置发送入口仍在shell `:626–658` 核当前连接、恢复状态及Us互斥；`DeviceControl::sendOriginalConfiguration:307–316` 另核更新状态。入队不表示设备执行或回读完成。shell `beginOriginalReagentWait:751–768` 超时保留未知、不自动重发；`:715–733` 只有当前连接且后台成功处理的Ur包含等待目标孔才结束该孔等待。连接/身份撤销 `:376–395` 保留需要恢复的上下文。这些是既有边界，不是按本次静态核查取得的并发或真实晚到回包通过证据。

待总控在原配置实际条件分别核：明确保存批号与清孔空批号的真实数据库后像、同孔模型覆盖及其他孔Default保留；COMMIT后重读/发布失败不进Us且错误不称回滚；LinuxNew与旧协议分支；排队间再次保存/Ur/重连的归属；真实Us字节与对应Ur；未知后的保留和恢复。旧非任务COMMIT三态回执、同连接晚到Ur归属、N/C交错、42/48孔对应仍属已有未闭合或待实物项目，不据本条放行全部消费者。
