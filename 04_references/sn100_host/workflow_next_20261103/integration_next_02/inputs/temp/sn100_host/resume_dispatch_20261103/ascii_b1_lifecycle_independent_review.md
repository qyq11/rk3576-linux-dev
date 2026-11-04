# B1手动通讯：请求身份与异步生命周期独审

2026-11-03。只读审 `manual_ascii_B1_candidate_20261103_01/tree` 四文件，并窄查当前正式启动、任务准备和User04收口链。没有改正式或候选，没有编译、运行产品/脚本、模拟、网络或板卡操作。本报告不是合并候选或当前687表的验收。

## 结论与当前阻点

原B1局部实现中，未发现确定的请求号复用、同名重新登录后旧请求复活、单击重复接线、确认后向新连接发送或晚准备回调完成新请求的问题。真实启动的延迟挂接已实际接到消费入口。发送调用前记录“已尝试”，false不冒称保证未发送，终态也先清旧再通知。

**确定的整合阻点：原B1基于User04之前的任务准备路径，不能直接覆盖当前公共文件，也不能只保留旧 `deviceTasksPrepared` lambda。** 当前正式 `requestDeviceStart()` 调用 `queueUserTaskPreparation()`；成功、失败及无提交收尾最后由 `completeTaskReagentAction()` 调 `finishDeviceTaskPreparation()`。手动Ts的身份映射、最终守卫、发送阶段和完成通知须接到这个公共函数，保留User04队列、原始身份及真正结束门槛。原B1四文件独审不能替代总控合并后四文件的增量独审。

## 请求身份与确认

- `instrument_page.cpp:118–128` 使用进程内静态 `QAtomicInteger<quint64>` 加CAS；计数达到最大值即拒绝，不回绕。页面重建不会重置该请求序列。任务准备号和连接代次另存，没有混充成手动nonce。
- 页面第1167–1183行在emit前登记等待，冻结正文、连接、页面session代次和 `AccountSession` 值副本；单击只有一处 `push_send_btn.clicked` 消费连接，pending期间拒绝第二次登记。没有emit返回后恢复旧等待的赋值。
- 页面每次 `setSession()` 撤旧并增加自身代次。主窗 `sessionChanged` 也独立增加身份代次并撤上下文。身份比较含姓名、角色、认证标志、来源、原权限根和授权字段；没有把 `actionReady` 作为登录代次，也没有按Guest显示名误拒原配置管理员。
- Ts确认前后核任务上下文、原连接及手动身份，确认期间旧会话撤销不会改贴新会话。暂停/静音同样在嵌套确认框前后检查主窗身份代次和手动上下文。暂停按 `pauseBlocker`；查询和静音不错误套入开始的新任务条件。
- 输入正文按一次点击冻结；后续编辑输入框不会替换该请求正文。空、编码失败、超预算及未兼容正文均在设备发送前拒绝，不截断、不补换行、不解析字面转义。

## 结果、同步重入与结束

主窗 `finishManualCommunication()`（第173–196行）先复制旧请求元数据，终态先清旧主窗等待，再发动态属性、日志和页面通知。页面终态接收（第1197–1206行）先比较请求/页面代次/连接，再清旧页面等待，然后更新按钮和报告；页面撤销也先清原等待再emit。原请求通知没有在通知之后无条件清除新请求的尾部操作。

`manualCommunicationSendAttempted_` 在查询、暂停/静音及最终start调用前设置。实际 `DeviceControl::send()` 会在传输入队后再次检查连接；因同步断线而返回false，确实可能已经入队。B1对应结果为unconfirmed或撤销后“可能已入队，待核”，没有用false伪装成保证未发送。准备过程只报preparing，不报设备开始成功。入队也不代表回包或业务完成。

原B1的准备排队失败、后台失败、最后守卫拒绝、确认取消都有对应终态。10秒定时器仅处理仍属原任务号且仍在准备的请求，撤销等待后晚到的原准备结果会被 `deviceTaskPreparing_` 和任务号挡住；新请求拥有新nonce和任务号。断线/连接换代、主窗身份或资料撤销、页面销毁都会撤对应手动请求；销毁回调先清 `instrument_` 和挂接标志，再取消，避免在销毁对象上回调。

这些判断针对原B1旧准备链。合并后必须继续保留User04真正结束前不释放后台事实的规则，不能把上述“结束页面等待”当成真实后台已经结束。

## 正常启动与幂等晚挂接

当前正式main第692行先 `enableDeviceControl(true)`，第1219行创建仪器页，第1227行挂页，之后才进入事件循环。原B1在enable立即尝试 `attachManualCommunication()`，此时缺页会返回；已有0ms回调第114行再次调用，动作状态更新第449行也幂等尝试。

`attachManualCommunication()` 第122–136行检查设备、页面及已绑定标志，先置标志再连接请求和撤销信号，最后才把页面消费者标为ready。`attachInstrumentPage()` 已把实际 `DeviceControl` 附给页面。因此0ms回调可在正式页面建立后实际连到 `receiveManualCommunication()`，不是只有声明或UI按钮启用。页面销毁清标志，新的页面可重新绑定；正常页面不会因多次动作更新重复连接。

## User04合并必须续上的位置

当前正式 `shell/device_control_shell.cpp:170` 已用 `queueUserTaskPreparation()`，`:187–199` 是公共 `finishDeviceTaskPreparation()`，`:617` 的旧信号只转接公共函数。User04另外从 `task_reagent_shell.cpp:501–507` 在真正退休时也调此公共函数。`finishUserTaskOperation()` 第731–744行核无写/无提交收尾，`advanceTaskReagents()` 第524–540行在不需要页面提交时直接退休；这一支不会依赖旧 `deviceTasksPrepared` 信号。

总控合并候选须满足：

1. 原手动nonce仍映射到本次 `deviceTaskRequest_`，但准备进入现 `queueUserTaskPreparation()`；排队失败即时收口原手动等待，不恢复旧裸 `prepareDeviceTasks` 路径。
2. 公共 `finishDeviceTaskPreparation()` 先核原任务号，保存对应手动号；在真正start前再核手动session/页面/连接，先设置SendAttempted，再调用既有设备start。成功、守卫失败、后台失败及false结果均对原手动号终态通知。无提交但真实成功的退休路径也必须到这里。
3. 10秒或手动撤销应按原任务号调用 `cancelUserTaskPreparation()`，保留User04已经进入后台动作的真实收尾，不仅清布尔值或仅排旧的无号cancel。先保留旧任务号再增代次；不能误取消后来的准备。
4. 页面/账号/设备/资料撤销会结束旧手动等待，后来的真实退休仍可登记旧事实，但不能发送start、完成或清理新的手动请求。公共函数与手动nonce两级校验都要保留。
5. 继续保留旧信号到公共函数的唯一连接，不额外再并列一个会start的旧lambda；避免同一准备的两条发送路径。

上述是明确的公共文件合并要求，不要求为B1再改任务规则。待总控合并候选到齐后，应围绕这些位置重新绑定后像和增量复核。

## 本次实际指纹与范围

只用原生Python读取字节并核SHA/补丁路径，真实退出0；没有执行作者检查器或产品。补丁恰为四文件，实际SHA为 `b291a84f4b327188e9c53b121a627a9e5bdc297bcd9bcb4f6d53aca978506910`。

| B1文件 | 实际候选后像SHA-256 |
| --- | --- |
| instrument/instrument_page.cpp | 8ac4b8964c940a12303d57dec60ac78562ff618dad6449d370f559246592b82c |
| instrument/instrument_page.h | 0215157511c2cf74cb3240c11a30a2c12d293728ca27a6b840837c7f9e313e11 |
| shell/frmmain.h | 3772b1f4e273e967b236366f61ce69b2c6e79120f32632bd54a7af760cd5365b |
| shell/device_control_shell.cpp | dc38f8e159a39e378e7e372fd614fc0c6c9e2b4168df4171791efdcc308401bc |

四前像/四后像均与 `changed_files.json` 一致。当前正式窄查依据指纹：`device_control_shell.cpp` 为 `eadccb771fc651cac7e6d26f52386ce8b570cbfc092910745356b129d7a331d4`，`frmmain.h` 为 `efae044127aecd82e7953e461e742b9537f624556c2e729fabe7ac5ff0803352`，`task_reagent_shell.cpp` 为 `0340e945c9f7664b4d266f9a53322a4791ba1b79469bb089e2c46d88d49b7224`。未回演全部687树，也未把旧685前像冒称当前全表。

B1只实现六项精确正文的真实入口；其他自由正文、B2任务/配置/维护及C组合参数仍未接入。本轮未取得编译、RK点击/确认/取消、同名重登、断线/重连、真实TX/RX、仪器动作或资源通过结论；六项源码可用不自动构成本轮六项实物动作授权。
