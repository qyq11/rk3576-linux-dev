# LIS首片A：患者、任务计划和确认正文业务独审

结论：本次实际九源候选在原Windows业务对照范围内未见确定阻点，可供Root继续后续源码接纳与接线审查。新增四源把患者/孔/项目变化写成纯值计划，ACK单独生成纯正文；没有把Prepared当成真实等待、claim、内存应用、MAX、保存或发送许可。五份字段/查询源码精确继承，没有借此片改原解析规则。

本次只读候选、必要原Windows实现及来源记录，计算摘要和已有回演后像的字节比较，只新增本报告；未执行任何编解码、报文生成函数、产品、测试、SQL、补丁、编译、网络或板端动作，未改候选、参考工程、正式源码或公共文档。普通私有补丁成功属于作者已保存的执行记录，不是本审重跑或ARM64验证。

## 实际范围与固定输入

已先亲读并核实际摘要：Root范围 `lis_application_plan_scope_root_01.md` 为 `42217f354af542d6abcc8ac5a9700295bd64bedb7e8ebf6e129c7163a44ef40b`；新提示词为 `47375310ef515e9979c4b49b271837041d78e39df465d6e595af652056b693eb`；范围独审为 `278781a50aad5153b9af4862c107f554d69fbe4263f6fc86c444295095f378c1`。

候选根 `temp/sn100_host/lis_application_plan_candidate_20261104_01/` 的handoff起止为 `c599c0e5eb19bf6fc995b431ea766ddbf435eb53d5865a4e08447c7f52dd6c5d`，补丁为 `2c1abfc3c607a991fb235f58495d5a51c7c3353e39a371f21d37030db9a0f5e5`，九源清单为 `df2424cf41da2845da9d05ee0b0548f15b549ca9fd73e85fb8df6f4c82be2501`。九份源码起止均等清单，九份已保存ordinary_replay也逐字等candidate；补丁仅新增九源，正式九路径仍不存在。

`original_lis_query.h/.cpp`、`original_lis_download_types.h`、`original_lis_download_codec.h/.cpp` 五份均逐字等冻结 `lis_download_candidate_20261103_01/candidate/lis_business/` 原件。它们仍携带完整ReceivedMessage原配置/profile/generation/peer/body/text，不在本片改贴当前新身份；本报告不重复替旧字段解析作新运行验收。

| 本片四个新源 | 实际SHA-256 |
| --- | --- |
| `original_lis_application_plan.h` | `737a72866c5a2bad50fc806f329dc5606732a353a1025ddc12790e01b162cb6d` |
| `original_lis_application_plan.cpp` | `d5df434a041a4a848a0f9190b9ea4d69bdd4e64ad5708d64b6337cb0a7646186` |
| `original_lis_download_ack.h` | `ba63e4da333e7709ad69a7f70844dabb375e76603239ca175d2b73a4828b409f` |
| `original_lis_download_ack.cpp` | `2282798e4999002f342fcf2c72fcb3bb08b4e72e16e13b4ca9e6ad4870645719` |

`virtual_688_plus_9_manifest.json` 私下读取核对为697键，正好等冻结688表加九个新源，旧688值不变。它只是合成虚拟组合；正式package规则尚未枚举lis_business，不能冒称正式697或本轮12已编进它。

## 患者值、转换及日期

直接依据只读参考根 `06_programs/sn100_host_reference/Seerain450_PC/` 下 `commlis/liscommreceiver.cpp:224–269`，并核 `tool/database/report/report_table.h:23–48` 和 `tool/database/database_com.h:23–28`。候选Plan.cpp `203–214` 的十个实际采用字段与原赋值一致：床号、姓名、性别、年龄、样本序号、患者条码、登记时间、备注、送检医生和科室。患者ID、病区/房、年龄单位、急诊/稀释/标本类型等原只解析未保存字段没有新增用途；其余报告字段仍须由真实拥有者保留。

- 原M/F/其它分别到Man=0、Woman=1、Unknown=-1；候选保持精确大小写比较及同值枚举。
- 原年龄是atoi；`originalAge:109–130`保留ASCII空白、可选符号、十进制前缀、NUL/首非数字结束、无数字为0，未统一改成Qt整串toInt或加临床范围。正负可表示边界分别2147483647/2147483648，超int32拒绝是已明列的本地安全差异，不把原未定义溢出猜成合法值或夹值。
- 原样本序号为Win32的toULong后落入ReportItemData.int32；`:133–138`用相同32位范围的toUInt，失败仍0，高半区明确按原补码落int32，不让ARM64 unsigned long扩大为64位，也没有用条码代替序号。
- 原登记日期直接无格式QDateTime::fromString；`:211`仍同一调用，非法/空串保无效日期，不补当前时间、ISO格式或别的默认值。
- 原OBR重复次数是整串toInt，失败/越界得0，再小于1改1；`:215–216`一致。它与年龄atoi不是同一种规则，DSP count也不参与建任务，更没有99上限。

Plan只复制oldHole并生成完整after，患者十字段整体应用标志始终为true；patientChanged另外表达值是否不同。任何失败返回新Rejected对象，没有泄露可误用的半患者计划。

## 名称、结果代码、资料名与空任务

原 `liscommreceiver.cpp:281–305` 名称命中直接结束项目外层；结果行lis_code命中仅结束内层，后面项目仍可覆盖。候选`:187–198/233–240`的名称首索引、结果代码末索引保留此结果，不从QMap索引遍历顺序重新选择。空名称/代码仍参与原比较；最终映射名为空才像原`:320–324`跳过，没有enabled预滤或拿基础project_lis_code作映射键。

原映射后还在`:336`按另一个名字查资料，`frmsystemmaterial.cpp:426–435`实际比较 `projectBase[project_en_name]` 首项，找不到返回默认空ProjectInformation；其结构在 `.h:25–36`没有自带稀释列表。候选分别保留name/informationName，`:245–256`按informationName再首查，找到才采用那一行项目身份/文件/版本及默认稀释；找不到仍保留空资料标志、空元数据和空默认比例，不拒绝、不擅自另选项目。默认比例按原`:351–359`取原顺序第一个toInt非零项，候选使用快照中已转换的defaultOption，未把文字true或启用状态换成新规则。

冻结codec的DSP结果仍按std::map的UTF8字节顺序；候选`:225–232`核原唯一升序而不替手工改写列表重排。不同DSP码映到同名项目只采用第一次，最终tasks按原QMap的QString项目名排序。每项只保留OBR重复次数标量与默认稀释，不展开可能很大的S_TestBase列表，不分配任务/报告号。

原孔属性赋值位于实际命中且未重复的项目循环 `:361–383`：仅MSH-16.contains("0")时类型改普通；针改MicroNeedle、容器改Tube。内部序号及独立条码原来又各自带“值不同才赋值”的if（370、377），因此候选`:259–270`相同条件下才置holeAssigned是真实原条件，不能误要求它们每次无条件赋值。针/容器/类型的Assigned与Changed也分别表达原是否赋值及实际是否变化。

全部项目未知时，患者仍更新，旧任务仍整体替换为空，类型/针/容器/内部号/孔条码保持旧值。最终孔任务状态必须变None：原`:402–403`无条件setHoleTask，`cussamplehole.cpp:107–113`再调用 `.h:500–533`先置None、非空Idle再Ready。候选`:283–295`完整表达这一点；没有把全未知改成Rejected、默认项目或所有孔状态不变。非空新任务均是原Idle重复，计划最终Ready；MAX与孔任务状态另用阶段描述，未混为一项。

## ACK的原构造、字段与时刻

亲读原 `liscommsetting.cpp:763–797`、`comHL7/Messages/ACK.hpp:22–67`、`Message.hpp:37–43/267–280` 和 `MSH.hpp:26–57`、`MSA.hpp:18–25`、`Segment.hpp:64–82/192–197/246–258`。原是空ACK()后显式添加局部MSH、MSA，不是ACK(request)交换双方字段的构造器。局部MSH在原内存应用→MAX之后取自己的本地毫秒时间；默认ACK成员或未使用timeNow不是最终正文时刻，也不是QRY五取时。

新ACK函数完全独立于Plan：只接受同配置profile、收到控制号及显式 `messageTimestampAfterApplication`。它不读时钟，不因Plan Prepared就生成ACK或签发许可；18位yyyyMMddHHmmss.zzz检查只证明输入形状，未来真实调用点仍须在应用和MAX之后取时。

Ack.cpp `33–39/56–65`精确保留：MSH-1/2的分隔符、MSH-3原sequence、7显式时间、9=ACK^Q03、10收到的控制号、11=P、12=2.3.1、16=0、18=UNICODE，其他原空字段直到20；MSA只有AA和同一收到控制号。没有请求字段交换、非空控制号新限制、猜标准字段、转义或通讯页ASCII转换。原Segment取到字段数组末尾，原Message逐段附CR；候选同样保留MSH-20尾空分隔符、两段各一个CR，不加LF或MLLP。末尾QString/UTF8往返对照原toStdString/fromStdString链，实际UTF8/UTF16封帧发送仍归已有传输。

## 保留的后续边界

新增四源只有纯值/容器操作，没有QObject、线程、IO、SQL、socket、发送、占号、内存仓库更新或全局配置读取。Plan仅核FieldsReady/Ready/成功DSR、同源profile/configuration/sequence/root和本地预算；这些公开值仍不能证明首匹配、R1、当前连接、目标修订、一次claim或保存资格。真实B/C拥有者继续负责这些检查与内存应用→MAX→ACK请求→整架完成→reserve事务的原顺序，不能用手工FieldsReady直接保存。

输入4096项目、16384代码/稀释项、4MiB逻辑内容，输出4096项/4MiB及ACK4MiB，均是Root批准的本地边界，不是原协议上限或RSS/RK资源通过。超界整体Rejected，不截断后报空任务成功；重复次数仅标量，后续真实下发容量仍由拥有者解决。本审不替代另一资源/失败边界审查或实际ARM64、RK和真实LIS验证。

完成的是本次九源的静态业务对照。正式构建登记、package范围、实际内存/报告/任务接线、生产QRY、ACK本地发送与对端确认均未在此片完成，也未计入本轮正在收证的第12批；急诊恢复待办和算法未扩项。
