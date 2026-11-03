# 任务出错、架通知、剩余时间和授权解锁：只读审查交接

审查时间：2026-11-02 19:46（北京时间）。本次只读源码，不修改业务代码、配置、数据库、公共计划或正式记录；未编译、未运行Windows/WSL程序、未连接RK或仪器。下面的“已有”只表示找到正式调用链，不能据此登记运行通过。交接供总协调决定下一项改动，不另立开发计划。

以下路径以仓库根为准；`原/`表示`06_programs/sn100_host_reference/Seerain450_PC/`，`现/`表示`06_programs/sn100_host/`。参考工程保持只读。

## 1. 设备通知的原版处理

| 通知 | 报文内容和原版入口 | 使用者看见或程序执行的结果 |
| --- | --- | --- |
| `TR`任务异常 | `TR[原因][数量][架号][孔号][任务号]...`，每个任务重复三项；原`communication/commandreceiver.cpp:230`的`pcTaskError` | 原`frmsample/frmsamplematerial.cpp:4398`的`slotTasksErrReturn`更新报告、任务/孔/架状态，退还未完成加样的试剂预留量并刷新页面。 |
| `TF`索取预设架信息 | 原接收器`:144`只发无参数通知；原样本页`:4391`的`slotPCTaskRackUdp` | 按原`communication/commandsender.cpp:70`，当前有Ready急诊孔时只回复`TF[Emergency]`，否则只回复`TF`；**不会列出所有普通架**。候选架来自原样本页`:3747`的`getRackIdList`，逐孔取Ready状态。 |
| `TB`架剩余时间 | `TB[数量][架号][剩余秒数]...`；原接收器`:261`的`pcRackLeftTime` | 原样本页`:4536`按架名设置对应架的秒数，未知架不创建。原`customwidgets/cussamplerack.cpp:69`只在RackTesting时每秒递减并显示，`:172`的`setLeftTime`存数值。原倒计时最低显示`00:00:01`，并不自行把架判定完成。 |
| `TK`索取授权解锁码 | 原接收器`:147`只发无参数通知；原`frmmain/frmmain.cpp:628`的`slotPCTaskUnLock` | 读取实际`pcConfigPath`；只有`code/code1`与`code/code2`两个键都存在，才回复`TK[code1原值]`，编码UTF-8，构建在原`communication/commandsender.cpp:499`。这不是清除样本架的在途任务锁，不能靠TK开放编辑。 |

原主窗口的四处接线分别在`原/frmmain/frmmain.cpp:392`（TK）、`:415`（TB）、`:418`（TF）、`:419`（TR）。报文大小写必须保留，`Tr`是结果，`TR`是异常。

## 2. TR原因和报告中的真实写法

| 原因值 | 原报告写入的文字 | 原样本任务状态 |
| --- | --- | --- |
| 0 | 六类结果文字写空；父报告仍写“测试失败” | `S_Task_Reset` |
| 1 | 错误 | `S_Task_Error` |
| 2 | 缺样本 | `S_Task_Error` |
| 3 | 缺样本清洗液 | `S_Task_Error` |
| 4 | 缺稀释液 | `S_Task_Error` |
| 5 | 缺试剂 | `S_Task_Error` |
| 6 | 缺试剂清洗液 | `S_Task_Error` |
| 7 | 缺耗材 | `S_Task_Error` |
| 其他非零整数 | 原报告default分支写“错误” | 原`if(isError)`写`S_Task_Error` |

依据：`原/tool/database/report/report_result_table.cpp:325`的`updateResultErrorState`。它先按任务号查结果，按结果的`fid`更新父报告`state="测试失败"`，再将原值、校准值、比值、INR、MDA、公式值六项都写为上表文字。原因0的父报告文字也写失败，这是原源码实际行为，不能凭“无错误”注释另改业务；如要修正应单独说明差异。

原样本页对每个任务先更新报告，再按架号/孔号找到孔，按任务号取得测试记录。非零原因置Error，0置Reset；当孔整体已Finished或Error时把`revise`置true；非零原因还把架颜色设为错误。`CusSampleHole::setTaskState`（原`customwidgets/cussamplehole.cpp:122`）会重新汇总孔/架状态。

原`pcTaskError`只先检查至少1项却随后读取第2项，且整数转换不检查成功。这些是原边界缺陷，移植不应复现越界；实现时先完整验证整包和整数，再写任何任务。

## 3. 试剂退还为什么不能只按错误码加回

原`customwidgets/cussamplehole.h:55`的`rgInfo`记录每一步：孔名`rgHoleName`、试剂步骤`addingSeq`、是否已加样`isAdding`、预留微升量`addVol`。下发时保存此清单，并从原试剂表`used_ul`扣预留量（普通分支示例为原样本页`:2025`至`:2051`）。

原`getRgInSingleTesting`（同头文件`:122`）只汇总`isAdding=false`的步骤。原`restoreRgForSingleTest`（原样本页`:5743`）把这些微升量加回当时表中的`used_ul`、最低归0，然后清空整个`rgHole`，所以重复TR不会再次退还。

原TD处理`slotSampleTesting`（原样本页`:4282`）在设备状态为`ChannelState_Testing=8`时把清单全部`isAdding=true`（`:4329`至`:4334`）。源码未见按R0/R1/R2逐步标记已用，因此移植先保留“到状态8全部视为已加”的原规则，不能凭步骤名臆造新消耗时机。原TD遇已Error任务不会让它变回Testing（`:4336`），这一点也需保留。原通道状态0表示Idle，9表示TestOK，其余中间状态映射Testing；映射见原头文件`:88`及`原/public/systemcommon.h:220`。

原断线处理`slotDisConnected`（原样本页`:4477`）把正在Testing的孔任务复位为错误，并对样本和校准记录调用同一退还函数，之后允许修订。它依赖进程内清单；不能由此推定重启后能安全自动退还或自动重发。

上面TD更新孔状态/退量清单的调用位于原`SystemCommon::taskSendDelete=false`分支；若`userOption/TaskSendDelete`开启，原`:4294`改走只更新报告状态的分支，任务下发后删除预设。现`original_runtime_settings.cpp:190`虽读取此键，检测会话初始化没有承接它，不能把已读键等同业务已支持。实施前需检查当时生效的`config_Debug.ini`，为开启分支单列行为缺口，不能擅自固定为false或在不支持时继续开始。

## 4. 当前正式工程已经接了什么

- `现/samples/original_detection_session.cpp:234`的`reserve`已用同一SQLite连接附加试剂库，把任务号、报告占位及`used_ul`扣量一起事务提交；具备原号防重复分配和乐观校验，不应重做。
- `answerRackRequest`（同文件`:326`）已处理普通/急诊架、扫码关闭分支、任务下发、已下发架去重；成功后保存`issuedTasks_`并设置`inFlight_`。
- `receiveDevicePayload`（`:365`）已处理Ur、TD和Tr。TD检查任务号、架、孔、非空项目名；Tr还检查样本条码、类型和项目，保存原曲线及参数/单位快照，保持“已收到曲线，待计算”，并防相同任务不同内容覆盖。完成一架后释放在途编辑保护。
- `现/samples/original_sample_repository.cpp:193`已有`updateDeviceTask`，按架名、孔、项目和重复索引定位；`SampleTaskState`已有Idle/Reset/Testing/Finished/Error。`现/samples/sample_task_reset.cpp`已有错误复位和复测的本地草稿规则。
- `现/samples/original_sample_store.cpp:94`已有带连接代次的工作线程分发与处理回执；`现/shell/device_control_shell.cpp:290`已有32包/1MiB队列上限、连接代次检查和失败显示。新增消息应沿用此通道，不另外绕过数据库线程。
- 连接失效会调用`invalidateDeviceTaskContext`（现主窗口同文件`:177`），取消未占号准备并使同步失效；`cancelUnsent`（现检测会话`:228`）明确不退还已占号试剂量、不删任务。当前这不是原断线退量的完整等价实现。

## 5. 当前缺口和最小实施接口建议

1. **解析TR。** 在`现/samples/original_result_codec.h/.cpp`增加`TaskFailureBatch`（reason、task列表，每项rack/hole/taskId）及`parseTaskFailures`。先验证命令大小写、数量、完整字段数、整数和上限，再交会话；不要一边读一边改库。没有可信任务身份的异常不能生成新任务。
2. **保存可退量。** 在`现/samples/original_task_codec.h`的Task增加按试剂步骤保存的预留值对象（孔名/原表行id、微升数、已用标志）；由`OriginalReagentRepository::assignTask`在实际选瓶时填入，顺序与`project.advanced.reagents`保持对应。当前Task只有`reagentSlots`，虽有项目快照可核用量，却没有可清空的退量状态。不要使用`saveRemaining`直接退量：现仓库此函数`:362`明确只允许数值减少，增加会拒绝。
3. **修正TD状态处理。** 现会话`:368`把任何3项/7项TD均当Testing，未使用`ReceivedProgress.state/type`；先保留3项通知不臆造已加样，7项按原状态0/8/9分支更新，并核非空type。Error后到达的TD不能复活错误任务。状态9只是设备阶段，不代表算法已计算；原曲线仍按Tr保存并显示待计算。
4. **接TR业务。** 在`OriginalDetectionSession::receiveDevicePayload`增加`TR`分支或提取`receiveTaskFailures`：对整批先找`issuedTasks_`、核架/孔/任务身份；事务更新六项结果错误文字、父报告状态及未用试剂量；成功后才更新内存状态和清空退量清单。退量幂等与错误原因更新分别记录，不能用一个“已见任务号”集合把后续不同原因全部吞掉。混有未知任务或身份冲突的包应明确报错，不静默改其他任务。
5. **让页面看到变化。** `现/samples/original_sample_store.cpp:139`成功TR后发`deviceTaskStateChanged`、`originalResultsChanged`，实际退量后发`originalReagentsChanged`；`现/shell/device_control_shell.cpp:293`加入TR准入。当前`updateDeviceTask`总写`hole.revise=false`，需提供与原错误/复位汇总一致的修订状态更新，并在一架所有已发任务终止后释放相应在途保护。不能把收到任何TR都算整个架结束。
6. **TF先做独立小项。** 在检测会话提供`rackUpdateReply`读取当前已保存快照并生成TF/TF[Emergency]；工作线程发带连接代次回执。当前`DeviceControl::sendTaskReply`（现`instrument/device_control.cpp:215`）只放行Tt，`sendOriginalConfiguration`也不放行TF；总窗口需新增明确的TF回应入口，不能靠放宽成任意T开头。
7. **TB需要显示接线。** 在`SampleRack`/仓库快照保存剩余秒数及接收时刻，提供`applyRackTimes`；现`samples/sample_widgets.cpp:38`把`lblLeftTime`永久写成`--:--:--`，需要让`SampleRackView`依据实际TB和Testing状态更新原位置。不能每秒重读数据库/重建整页；未知架不新建，未上报保持未知。原最低1秒显示的处理须在对照记录说明是否原样保留。
8. **TK单列授权码回应。** 现`original_files/original_runtime_settings.cpp:185`已有读取code1/code2到`RuntimeSettings.barcodeOne/barcodeTwo`，但没有TK调用链，且字符串空值不能区分“键缺失”和“存在但空”。实现时保留两键存在条件，读取实际生效INI，不修改或生成授权码。新增明确TK回应接口和适当的在途/连接防重发，不把授权码复制到临时交接或公开证据。当前`DeviceControl::wireActivity`会把所有字节以十六进制写日志，接TK前必须针对该报文遮蔽值；仅把明文改成十六进制仍会暴露原值。

建议第一项只落TR解析、按任务记录退量与TD状态规则，然后总窗口统一接主窗口准入/页面刷新、ARM64构建和板B原资料联机。TF和TB可随后独立验收；TK要先解决授权值可见日志问题。

## 6. 连接、重复和重启要分别界定

原版靠清空内存清单避免重复退量，当前已占号列表也只在本次进程内；两者都不能证明崩溃后可恢复任务身份。最小改动可明确保证当前进程/当前已知连接代次内的身份与幂等；进程重开后找不到任务的TR/Tr应保留原记录并报待核，不能按报告号猜孔位、自动重新下发或自动退量。

现连接失效保留已占号任务，比原断线立即复位/退还更保守；若下一切片要实现原断线行为，应明确记录这一行为变化，核实际控制器断线后是否仍执行，再在同一事务处理报告与退量。不能把“SSH断线”“应用与主控断线”“机械任务终止”混为一个事件。

## 7. 后续实际验证条件（本次未执行）

- 由总窗口在确认板B序列号/SSH指纹、当前发布权、实际INI和允许动作后，收集主控真实TR/TD/TF/TB/TK；本审查不授权启动检测、机械动作或生成授权码。
- TR核同一任务重复返回不重复退量；TD到状态8前后退量不同；TR0与非零原因的报告文字、孔/架汇总、修订入口和失败恢复均对上；未知任务/错架孔不改其他样本。
- 普通架和Emergency分别核对；TF不能夹带普通架；TB变化可见但不能因此自动判完成。
- 断线/重连不得重发未知任务、重复扣量或凭空恢复；进程重开不能冒充拥有旧任务映射。
- TK只核原配置条件和实际主控回应，证据遮蔽授权值。缺条件保留待联机验证，不安排匿名资料、模拟服务器或本机回环。
- 本次完成范围仅为Windows只读源码对照；Windows实际运行、ARM64编译、RK功能、真实仪器、响应/资源均未新增通过结论。

## 8. 本次关键源码指纹

| 文件 | SHA-256 |
| --- | --- |
| 原`communication/commandreceiver.cpp` | `A4174F13056668FDE17AF00C86C6F17D5289414CEEE276A83D462BE6EFC81470` |
| 原`frmsample/frmsamplematerial.cpp` | `F9455D9F89B56CC69A86A268B5A0304468F064D7D8E36E84671CEE4755FFF244` |
| 现`samples/original_detection_session.cpp` | `E3E21BF1E9086DC6AAB1D22E180788E0A772FBB427289D935E282EB32FF51617` |
| 现`samples/original_task_codec.h` | `3F8037617EA5878270A9EA4AF72CA36A7663EBCD125144B0A31B3E3044B54D1C` |

共享工作树后续会变化，实施前按函数名复核行号和现有差异。交接之外没有创建、移动或删除工程文件。
