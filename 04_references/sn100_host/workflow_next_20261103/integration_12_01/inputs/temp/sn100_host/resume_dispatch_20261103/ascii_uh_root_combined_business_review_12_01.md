# 精确Uh与当前Mg入口组合后的业务、守卫独立审查

结论：本次实际六源组合及相对b078的两主窗窄增量，未见新增业务阻点。上次列出的九项保留要求均已保留：原试剂单孔探测（Mg）的专门开始/暂停门、User真实任务准备与取消、B1手动请求的身份及通知收尾没有被旧候选覆盖。Root新增的两个能力函数及Uh分派与作者已审候选逐字一致。可供Root继续下一批串行接纳，但本报告不代表已经正式采用、编译或在仪器通过。

**Uh仍不属于第11批正在编译的688项/b078源码。** 本次只读Root候选、实际补丁和保存的检查记录，计算局部摘要及字节比较，未运行补丁、产品、SQL、模拟、编译或板卡动作，未改源码、候选和公共文档。没有重拷或重演整树；本报告只核本次组合业务与守卫增量，不替代完整生命周期审查。

## 实际审查对象与合并事实

Root候选根：`temp/sn100_host/workflow_next_20261103/root_ascii_uh_candidate_12_01/`。实际补丁 `packaging_01/ascii_uh_on_b078.patch` SHA-256：`d89e920d59e6fde53cb87a1290dc1a67483fb072fd38321071a9799d3ba71c15`。补丁实际只有六个既有文件，无增删改名、二进制片或第七个文件。

六份Root before均与本次读取时的对应正式文件逐字一致。两主窗前像分别仍为 `1250f9e536f874ee97f2f2c5f9ebc6b2ff012481bdd435d06968df6f7f70ad50`、`88ecd1b79de875f4e0b40385df57f077aba231a42de21c23d09ef697a08e7ee2`；不是作者历史f741两份旧主窗。

| Root实际后像 | SHA-256 | 独立字节核对 |
| --- | --- | --- |
| `instrument/original_channel_receiver.h` | `6582e92a788e392b24002649af1604257240f6e70904d3b4be835381e0e451e7` | 等作者tree，等Root普通回演后像 |
| `instrument/original_channel_receiver.cpp` | `ce0a9932259452890c9241efbf3427a2c05c5dd3d68c16aa9d00f45f5070a497` | 等作者tree，等Root普通回演后像 |
| `shell/main.cpp` | `a38cfec6c77cec58fc546b2a775ab0b0fe7dbf471f7c0fa5464e78ed4fddbc06` | 等作者tree，等Root普通回演后像 |
| `instrument/instrument_page.cpp` | `1b887ccea0f7d5f83d15a54bd3421f4e89bccd567103c50baa62bc15bd3955e1` | 等作者tree，等Root普通回演后像 |
| `shell/frmmain.h` | `3d0ebdbef06ebbd3aec0d3036aad4c2821298c4aa776bc61252400f1358ae433` | 等三向stdout，等Root普通回演后像 |
| `shell/device_control_shell.cpp` | `14ca22a5f0f581d68aead97ef6c34f9aeddaa1a9ba3ed135101feb350e6e39e4` | 等唯一include冲突的精确解决结果，等Root普通回演后像 |

三向原命令采用current_688/historic_f741/ascii_Uh三侧与命令级 `core.autocrlf=false`，保存的头文件退出0、cpp退出1分别实核，未把有冲突改写为初次成功。cpp未解决原件与原stdout逐字相等，SHA为 `6ec0d3381471ebc6a48a925a57f7a4c272e37d590e380f45a948d6d5e3db9481`，仅一组冲突标记，位于include区：ours是QUuid，theirs是QDir/QThread/limits。只把这一精确冲突段替换成四项include依次全部保留后，完整字节即等最终cpp；无额外手改。最终两主窗无冲突标记，仍全CRLF（425/1827行，没有裸LF）。

Root保存的普通补丁检查/应用 `06.actual_exit`、`07.actual_exit` 均0，标准错误均为空；命令cwd均为独立 `packaging_01/ordinary_replay`，没有在正式执行。独立读取回演目录的六份文件，均逐字等Root candidate。本审没有重新执行这些命令。

`sparse_after_6.json` 实际 SHA-256 为 `67d0067d5b266435c82b775f078fe17ee5367bdcbfa7a0e2a58670d819dd218f`，六项逐份等实际candidate。合成的 `composed_expected_after_688.json` SHA-256 为 `0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d`；独立读取比较确认其与b078同为688键、键集相同，只替换本片六键。这是未来预期表，不是正式全源实扫或第11批构建源表。

## 两主窗实际增量与九项保留核对

相对Root before完整读差异，头文件只有receiver前置声明、公开登记接口及四个登记成员/私有能力函数声明。cpp只有receiver/QDir/QThread/limits include、两个新函数，以及 `receiveManualCommunication` 的精确白名单和Uh独立分支；QUuid、`ProbeSourceRelease` 和全部现有Mg头/状态声明保留。

按保留CRLF的完整成员函数文本段比较，Root候选相对当前before只新增 `attachOriginalChannelReadback`、`channelReadbackRequestBlocker`，只修改既有 `receiveManualCommunication`，无删除函数；这三段与作者Uh tree逐字相等。以下现有关键函数则逐字等Root before，包括已经合入的Mg修改，因而本次没有借合并重写它们。

| 上轮要求 | 组合后实际依据（行号为Root候选cpp） |
| --- | --- |
| 1. 探测门只限制新开始/暂停 | `deviceUserControlBlocker:180` 与 `reagentProbeTaskControlBlocker:197` 各自逐字保留；后者未搬入通用身份门，原查询/消音没有被新的Root通用门误挡。 |
| 2. 开始确认前后及真实准备完成后继续受门约束 | `requestDeviceStart:379`、`finishDeviceTaskPreparation:458`、`detectionStartBlocker:613` 全部等before。原确认前后及finish再核都在；普通按钮、QB、手动Ts仍走真实User链。 |
| 3. 暂停与消音条件分开，QB不改规则 | `requestDevicePauseOrSilence:343` 保留pause为true时两次核探测；`receiveDeviceStartKey:487` 保留原动作选择与暂停门；`updateDeviceControlActions:561` 保留探测暂停禁用/提示和试剂忙状态。三函数均等before。 |
| 4. cancelManual先撤旧User采用资格 | `cancelManualCommunication:247` 等before：先保存旧准备号，只要有号即cancelUser，不依赖准备bool；随后才撤同号的旧准备状态，不用新号、不假装Worker已结束。 |
| 5. 真实完成链及唯一finish入口 | `requestDeviceStart` 保留先绑手动号、更新界面后再核、queueUserTaskPreparation和10秒撤销；`finishDeviceTaskPreparation` 保留真实finished/退休后再核才start。`attachDeviceTaskStore` 等before，`:912` 仍是唯一deviceTasksPrepared→finish helper接线，没有第二发送lambda或恢复旧prepareDeviceTasks入口。 |
| 6. B1身份、晚挂幂等与通知重入 | `attachManualCommunication`、`manualCommunicationBlocker`、`finishManualCommunication:222` 均等before。原管理员来源、会话/页面/连接校验仍在；终态先清旧手动等待，再setProperty/通知，不在通知后清新请求；不把手动结果当后台或N/C/Task退休。 |
| 7. Mg上下文撤销/业务回包/优先使用互斥 | `invalidateDeviceTaskContext:678`、`attachDeviceTaskStore`、原同步/优先使用相关函数等before。原cancelManual后仍cancelMg，保留冲突业务回包取消/拒写、detectOne分派、探测接线和操作列表。 |
| 8. Mg拥有者、Prepared finally与真实读者收尾 | cpp原`ProbeSourceRelease`未改；`preparedReagentProbe:1603`、`cancelReagentProbe:1648`、`finishReagentProbeIfReady:1793` 等before，其他既有成员函数亦无新增差异。原来源锁、token、准备/60秒期限、发送重入和Mg/N/reader收尾不受本次手动终态替代。 |
| 9. Uh不增加探测期间发送许可 | 六源没有device_control.cpp。当前正式底层send仍只例外Qj/Qk/QH/Mz/MP，不含Uh；新分派不带ownProbe权限，也不清探测未知。被拒仍保守报unconfirmed，不能改作保证未发送或自动重试。 |

## Uh分派的业务核对

Root cpp `137–159` 的登记和能力门只保存已有receiver、原消费者页面、原根、单调登记号，不取得关闭/线程/数据库控制权；同绑定幂等、活绑定不替换、登记号耗尽拒绝，原页QPointer必须仍等当前instrument。实际发送时再调用receiver的只读动态能力检查，不把main初始页面快照、空闭包或登记成功当已具备真实保存能力。

`receiveManualCommunication:267` 保留原管理员与请求预算守卫，仅给白名单追加精确Uh。`:301–330` 是独立Uh分支，位于原Qj/Qk/QH兜底之前。发前保存四项登记来源，先核原手动请求和动态能力，再记SendAttempted并原样调用 `maintenanceCommand(payload)`；没有trim、换码、增字段或增加Uh参数形式。发后先核同一个手动号，随后核身份/登记与动态能力；所有分支return，不会再发QH。

true只报告已入发送队列；false或调用后能力变化保留未确认/观察不可用，不重试、不清未知，也没有把独立Uh接收批次配成手动请求确认。四个不重叠文件逐字等已审作者后像，原被动Uh保存、重读、页面采用清AD和批号/手动号分离没有新的组合修改；前轮业务来源与原Windows对照仍见 `ascii_uh_business_independent_review_01.md`。

本次完成的是上述源码组合与守卫审查，未发现需要退回Root修改的确定阻点。正式采用仍须由Root另行记录真实检查、应用和正式后像；完整ARM64构建与RK、真实管理员点击/取消/换会话、真实Uh TX及独立接收保存、失败恢复和资源验证分别待后续，不能计入当前第11批或用其现有C7观察替代。
