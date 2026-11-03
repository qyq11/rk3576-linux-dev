# 下一切片：成功路径的逐步舍入和原步骤退量账本

本方案只读准备，尚未实现。精度事实报告`review.md`及其`source_sha256.json`已固定，SHA分别为`08f9bfdfdc08afd80df6a283d694f7d58ea43cdf776999fc1d7f23d746611f0b`和`9f1271ad091f2b2bb6692f74b3f48b2d83961091399ff95e352a15f54a9a09ae`，不回改它们。当前08继续使用冻结bfba输入，原03/07/08、默认容器候选、协议编码和N均不动。

## 先明确能达到的范围

最小可行切片应把三件事一起完成：Tt每测试/试剂步骤g6往返、独立保存原步骤配置量、TR每原任务每孔g6往返。只改最后的number格式会留下不一致的选量或错误退款。

若保留现有每孔一次SQL，可以对照的是**相同原任务顺序、相同已选孔/步骤序列、全部相关SQL成功时的逻辑used最终值**；它不会复原原SQL次数、逐次中间状态、触发器、途中失败或其他连接可见性。原Windows的可用量采样和选瓶节奏也有独立差异，见下节，不能因此称所有选瓶行为已一致。当前只处理实际已知全未加/全已加的边界；真实部分加样恢复须先完成原TD8与isAdding活跃链核对。

## Tt：舍入应进入分配工作值，不能只在末次写库发生

主要位置是`reagents/original_reagent_repository.cpp:assignTask`第223–311行。每个批组仍使用独立trial，失败批组丢弃，成功才交回remaining，不能把试探写入公共工作值。每次真正选中一个原试剂步骤后，执行原下界与格式顺序：

```text
rawAfter = max(0, trial[rowId] - 本步骤原配置量)
stepText = QString::number(rawAfter)       // 原默认g6
trial[rowId] = stepText.toDouble()         // 模拟原写后再读回的数值边界
本任务原步骤量[rowId] += 本步骤原配置量   // 与舍入损益分开；选中的0也建立key
```

这些值供现有分配器中后续步骤和下一测试的可用量判断使用；不能仍由隐藏的未舍入double决定分配，再在最后装成g6。每步都保留有限值、原孔身份、原配置量合法性和相应原允许条件；不增加max_ul夹取，不改协议字段中的金额文本。

`assignTask`当前会向remaining播入全部eligible孔（第224–232行），所以不能把remaining所有key当作“执行过步骤”。可复用本任务原步骤量map的key，以及已选的`reagentSlots`，在Session按本次assigned任务并集取得touched。包括选中的0量步骤；未选孔不因本片被统一规范化。

### 可用量与选瓶采样时点必须另核，不能过度承诺

原Windows不仅有减量循环：

- 普通/质控第1652–1658行、简化第2332–2338行先按当前样本孔缓存各项目`infoTotal`；同一测试的readyList先在第1853–1966行及对应简化段选好，之后才进入第1998/2663行的逐步骤写库。
- CAL第2946–2947行和简化第3342–3343行先取项目info；步骤按该info及原优先级挑孔。
- 信号真实连接为`frmmain.cpp:485`→`frmReagentMaterial::slotGetReagentAvailable:1350`→`wgtReagentPanel::getPgInfoFromPgName:1191`；后者调用`getReagentRemainTests:1137`从DB used读取，再由第1121行`floor((volume-out)/stepVolume)`筛可测孔。普通分支另有多瓶测试从info移除已选瓶的原规则。

因此，原Normal不是每写一步就重新生成整个readyList，CAL与Normal采样范围也不同。给当前trial加g6能修正当前分配器的工作量，但**不能自动证明原缓存、选瓶和批组策略相同**。完整下一切片须按原分支的采样时点和选孔序列保留/核对这些条件；只修舍入时应把成功域限制在已核同一选孔序列，不能称全面选瓶等价。0量在当前number/assignTask中合法，原写点在选中时也不跳过；但原可测次数还含除以步骤量，不能仅凭赋值处断言所有0量配置都能走到原成功选孔。

## 账本：必须去掉“舍入后净差就是未加步骤量”的假设

`samples/original_detection_session.cpp`正式07 CAL第160–168行、普通/QC第227–235行，当前按assignTask前后的remaining差值填`Task.reservedReagentVolumes`。逐步g6后，这个差值包含舍入增减，不等于原`rgInfo.addVol`之和，不能拿来退款。

较小的实现选择是：assignTask在每个试探批组同时维护本任务原步骤量map，按成功选中的原步骤配置量累计；成功时交给现有`task->reservedReagentVolumes`，失败组完全丢弃。Session上述两段改为承接该结果，不再覆盖为remaining净差；只删除因而确定不再使用的before局部变量。原Task字段类型可保留，`samples/original_task_codec.h:36–37`的注释须改为“原步骤配置量按孔汇总，不含格式舍入损益”，不能继续标成前后净差。

这不改任务金额编码、任务ID、原任务顺序、reagentSlots或纯值凭据字段。bfba `ReceiptWriter::taskOrigin:218`仍能复制该map，但新切片必须把`reservedByHole/returnedByHole`明确说明为原步骤账本，实际DB净变化继续由before/after typed行证明，二者不能混称。

## TR：按原任务先汇总同孔，再按任务×孔舍入

主要位置是正式Session `receiveTaskErrors`第675–768行，bfba同函数第697–822行附近。保留原errTask到pending的实际顺序与去重/身份/已退守卫。对每个可退原任务，先按该任务真实未加步骤汇总同孔配置量；再按原任务顺序逐孔折叠工作值：

```text
for task in 原顺序:
    taskRefund = 本任务未加步骤按孔汇总的原配置量
    for hole in 原任务孔顺序:
        stepText = QString::number(max(0, workingUsed[hole] + taskRefund[hole]))
        workingUsed[hole] = stepText.toDouble()
        touched加入hole
```

不能仍将整个pending的refunds先全部相加后只做一次g6，也不能把TR改成每个试剂步骤都g6：原Windows是单测试内部先汇总再写。原同任务孔为QMap孔名顺序；跨任务仍按原errTaskList顺序。同孔会把上一个任务的g6结果作为下一个任务的起点。

现有TD状态和整任务`addedReagentTasks_`不能代替尚未证明的逐步骤isAdding。下一项先查真实TD字段/连接/置位链，再决定原步骤映射；部分加样身份未核前不能凭结构bool推定少退/多退。已知全未加时使用该任务原步骤量；已知全已加不退；未知/部分分支不在本方案宣称通过。

如保留选中0量key，正式TR第719行（bfba760行附近）的`<=0`拒绝不能误拒0，应保留finite与身份校验，仅按原成功允许范围处理负数；不能借此放开非法原值。Tt最终第382–383行现有“最终volume不得大于未舍入previous”和“数值相等不写”也需分开处理：选中0量仍可能规范化原文本，g6还可能向上舍入，不能把这种格式结果误判成原扣量非法。扣量合法性应在舍入前的步骤与真实选孔依据核实，最终按touched决定写孔。

## 保留一次SQL时，凭据怎样保持真实

最终对每个touched孔生成一份g6 `usedText`，由**同一局部QString**供实际SQL绑定和紧随成功UPDATE的`matchedUsedWrite`使用；不得一边SQL g6、一边凭据g15，也不得从after反算“写入了什么”。工作值已经历每个逻辑边界的g6往返；最终再次格式化应采用同一g6规则，新切片需核相应边界，不以15位double输出代替。

当前bfba writer第130–135行和Page `original_task_reagent_display.cpp:81–89`都要求每个rowId至多一份sqlWrites。保留每孔一次SQL后，`matchedUsedWrite`仍记录这次**实际**唯一SQL的原WHERE值与实际最终绑定；`captureTransactionAfter`及COMMIT后的`verifyAfter`照旧读取完整真实typed值/typeof，不预造SQLite亲和结果。Writer函数与Page验证器无需为“逻辑多步、物理一写”改成重复行凭据；纯值ABI也无需扩大。当前纯值头第68行的g15文字是冻结版本的旧说明，未来新delta须另说明格式范围，不能静默改当前已冻结头及其SHA。

如果要在同孔真的执行多条原SQL，就必须另设计有序多写事实、每次实际WHERE和绑定及typed链，并同步writer/Page验证；不能把首次WHERE和最后绑定伪装成任意一次中间SQL。这是更大的切片，不是本最小方案。

## 修改目标与次数边界

成功最终值最小功能目标是`reagents/original_reagent_repository.cpp:assignTask`及`samples/original_detection_session.cpp`的两段账本、最终reserve和TR折叠；`samples/original_task_codec.h`只需同步账本含义说明，不改变字段类型或协议编码。CAL helper当前复用assignTask，若仍用现Task字段交回账本，不必改其功能或引入新全局状态。若实施者选择新增out参数保存逐次文本，则必须如实扩大Repository/CAL helper声明及调用文件，不以本报告强制压回三文件。

| 范围 | 原Windows成功路径的used UPDATE数 | 保留现SQL聚合的数量 |
| --- | --- | --- |
| Tt | 成功选中试剂步骤数S，含能实际选中的0量步骤 | distinct touched孔数 |
| TR | 每可退原任务t的不同退量孔数之和`Σ|H_t|` | 各任务退量孔并集大小`|∪H_t|` |

这份方案固定后，下一项只读追踪TD8/isAdding活跃链；当前不动冻结输入、不编译或执行任何测试、产品、SQL、网络或板端动作。完整下一切片需把逐步舍入、原步骤账本和真实step映射一起完成，不能仓促改格式留下错误退款。
