# B1与User04合并：生命周期增量独审

2026-11-03。基于原B1生命周期独审，只读审 `ascii_b1_root_merge_01/tree` 两个公共文件的增量，仪器页两文件只核与原B1字节一致；窄对照现User04真正结束的调用链。本报告不审包装脚本或整棵687树，没有改合并tree或正式源码，没有编译、运行产品/流程、模拟、网络或板卡操作。

## 结论

本次绑定的合并后像中，原B1与User04准备链的整合阻点已关闭，定向范围未见新的确定生命周期阻点。手动Ts保留现有User来源准备；真实无提交收尾可经同一个公共完成函数结束手动等待，没有恢复旧裸准备入口或增加第二条start发送回调。

## 准备与最终发送

合并 `shell/device_control_shell.cpp:334–337` 将原手动nonce映射到当前任务准备号；`:355` 调用 `queueUserTaskPreparation()`。排队失败在`:357–361` 结束准备布尔状态并对该手动号给真实拒绝终态，不把“已登记/准备中”报为已入队。

公共 `finishDeviceTaskPreparation()`（`:377–404`）先核原任务号与仍在准备，再保存该准备对应的手动号。它在发送前重新核当前设备/业务条件及手动页面、会话和连接条件，调用start前设置SendAttempted。成功、后台失败、最终守卫拒绝和调用false均对原手动号收口。false继续使用“可能已入队，待核”，不保证未发送。设备同步通知若已撤掉原手动请求，后续旧号终态会被nonce检查挡住，不会清新请求。

现正式 `task_reagent_shell.cpp:501–507` 在User动作真正退休时调用此公共函数。`:731–744` 的无写完成核对与`:524–540` 不需页面提交时的退休路径均保留，因此NoCommit不依赖旧 `deviceTasksPrepared` 信号，也能到公共收口。两公共头中的User04声明和手动声明均保留。

合并文件中 `deviceTasksPrepared` 只有一条到 `finishDeviceTaskPreparation` 的连接（`:824`），没有并列旧lambda再次调用start；也不存在对 `prepareDeviceTasks` 的旧直接invoke。无需为了手动Ts改变真实任务规则。

## 取消、超时和晚返回

`cancelManualCommunication()`（`:198–216`）先保存旧手动号及关联任务号，按该旧号调用 `cancelUserTaskPreparation()`；即便页面等待布尔已经结束，仍会撤对应User动作的采用资格。只有准备号仍属于该关联时才清当前准备并增代次，不会取消新任务。随后原手动终态仍保留发送是否已尝试的区别。

10秒分支（`:363–373`）只处理原任务号且仍在准备的等待，按该号撤User资格并完成原手动超时终态。身份/资料撤销先走手动取消，再走原 `cancelTaskReagentContexts()`。User04的取消函数只标动作取消和推进收尾，不伪造processed/finished；在途Worker及reader仍保留其真实结束门槛。结束页面等待不等于宣布后台已经结束。

晚来的原退休调用会被准备布尔/任务号挡住；原手动终态另有不复用nonce检查。不会将旧结果改贴新手动请求或自动再发start。User04的动作、回读和真实退休实现未被这四文件合并重写。

## 原B1边界没有倒退

以下合并函数逐字节等于原B1对应函数：`attachManualCommunication()`、`manualCommunicationBlocker()`、`finishManualCommunication()`、`receiveManualCommunication()`、`requestDevicePauseOrSilence()`。故前轮已核的幂等晚挂接、页面销毁先撤指针、双会话代次、终态先清旧再通知以及确认前后核原身份/连接结论继续适用。

仪器页CPP/H两个完整文件也逐字节等于原B1，没有重新审其全部实现；进程nonce、先登记再emit、页面终态保护、严格原正文转换和显示计数边界未改。

## 实际核对和后像绑定

只执行自写的字节/源码文本核对。首轮函数提取正则漏掉const及换行签名，检查器自身退出1；调整提取规则后五函数逐字节相等、连接唯一、旧直接准备入口不存在及无冲突标记检查真实退出0。没有执行任何候选函数或构造结果样本。

| 合并文件 | 实际SHA-256 |
| --- | --- |
| instrument/instrument_page.cpp | 8ac4b8964c940a12303d57dec60ac78562ff618dad6449d370f559246592b82c |
| instrument/instrument_page.h | 0215157511c2cf74cb3240c11a30a2c12d293728ca27a6b840837c7f9e313e11 |
| shell/frmmain.h | a84c57bcd0f98979510caca2823ea271c8d8add7f90724395aebbcf7d5d4f068 |
| shell/device_control_shell.cpp | d7bb3b6c05ed6cc576935eb67e4b9fdd528583b3959c4e131434114bcc523ecc |

本次对照的正式User04退休文件 `shell/task_reagent_shell.cpp` 实际SHA为 `0340e945c9f7664b4d266f9a53322a4791ba1b79469bb089e2c46d88d49b7224`，与原B1生命周期报告对照时一致。

包装仍在收口，本结论只绑定上述tree实际字节。包装后四后像若变化，须由总控重新核差异与本报告适用范围，不能以报告日期代替指纹一致。尚未取得补丁正式应用、完整编译、RK点击/确认/取消/断线/晚返回、真实收发或仪器动作通过结论。
