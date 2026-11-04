# Root下一批Uh组合：生命周期与归属独立窄审

2026-11-04。只读 `workflow_next_20261103/root_ascii_uh_candidate_12_01` 的before、六份candidate、三侧合并原件和已有私有回演证据。以当前688的b078前像为本次合并基线，不让作者旧f741两Main覆盖现场。没有修改正式或任何候选，没有执行补丁、产品、SQL、模拟、编译、网络或板端流程；仅新增本报告。

结论：本次六文件Root组合未发现确定的新增生命周期或请求归属阻点，可供总控继续下一批源码接纳。它是下一批候选，不在当前第11批b078构建中；本报告不把私有回演或源审算成完整编译、RK运行、真实Uh回包或业务完成。

## 合并范围和保留链

before的六个实际SHA均与b078/688表对应值相同。四份非公共文件——instrument_page.cpp、original_channel_receiver.cpp/.h、main.cpp——与已独立审查的作者Uh候选逐字节相同；沿用 `ascii_uh_lifecycle_independent_review_01.md` 对它们的固定后像结论，没有重做全库业务审。

头文件三侧合并真实退出0，stdout精确等最终头。CPP三侧合并真实退出1及原冲突stdout保留；本次根据原件独立重建确认，最终CPP只把唯一include冲突替换为双方并集，保留QUuid以及QDir/QThread/limits，其他字节与自动合并结果相同，最终没有冲突标记。没有把原失败改写成0。

对两个公共文件完整差分以及CPP逐函数核对，既有方法没有删除；唯一改变正文的既有方法是receiveManualCommunication，且该方法最终正文与已审作者Uh候选相同。新增的attachOriginalChannelReadback、channelReadbackRequestBlocker也与作者固定方法正文相同。其余手动、User04和Mg方法相对b078保持原文本，头文件仅增加Uh登记所需声明和字段，未挤掉Mg状态或B1身份字段。

## 手动请求和通知重入

`cancelManualCommunication()` 第247—264行仍先保存旧手动号/旧准备号，按旧准备号撤User采用资格；仅准备号仍匹配时结束等待并推进代次，再发原手动号终态。取消没有被合并回只清bool的旧版本，也不把手动取消当Worker已完成。

`finishManualCommunication()` 第236—244行仍在外部属性/页面通知前清旧等待，通知后不再清可能新建的请求。queueUserTaskPreparation、真实finished退休后的finishDeviceTaskPreparation最终出口、发送调用前attempt标记和false可能已入队口径均保持b078；没有并列旧发送lambda或新增重试。

Uh第301—329行保存receiver与原页面两个QPointer、原根和单调登记代次四项来源。发送前重核原nonce、角色/会话、页面、连接和动态能力；随后先记SendAttempted，仅调用一次既有maintenanceCommand。调用若重入取消/换会话，返回后原请求号不再匹配即退出；仍匹配才重核四项登记身份和能力。true只说明进入队列，false仍可能已入队，观察条件变化也不改成保证未发送或回读成功。Uh分支返回后不会掉进QH兜底。

原页绑定、同receiver/DeviceControl/根、当前连接代次、真实opened/reloaded镜像、构造时缓存专用QThread并在发送时读isRunning、未停止/关闭及counter耗尽拒绝等规则，均由上述精确后像继续保留。没有以consumer非空独自代替原页存活，也没有调用consumer、SQL或reload来试探能力。main登记仍在线程启动前，发送才核真实ready。

## Mg与底层拒绝边界

Mg的准备、取消、回复及N/reader退休方法相对before未改，ProbeSourceRelease调用栈末尾归还来源的结构也没有被include冲突吞掉。取消仍先cancelManualCommunication，再cancelReagentProbe，再原cancelTaskReagentContexts；旧手动号和探测token/请求域没有互清。Queuing重入取消不提前归还持有的来源租约，prepared返回栈结束才排归还；真实prepare/N/页面采用/reader结束仍分别等待。

现有DeviceControl::send在其自身reagentProbeUnresolved条件成立时，例外仍只有Qj/Qk/QH/Mz/MP，Uh不在例外。新主窗分支调用普通maintenanceCommand，没有ownProbe特权，也没有跳过private send。因此底层已登记探测拥有者或Unknown时仍会保守拒绝Uh；通道能力检查通过不等于获得绕过探测门的许可。这里不扩大声称“Root所有探测准备阶段都会被同一底层门拒绝”，两层状态应按各自真实条件解释。主动Uh取消也没有停止原被动receiver、清批次队列或否定真实已提交保存。

## 指纹和实际证据

本次独立重算六份候选和六份私有ordinary_replay后像，均与sparse_after_6.json相同。已读取实际命令和原退出码，普通check/apply为0/0，本审没有重新执行。合成预期688表精确等b078表覆盖六项后的字典；它不是正式目录扫描或正式合入后的结果。

| 对象 | 实读SHA-256 |
| --- | --- |
| b078/688基线表 | `b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767` |
| sparse_after_6.json | `67d0067d5b266435c82b775f078fe17ee5367bdcbfa7a0e2a58670d819dd218f` |
| 组合device_control_shell.cpp | `14ca22a5f0f581d68aead97ef6c34f9aeddaa1a9ba3ed135101feb350e6e39e4` |
| 组合frmmain.h | `3d0ebdbef06ebbd3aec0d3036aad4c2821298c4aa776bc61252400f1358ae433` |
| packaging_01/ascii_uh_on_b078.patch | `d89e920d59e6fde53cb87a1290dc1a67483fb072fd38321071a9799d3ba71c15` |
| composed_expected_after_688.json | `0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d` |
| packaging_01/acceptance.json | `ab81cff0236092bf8bde5c946ca0baf28d9760bd6f484d39ef75a34889c8fbfc` |

其余四文件精确匹配已审作者后像：page为 `1b887ccea0f7d5f83d15a54bd3421f4e89bccd567103c50baa62bc15bd3955e1`，receiver.cpp为 `ce0a9932259452890c9241efbf3427a2c05c5dd3d68c16aa9d00f45f5070a497`，receiver.h为 `6582e92a788e392b24002649af1604257240f6e70904d3b4be835381e0e451e7`，main.cpp为 `a38cfec6c77cec58fc546b2a775ab0b0fe7dbf471f7c0fa5464e78ed4fddbc06`。

后续仍由总控固定正式组合、完整构建并按授权核RK真人输入、取消/同名重登/断连、真实Uh TX、独立接收批次、channel.db实际提交/重读、页面采用清旧AD、失败恢复及同板资源。急诊保存部件不在这六文件片内；既有LIS观察或第11批构建不能计作本片验收。当前程序留驻没有被本审操作。
