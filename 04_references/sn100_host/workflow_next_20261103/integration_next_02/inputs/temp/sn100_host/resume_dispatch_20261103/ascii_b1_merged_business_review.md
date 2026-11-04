# B1与User04合并：开始准备与取消收尾窄审

2026-11-03。只读 `temp/sn100_host/resume_dispatch_20261103/ascii_b1_root_merge_01/tree/`，对照原B1及当前Root User04必要调用；不改合并tree/正式，不构建、运行产品、联网或操作板卡，未开子Agent。没有重新审全部六正文或打包/补丁回演。

**本次合并增量未见确定的新业务阻点，前一报告列出的User04接合要求已落实。** 本结论只绑定下列实际代码指纹；包装仍由另一Agent收口，不能把它扩大为编译、RK或完整B1运行通过。

## 实际输入与未覆盖范围

| tree相对文件 | 本次读取SHA-256 |
| --- | --- |
| instrument/instrument_page.cpp | `8ac4b8964c940a12303d57dec60ac78562ff618dad6449d370f559246592b82c` |
| instrument/instrument_page.h | `0215157511c2cf74cb3240c11a30a2c12d293728ca27a6b840837c7f9e313e11` |
| shell/frmmain.h | `a84c57bcd0f98979510caca2823ea271c8d8add7f90724395aebbcf7d5d4f068` |
| shell/device_control_shell.cpp | `d7bb3b6c05ed6cc576935eb67e4b9fdd528583b3959c4e131434114bcc523ecc` |

仪器页两文件与原冻结B1逐字节一致，原ASCII/权限/六正文结论保持原范围。只对主窗两文件读了相对B1、相对当前正式的全部差异，并直接追踪当前User04的worker、真实finished、NoCommit（没有开始SQL事务/写库）校验与动作退休。

合并目录 `source_before.json` 与Root批准的 `observer_04/source_after.json` 清单内容和字节SHA均相同，后者SHA为 `b688f7a8c9c888482bf53b9520239ec9618c61a2da5a701d526387e644add5d3`，687项。tree实际只有上述四文件。没有重核687整树；点核当前15份User04/CAL/main必要文件及Observer四份、LIS三份均匹配该前表。Observer四份为build.sh、lis_connection_observer_09.h、lis_keep_running_observation.cpp、shell.pri；LIS三份为lis_connection_controller头/源及lis_connection_values.h。它们不在这次覆盖范围内。

下文合并行号相对tree；User04桥/worker行号指当前正式 `06_programs/sn100_host/`。

## Ts仍走User04，不回到旧prepare

合并 `shell/device_control_shell.cpp:298–375` 保留B1的真实手动来源记录、原deviceTaskRequest关联、确认前后守卫及updateDeviceControlActions之后的再次复核；实际派发在`:355`是 `queueUserTaskPreparation(request,connection,linuxNew,...)`，不再字符串调用旧prepareDeviceTasks。队列登记失败直接结束本次准备，手动结果为拒绝，没有调用start。

当前User04 `task_reagent_shell.cpp:647–691`继续负责用户来源、三INI/项目/样本输入身份、保护门和有界排队；真正worker是SampleStore::prepareUserDeviceTasks。其原同次初始化及prepareUserOperation检查未被覆盖，包括SendTaskImmediate=true时拒绝尚未实现的Emergency流程，不改走Ts。手动请求号仍只关联原准备号，不填成到包号、actionId或SQL提交凭据。

## 唯一开始收尾承接真实NoCommit结果

合并中只有一个 `finishDeviceTaskPreparation` 定义（`:377–409`），本文件只有该函数在`:390`调用DeviceControl::start。旧B1的deviceTasksPrepared发送lambda已移除；`:824`保留当前User04的成员函数指针连接，没有第二个发送lambda。

实际User04生产链仍是：

`prepareUserDeviceTasks` → `userTaskOperationFinished` → `finishUserTaskOperation`核回执归属及None/NotStarted/无SQL/无任务等完整无写条件 → `advanceTaskReagents` → `completeTaskReagentAction`等待真实worker/reader收尾并退休动作 → 再调用唯一 `finishDeviceTaskPreparation`。

直接回读了当前 `task_reagent_shell.cpp:706–762` 的归属/无写检查和`:470–507`退休流程：可疑或冲突事实进入原事务未知保护；错误finished不被当作当前worker已结束。退休后仍核未取消、epoch及userTaskOriginCurrent，再算success。合并没有把这些条件换成一次bool准备成功。

唯一收尾函数先核 `deviceTaskPreparing_` 和原request；取对应手动请求后再核当前设备用户入口、detectionStartBlocker及manualCommunicationBlocker。只有success且所有blocker为空才调用start，调用前登记SendAttempted。失败/拒绝不发Ts；实际调用返回false保持“可能已入队，结果待核”，不自动重发。原startAccepted消费者保留。

兼容的deviceTasksPrepared信号仍连接同一收尾函数，并未被删除；本合并开始入口已无旧prepareDeviceTasks生产派发，不能把保留兼容信号误写成新增旁路，也不能以后新增直接调用却假称它自动具备User来源证明。本次确认的是当前真实生产链没有另一个旧lambda重复发送。

## 取消、超时及失效保持真实收尾

`cancelManualCommunication:198–215`先保存旧 `manualCommunicationTaskRequest_`，只要非零就调用 `cancelUserTaskPreparation(taskRequest,reason)`，不以deviceTaskPreparing是否已false为条件；随后才在本片确实拥有当前准备时清bool/递增原请求号并排worker cancel。它没有清User action、transactions或reader集合，也不伪造processed/finished。

当前 `cancelUserTaskPreparation:694–703`只按该用户准备号标cancelled、撤对应冻结依据并推进原收尾；已经派发的动作仍受 `completeTaskReagentAction` 的processed/reader/ack条件约束。因此“页面手动等待已结束”和“后台动作真实结束”仍是两件事，不会用手动终态释放任务/N-C保护。

`:364–372`的10秒处理仍保留User04取消、worker cancel，另通知B1页面preparation-timeout。超时先使deviceTaskPreparing为false，迟到NoCommit回到统一收尾时原request/准备状态检查会拒绝再发Ts；User bridge继续等真实finished退休。失效入口 `:592–599`先撤手动原请求，再保留原cancelTaskReagentContexts、N撤销和配置失效；连接/账户变化及页面销毁不把旧请求转给新身份。

确认No发生在User入队前，不生成假回执；确认后update期间撤销会被B1再验拦下。queue失败、配置/身份变化、真实worker失败、超时和晚到取消结果均不能被合并新增逻辑改写成开始成功。手动终态仍先清本片旧请求再通知，保留原B1防重入处理；没有追加清原未知状态的代码。

## 当前Root片没有被旧文件盖回

相对当前正式头文件的差异仅为B1手动接口/字段和兼容的可选参数，User04的RuntimeBasis前置声明、queueUserTaskPreparation、userTaskOriginCurrent、finishUserTaskOperation、cancelUserTaskPreparation及唯一finishDeviceTaskPreparation声明都保留。

相对当前device_control_shell的差异为B1手动接线、共享暂停/静音、手动上下文检查/通知，以及上述合并接点；原detectionStartBlocker、QB实体键流程、CAL/Us/Ur消费者没有被替回旧底稿。当前User04配置来源/Session/CAL文件、Observer和LIS片不在tree中，局部指纹亦匹配Root前表。没有从B1的7cad整文件恢复旧Root，也没有用准备成功清CAL批号或任务事务事实。

## 锁定范围与下一步

开审与审尾按四文件指纹核对，代码如有变化须列新SHA及差异补审；不能将本报告直接绑定后来包装或修改后的字节。原B1六正文/权限/ASCII审查仍见 `ascii_b1_business_independent_review.md`，本报告只关闭其User04接合增量。

本次未证明完整编译/moc、RK真实点击/取消/重登/断线/超时、真实TX/RX或仪器动作。包装指纹和普通补丁检查由对应Agent/Root另收，字节不变时本次窄审结论可继续引用；随后必要完整ARM构建及获准原配置RK验证仍由Root安排。
