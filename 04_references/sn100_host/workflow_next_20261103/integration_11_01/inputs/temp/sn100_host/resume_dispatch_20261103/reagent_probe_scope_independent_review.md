# 试剂Mg探测：范围与最小闭环独立审查

2026-11-03。已读作者的handoff、scope_matrix、windows_chain_revision_02、linux_chain和next_task_prompt，并独立回读下列必要当前源/原Windows。只新增本报告，不改作者/正式源码，不编译、运行产品、执行SQL、模拟、联网或联板，未开子Agent。

当前以 `temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/lis_profile_queue/source_after.json` 为准，SHA-256为 `c8bf26a4247c03060feb778d250bf9b5c14a9b93f3bcd550081fb617c88c8c18`。只点核14份实际读取的核心局部源，均匹配该表，没有扫描685整树。作者旧7cad审阅时点及三处User04漂移记录保持；不把它们改成当前全量审查。

**结论：作者指出的生产缺口、两道自身busy拒N、非priority准备期限无限及续时无生产调用均属实。四文件方案可以作为必要准备前片，但不是Mg功能闭环。** 总控若要真正关闭一项设备功能，应把“单次纯Mg探测→真实Mg动作回报→实际N保存及页面采用→失败/未知收尾”作为目标，并按消费者扩大文件范围；不应因四文件上限停在无人消费的准备代码，也不应仅增加白名单或取消busy。

## 原Windows实际行为核实

原路径均在 `06_programs/sn100_host_reference/Seerain450_PC/`，只读未运行。

- 单孔 `frmreagent/frmreagentmaterial.cpp:827–831` 调 `slotDectLVContinue(1)` 后return；活跃实现 `:724–763` 先核actionReady，取当前孔，按实际涉及区域核稀释液盖/试剂盖并确认。return后的None/Unknown过滤不是活跃规则。整盘 `:875–880` 调 `slotDectLVTotalContinue(1)` 后return；活跃 `:656–718` 按面板顺序选非None/Unknown孔、核非空、涉及区域舱盖和确认。不能把后半段旧代码当新规则。
- actionReady来自 `frmmain/frmmain.cpp:791–815`：Idle灯且主状态育温/就绪。它不是prioritize专用LinuxNew条件，也不是Ts的全部水杯/任务门。舱盖必须来自当前真实状态；缺值和构造默认true不等于已经关门。
- `communication/commanddefine.h:81,97` 确认线上是 **Mg，小写g**。`commandsender.cpp:170–219` 为孔数、每孔8项：孔名、试剂名、区域、针号、发送孔号、瓶型、底径、角度；字段Latin1，底径/角度原文。瓶型按名称第一项，针缺映射时试剂区1/样本稀释区3；`frmreagentmaterial.cpp:801–818` 后项目同名映射覆盖前项目。正文没有本地request token，分片序号不能当动作号。纯探测不先发Us，不能拿MN替代Mg。
- 原接收 `commandreceiver.cpp:154–169` 按小写g分流；`frmreagentmaterial.cpp:438–445` 非空取最后项 `toInt()>0` 只结束“完成/失败”提示。不能缩成只接受自造的 `Mg[1]` 格式，也不能从日志“MG消息”把大写MG当已证协议。
- 顶层N由 `commandreceiver.cpp:85–86` 独立交 `slotPCLevelUpdate:418–429` 保存/显示，且仅原Optioning灯态时resetWaitTime。N不是Mg ACK；原Mg等待60秒（`:822`）不是固件事务期限、停止或自动重试合同。
- 原试剂N至少9项，新协议另有体积/高度共10项（`wgtreagentpanel.cpp:2167–2203`）；本次不放宽现有严格长度、身份和错误高度保护。原`:2277–2338`保存高度/量，Testing保留used、非Testing另处理used，动作回报不等于这些数据已经保存/采用，Mg失败也不回滚此前N。

扫码只核本次边界：原 `frmreagentmaterial.cpp:299` 默认禁用btnScan，当前正式 `reagents_page.cpp:155,190–191`也禁用/提示未开放。这里不推断它永远不可达，不开放Mh/A，不讨论算法。

## 当前生产链的确定缺口

当前路径相对 `06_programs/sn100_host/`。

| 环节 | 已接事实与确切断点 |
| --- | --- |
| 页面请求 | `reagents_page.cpp:179–182,432–475` 已有detectOne/All，确认后签一次许可、先setBusy(true)、再发deviceOperationRequested。 |
| 准备 | `original_reagent_store.cpp:107–187`单次认领许可、调用Editor并返回packets；Editor `:784–820`复用Repository `detectionPayload:904–944`生成Mg，纯detect无changes/localSaved，不写量或批号。 |
| 生产准入/消费者 | 当前 `device_control_shell.cpp:856–866` 只开放/接受prioritizeHole；`:1002–1017`准备结果消费者只认本次priority的唯一Us。信号有接收者不等于Mg已有生产消费者。 |
| 实际发送 | `DeviceControl::sendOriginalConfiguration:307–316`只接Ur/UP/Tu/Up/Us；maintenanceCommand固定表没有Mg/Mh。没有严格Mg发送API及动作等待。 |
| 动作回报 | DeviceControl先核连接并转发真实payload；Root泛业务入口仍不接Mg，没有本地探测owner负责完成/失败及未知。显示收包不能替代动作消费者。 |
| Root N门 | `reagent_level_shell.cpp:288–291`无条件把 `reagents_->isBusy()` 当外部占用，且此时Page确已busy。解包在该门之后，当前没有同owner/孔范围的例外。 |
| Page N门 | `reagents_page_level.cpp:238–252`即使后台已保存，仍因busy_/dirty_/pendingOrdinaryReload拒专用展示。只修Root会形成“已提交但页面拒绝采用”。 |
| 准备期限 | Store `authorizeOriginalDeviceOperation:23–34`只给priority10秒；其他操作deadline为time_point::max；permit结构 `reagent_store.h:92–100`也默认无限。纯值原子撤销/一次认领已有，但不等于所有准备均有真实期限。 |
| 续时 | `ReagentsPage::renewDeviceOperationWait:484–489`只有定义和声明，当前生产源码无调用。Page自身60秒timer不等于Root/Store期限或同源N续时已接。 |

必要点核14份为：ReagentsPage头/源、Page level、ReagentStore头、original_reagent_store、Editor源、Repository源、DeviceControl头/源、original_device_commands、Root device_control_shell/frmmain头/reagent_level_shell及原N解包器。当前Repository、Root主控制和主窗头中的User04变更已以c8局部回读，不沿用旧7cad行号推定。

## 四文件前片的价值与局限

原提议的 `shell/frmmain.h`、`shell/device_control_shell.cpp`、`reagents/reagent_store.h`、`reagents/original_reagent_store.cpp` 能建立一个**有真实调用者和真实prepared结果消费者的有限准备合同**：原本地请求、动作、连接、账户、root、孔范围/身份、资料版本、真实单调时钟期限、一次认领及撤销；准备返回必须是唯一Mg、localSaved=false，过期/撤销不进入发送阶段。若只加几个struct/未调用方法也不算该前片完成。

该前片不发包的约束本身合理，可作为审查切片；但单独完成后仍不能让用户执行探测。总控可以分审后一起交一个完整候选，或明确将它列为前片继续后片，不需把四文件当项目授权硬上限或停点。新的实际派工应引用c8前像和windows_chain_revision_02；next_task_prompt仍提windows_chain初稿/旧7cad，只能作历史草案，不能原样当当前开工指令。

还有一个不能跳过的依赖：User04的InitializationSource只闭合SampleStore→SampleRepo/Session内ReagentRepo，**未闭合独立ReagentEditor的初始化缓存**。现Editor `orderedOriginalProjects:136–166`在准备时另外读config/sendSequence；Editor已缓存表名、孔/项目及版本。不能在prepare末尾只抓新INI指纹给旧表/旧项目贴新来源。完整探测应在真实Editor初始化/刷新边界消费对应不可变来源，记录实际表、配置/项目排序来源，再核准备前后current；现有User04可复用的类型/方法不等于这个独立消费者已接。

## 建议的最小功能目标和源码范围

先闭合**一次已配置合法孔的纯单孔探测**，不混充满、清盘或扫码；它有独立真实入口、一个Mg、独立动作回报和原N保存/展示消费者，可在授权真实条件下一项项验收。共用owner/发送/回读链应同时容纳冻结孔集合，随后整盘复用既有筛孔/编码器扩展并核多孔先后，不能另造一套动作/扣量模型。

这不是将原单孔按钮全部分支提前关单：原活跃单孔没有None/Unknown过滤，当前Linux编码器`:923–928`还要求已配置试剂/瓶型。现有更严格的输入/Latin1保护也有已记录差异。若仅支持合法已配置孔，必须明示该范围；需要还原其余原分支时另按原源码审定，不以放宽全局校验凑“全功能”。整盘的实际完成/逐孔N也独立验，不把一孔成功复制为全盘结果。

从实际消费者推出的完整范围约为下列12个既有文件，具体结构可合理复用减少，文件数量本身不是验收条件：

| 必要文件 | 为闭环承担的职责 |
| --- | --- |
| `shell/frmmain.h`、`shell/device_control_shell.cpp` | 独立probe owner、准入/确认后复核、准备结果、实际Mg派发、小写Mg分流、动作结束与全局未知/关闭收尾；不能借priority字段清Us→Ur状态。 |
| `reagents/reagent_store.h`、`reagents/original_reagent_store.cpp` | 有限准备许可/纯值结果、原子撤销、一次认领、当前性检查，真实Editor来源安装；不在GUI做SQL。 |
| `reagents/original_reagent_editor.h/.cpp` | 把同次配置来源接到真实缓存消费者及项目排序，复用现有纯探测准备，不修改CAL setter或任务扣量。Repository现已有可选source接口可复用。 |
| `instrument/device_control.h/.cpp` | 专用Mg入队及当前连接/状态检查，明确排队与未知；现有通用控制pending是10秒，不能把Mg直接塞进去便声称与原60秒等待/合法N续时一致。期限、结果与Root owner须同源。 |
| `shell/reagent_level_shell.cpp` | 只给当前probe自身busy、同连接/root/孔范围及原身份的真实N开窄例外；原任务C、其他N/reader、草稿、配置/Ur等待、未知和孔数对应仍拒绝。 |
| `reagents/reagents_page.h/.cpp`、`reagents/reagents_page_level.cpp` | 保留probe忙与N采用忙的各自拥有关系；同scope的专用N采用、进度/续时、动作结果和读回结果分别显示，失败/取消不错误解除另一owner。 |

以上为4+2+2+1+3，共12。若还需改变原未配置单孔分支、增加独立公共值文件或在N后台传递新证据，再按真实依赖加入Repository/相关头及构建登记，不强塞到12文件里。已有N解包、容量计算、writer提交、processed、专读与页面完成信号尽量复用；不因要少改文件而绕过它们。

## 需要写进实际代码任务的闭环规则

1. **准入与发后状态分开。** 发前保持原登录/角色、fresh Qj、actionReady、实际目标涉及区域的两类舱盖及42/48对应保护。不得机械附加priority的LinuxNew或Ts全套新任务门。发后原灯态可能进入Optioning，不能继续套“必须actionReady”而自行取消自己的动作；门/身份/连接真实变化按已入队阶段保留未知，不假发stop或回滚。
2. **准备来源与输入冻结。** 请求携带真实本地id、连接/账户/root、动作和目标范围；准备结果带所消费的表/配置/项目和原孔身份。返回前和实际入队前均核同一来源。纯探测只capture/current批号模型，不publishDefaults/overwrite/invalidate，不发Us/UC、不占号扣量；新配置不能给旧缓存续签。准备期限是本地预算，应有限且与页面取消同步；发后60秒等待及合法N续时是另一个阶段，不当作固件执行时限。
3. **严格发送，不改白名单。** 增加窄Mg API，只接受后台已验证的原Mg及冻结范围/当前owner，走原transport封帧。不得把Mg加入配置或自由维护白名单，也不改通讯测试入口绕过业务。记录“未入队/可能已入队/等待结果”，队列false遇重入断线也不能无条件声称从未发送。
4. **两道N门成套接。** Root可先做无副作用的原解包和目标身份核对，再判断是否为当前probe自身busy；不能先写库再决定归属。Page专用采用也要核同一owner证据，保留busy给探测，不临时setBusy(false)放开编辑。现 `enqueueOriginalLevelPresentation:259` 排队失败会setBusy(false)，这一分支也须避免清掉probe的busy；普通loaded、N失败和超时等收尾同样不能释放另一owner。任务C、已在途N/reader、外部草稿/操作、运行量待核均保持。
5. **N保存/页面采用沿旧消费者。** 每条N仍按真实当前Context、容量来源、writer及savedFingerprint处理；不把DB最大值或新协议附加体积当新实测量。owner允许的N版本推进只能来自真实processed/专读结果，不能重算新SHA给旧回调续签。合法同scope N且原Optioning灯态才可续相应等待；续时不等于保存成功或Mg完成。样本N、其他孔/旧连接N不能续当前动作。
6. **三个结果分别收尾。** 小写Mg非空按原最后项处理动作完成/失败；N是否真正提交、页面是否采用另记。Mg先到而N处理未收尾时，等待真实worker/reader，不丢提交；Mg成功但没有齐备N只报动作回报成功、数据待核，不补造N/Ur。N保存成功而Mg缺失则动作仍待核；Mg失败不退回已保存N。原包没有业务请求号，只能记录当前连接/本地等待窗口及可核范围，不能伪称设备回显了本地token或强行给晚到包换标签。
7. **未知不借重连/Qj抹掉。** 发前取消仅撤未发送许可；可能已入队后取消、超时、断线、关闭保留独立探测未知和在途N/reader事实，不能自动重发Mg或补Us/Ur。无owner/错连接/不归属包不结束新动作。全局开始/配置/其他机械动作应看到这份未知；`confirmObservedState`或新Qj不得顺便清它，也不得清原任务SQL未知。关窗等待已派发后台真正结束，不终止线程。

## Root可以据此分派与验收什么

可继续派既有试剂窗口做独立候选，目标写为上述单次纯Mg软件闭环，先交来源/owner接口与具体12文件边界给Root协调公共重叠，然后连续补完消费者，不停在“准备成功”或无人调用的API。Root串行处理公共主窗、DeviceControl/构建入口与当前User04、CAL、LIS组合；无需新开功能窗口。

源码闭环后才由Root做必要ARM完整构建和获准原配置RK/仪器核对。需分别留：真实Mg正文及入队、涉及舱盖/角色/机型条件、Mg完整正负回报、每条N实际保存/页面采用和实际次序、无/部分N、超时断线和迟到恢复、资源与同板影响。原协议无请求号、Mg与N先后和异常字段尚待实际资料，不造包、不放宽保护或重复探测凑证据。现阶段报告和准备函数不构成编译、RK动作或完整仪器通过。
