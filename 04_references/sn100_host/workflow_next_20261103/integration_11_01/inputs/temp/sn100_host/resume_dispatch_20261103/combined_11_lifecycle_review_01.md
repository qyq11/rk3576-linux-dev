# 公共生命周期组合独立窄审（11/01）

本次只读审查 `temp/sn100_host/workflow_next_20261103/root_combined_candidate_11_01` 的17份稀疏后像，重点核对通讯测试手动发送（B1）与单孔探测（Mg04）在主窗口的公共接线。没有修改正式或候选源码，没有执行补丁、构建、产品、SQL、模拟数据、网络或板端操作。当前c7正式程序的留驻状态不由本次源码审查重新认定。

结论：取消、真实后台完成后退休、探测来源租约和N专读收尾的组合未发现新增误发、假结束或误清另一请求的问题；构建登记的改动范围正确。但发现一项确定的行为重叠，需要修订或由总控明确接受业务差异后重新冻结，不能把当前组合直接记为“B1行为全部保持”。本报告没有给出编译、RK运行或真实仪器验收结论。

## 唯一确定问题：探测门挡住了原来允许的查询和静音（P2）

组合的 `candidate/06_programs/sn100_host/shell/device_control_shell.cpp:158` 在通用 `deviceUserControlBlocker()` 中无条件检查 `reagentProbe_ || reagentProbeUnknown_`，其提示文字写的是“未接受新的开始/暂停”。但该函数现在还服务于以下既有B1入口：

- `manualCommunicationBlocker()` 第185—186行对全部手动命令调用 `deviceUserControlBlocker(connection,false)`。`false`只跳过fresh状态检查，不会跳过新增的探测门。因此合法管理员在探测中，或探测结束但结果仍待核时，手动 `Qj/Qk/QH` 也在第268—269行返回，不会进入 `query()`。
- 手动 `Mz` 走第266—267行的 `requestDevicePauseOrSilence(...,false,request)`；顶部“关闭蜂鸣器”也在第119—120行走同一入口。第288行同样被该探测门拒绝。顶部按钮在第504行只按登录和连接情况启用，因而还会出现按钮可点但不能静音的情况。
- 组合的 `instrument/device_control.cpp:199` 明确保留了探测中 `Qj/Qk/QH/Mz/MP` 的发送例外，`query()` 第226—233行和 `silenceAlarm()` 第322—325行没有取消这些能力。主控自动Qj轮询仍可继续；本问题限定为上述公共界面入口被提前拦截，不能写成所有Qj查询均已停止。

这是一项确定的保守拒绝，不是多发或误发。但它改变了原B1中“查询可用于读取已失效状态；静音不套开始专用状态或任务条件”的行为，而且Unknown不会被通用确认清除，影响可能持续。建议把探测门限制在开始、暂停等需要互斥的动作入口，查询和静音保留原连接、身份、页面和关闭检查；不要为修此问题一并放松角色或取消保护。修订后需重算两个公共文件及组合表、补丁和私有回演证据。当前报告所列指纹不能直接晋级为修订后的结果。

## 生命周期组合核对结果

1. 唯一三侧冲突的原件仍在 `merge/device_control_shell.raw_conflict.cpp`，原 `merge-file` 退出1未被改写。本次按实际原件重建确认，最终CPP只把该冲突替换为：先 `cancelManualCommunication()`，再 `cancelReagentProbe()`，最后原 `cancelTaskReagentContexts()`；其余字节与自动合并原件相同，最终无冲突标记。头文件自动合并0的结果没有吞掉B1字段。

2. 与当前f741正式CPP逐函数对照，B1的挂接、请求接收、身份与页面检查、结果通知、取消、开始、暂停/静音以及 `finishDeviceTaskPreparation()` 的函数正文保持原文本。头文件相对f741仅新增Mg头引用、ReceivedPacket声明和探测状态/方法声明，未删改B1 nonce、会话代次、原准备号映射或发送尝试标记。这里的“保持”不包含上述通用拦截函数新增条件带来的调用行为变化。

3. 手动Ts仍走 `queueUserTaskPreparation()`，没有退回旧的准备入口。组合CPP第844行保留唯一一处 `deviceTasksPrepared` 直接连接 `finishDeviceTaskPreparation`，没有并列旧lambda再发送一次。`task_reagent_shell.cpp` 不在本片17路径内；本次读取现有第476—507行确认，仍先核真实processed、reader、enqueue和页面采用结果，记录Terminal、移除旧动作，再由UserOperation分支按原requestId调用唯一完成入口。NoCommit的真实退休出口未被本片替换。

4. B1取消先保存原手动号和原准备号，再撤销对应User采用资格；只有原准备号仍匹配时才撤等待并推进代次。其注释和真实代码均没有把手动终态当成Worker已完成，也没有调用 `cancelUnsent` 清掉其他Testing/prepared。终态在通知页面前先清旧手动号，通知后不再清字段，因此没有新增“页面回调创建新请求后被旧通知抹掉”的覆盖。

5. Mg的14个探测方法与已审Mg04逐函数文本相同，`shell/reagent_level_shell.cpp` 整文件与Mg04逐字节相同。Mg准备返回最先构造按旧token回收的 `ProbeSourceRelease`，旧回调只释放旧来源；Queuing中的取消不提前释放，仍由prepared调用栈退出后的回收对象排队归还Store线程持有的租约。主动取消只标记取消和停止后续采用，没有把prepareReturned或真实N读者返回伪置完成。

6. Mg退休仍同时等prepareReturned以及已派N的processed、页面展示完成、真实reader返回；`nReaders`按packet及connection删除，不删除另一份读者。`retiring`在页面完成通知前设置，避免自己的operationFinished再次取消；只有页面归还同一来源后才清本探测owner。B1与Mg分别用手动request/准备号和探测token/request/connection管理，没有新增互清字段或相互当作完成回执的接线。已有协议没有业务请求号这一限制不因本次组合消失。

## 构建登记和来源绑定

本次独立重算17份候选和17份私有 `packaging_01/ordinary_replay` 后像的SHA，均与 `sparse_after_17.json` 相符。Mg04冻结清单的13份原候选也逐项重算一致；其中11份在本组合保持精确字节，另外两份是本次专审的公共CPP/H。两份LIS来源状态候选与先前批准后像逐字节相同，不在本次重做业务审的范围。

`reagents/reagents.pri:40` 仅把新纯值头 `original_reagent_probe.h` 加入HEADERS；`shell/build.sh:264` 仅把同一头加入headers数组。新头没有Q_OBJECT，不需要新增moc源码；已有build循环只对含Q_OBJECT的头执行moc。`frmmain.h` 通过 `reagents/original_reagent_probe.h` 引用，已有 `-Iprogram_dir` 可解析；14个探测方法均有且只有一处定义，声明齐全，ReceivedPacket有前置声明。没有新增独立CPP、改变编译参数或替换主入口。这里是源登记检查，尚未用真实编译验证。

关键指纹如下：

| 对象 | SHA-256 |
| --- | --- |
| f741基线687表 `integration_10_source_acceptance_01/ascii_b1/source_after.json` | `f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b` |
| Mg04清单 `reagent_single_probe_20261103_04/freeze_04.json` | `f1f21c931da5d4b1b2cbfbf0c02dbc8bc6c576647ef9c16bee1f2148c5efead7` |
| 17份组合清单 `sparse_after_17.json` | `17f3b796184b626d2175e3ba78a31a951a0a572e6bca5656e68e6d307c91d89a` |
| 组合CPP | `065dafd5701ee47cec53f92ecca4dc82ccbca887784a9ae32c9cecaa60271702` |
| 组合frmmain.h | `0f7f034d7b01644c68777ac05788971977a5f52894070a500460c8f0744fb491` |
| 新纯值头 | `98a65bd9015548b8a13c139ed4cf1ca02b0fbc32237cd7c1f95c46401aa8fc62` |
| reagents.pri | `8f7c6b8ad8e342f15fa9435589de415a49e3fa680de9bebce396b8492a7f3ca1` |
| shell/build.sh | `f5293cfc2d8dea9c9e87cb613144366252665f1cf950e480a6c41d2ecc1e9408` |
| 普通补丁 `packaging_01/mg04_lis_profile_on_f741.patch` | `35ee743734107fa9553808852ac870b09e201ca100524d3ccde7b4adc757d333` |
| 合成预期688表 `composed_expected_after_688.json` | `b2676bb40c584d32509436bff0f63e99b08e435cc5f38689648bf20c67591c7b` |

合成预期688表等于读取的f741基线表覆盖本片17项后的字典：16修改、1新增。它是预期组合表，不是正式目录扫描结果。本次仅重核16个受影响正式前像仍等f741清单，正式新头仍不存在；没有重扫或改写正式687源。已读取包装命令及原始退出码，普通patch check/apply确实为0/0；本审只核原件和后像，没有重新执行这两条命令。User16急诊保存部件及Uh消费者能力不在本片。

后续验收范围：先处理查询/静音被误套动作门的问题，再由总控安排修订组合的正式接纳、ARM64完整编译及获准的RK验证。真实探测命令、Mg回报、N提交与页面采用、实际读者返回、取消/断连/关闭和同板资源影响仍需分别留证，不能由本次静态组合审查替代。
