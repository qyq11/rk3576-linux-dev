# LIS首片A：资源、失败和值生命周期独立审查

2026-11-04。只读 `lis_application_plan_candidate_20261104_01/candidate/lis_business` 九份实际源码，按Root批准范围和278781范围审查核新四源。五份继承源只核固定字节及必要预算/值结构，不重新扩大原业务审查。没有改候选或formal，没有运行产品、匿名/模拟报文、SQL、编译、网络或板端流程，也未执行作者检查器或私有补丁回演；只新增本报告。

结论：本次固定后像未发现确定的资源上界、失败半计划或值生命周期阻点。输入、输出预算在相应增长前生效，重复次数没有展开，失败返回新Rejected值；Ready/FieldsReady/Prepared均没有产生真实应用、claim、SQL或ACK许可。此结论是纯值部件源审，不是完整LIS生产接线、编译、RK响应/资源、真实接收或发送通过。

## 输入预算和索引增长

Plan第165—178行先重核冻结codec结构上限：body≤1MiB、source.text≤1048576个UTF16单元、段数≤64、DSP项≤4096；映射项目≤4096，结果代码与稀释项合计≤16384。mappingItems使用qint64，并在累加前核剩余预算，没有先resize或按输入次数预分配大表。

第179—185行输入账把旧孔/患者、解码原body字节、source.text、profile/配置身份、段文字、已解析头/患者/订单/DSP/错误文字及全部映射项目相关文字一起保守累计到4MiB。QString按size×2计，body按实际字节计；同一共享字符串在多个逻辑位置仍重复计费。计费只读size，少量字段列表为固定数量QString值包装；在建立三个索引或复制输出计划前已完成输入总账。上游已经创建的decoded本身仍受冻结codec预算，本函数的4MiB不是追溯保证上游从未分配过内存。

TextBudget第16—29行用“新增量≤maximum-used”检查后才加；QString长度先升qint64再乘2。当前最大值及容器int长度不会导致此处窄整数溢出。replace只在已计入after的内部序号/条码实际被替换前扣旧笔、核新笔，避免先扩大输出再补账。

索引在预算后建立：nameFirst与informationFirst各≤4096键，codeLast≤16384键，defaultRates长度≤4096；保存行号及QString键，不复制完整项目结构。QMap查找/插入为对数级，包含有界的文字比较开销，不做DSP×项目×全部结果项的三重扫描。名称首匹配、结果代码末匹配和资料名首匹配分别建表，索引没有重排输入。

Plan第227—232行按原顺序逐个处理DSP，只比较相邻UTF8字节串的严格升序；乱序/重复即Rejected，没有为通过检查另行排序或覆盖。合法选择后，输出任务才由QMap保持原QString项目名顺序。设P/C/D/T分别为项目、结果/稀释项、DSP和任务，结构操作约为 `O((P+C)log(P+C)+D log(P+C)+T logT)`，另计受输入预算限制的字符串扫描/比较；不把该公式当成RK耗时测量。

## 输出、重复次数与临时分配

第220—223行输出另立4MiB账，包含配置/profile、控制号及实际after孔/患者。每个任务计map键、projectName、文件、指纹和默认稀释；第257—276行在最多4096任务及文字计费成功后才insert。内部号与条码仅在真实命中阶段替换对应费用，全未知项目不会被虚构成这些孔属性已赋值。

OBR重复先按原toInt转换，失败/越界或小于1变为1；每个任务只保存一个int，不按可能很大的repeats创建列表、分配任务号或复制任务。scalarRepeatTotal用qint64，累加前有溢出守卫；当前4096项×int32正最大值也远低于qint64上限。这里的计总数不是后续实际任务展开或设备容量准入。

年龄直接扫描UTF16中的ASCII十进制前缀，没有先分配UTF8副本；正/负边界在乘10前核，int32不可表示明确Rejected。样本号按toUInt保留Win32无符号32位范围，再以qint64完成高半区转有符号值，避免ARM64 unsigned long扩大范围。重复次数失败1、无格式日期解析结果无效等规则没有补成临床默认值或当前时间；QString::number(int32)产生的内部号最多11个字符。

UTF8临时分配有明确但不同于4MiB账的边界：

- Plan的item.code.toStdString发生在输入总账通过后；迭代只保当前/上一条UTF8字符串，赋值可能保留容量或临时再分配，不形成按D或repeats展开的字符串历史。按4MiB输入文字账，任一QString最多2Mi个UTF16单元，其UTF8有效字节长度可到约6MiB；冻结codec真实值通常还受更紧关系约束，不能把4MiB写成这些临时分配的峰值。
- ACK固定21+3个字段元素，先计完整分隔符/CR及字段长度，再reserve。codeUnits≤2097152，转int安全，最终正文的逻辑UTF16费用≤4MiB。末尾保留原toStdString/fromStdString往返，可能同时存在UTF16正文、最多约6MiB UTF8字节和回转QString。

索引/容器节点、共享字符串容量、QDateTime值、转换器与分配器开销均不等于文字逻辑费用。预算可限制随输入增长的内容和项数，但不是进程RSS、峰值或分配必成功的硬保证；本审没有做内存/耗时实测。

## 失败和值的生命周期

Plan所有显式校验失败都通过同一rejected()构造全新对象：state保持Rejected，任务表为空、赋值/替换/阶段描述旗标保持false，不把局部已经算出的患者、after或部分task带出去。第295行只在全部成功后置Prepared。局部plan虽可能在后续失败前已有计算结果，但没有修改const输入，也没有把它传给回调或外部存储。

ACK同样失败返回新Rejected、空body；完整正文及原UTF8往返完成后才置Prepared。正常“全未知项目”是合同规定的Prepared空任务计划、最终孔状态None，患者仍更新，不能和失败半计划混为一谈。验证失败前后没有对整个流程“从未发包/未置R1/已回滚”的承诺；上游历史阶段仍由真实拥有者处理。

输入项目/旧孔/decoded只在当前纯函数调用中以const引用读取；输出保留QString/QDateTime/QMap等值和const ReceiveProfile共享所有权，没有保存指向输入容器元素的引用或迭代器。ReceiveProfile本身只含不可变配置身份、序列号、开关和原根，不持QObject、socket、SQL、线程或控制器寿命；保留旧profile不是继续使用旧连接的资格。

新四源没有文件/数据库/网络读写、QObject或线程创建、计时器、当前时间API、任务号预留、claim或发送调用。Plan不调用ACK函数，也没有ACK正文成员；describeMaxAfterMemory/describeAckRequestAfterMax只是未来阶段描述。ACK调用者须显式给出格式正确的时间，但函数不能证明该时间来自真实应用/MAX之后；Prepared只说明正文值形成。未来等待/首匹配/R1、当前generation/peer/profile、claim、真实内存采用、MAX及发送时机仍须由完整生产链核定，不能以手工Ready/FieldsReady或本函数Prepared签发许可。

## 固定范围和实际字节

九份实际文件均匹配candidate_manifest，审前后清单和handoff摘要稳定；五份query/codec/types与 `lis_download_candidate_20261103_01/candidate/lis_business` 继承原件逐字节相同。必要点核的formal九路径均不存在，没有扫描/修改正式688或把虚拟697表当实际package覆盖结果。

| 对象 | 实读SHA-256 |
| --- | --- |
| Root批准范围 | `42217f354af542d6abcc8ac5a9700295bd64bedb7e8ebf6e129c7163a44ef40b` |
| 独立范围预审 | `278781a50aad5153b9af4862c107f554d69fbe4263f6fc86c444295095f378c1` |
| handoff.md | `c599c0e5eb19bf6fc995b431ea766ddbf435eb53d5865a4e08447c7f52dd6c5d` |
| candidate_manifest.json（九源） | `df2424cf41da2845da9d05ee0b0548f15b549ca9fd73e85fb8df6f4c82be2501` |
| original_lis_application_plan.cpp | `d5df434a041a4a848a0f9190b9ea4d69bdd4e64ad5708d64b6337cb0a7646186` |
| original_lis_application_plan.h | `737a72866c5a2bad50fc806f329dc5606732a353a1025ddc12790e01b162cb6d` |
| original_lis_download_ack.cpp | `2282798e4999002f342fcf2c72fcb3bb08b4e72e16e13b4ca9e6ad4870645719` |
| original_lis_download_ack.h | `ba63e4da333e7709ad69a7f70844dabb375e76603239ca175d2b73a4828b409f` |
| lis_application_plan_A.patch | `2c1abfc3c607a991fb235f58495d5a51c7c3353e39a371f21d37030db9a0f5e5` |

本审未独立执行补丁/候选或编译，未借作者静态及私有回演结果冒充本Agent运行事实。后续正式接纳、build/pri/package登记、真实拥有者接线、ARM64与RK/真实LIS、响应/资源仍分别待Root安排；当前C7或第12运行程序没有被本审操作。
