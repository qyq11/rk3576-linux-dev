# 任务预留和退未用的used_ul格式：只读事实核对

本项只核源码事实，未修改、编译、运行、执行SQL、联网或联板。默认容器候选、08固定底稿、后台冻结候选及正式源码均未改。完整源指纹见同目录`source_sha256.json`；后续逐步舍入方案另写，不覆盖本事实报告。

结论：**原Windows五个有效任务写入点都调用默认`QString::number(cur)`；本地ARM Qt声明的默认参数是`'g',6`。正式07真正的任务预留和TR退量两点已显式使用`'g',15`，bfba后台候选延续它，故这至少是正式07已经存在的移植差异，并非此次凭据组件新增。** 格式差异可能改变写入字符串和后续读回参与运算的数值，不只是页面显示精度；但不是每个输入都会产生不同文本，也不能未经实际列亲和取证就断言RK所有写后存储类型都是TEXT。

## 原Windows五个有效点与一个禁用点

下表主源均为`06_programs/sn100_host_reference/Seerain450_PC/frmsample/frmsamplematerial.cpp`。四个预留函数在第3931–3952行按`SystemCommon::isNewProtocal`实际选择：true调用两个Simple，false调用两个非Simple；没有把它们混为平均行为。

| 分支和函数 | 读、算、下限、格式与真实写库 |
| --- | --- |
| 普通/质控，非简化`getRackNormalTasksCommand` | 第2024行重新查询所选孔；2026–2029行取used、减本步骤`reagent_amount_ul.toDouble()`并按`cur>0?cur:0`限下界；2030行`QString::number(cur)`；2033行`updateSettingByHash(rgSet)`。2034–2037行记录该步骤孔、顺序、原用量和`isAdding=false`。 |
| 普通/质控，简化`getRackNormalTasksCommandSimple` | 2691行重新查询；2693–2696行同样逐步扣量和下界；2697行默认number；2700行实际更新；2701–2704行保存步骤。简化协议并未为used改用g15。 |
| 校准，非简化`getRackCalibrationTasksCommand` | 3161行重新查询；3163–3166行逐步扣量及下界；3167行默认number；3169行更新；3170–3173行保存步骤。 |
| 校准，简化`getRackCalibrationTasksCommandSimple` | 3537行重新查询；3539–3542行逐步扣量及下界；3543行默认number；3545行更新；3546–3549行保存步骤。本段条件是3533行`if(hole)`，另外三段用所选孔条件；这是原代码事实，未借精度核对修其条件。 |
| 活跃TR退未用`restoreRgForSingleTest` | 5745行先取得**本单测试**未加步骤的按孔总量；5746行遍历QMap孔名；5749行重新查询该孔；5751–5754行加回本任务该孔总量并限下界0；5755行默认number；5757行更新。5759行发整盘刷新，5761行清空该测试的rgHole，成功重复处理不会再退同一份记录。未见max_ul上限夹取。 |
| 禁用整孔退量`restoreRgAvaliableTestTimes` | 5718–5741整段为`#if(0)`。5732行虽也是默认number，但不属于活跃TR写点，不能用它代替上行真实入口。 |

QC并没有另一套used写法：1767/2446行按`TypeQuality`设置报告类型，仍走对应Normal函数的同一个试剂步骤循环。普通架与急诊传入不同rack，未发现按架类别另写used格式。

原预留每个实际测试的每个试剂步骤都在步骤循环中读库、扣量、格式化、写库；例如普通第1998行循环`projectReagent`，2024和2033的SELECT/更新在该循环内。`ReagentSettingTable::getSettingByHoleName`第178–225行执行真实SELECT，第215行原样取回used字符串；并非只读一个永不更新的旧缓存。因此，同孔后续步骤可以读到上一步已按g6写回的值。

TR的层次不同：`customwidgets/cussamplehole.h:S_TestBase::getRgInSingleTesting`第122–140行按rgHole原步骤顺序，仅累计`isAdding=false`的步骤，合并同孔**配置用量double**；在一项测试内不逐步骤g6，而是在该测试汇总后、每个孔更新时g6。调用方`slotTasksErrReturn`第4403行按errTaskList逐测试处理，第4453–4456行调用restore，所以不同测试同孔仍可逐测试写回和重读。不能改成跨整包任务先总加一次，并声称舍入顺序相同。

此处只证明getRgInSingleTesting的筛选和restore调用；`isAdding`是否由真实TD字段活跃更新、如何对应某个步骤，尚待下一项调用链核实，不能只因结构中有bool就声称部分加样退量完整可用。当前成功路径方案须区分已知全未加与全已加，真实部分加样恢复保留待核。

## 默认precision和原TEXT写入依据

本地ARM Qt头的实际文件为`/home/qyq/.cache/sn100_qt5_headers_5.15.13_20261102/QtCore/qstring.h`，SHA为`0b7fce99d3795861433b34dd67f18dd867aff3d5e7d79bf56e53e946f7433b62`。第801行声明`number(double, char f='g', int prec=6)`，第793行setNum同默认。此处只读取头，不运行Qt，也不把本地签名当成Windows运行测量。

原`tool/database/reagent/reagent_setting_table.cpp`第39行声明used_ul为TEXT；249行将上面的QString保存到`std::string`，71行通过`sqlite3_bind_text`绑定；215行读取时用`QString::fromStdString`，没有再次改成g15。对原TEXT表及需要更多有效数字的结果，g6与g15可以留下不同的写后TEXT值；即便某些整数或短值碰巧相同，两表达式也不等价。实际RK列的亲和、typeof和值另以真实typed读取为准，不能仅由绑定QString推定所有RK行必为TEXT。

## 正式07和bfba后台候选的真正写点

官方07源码包SHA为`18eaa7fb091d82cddee883c52ac8d6d50d2d86d9d65cd6f25cff99cd74520b41`。包内及当前正式`samples/original_detection_session.cpp`均为SHA `d99745a315c380117273362fb7badaf8b05e46bee842104fe87b3b034875a64e`。bfba精确回演同名源SHA为`096cc7dea84e25e5cf87695d27b2cd3acb194fa83a1f20dae38ef89ce2a5e2ee`，其writer为`1a04c0945b35be9ed80182a75fb0ae3d2aac6692081b304a9008ae8468a82f5c`。

| 对象 | 预留 | TR退未用 | 凭据记录 |
| --- | --- | --- | --- |
| 正式07 Session | 384–385行UPDATE，值为`QString::number(volume,'g',15)` | 767–768行UPDATE，值为`QString::number(amount,'g',15)` | 尚无本次新ReceiptWriter |
| bfba精确回演Session | 401–402行同一UPDATE及g15绑定 | 820–821行同一UPDATE及g15绑定 | 403及822行`matchedUsedWrite`再次传入同一g15格式 |

正式07预留的计算位于`reagents/original_reagent_repository.cpp:assignTask`：第223行复制传入remaining；第276行每个试剂步骤只做double减法，第311行任务成功后交回工作map；CAL的`samples/original_control_tasks.cpp:48`复用这个分配器。没有原逐步骤写后g6读回。Session第375–386行最终按observed行逐孔一次UPDATE，remaining未含则跳过，最终值等于previous时也跳过；另保留finite、非负及不大于原值的校验。

零量是一个需要分开的有效分支：分配器第20–24行的number允许finite且大于等于0；254–256行接受volume=0，270行满足可用量条件仍能选中，276行减0。Windows所选步骤仍会格式化和更新；正式reserve第383行按数值相等跳过，可能保留未规范化的原字符串。逐步g6还可能向上舍入到略大于previous，而正式第382行现有`volume>previous`会拒绝；合法选瓶/扣量判断与最终格式舍入不能简单混成一个上界校验。

正式07 TR第707行的refunds跨本包pending任务按孔累计，第710行跳整任务已加样，第724行累加；第758–768行再每孔一次写入`qMax(0,previous+refundSum)`，没有max_ul上限。bfba对应第811–823行保留同一算法，只增加真实写入凭据。它与原每测试先汇总、每测试每孔写回g6的层次不同。

Task的`reservedReagentVolumes`目前来自分配前后remaining净差（正式Session校准160–168、普通/QC227–235），TR据它退量，bfba writer第218行又复制为凭据reservedByHole。逐步骤舍入后，净差会混入舍入损益；不能继续直接当成原未加步骤配置量总和。此连带在后续方案中单列，不在本报告改源。

## 只消除最终格式差异的最小表达式范围

- 正式07：仅Session第385、768行两个number表达式的格式参数。
- bfba：仅Session第402/403及821/822行四个number表达式；SQL绑定与`matchedUsedWrite`必须使用同一转换结果，不能只改SQL两处留下g15凭据。

这只消除末次绑定字符串格式差异，**不代表逐步骤扣量、逐测试退量和后续选瓶判定已与Windows一致**。完整成功路径的最小逐步方案另写，冻结组件本次不动。

`ReceiptWriter::matchedUsedWrite`第256–263行只保存传入usedText，不另格式化；writer第133行拒绝同rowId重复写入凭据，第157行明确实际存储亲和转换取事务内typeof/值；Session第407/824行调用`captureTransactionAfter`，writer第265–271行捕获实际事务写后整盘typed值，283–320行再读锁内COMMIT后实际值及typeof。不能以g6或g15计算值伪造typed after。

## 其他不同分支：分别保留

- `reagents/original_reagent_repository.cpp:saveRemaining`第655行也用g15，但正式07非tests产品源码没有调用，不是上述Session真实入口，不可只改这个函数来宣称修好。
- 同文件原Ur回写第677行先写used_ul常量`'0'`，678–688行按incoming原字段值绑定，used为空时跳过；这里不是任务预留格式入口，不能一并替换。
- 原N/人工设量及整盘amount→used复制各有独立入口；本报告未把它们当任务预留/TR成功链，也未提出改N、协议命令或任务金额编码。原frmsamplematerial六个used赋值点已逐一列明：五个活跃默认number、一个禁用默认number，没有发现其中另一个显式g15写点。
- 原Windows各步Update的失败处理、现有事务聚合/条件更新、部分加样step状态及真实SQLite亲和属于不同维度。本次为源码事实核对，未证明这些分支已经完全等价，未用模拟或脱机运行填补。
