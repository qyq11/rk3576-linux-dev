# 08 final review：Task关闭合同的源码依据

本说明是2026-11-03对固定final-review源码的只读审查，**不是官方发布绑定、编译结果或运行验收**。只新增本说明；未生成冻结合同，未编译、运行、联网或构造Task/N输入，未改正式源码、07、固定Stage2或私有检查器候选。

本次依据：

- `temp/sn100_host/workflow_night_20261102/integration_08_review/final_merge_review.json`，实际SHA-256为 `e2a7e04555b37b12d5be0c205aca37d3c5eddf7bca7e962848f471574848087f`。
- 它指定的源码为 `C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/integration_08_review/stage_02/source/06_programs/sn100_host/`。以下行号均指这里，不能移作旧动态候选或未来官方源码行号。
- review记录674项源码、5个补丁，含默认容器片。已只读核对16份生命周期相关文件与 `final_source_files`，16份一致。674是这份review的数量，不是推算对象数或运行通过数。

## 直接用于合同适配的结论

原14个QThread变量仍在。main因默认容器配置及失败返回分支改变了字节，原17项启用日志和线程收尾含义未变；N关闭READY保留原真实字段。07合同的提取规则可以作为基础，旧JSON、指纹和行号不能照搬。

本次固定源码新增了真正的 `TASK_REAGENT_SHUTDOWN_READY`。它表示Root关闭守卫没有再等待其活动任务或专用读取，**不表示历史提交不明、显示不明已经清除，也不表示Task业务或在途一次退出实测通过**。没有Task、Provider或Worker专属STOPPED日志，不能发明。

最终适配应按实际启用条件提取原集合及新增真实READY，不先猜总条数。运行中同一关闭流程可以重试并多次打印READY；源码日志位置唯一不等于运行只准打印一次。

## Root的实际关闭顺序

[frmMain.closeEvent](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/integration_08_review/stage_02/source/06_programs/sn100_host/shell/frmmain.cpp:624) 的顺序为：

1. 进入 `enterTaskReagentCloseAttempt()`，清本次“专用读结束后重试”的临时标志。
2. 处理原登录、注销、告警、校准、质控及仪器离开守卫。
3. catalog离开失败时调用 `deferTaskReagentCatalogClose()`，再忽略Close；随后才处理历史、用户设置、LIS和原告警关闭。
4. :680先经过 `requestTaskReagentShutdown()`；拒绝则忽略Close。
5. 再经过原 `requestReagentLevelShutdown()`；接受后才允许内层Close。

外层 `ShellViewport::closeEvent()` 仍先让内层关闭守卫接受，再停止原通道Receiver，最终接受外层Close。不能直接退出事件循环来代替这条路径。

### Task READY的真实分支

[requestTaskReagentShutdown](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/workflow_night_20261102/integration_08_review/stage_02/source/06_programs/sn100_host/shell/task_reagent_shell.cpp:616) 当前逻辑摘录如下；省略提示文本和调度调用，实际绑定仍读取源文件：

```cpp
if (!taskReagentBridge_) return true;
auto &b=*taskReagentBridge_;
if (!b.closing) { b.closing=true; cancelTaskReagentContexts(...); }
bool waiting=!b.nReaders.isEmpty();
for (const auto &a : b.actions)
    if (a.dispatched || a.readerPending || !a.processed) waiting=true;
if (waiting) { ...; return false; }
qInfo()<<"TASK_REAGENT_SHUTDOWN_READY";
return true;
```

日志原句在:625，是纯文字，没有active、busy或成功布尔字段。适配器应核该真实guard及调用位置，不给它拼上不存在的字段。

若Bridge不存在，函数直接接受且不打印READY。完整启用场景需要结合接线判断是否应要求该日志，不能把未启用解释成“线程未停止”。`device_control_shell.cpp:378–388` 在设备控制、样本、试剂和项目页存在且取得SampleStore后，先设置deviceTaskStore，再调用 `attachTaskReagents()`；后者 `task_reagent_shell.cpp:264–269` 构造Bridge并作幂等连接。

### 关闭时等待什么、保留什么

`cancelTaskReagentContexts()`（:602–613）推进epoch，撤来源票据及确认来源，将当前动作标为cancelled，撤页面basis，关闭N准入并安排继续处理。它不伪造processed、不假定已回滚。

`advanceTaskReagents()`（:490–580）负责真实后台回执、页面采用和专用读取收尾。未派发的等待动作在closing时退出等待；已派发动作仍等处理结果。`completeTaskReagentAction()`（:464–488）对已派发动作还检查processed、readerPending、enqueueing及页面ACK，再移除活动动作，保留有界结束记录。

Root的 `transactions`、`transactionOverflow`、`displayUnknown` 等记录不会因关门自动清空。READY守卫检查的是活动等待，不要求这些历史不明记录为空。日志齐全不能据此把原业务未知改成成功。

### 专用读取及外层重试

`deferTaskReagentCatalogClose()`（:589–600）只处理本链专用读取造成的试剂忙，排除草稿及其他页面忙，避免自动替用户处理草稿选择。Root在:565–571检查N读及页面读/ACK等待结束后重试 `window()->close()`；closing分支在:573–578等活动等待结束后同样重试真正外层窗口。

N的页面读取也纳入这条等待：`reagent_level_shell.cpp:349–377` 在专用读取成功排入后登记nReaders，再给同一个ReagentStore队列排内存哨兵；只有存储槽实际返回并把哨兵送回GUI后才移除nReaders。`REAGENT_LEVEL_N_READER_GUI_QUEUE_FAILED` 是:371的真实warning；投递失败保留等待，不假报完成。该情况应记录未确认及缺少正常关门证明，不能补造STOPPED。

## Provider、Worker及存储线程

| 对象 | 实际归属及结束依据 |
| --- | --- |
| Root Bridge | 主窗口线程中的内存记录；500ms计时器也以frmMain为parent，没有新线程。 |
| RuntimeProvider | `reagents/original_reagent_level_runtime.h:11` 的普通值对象，由页面持有；无QObject、独立线程或关闭日志。 |
| N ObservationWorker | `reagent_level_shell.cpp:211–215` 移到现有ReagentStore线程，thread.finished→deleteLater，Store.closed→revoke。 |
| Task后台处理 | 使用已有SampleStore/sampleThread；提交事实与devicePacketProcessed是两个回执时点。 |
| Task专用重读 | `original_task_reagent_readback.cpp:195–229` 在既有ReagentStore线程处理，最终发带原receipt的readback，不走普通loaded旁路。 |
| ReceiptWriter | 普通栈对象，析构/finish作一次收尾；不创建线程、计时器或停止日志。 |

N Worker的 `revoke()`（worker.cpp:278–284）撤permit、运行依据及新N准入，不清真实pending；`isBusy()`（:379–383）仍检查pending。处理结束在:522–526清pending并发结果，GUI消费仍是另一时点。

Provider冻结Task basis、取消和晚到结果处理在 `original_reagent_level_task.cpp:134–258`。取消会保留有限的旧basis及生命周期token，用来撤销同一旧来源，不授权把晚到回执当新结果采用。

Task页面 `reagents_page_task.cpp:330–400` 使用GUI单次10秒计时器及单调截止。超时走统一finish，不重发任务、不退量；:556–561在本链取得busy后排专用重读。finish（:595–623）停timer、清commit/basis，归还本链busy并发 `originalTaskReagentAdopted`；cancel（:625–643）有提交时按同一路径收尾，无提交时保留取消依据处理晚到结果。页面取消不等于实际存储读取已经返回，Root另等readerPending/readback。

main中的14个变量仍为database、account、history、project、reagent、sample、maintenance、originalChannel、printer、lis、alarmHistory、quality、calibration、originalAlarm对应的Thread。真实清理包含 `sampleThread.wait()`（main:1642）、`reagentThread.wait()`（:1648）；汇总:1649–1650包含这两个线程，原通道仍另需专属停止标记。没有新增Task线程需要退出。

## 真实日志与未确认结果

以下是源码表达式和字段说明，没有抄录运行日志或敏感payload：

| 日志 | 固定源码位置及含义 |
| --- | --- |
| `TASK_REAGENT_ARRIVED` | task_reagent_shell.cpp:348：本地动作号、包号、连接、epoch、动作枚举、payload SHA；仅到达并登记。 |
| `TASK_REAGENT_OBSERVED` | :404–405：动作号、包号、连接、receiptId、事务枚举、committed、reagentSqlWritten、postCommitVerified；只保存事实，processed前不安排页面抢读。 |
| `TASK_REAGENT_PROCESSED` | :429：动作号、包号、连接、success、observed及说明；不等于页面采用。 |
| `TASK_REAGENT_DISPLAY` / `TASK_REAGENT_HOLE` | :448–451：整体采用及逐孔结果；false可来自取消、缺来源或拒绝旧结果，不能全当关闭失败。 |
| `TASK_REAGENT_RETIRED` | :486：动作已从活动队列回收，另打印displayUnknown/transactionUnknown；不表示未知已消失。 |
| `TASK_REAGENT_WAIT_TIMEOUT` | :287：超过10秒的等待提示，原等待保留，不证明取消、回滚或排空。 |
| `TASK_REAGENT_UNBOUND_FACT` | :385：提交事实不能归到当前原动作，保留事务/显示不明。 |
| `TASK_REAGENT_UNCONFIRMED_DISPLAY` | reagents_page_task.cpp:576：receiptId、事务枚举、committed及原因，仅提交未确认分支。 |
| `TASK_REAGENT_SHUTDOWN_READY` | task_reagent_shell.cpp:625：仅上述真实关闭guard到达成功分支。 |

事务枚举在 `samples/original_task_reagent_receipt.h:20`：NotStarted=0、NotCommitted=1、CommitUnconfirmed=2、Committed=3。Root:396–401检查CommitUnconfirmed及committed与枚举是否矛盾。若合同需要解析日志枚举，应从最终声明绑定，不能把某个false直接等同失败或回滚。

提交不明、归属冲突、已回收动作仍带未知等，需要单列结果状态，不能因为READY及线程停止齐全而宣称Task业务通过。当前没有Task专属 `*_SHUTDOWN_UNCONFIRMED` 或STOPPED日志。异常直接退出事件循环仍可能绕过Task GUI守卫；必须结合真实关闭路径、READY、实际应用/检查器/封装退出码及超时，不能只靠main最终wait。原通道/告警未确认日志继续保留其原有范围。

## 给后续adapter的边界

- 从最终实际source与启用参数提取原main集合、N真实READY及Task真实READY；保留Bridge不存在的分支条件，不虚构线程或日志字段。
- 绑定Task Root/context/source、实际close入口与device接线、Provider/Page、Worker及专用读取来源。只更新main指纹无法涵盖这些变更。
- 正常关闭证明、Task事务结果、页面采用及未知记录分别保存；不把PROCESSED/DISPLAY false或WAIT_TIMEOUT一刀切当关闭失败，也不把RETIRED/READY当业务成功。
- 无实际Task或N时，功能、Provider采用及在途一次外层退出仍标未覆盖；QA定时重试或闲时READY不能替代在途场景实测。
- 本次是final-review层面的准备。官方08发布编号、源码包、snapshot、实际构建对象及最终工具仍待各自授权和真实绑定；本说明不生成或固定冻结合同。

本次核对的16份文件及指纹，以上述final review的 `final_source_files` 为准：shell/main.cpp、frmmain.cpp/.h、task_reagent_context.h、task_reagent_shell.cpp、task_reagent_source.cpp、device_control_shell.cpp、reagent_level_shell.cpp；reagents/original_reagent_level_worker.cpp/.h、original_reagent_level_runtime.h、original_reagent_level_task.cpp、reagents_page_task.cpp、original_task_reagent_readback.cpp；samples/original_sample_store.cpp、original_task_reagent_receipt.h。未将旧动态候选结论冒充这份固定源码结论。
