# Uh v2：补上操作者能看到的真实结果

v1固定原件保留在相邻`channel_switch_candidate/`。本版相对v1**只改`candidate/shell/main.cpp`**，不改接收、SQL、页面方法、05正式源码或其他原链。`v1_candidate/`是v1候选的只读底稿副本，`base/`仍是原累计补丁的捕获底稿。

## 为什么需要修

v1的`batchFinished`只写日志、登记退出错误；失败或成功都没有送到实际可见提示。现有`InstrumentPage::statusChanged`虽接到宿主，但`instrument_shell.cpp::updateInstrumentStatus()`在已有DeviceControl时直接返回，因此只emit这个信号也不能补齐。

回看原Windows源码：`CommandReceiver`构造里把`signalReceiverFinish`接到关闭等待框并弹`CusMessageBox::showInformationBox("请求完成！")`；`pcReceiveChannelInfo()`先发此信号，再发通道列表去保存。`frmDriverSet::slotReceiveChannelInfo()`保存失败只输出qDebug，最后刷新。原框早于落库，不能代表真实保存成功。

当前`CusMessageBox::openWarningBox()`虽不用exec，遮罩仍设置WindowModal/ApplicationModal，会阻挡所属窗口操作。本版不用这条自动模态链。沿用工程`ReagentsPage`已有的`QToolTip::showText`非模态方式，延后到真实终态说明结果；这是明确的通知时机/形式差异，不声称逐字照搬原早期成功框。

## 本版实际接线

主入口内`presentOriginalChannelResult`统一保存最近结果到通道页面的属性及页面tooltip，再发既有statusChanged。属性含完整文字、请求号、连接代次、结果名及页面是否刷新；这些保存动作不代表操作者已经看见。

当主窗实际可见、活跃，且原`statusArea`可见区域非空时，以其可见区域定位浮动提示，最长参数为12秒。不切页面、不移动焦点、不打开模态窗口、不改主控状态灯或颜色。**不调用statusArea->setToolTip，也不覆盖其原设备/医院提示；仅借其定位。**通道页隐藏时仍能在原全局状态区看到浮动提示。

失焦、遮挡/不可见或退出时不强行显示或抢焦点；仅保留最近结果。重新激活不会自动补弹，页面自身tooltip可查看最近结果，原完整日志继续保留。Qq可能更新通道表tooltip，因此本版不把通道表tooltip当持久终态存储。

| 真实回执 | 显示口径 |
| --- | --- |
| Failed | “通道回读未保存”，附后台可理解原因与原详细错误。不报请求完成，不指示盲目重放。 |
| CommittedReloadFailed | “已保存，但重新读取失败，页面尚未刷新，需重新读取核对”。不用“正在重读”，因为关闭期间不会继续reload。 |
| Committed且displayed为true、控制对象仍连接且代次相同 | 才说“已保存，当前通道表已刷新并清除旧AD，等待主控新读数”。 |
| Committed但页面没消费/连接过期 | 明说已保存但当前页面未刷新；旧连接带明确说明。 |
| Cancelled | 明说本批已取消、未保存，不伪报成功。 |
| Unconfirmed或未知结果 | 保存结果待核，未确认完成，不会自动重放。 |
| snapshotRecovered | 独立提示真实重读恢复；同样复核displayed、连接和代次，才说当前页面已刷新。不产生第二次批次提交终态。提示前连接已变时不附旧“当前已刷新”的误导文字。 |
| Receiver.error | 原错误也走同一非模态路径，修复原statusChanged被宿主跳过而不可见的问题。 |

以上是接线/源码核对，不是Windows或RK画面实际显示通过。原版无可靠事务回执和所有旧边界仍见v1交接；本版没有改变v1数据、撤销、队列或恢复规则。

## 固定文件与整合

- `channel_notifications_delta.patch`：v1→v2，仅main；SHA `d440a5e035682671c2ee0398fea3cf5cbbc013365a0fc6026d1d4d6e8c2523c9`。
- `channel_switch.patch`：原底稿→v2累计；SHA `bdae1b3d7a0220ac77ff588e515988b2e9687f5a1689864653c95edf0445465d`。其中core/page片与v1完全相同，wiring片更新为`0638262a82a90c78b929257c2b27de8d8a30695234d7d8af5fc257e9064c1aec`。
- `candidate_files.sha256`固定11文件，`patches.sha256`固定delta/累计及各窄片。两个补丁均在私有v1副本/原base上只读apply检查通过，未应用正式；总窗口负责对05串行变基。
- 没有新正式目录或新资源，package snapshot目录要求与v1相同，仍由既有instrument/original_files/shell覆盖。

编译结果以`verification_result.md`为准。本版只编实际改变的main对象；159个项目头先捕获到`compile_support/`，编译不再引用实时正式项目头。v1八对象不重跑，不混拼为可发布产品。

## Windows及交接待办

本版修完后只读回看原Windows接收器完成框、保存函数和Linux可见链；**没有运行Windows程序、重新构建Windows程序、模拟/脱机测试、真实数据库访问、板卡或主控操作。**继续按总窗口条件安排实际Windows和RK验证，不能以属性有值或对象编译当作操作者已看见。

需要现场核对：实际保存成功且同代刷新、指纹冲突/约束拒绝、已提交但重读失败及恢复、旧代/取消/待核；在通道页/其他页、可见/滚动遮挡、活跃/失焦以及关闭期间核提示不阻挡操作、不覆盖设备主状态或公共tooltip。资源/响应、Qq及设备动作并行影响仍待总窗口统一安排。v1热换库不支持等边界全部保留。
