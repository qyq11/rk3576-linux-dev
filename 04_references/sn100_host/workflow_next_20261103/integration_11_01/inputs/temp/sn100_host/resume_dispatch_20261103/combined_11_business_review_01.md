# Root组合11：Mg04、LIS来源值与B1交界窄审

2026-11-03，独立审查者`/root/review_amount`。仅只读`temp/sn100_host/workflow_next_20261103/root_combined_candidate_11_01/`稀疏17源、冲突前后原件、必要调用片段和原独审指纹；没有重审全部历史，没有修改正式/候选、运行工具阶段、应用补丁、构建、产品、SQL、模拟、网络或板卡操作。c7未被本次审查改变。

**结论：组合保留了两方源码与唯一冲突的两条撤销，字节来源和构建登记成立；但发现一项新交界回退，当前01不放行。** Mg的“禁止开始/暂停”门被B1查询/静音共用，导致合法只读查询与静音也被禁止。修订后只需针对实际差异重新窄核；本结论不是编译或RK结果。

读时关键绑定：

| 对象，相对组合目录 | SHA-256 |
|---|---|
| `packaging_01/mg04_lis_profile_on_f741.patch` | `35ee743734107fa9553808852ac870b09e201ca100524d3ccde7b4adc757d333` |
| `sparse_after_17.json` | `17f3b796184b626d2175e3ba78a31a951a0a572e6bca5656e68e6d307c91d89a` |
| `composed_expected_after_688.json` | `b2676bb40c584d32509436bff0f63e99b08e435cc5f38689648bf20c67591c7b` |
| `candidate/06_programs/sn100_host/shell/device_control_shell.cpp` | `065dafd5701ee47cec53f92ecca4dc82ccbca887784a9ae32c9cecaa60271702` |
| `candidate/06_programs/sn100_host/shell/frmmain.h` | `0f7f034d7b01644c68777ac05788971977a5f52894070a500460c8f0744fb491` |
| `merge/device_control_shell.raw_conflict.cpp` | `09358b11b7a408c6125accebe83dd4cd071c85106e5cfb76ac5a482cb1771e84` |

1. **[P2] 探测专门门错误作用到B1的Qj/Qk/QH和Mz，并影响主窗静音。**

   组合`device_control_shell.cpp:153–169`的`deviceUserControlBlocker`无条件检查`reagentProbe_ || reagentProbeUnknown_`，第159行文字也仅称“未接受新的开始/暂停”。但B1的`manualCommunicationBlocker:172–186`与`receiveManualCommunication`初筛都复用此方法；`requireFresh=false`只跳过fresh检查，并不跳过探测门。`requestDevicePauseOrSilence:280起`在静音分支也调用它。

   因此当前探测在途或结果未知时，实际手动Qj/Qk/QH、手动Mz和主窗原静音按钮都会在Root层拒绝，尚未到DeviceControl。原B1的查询/静音有意不套开始专门业务门；原Mg `DeviceControl::send:196–200`也明确在探测保护下保留Qj/Qk/QH/Mz（及原MP），没有要求这些命令先清探测未知。这不是将来才出现的缺接口，属于两方合并后的可达守卫冲突。

   最小修法：让probe在途/未知禁止开始、暂停的检查只作用于真实Ts/Tp动作，或为精确Qj/Qk/QH/Mz调用保留窄的共用身份/连接/关闭检查。仍保原管理员权限、会话/页面来源、原连接、关闭确认、严格正文及发送前复核；Ts/Tp继续挡，不以查询或静音清任何probe、业务或事务unknown。不要用放宽maintenanceCommand或开放raw send解决。本报告未改候选。

2. 非重叠字节和构建输入登记已核。

   实读候选恰17份，逐份SHA等于`sparse_after_17`并等于合成后表相应键。Mg04的11份非重叠业务源逐字节等于已审`freeze_04`，包含已修原值1映射/原SELECT*顺序/formula消费过滤的Editor `bb68c3c1d21f66fcb31483d9dc6b4bfcdfbf003e019459f0872ed08ede364163`。没有从03旧Editor覆盖回来。

   LIS两源分别为controller `7ca09f58d332ac20bf0f7d234c7fc4081a5acddadf646484bba11d81f69c7b61`、values `41bb19561f6b3996107470107a59b04b01df1310fa1afb5abf16caf9a79b2051`，精确等于已审来源值两文件。所引用Root独审报告实际SHA为`4ecd62706efccd84ae37210cb5f2c2af71b27c62a6f2779956ed6e1a4f92215a`；本轮仅承接它的有限范围，没有把自动取任务值字段当成生产查询/患者保存消费者。

   两个登记文件相对f741前像只有各一行：`reagents/reagents.pri:40`将新增纯值头加入HEADERS，`shell/build.sh:264`将它加入headers实际输入列表；没有新增CPP、线程或新入口。结合11份Mg非重叠+2份LIS+2份登记，共15份；另两份是下述主窗合并。

3. 主窗合并实际保留双方逻辑，唯一人工冲突范围精确。

   `frmmain.h`相对f741只增加Mg纯值头、ReceivedPacket前置声明及Probe状态/方法；相对Mg04只增加原B1声明、参数与手动请求状态。未见User04/CAL接口、Observer或LIS入口被旧头覆盖。

   CPP raw merge只有一个冲突，原Git merge-file实际退出1已保留在preparation及原raw文件；header原merge-file实际0也保留，未把原冲突记成自动成功。本人只在内存把唯一冲突替换成双方原有的`cancelManualCommunication`、`cancelReagentProbe`两行，结果逐字节精确等于组合CPP。因此人工解决没有夹带第三种改动，候选也无残留冲突标记。`preparation.json`中unresolved仍是该准备时点的历史状态，不应抹掉原件或把它当最终候选尚含标记。

   组合`:610–615`按原次序保留手动撤销、探测撤销、任务上下文撤销和液面失效。B1的旧taskRequest无论preparing bool都撤User来源仍在；探测按是否可能入队保独立未知、等待prepared/N/reader的真实收尾也未被清bool替代。

   B1的按钮/QB/手动Ts仍到`requestDeviceStart→queueUserTaskPreparation`（组合`:369–371`），没有退回旧`prepareDeviceTasks` invoke。`finishDeviceTaskPreparation:393起`仍承接原User桥退休后的NoCommit收尾、手动guard/sendAttempted和终态，未出现第二个旧callback直接发Ts。暂停/静音仍走B1共享确认入口；第1项要求修其probe门适用范围，不要求退回旧lambda。

4. 范围与证据边界。

   Mg的Page请求→`beginReagentPriority`精确detectOne分流→独立prepare/claim→专用sendReagentProbe→Mg及真实N/reader收尾的调用链保留。整盘、扫码、换位、恢复、容量标定等未因本组合获得新的消费者；合法已配置单孔范围、None/Unknown拒绝、原机型/门/孔数守卫和原协议无业务请求号限制承接原审范围。B1仍只认Qj/Qk/QH/Ts/Tp[0]/Mz六个精确手动正文，未扩C、Uh、任意raw send或维护白名单；17份列表不包含急诊User16，全文必要关键词核对也未见其EmergencySave/UserAdmission入口。

   私有packaging命令原件记录普通check/apply均0、回放17份相等；本人没有重跑Git。另独立读取ordinary_replay17份，均逐字节等候选。before中的16份既有文件精确等f741表相应键，也逐字节等本次读取的正式16份；没有读取正式687整树来认领`formal_687_unchanged`的全量结论。

   687→688后表经只读字典比较，恰为f741完整表叠加这17个键，唯一新增`reagents/original_reagent_probe.h`。它是**合成预期**，不是正式源码已经实际晋级或已构建688的证明。当前第1项未修，不能选其补丁/合成表安排本次源码接纳；修改后应另存新版本及真实补丁/回放记录，保留本01与报告。

所审成功范围只证明组合来源、已审字节保留、构建登记及必要调用链；没有完整ARM64编译、RK软件、真实仪器、实体动作或资源通过结果。
