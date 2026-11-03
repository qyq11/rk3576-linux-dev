# 新告警候选的独立只读复核

复核时间：2026-11-02 19:54（北京时间）。未改候选或正式源码，未编译、未运行功能、未连接设备。审查对象为`alarm_candidate/original_alarm_receiver.*`及正在补齐的`alarm_candidate/tree/06_programs/sn100_host/`窄接线，路径都相对于本文件目录。树仍在并行维护，下面发现按本节最后的指纹定位，修后须重新检查。

## 需要先处理的真实缺口

### 1. 启动失败直接退出会取消未写告警，失败回执也送不到接收器

位置：候选树`shell/main.cpp:1135`至`:1141`、`:1163`；正式`alarm_history/alarm_history_store.cpp:619`至`:656`。

候选正常关窗等接收器排空，但`main.cpp`仍有多处`app.exit(3)`直接结束事件循环。设备连接由仪器页读取配置成功后自动打开，与样本、项目等全部初始化完成没有依赖，所以启动过程中收到新告警后，另一初始化报错可以进入此路径。

事件循环返回后候选只记`pending_batches`并将结果置3，接着用BlockingQueuedConnection调用Store::close。Store::release将自己的未写队列逐批发`originalEventsError`后清空；这些信号到接收器是QueuedConnection，而GUI事件循环已经停了，接收器无法处理它们。它自己尚未提交或待确认的Rows也仅保留在内存，随父窗口销毁丢失。日志只剩批次数，无法完整恢复当时行内容、账户和接收时间。

这里没有假报退出0，但非0退出不能代替保存未确认行。处理办法应为失败退出也先停止新输入、在主循环仍运行时等待告警排空；若必须直接退出，则在清理前原子保留完整未确认Rows、requestId、原来源和错误状态，权限限制在运行资料目录，并明确不得自动重放。已有提交但尚未收到确认的批应标“结果待核”，不能当作确定未写重试。

已向总协调和alarm_flow报告，由alarm_flow修正。

### 2. 活着的程序发生SQL失败或队列超限，也只记条数就丢掉整批内容

位置：接收器`:165`至`:180`、`:202`至`:234`；候选树`shell/alarm_shell.cpp:110`至`:114`。

例如数据库被其他连接占用超过现有200毫秒预算，Store会回`originalEventsError`。finishBatch先从pending_删除该批，再发historyFailed；shell收到Rows但只把`rows.size()`和错误文字写日志，不保留Rows。队列超出32批、4096条或2MiB时也走相同historyFailed，整批没有后续所有者。新告警不再进入原历史，后续退出时pending计数已经是0，用户无法仅凭该计数知道有哪批缺失。

这与第1项不同，不需要退出或异常事件循环就能触发。修第1项的退出旁证应同时覆盖这些historyFailed分支；不能只在退出时保存pending，因为这批已被移除。旁证保存失败本身也必须明确记录，不能再无条件丢掉当前Rows并宣称已留存。

### 3. 设备自动连接与接收器进入started状态没有依赖，首批有时间窗口被拒绝

位置：候选树`shell/main.cpp:481`、`:492`至`:493`、`:1122`；接收器`:165`；正式`instrument/instrument_page.cpp:160`至`:163`。

接收器创建时started_=false。它要等OriginalAlarmThread发started，再由GUI的排队回调调用openOriginal，才置started_=true。同时，先前enableDeviceControl(true)允许仪器页一拿到连接配置就自动open主控。原告警线程和维护线程是并行启动的，没有“接收器开始接受有界排队后才连接设备”的依赖。在告警线程启动回调较晚的合法调度顺序下，设备首批Ql/QL会被receivePayload的!started_分支拒绝，并落到第2项只记计数的失败处理。

可以让接收器从明确绑定原资料后就有界排队、等待SQL打开，或让自动连接等待接收器已开始；不要求先写库才能收首批，但必须存在明确先后关系。单纯调换两个QThread::start调用的排列不能证明该关系成立。

## 已核对成立的部分

- **仅Ql/QL进入新事件通道。** receivePayload先过滤，随后现有parseReadOnlyMessage检查完整字段数和数量，originalNewEvents再核notification/from_board；Qk/QK轮询不会进入追加。重复告警在原协议里没有可靠事件编号，候选保留输入顺序、按每次实际通知记历史，没有凭相同文字去重掉合法新事件。
- **SQL线程归属正确。** 独立parentless Store移动到OriginalAlarmWriter线程；打开、追加、关闭都Queued投递，Store检查所属线程。每批OriginalBusiness::Database在该工作线程创建和销毁，GUI不跨线程使用连接。历史弹窗使用另一个Store，不会关闭写入接收器的来源。
- **接收账户和时间取值正确。** shell的DeviceControl::payloadReceived回调直接取当时session_.name及QDateTime::currentDateTime；空名映射原字符串Guest。Rows保存在pending里后不会在SQL排队完成时改成新登录账户。
- **有界队列和确认关系清楚。** 接收器最多32批/4096行/2MiB文本；Store有同量级预算。只有Store事务提交并返回同一requestId和条数，才发historyCommitted。提交失败不自动重发，不把“收到”当“保存”。
- **正常关窗顺序未见具体丢批错误。** 当前窄接线先走所有草稿/辅助窗口守卫，再关闭DeviceControl停止新输入、requestStop。接收器等pending全部收到SQL成功或失败回执，才发Store::close；等closed后发stopped，再排队重试关窗，主循环最后退出。不能将第1项的失败退出缺口泛化成“所有正常关闭都会丢批”。

## 重入和对象销毁的具体边界

当前窄接线中，newEventsReceived/historyFailed的直接槽只写日志；storageError只改tooltip并转navigationRejected，未发现它们启动嵌套事件循环或同步删除接收器。因此不能据候选公开信号本身就断言现有正常流程发生了重入崩溃。

有两处接口边界须在后续添加失败旁证/新槽时注意：

1. closed/openFailed/destroyed分支在对外发sourceReadyChanged/storageError/historyFailed之后才设置stopRequested_。如果新直接槽在处理historyFailed时再次receivePayload，旧pending_已清空，新批可重新入队，随后分支又直接宣布stopped；此时有残留批却停止。最小修正是进入终止分支先设置拒绝新输入状态，再发外部通知。**目前候选shell的直接槽没有调用receivePayload，所以这是一处可确定的接口重入缺陷，不是已证实的现有现场故障。**
2. shell将设备信号连接到shell上下文，而lambda无条件使用QPointer originalAlarmReceiver_。如果接收器独立销毁、shell和DeviceControl仍存活，连接不会随接收器一起断开，下一包会解引用空指针。最小修正是在lambda里取局部QPointer并检查。当前正常父子销毁路径中接收器随shell销毁，未发现一定触发这条路径；独立销毁恢复路径不能当成已支持。

接收器对Store::destroyed用Queued回调是可接受的：回调属于接收器上下文；正常清理先关闭工作库和线程，再销毁父shell，不跨线程delete工作对象。若主事件循环已经停止，它无法再处理该回调，仍归第1项失败退出问题，不能靠destroyed信号补救。

## 来源身份的验证范围

接收器核opened回执的canonicalPath与指定原根路径相同，并将Store给出的非空sourceToken绑定后续追加，能拒绝另一个打开来源的旧token。Store打开原资料时也核许可标记和路径中的符号链接，未见候选把原参考目录拿来写入。

这个token对应一次按路径打开，不证明文件在进程运行期间从未被同路径替换；Store每批writer会重新打开该路径。本轮没有发现应用自身在线替换该告警库的流程，不将其列为新增阻断bug。若以后允许在线恢复/替换告警库，应把停止接收器、排空、关闭两侧连接列入该恢复操作，不能沿用旧token宣称还是同一份已核来源。

## 复核快照

| 文件 | SHA-256 |
| --- | --- |
| `alarm_candidate/original_alarm_receiver.cpp` | `4A981F7EE83BEF1C3916AD8BC0554F2D82FEBEF6C17D12100889A7DFADDCE831` |
| 候选树`shell/alarm_shell.cpp` | `6CA4E1C628F4639DEE1E3D1800ABFC8AD7598766584BD66A4D32FD1238BF352B` |
| 候选树`shell/main.cpp` | `38FB74459EA1DD7DE29F989AC246150B12CCF1C53328F79FE86D1172E2B48ADB` |

本结论仅覆盖这份源码快照。ARM64编译、板B原资料联机、新告警实际落库与页面更新、正常/失败退出均未由本审查执行，不新增任何运行通过结论。
