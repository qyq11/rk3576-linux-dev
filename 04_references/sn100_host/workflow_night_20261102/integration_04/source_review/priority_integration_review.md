# 优先使用第二片：同步与任务接线独立审查

本报告只核源码和补丁，不运行产品、数据库、模拟端、网络或RK，不修改候选和正式源码，也未另开审查Agent。页面许可、回读事务由其他审查者负责；本报告集中核唯一Ur入口、原资料同步、单孔异步通知、开始检测及共享文件合并边界。

## 已固定的本次范围

2026-11-02最后复核固定manifest的SHA-256为`3c6e40364a33b41a5b2d6b7c558062a3428485781e9179881dabf477f7d3e7ad`，对应三个补丁：

- `priority_reagents_delta.patch`：`d587e1df1b86779ebb0b008bec691afad855346f7496c6531c0a638f1527163c`。
- `priority_shell_delta.patch`：`4fdd31794ae74befea16e94f1a315d618162bc8db2ebd9a44f2b26eeba8bcb4f`。
- `priority_transport_delta.patch`：`18660dde2c586f169d869e72554dda5c8d6417be97a7482cd0c80fd88028f255`。

三个补丁指纹以及manifest所列十个候选目标均已匹配；对当前正式源码分别执行`git apply --check --ignore-space-change`均退出0，未实际应用。先前shell补丁`9b836fca...`已被新版本替代。对象编译状态不用于判断源码是否可以阅读，也不作为运行通过依据。

## 两项明确问题和处置边界

1. **旧版本丢到达代次的缺口已在新固定片修正。**原正式transport接收lambda忽略了generation，旧Root随后重新读取当前generation；该缺口有确定源码依据。新DeviceControl把收到的原代次传入`receive(message,connection)`，在入口、Ts的publish后、startAccepted后、严格信号前和严格信号后检查同连接且仍连接。Qj的publish之后也会经过最终检查。严格接收信号携带原代次；旧兼容信号只在检查仍有效后才发。新Root唯一入口先拒绝旧代次，给Ur路由及SampleStore排队使用收到的原代次，不重新贴当前编号。未见旧publish回调重连后继续结束新开始请求、查询新连接或提交旧包的路径。
2. **可选构建补丁目标路径错误，已撤出本次整合。**`usage_build_delta.patch`的三个构建hunk指向`06_programs/sn100_host/build.sh`，内容和底稿指纹实际属于`shell/build.sh`。对正式检查退出1，首hunk失败；根build.sh是另一个较早的独立程序脚本，不能写入这些统一程序增量。总协调已决定完全省略此错误可选补丁，另由首批窄接线补齐，故不要求为03重新应用它。旧manifest该路径及“私有应用成功”不能当作正式构建接线已完成。

底层`protocol/tcp_transport.cpp::readAvailable()`在发`wireReceived(bytes,generation)`后立即重新检查`current(socket,generation)`（357至359行），重连时`clearConnection()`先递增代次、清队列和解码器。wire观察者重连的底层拒绝已存在；仍需要上层保留代次和处理自身publish后的重入。

## 唯一Ur分流和原保存链

已核固定旧候选`shell/device_control_shell.cpp`：

- `attachDeviceTaskStore()`只在首次取得SampleStore后安装一次通用接收槽；优先处理器也有独立的一次接线标志。
- 通用接收槽在排入`SampleStore::receiveDevicePacket()`之前先调用`routeReagentPriorityReply()`（379至380行）。优先请求有效期间，所有Ur都被本次入口认领或拒收，不会先落入`OriginalDetectionSession::receiveDevicePayload()`的通用`reagents_.applyDeviceReply()`再核归属。
- Preparing、Queuing、SavingReply及WaitingPage阶段收到的Ur只记问题/拒收，不另写通用库；WaitingReply只有与本次Us严格匹配的完整单孔Ur才提交专用后台。
- 当前连接已消费的SHA-256回包摘要在没有活跃请求时仍先过滤；结果未知状态同样拦迟到Ur，不能清等待后让它重新落入通用保存。满4096条禁止新增请求，不静默遗忘历史。
- 普通通用包仍保留32包/1MiB队列、packet身份及工作线程处理。优先请求只在`devicePackets_`为空时开始；旧通用Ur的处理完成通知不具备跨等待插入新优先请求的直接重入点。其后活跃期间的新Ur也不会进入这条队列。

协议没有请求号，尚未见过但与新请求内容完全相同的旧Ur，无法仅凭本地编号证明来源；固定候选已明确这项限制。这里不要求凭猜测增加报文字段、改变保守拒收或开放新动作，实际时序仍待主控验证。

## 初始同步、单孔异步更新和开始互斥

- `reagentPriorityBlocker()`要求当前原配置已完成且连接号相同，并拒绝正在准备测试、旧单孔等待、控制待回执和结果未知。初始Ur/UP尚在等待时不会开放优先。
- `updateOriginalDeviceSync()`每连接只启动一次。其工作线程按初始Ur提交后再下发各项目Up/UP；最后项目确认后才发配置就绪。固定片未改OriginalDeviceSync及其ignored语义，没有由坏包给同步续时的新增调用。
- 优先请求先设置共享孔位等待，再排准备工作；`detectionStartBlocker()`拒绝该等待和结果未知。即使任务准备回执迟到，也会在调用真正开始前再核此blocker，未见优先在途直接放行开始。
- `originalHoleSaved`首先固定“可能已入队/已有未知/并非Preparing”的危险状态。若只是Preparing且从未可能入队，可先取消优先，再重核当前状态走原单孔同步；其他情形不发第二条Us，保持本地已保存而设备待核。
- 原单孔Us的准备通知异步到达时再次检查优先请求与未知状态（324至338行）。冲突的延迟Us被丢弃并明确结果待核；不会仅因保存通知时核过一次，就晚发第二条Us。
- 结束优先先直接撤销后台原子许可，再清owner、发页面完成和重算入口。准备/回读完成通知同时核请求号、连接号和阶段；旧通知不能直接把新请求结束为成功。旧已提交结果仅留`LATE_COMMIT`事实，不冒充未写库。

在最终三个固定片和上述信号顺序范围内，未发现尚待修正的确定同步/任务接线错误。没有将协议同字节归属限制或未做联机测量泛化成新的业务修改要求。

## 公共源码合并风险

当前正式`shell/device_control_shell.cpp`SHA-256为`831ef89948a8d411cb80b3db5a71160624063002f4ed88f06e674776094054c3`，`frmmain.h`为`224362ae392dd5e6d70c5302aea6dca70c1ab3336f2f0965fe59549712d029dc`，与固定shell底稿一致；对正式运行`git apply --check --ignore-space-change priority_shell_delta.patch`退出0。本报告未实际应用。

补丁仅更改六个试剂目标及两个shell目标，不含样本检测会话或QC/CAL任务文件。当前正式`original_detection_session.cpp`的SHA-256为`d99745a315c380117273362fb7badaf8b05e46bee842104fe87b3b034875a64e`，已核仍有独立`storedResultPayloadHashes_`：同任务同包早返回、不同包拒绝，成功完整存储后才登记终态并释放架；TD9不会据进度先释放待曲线归属。该文件不应从旧副本覆盖。

候选目录中的`main.cpp/frmmain.cpp/catalog_shell.cpp/build.sh`有为对象编译保存的整文件副本。中间main基线`f200237e...`曾早于正式QC/LIS接线；最终支持副本已更新并与当前正式main同为`c2d7e6d857aee1f47ade23a8be4605689f7b41791f57beb31fa07fc63a1872c4`。这些副本仍不属于priority补丁，不得整文件覆盖。三个窄补丁应在当前正式基线上整合，然后固定04全量输入并完整重编；不能沿用早期支持对象。

本报告没有把14对象编译、补丁检查或源码阅读写成正式链接、RK操作或真实Us/Ur执行通过。
