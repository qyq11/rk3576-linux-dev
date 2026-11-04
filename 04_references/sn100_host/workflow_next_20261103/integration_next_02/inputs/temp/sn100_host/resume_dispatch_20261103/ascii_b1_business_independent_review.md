# 手动ASCII B1：六正文、权限与User04接合独立审查

2026-11-03。只读冻结B1的handoff、四文件完整差异/实际tree及必要当前正式调用，没有把作者两审当本次结论；不改正式或候选，不构建、运行、模拟、联网或联板，未开子Agent。B1本次仍只是六正文子集，不是原自由正文全部移植。

**原B1在其7cad底稿上的六正文分流、权限和发送阶段处理中，未见须先修的确定新业务阻点；但不能把四文件整文件直接放回当前正式。** 当前User04已经替换开始准备和收尾链，B1旧直接prepare与旧回调必须接入现有User04再窄审。下文的接合要求是当前合入前置，不是要求用户重复授权。

## 输入绑定及当前重叠

B1 `manual_ascii_B1.patch` 实测SHA-256为 `b291a84f4b327188e9c53b121a627a9e5bdc297bcd9bcb4f6d53aca978506910`。四份实际后像均匹配handoff：

| tree相对路径 | 实际后像SHA-256 |
| --- | --- |
| instrument/instrument_page.cpp | `8ac4b8964c940a12303d57dec60ac78562ff618dad6449d370f559246592b82c` |
| instrument/instrument_page.h | `0215157511c2cf74cb3240c11a30a2c12d293728ca27a6b840837c7f9e313e11` |
| shell/frmmain.h | `3772b1f4e273e967b236366f61ce69b2c6e79120f32632bd54a7af760cd5365b` |
| shell/device_control_shell.cpp | `dc38f8e159a39e378e7e372fd614fc0c6c9e2b4168df4171791efdcc308401bc` |

当前Root表 `workflow_next_20261103/integration_10_source_acceptance_01/observer_04/source_after.json` 实测SHA为 `b688f7a8c9c888482bf53b9520239ec9618c61a2da5a701d526387e644add5d3`，687项。只点核上述四份正式文件均匹配该表；仪器页两份仍匹配B1前像，主窗两份已不同于B1前像。未重核整687树，也未应用补丁。以下B1行号相对 `temp/sn100_host/manual_ascii_B1_candidate_20261103_01/tree/`；当前正式行号另写“当前”。

## 六种精确正文的实际消费者

| 正文 | 独立核到的代码路径与守卫 |
| --- | --- |
| `Qj`、`Qk`、`QH` | B1 `shell/device_control_shell.cpp:215–258`仅精确匹配后调用原query枚举；没有调用confirmObservedState。查询准入不要求fresh，仍核管理员、页面等待、连接、登录/退出及关闭。原DeviceControl接到真实Qj后原有状态处理保持，本片没有把一次查询当任务/配置/SQL恢复。 |
| `Ts` | `:295–383`调用原主窗确认/准备流程，确认前后均核deviceUserControlBlocker、startBlocker、detectionStartBlocker及本次手动上下文；更新动作状态后再核原请求，才排后台。后台成功返回前不调用start。旧底稿`:791`回调在最终start前再次核守卫和手动身份。当前正式必须按后文换成User04生产链，不能保留旧直接prepare。 |
| `Tp[0]` | `:261–293`与顶部暂停共用requestDevicePauseOrSilence真分支，确认前后核原连接、会话/关闭及pauseBlocker；最终调用DeviceControl::pause。没有发伪QB，不调用开始prepare，不套“必须有新任务”等开始/N-C条件。 |
| `Mz` | 同函数false分支与顶部蜂鸣器按钮共用确认，deviceUserControlBlocker(requireFresh=false)，不要求fresh或新任务，调用silenceAlarm。它不清任务、配置或SQL未知，也不把静音称故障解除。 |

B1没有让手工Ts/Tp走maintenanceCommand快捷路，没有公开DeviceControl::send；DeviceControl和固定维护白名单根本不在差异中。共享暂停/静音只新增确认前后身份/连接复核，保留各自业务条件。原实体QB函数整段仍是原调用，不把手动请求的来源记录成实体键。

## 权限、ASCII与界面状态

`instrument/instrument_page.cpp:1147–1183`点击时核consumer就绪、authenticated、role精确5及Password/Configured来源、连接、页面忙和连接草稿。Root `receiveManualCommunication:215` 再核实际Page待处理请求与AccountSession值；`sameManualIdentity`比较姓名、角色、登录、来源、原权限根和授权列表，不把actionReady当登录代次。合法Configured管理员可显示为Guest，未按名称误拒绝。

Page每次setSession撤旧等待并推进页面代次；Root的sessionChanged另推进代次后撤原上下文。同名重登不能复用旧确认。连接号、页面代次、原正文和本地不复用请求号都冻结在请求里，后台回调不重新读输入框或贴到新连接。原编辑器仍可编辑，待处理期间改变文字不会改变这次已经冻结的正文。

ASCII仍用原已审助手 `instrument_page.cpp:46–66,1107–1112`：完整检查U+0000至U+007F后逐字符取字节，文档段换行映LF，不trim、不加行、不解析hex/反斜杠转义。点击先拒空或超过128000字节，再按六个完全相等正文匹配；带空格/LF、其他大小写或其他ASCII正文返回“尚未兼容”，不是擅自修正或称设备拒绝。非ASCII失败保留输入；结果、取消和撤销函数都没有清输入代码。

点击不增加TX计数。`appendOriginalCommunication:1243–1250`沿原实际字节事件展示和计数；原发送链由wireActivity完成帧解码后进入这里。C组合参数、自测接收注入、其他正文没有新clicked消费者/能力开放，不调用接收处理假造验证。

## 准备、入队与未知的分开处理

Root在调用query/pause/silence/start前设置SendAttempted；编码/正文/准入拒绝发生在此前。query或动作发送返回true只报告已入队，start也继续区分主控接受/真实运行；不报告检测或设备动作完成。发送调用重入断线后即使返回false，仍保守报告可能已入队，不承诺“从未发送”，也不自动重放。

`finishManualCommunication:173–196`终态先清原请求再发布结果/通知Page；Page `receiveManualCommunicationResult:1197–1206`只接受同一请求/页面代次/连接，终态先清对应等待。旧通知不能撤掉后来请求。日志/属性记录请求号、连接、正文长度/摘要、阶段和SendAttempted，不把本地号填成任务号、真实到包号或提交凭据。

`attachManualCommunication:119–138`在enable立即尝试、0ms晚挂和状态更新处幂等调用，先置绑定标记；这与正常main先enable、后挂仪器页的顺序兼容。页面销毁先清指针/绑定，再撤旧请求。没有仅在enable时if(instrument_)连接而漏掉正常启动的旧问题。

## 与当前User04必须怎样接合

以下是合并后需要逐项核的条件；当前原B1没有这些User04适配，不能以其原审结论代替合并后审查。

1. **保持当前生产用户准备链。** 当前 `device_control_shell.cpp:164–181`使用queueUserTaskPreparation，`task_reagent_shell.cpp:647–691`冻结真实用户来源/样本/三INI/项目、排动作并建立保护；后台是prepareUserDeviceTasks，不是B1旧`:350`的字符串prepareDeviceTasks。合并应保留手动请求到原deviceTaskRequest的关联及updateDeviceControlActions之后再核上下文，但用当前queueUserTaskPreparation的bool处理登记失败。手动号不能代替User04的原准备号或设备包号。不得把同次初始化/准备前后current和SendTaskImmediate=true明确拒绝丢回旧逻辑。
2. **只保留一个真正开始收尾函数。** 当前 `frmmain.h:149`及 `device_control_shell.cpp:187–199`已有finishDeviceTaskPreparation，当前`:617`使用成员指针连接；User04 `task_reagent_shell.cpp:501–507`在真实完成、来源核验并退休原action后调用它。把B1旧`:791–820`回调中“手动关联、最终手动守卫、SendAttempted、阶段结果”的部分并入这个现有函数，不能再接第二个发送lambda，也不能绕过User04提前调用它。正常顶部开始/实体键、原startAccepted消费者继续保持。
3. **撤手动请求也要撤原User动作，不能伪退休。** B1 `cancelManualCommunication:198–212`在旧底稿只清deviceTaskPreparing/递增原准备号并排cancelDeviceTaskPreparation。当前合并必须先保存它关联的旧manualCommunicationTaskRequest，并据此调用cancelUserTaskPreparation(旧号,原因)，再清/失效本片关联状态。不能只排worker cancel而漏Root动作，也不应仅因deviceTaskPreparing已经false就跳过对仍在途User动作的撤销。原User取消按请求号匹配，保留已派发动作等待真实finished，不删action、不清事务/N-C门、不取消另一个新请求。
4. **10秒超时保留User04收尾。** 当前timeout已有cancelUserTaskPreparation及worker取消，合并不得丢失；B1可结束本片页面等待并记录preparation-timeout，但这不代表后台/reader真实结束。后来的finished仍走原归属、冲突/取消核对后收尾，不能再次发送Ts，也不能因手动终态就清User桥或旧未知事实。
5. **撤销/签名声明完整保留。** B1的invalidateDeviceTaskContext新增cancelManual应与当前cancelTaskReagentContexts等撤销并存；sessionChanged双代次、连接改变和页面销毁仍只结束本片关联。frmmain.h保留当前queueUserTaskPreparation、cancelUserTaskPreparation、finishUserTaskOperation及唯一finishDeviceTaskPreparation等声明，再加入手动字段/新可选参数；不能用B1旧头整段覆盖掉User04。Observer及LIS其他现有接线同样保留。

原B1直接准备/旧回调是继承7cad底稿，不能当作B1在当时新增的缺陷；但今天若不适配就覆盖，会退掉已接受的User04配置来源、动作保护和真实收尾，是明确的当前合入阻断条件。当前InstrumentPage两文件可按其仍匹配的前像单独处理，公共主窗两份必须用Root独立合并候选，并在新指纹下补审上述接合。

## 本次结论范围与剩余验证

可供合并的成功范围是：严格原字节输入、六种精确正文与真实消费者、双处真实管理员/请求身份核对、暂停与静音各自条件、查询不额外解除未知、late attach、保留输入和真实TX计数、发送调用与结果未知分开。没有任意文本、C参数、自测注入、裸send或新增维护授权。

尚未证明：Root合并后的User04接合、C++/moc/完整链接、RK点击/取消/重登/重连、后台超时与晚到真实finished、实际收发及主控/仪器动作、响应资源。六项源码支持不自动变成六项本轮板端动作已验，亦不代表原自由正文已完整移植。下一步先审合并候选，再由Root安排必要完整构建和获准实际验证；不跑电脑产品或模拟来补结果。
