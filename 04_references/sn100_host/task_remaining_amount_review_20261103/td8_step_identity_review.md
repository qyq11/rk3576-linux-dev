# TD8与isAdding：实际活跃链和标记粒度

本项在精度事实报告及成功路径方案固定之后继续只读核对，未回改它们。没有修改任何协议、编码、N、冻结03/07/08或候选源码；未编译、运行测试/产品、执行SQL、联网或联板。所读源版本见`td8_source_sha256.json`。

**结论：原Windows确实有活跃的isAdding赋值链，但收到状态8后会把该测试的全部rgHole都置true，不是按试剂步骤逐个置位。addingSeq仅在四个任务构造入口赋值，没有查到活跃的步骤比较或消费。原接收代码和当前decoder都跳过TD第5项，不能据此断言线上协议根本没有step/order字段，也不能把未知项猜作步骤序号。** 当前正式07及bfba的TD8整任务QSet标记，与这条原活跃链的粗粒度相符；这不代表迟到、错误、删除或真实部分加样的所有业务都已经等价。

## 原版使用的是哪份源

`SeeRain450.pro`第123/126/138/140行包含frmmain、frmsample、customwidgets、communication各pri；`communication/communication.pri:8`列`commandreceiver.cpp`，`customwidgets/customwidgets.pri:100`列`cussamplehole.cpp`，`frmsample/frmsample.pri:22`列`frmsamplematerial.cpp`。没有把`commandreceiver - 副本.cpp`的旧四参数信号当成实际六参数入口。

## 从原TD包到真实对象赋值

主通信实际接线可继续向上追：`frmMain`构造第287行调用`connectSignalSlot`；第361行将`tcpBaseMain::tcpReceived`接到`CommandThread::slotReceiveMcuData`，该槽第365–375行在linkOpen且数据非空时加入接收缓存。`mcuReceiveDataProcessPartTwo`在第268–272行确认完整拼包后发`signalSendMcuReceiveData`；`frmmain.cpp:378`将此信号接到`CommandReceiver::slotCommandReceiver`。相关commandthread.cpp也由communication.pri第10行实际列入。另有DriverSetReceived路径，本报告的主通信依据是上述TCP接收链。

1. `communication/commanddefine.h:51/72`将外层任务命令定义为`'T'`、该子命令定义为`'D'`。
2. `CommandReceiver::slotCommandReceiver`第64–74行移除T后派到`pcTaskCmd`；后者第108–109行再取/移除D，第141–142行调用`pcTaskSampleAdd(commandChanger(...))`。
3. `commandChanger:30–57`按方括号逐项收集，保留空项。`pcTaskSampleAdd:279–298`提取下表字段，再发六参数`signalPCTaskSampleAdd`。
4. `frmmain/frmmain.cpp:416`真实connect该信号到`frmSampleMaterial::slotSampleTesting`；第417行另连接报告页，不能用报告槽代替试剂标记槽。
5. `slotSampleTesting:4282`在state空时直接返回。`taskSendDelete=true`走第4294–4297行仅更新结果状态；否则按rack、hole、task查找原测试。`CusSampleHole::getTaskBase:170–199`从现存普通或校准任务列表以引用返回真实测试指针，不是改一个临时副本。
6. `public/systemcommon.h:222–232`从Idle=0顺序枚举，`ChannelState_Testing`值为8。`slotSampleTesting:4329–4335`在`taskState.toInt()==ChannelState_Testing`时执行：

```cpp
for (auto &rg : testBase->rgHole) {
    rg.isAdding = true;
}
```

这条赋值在原`S_Task_Error`判断前。代码位于正常编译段，非`#if(0)`。只有取得原测试指针且走到此分支才赋值，不能把未找到任务、taskSendDelete或state空的分支说成已标记。

四个正常选中试剂步骤的构造点分别在`frmsamplematerial.cpp:2034–2037、2701–2704、3170–3173、3546–3549`：记录原孔、`projectReagent["xuhao"]`到addingSeq、原配置量addVol，并设isAdding=false。rgInfo结构第59行的bool本身没有默认初始化；未选中/异常回退不能仅凭结构猜为false。本报告只按真实赋值成功路径判断。

全工程检索isAdding/addingSeq，除上述四组初始赋值，唯一true赋值就是第4333行；第122–140行的活跃`getRgInSingleTesting`只按false汇总原量。第414/435行的其他筛选在`#if(0)`内，不能拿来补成活跃逐步骤恢复。未见按addingSeq、试剂针、某个TD位置或状态3/5/7单独改一项rgInfo的活跃代码。

## 原包和当前解码：七项位置逐项对照

TD后括号字段下标从0开始。原`pcTaskSampleAdd`至少7项时读取第一批七项，当前`OriginalResults::parseProgress:122–132`只接受恰好3项或7项。

| TD字段下标 / 从1开始的位置 | 原Windows实际处理 | 当前ReceivedProgress实际处理 |
| --- | --- | --- |
| 0 / 第1项 | rackID | rack |
| 1 / 第2项 | holeNum | hole |
| 2 / 第3项 | taskId | taskId；仅ASCII数字、1到INT_MAX |
| 3 / 第4项 | state | state |
| 4 / 第5项 | 第293行只`offset++`，未传给槽 | 第130行完全不读取，不保存到结构；只受通用括号/字节预算约束，没有步骤/顺序含义校验 |
| 5 / 第6项 | taskType | type |
| 6 / 第7项 | projectName | project |

原信号/槽只有rack、hole、task、state、type、project六参数，未传第5项。当前`ReceivedProgress`在`original_result_codec.h:16–20`也只有这些字段，没有step/order。**第5项的真实协议含义本次尚未由这些源证明；“两边未消费”不等于“线包不含其他身份”。**

原接收条件是`size>=2`却直接读取前三项；不足三项不能因此被称合法包。3到6项时state/type/project保持空，至少7项才读取完整后四位；额外尾项不消费。当前严格3或7项，7项state为空也仍为空。原按`QString(QByteArray)`构造字段，当前通用fields显式Latin1解码；这些是源码层差异，本项不改编码或放开输入条件。

## 当前07的消费、bfba保持情况

`shell/device_control_shell.cpp:607`实际业务白名单包含TD，按原连接排到SampleStore；`samples/original_sample_store.cpp`第336行先核当前已确认连接，第408行把TD交给`OriginalDetectionSession::receiveDevicePayload`。此处引用正式07入口，不把未合入的Root候选当实际运行证明。

`original_result_codec.cpp:23–40`的通用fields还要求完整括号、拒控制字符和嵌套左括号、最多4096项，并采用`protocol/frame_codec.h`的128000字节预算。这些是通用语法/资源限制，不是第5项的业务身份校验。

Session第448–489行真实TD分支：

- 第452–459行按已下发taskId核rack、孔，以及非空project；不从TD另选当前新任务。
- 第460–467行处理已错误/已完成任务；错误后首次迟到“8”而原先未标added时返回待核错误，不凭迟到包重新退量。
- 第469行state空直接返回成功，不由三字段或空state推断已加试剂。
- 第470–473行要求state为规范整数0…9且type与原任务一致。
- 第474行只有state==8才将**taskId**插入`addedReagentTasks_`，没有step/order索引；该集合定义在头第43行。
- 第475行之后才核阶段倒退，并处理CAL状态库。因此不能把“后续处理失败”概括为这条标记一定未发生。
- TR第708–711行按taskId整任务跳过已加标记，否则继续按任务量map处理；不是按rgInfo步骤逐项减除已加量。

bfba的对应TD块第476–517行与正式07逐字节一致，块SHA为`36ab2c804dda92693f16b3e325166c086d1a6bd45ba823e75b8eedf998ffc8b9`。bfba第502行标记整task，第751行TR整task跳过；771–773行凭据按任务记录skippedAdded或returnedByHole，也未新造逐步骤事实。该结论由独立只读复核再次确认。

## 下一切片应保留的边界

源码支持的原行为是：成功构造步骤初始false；本测试收到有效TD8后全部true；TR只退原记录仍未标加入的量，并清空已退步骤记录。没有查到原Windows能依据现有活跃TD链区别“同一测试只有部分步骤已实际加完”的实现。因此，上一份方案保留“真实部分加样恢复待核”是范围限制，不能改写成“原版已有step级恢复、只差移植字段”。

**对已固定方案的独立更正：**`successful_rounding_plan.md`结尾“逐步舍入、原步骤账本和真实step映射一起完成”的表达范围过宽。按本次活跃链核对，下一切片应只处理独立的原配置步骤量账本、逐步舍入及原info采样/选孔时点，保持原TD8整测试标记，不新设计逐步骤物理退款。结构中的addingSeq不等于已有活跃step映射，第5项也不补猜为步骤编号。旧方案字节和SHA保持，后续以本补充的明确边界为准。

此处不提出新的协议/字段或物理退款设计。现有任务编码、协议、N和当前冻结输入本次都保持原样。

本报告只说明活跃源码和消费粒度，没有任何接真实主控或物理加液结果。
