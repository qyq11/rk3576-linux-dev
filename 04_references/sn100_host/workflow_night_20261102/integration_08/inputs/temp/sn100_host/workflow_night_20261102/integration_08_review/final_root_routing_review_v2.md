# 主窗口第二版：事务待核范围修点窄审

2026-11-03。本次只读核对Root第二版与前一冻结版的差异，**页面终审指出的C遮罩漏孔P2在本修点范围内已收口，原已审路由、生命周期和预算没有新的源码漂移。**可以交总协调继续其余合入前核对；不代表整批业务、RK或真实仪器已通过。

本次只新增本说明，未修改作者候选、正式源、旧补丁、旧报告或任何运行数据；没有执行生成器、晋级脚本、补丁应用、编译、SQL、产品或网络操作。旧版的阻断结论按旧版原范围保留，不回写成当时已经通过。

## 精确输入及实际字节差异

| 输入 | SHA-256 |
| --- | --- |
| `temp/task_reagent_root_candidate/revision_02/packaging/task_reagent_root_on671_v2.patch` | 3e29c16c008482cc51c51ee890d751fc6989ec8fac63fb2ae64e96f737450e82 |
| 同目录`manifest.json` | ead7054087684e9ffebc7b5abaab41fca51978047d524565510278471a969f19 |
| `revision_02/source_snapshot.json` | 957d7991629c4eff7103324255e9036252c008cc0f332e92d85daddaebffa906 |
| `revision_02/candidate/06_programs/sn100_host/shell/task_reagent_shell.cpp` | e27fc51eb505798a9082a7553506c50d200ec60ec6af375940d7e9b86758b518 |

读取前后这些指纹相同。第二版实际源码集合仍是674份，与前版`cb86ca5f…`表相比无新增/删除，唯一字节变化是`task_reagent_shell.cpp`由`ed3d788a…`变为`e27fc51e…`；其余673份逐字节相同。另按第二版完整表逐项复算674份，差异0。本次没有重新执行manifest中记载的普通check/apply，也没有以该声明冒充自己的执行结果。

## 为什么这次能覆盖原漏孔路径

原[页面/Provider终审](final_page_provider_review.md)指出：整个basis可以可用，但其中某孔没有可信运行量；该孔仍可能受原SQL事务影响。旧`foldTransaction`仅保存逐孔available=true的范围，因此后续真实Ur使另一孔恢复来源时，该孔没有被C遮罩覆盖。

第二版`task_reagent_shell.cpp:75–90`只改此处：

- 保留原事务键、原动作/连接/根、receiptId、真实transaction和committed字段；不把CommitUnconfirmed伪改为Committed。
- 删除用`HoleRuntimeBasis.available`筛孔和合并旧子集的路径。
- 对所有需要归档的事务未知事实，直接设`fact.unscoped=true`；新建事实的holes保持为空，不再声称已证明具体影响范围。

现有`syncTaskReagentDisplayUnknown:169–196`遇到同根C事实的unscoped或空孔表即给页面整盘待核遮罩；已结束动作仍在transactions保存该事实。原Source/Ur/手工恢复链仅解除B显示来源待核，没有删除transactions，因此后来建立了可信来源的孔也不会绕开这笔尚未核清的C。事实达到32条上限时的transactionOverflow整盘待核仍在，没有借修点清理未知。

这与原P2中的“盘中A孔可信、B孔尚无可信前版，事务可能影响B，后来Ur确认B”的路径对应；不依赖模拟信号、数据库夹具或仪器复现来得出本静态结论。

## 前版哪些结论可以沿用

原[接线与生命周期终审](final_root_routing_review.md)中的原包32包/1MiB联合记账、Observed缓存后等同包Processed、false但真提交保事实、回应不等页面回执、B收口后不堵合法任务、C只阻新事务、实际N专读哨兵、Source/Qj/Start/close接线、取消/晚到保护、构建条目和单个500ms等待提示计时器均未被本次差异修改。相应函数位于同一文件的其他部分或其他673份完全相同的源码；没有因新修点增加第二个队列、计时器、线程、退款或重放。

这里只沿用已审源码范围，不将早审或前版编译记录说成第二版的实际构建；第二版对象及真实依赖应使用作者独立保存的修后证据，并由总协调绑定最终合入清单。

## 仍然保留的限制

事务未知事实仍只保存在当前进程的有界记录和真实日志，跨重启核对未因此完成。无新COMMIT重复TR的原弱刷新、g6/g15及逐步骤/按孔累计差异、物理已加和退款边界按既有交接继续待核；本修点没有改冻结后端。

完整ARM64链接、RK实际任务/N交错、读取失败和退出恢复、显示数值/次数、资源及同板主控影响、长稳与真实仪器继续分别验收。只读审阅与文件指纹不能代替这些结果。
