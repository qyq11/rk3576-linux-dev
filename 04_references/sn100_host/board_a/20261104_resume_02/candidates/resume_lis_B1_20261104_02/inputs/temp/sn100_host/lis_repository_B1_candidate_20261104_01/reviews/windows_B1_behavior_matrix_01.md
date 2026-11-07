# LIS B1：原Windows行为与同仓库实现边界

日期：2026-11-04。只读核对人：独立子代理 `/root/origin_audit`。

B1的用途是让同一个原样本仓库能准确保存患者item、独立扫码条码、LIS阶段及A9已经算出的孔任务，并识别旧实例/旧架孔回调。本轮只形成行为矩阵，没有修改源码、运行产品/测试、执行SQL或模拟、编译、联网、发QRY/ACK、预留或联板；Windows证据全部为只读源码。

Root最新范围仅`samples/sample_types.h`、`original_sample_repository.h/.cpp`，并已裁定由B1同一个Repository持有专用原映射读取和内部A9调用，成功发布点可补纯修订记账，原规则不变。旧`inputs/handoff_B.md`描述完整B/C，所含Store/Session/Root/Page/History/build、预算建议及报告分组修正不构成本轮授权。急诊06冻结，不追该片资料或其他作者样本加号两源。

## 基准和点核

- B1的`before_source_manifest.json`为697份已接纳源表，SHA-256：`ed4a3eb3025ef0602cdb49d41a6094f35011eb68ac19cf9f36e4f75fb3f6e254`。本目录只稀疏保存B1三源，不把它称整树实体副本。
- `inputs/read_basis.json` SHA-256：`eb630dedb7757c0f53d831af89f98ad4647b973ef248e4f0080579b322a6a696`。其中所列正式A9九源实际SHA全部一致，0不匹配；所列参考源码SHA一致，0不匹配。只阅读本任务所需函数，没有把哈希核对称为全工程业务审查。
- 三份实际稀疏base与`before_target_manifest.json`逐SHA一致：sample_types.h `d93e30392e17ebacf45bcc9726ac142b500bdebdb2581acabd39b2e3fc470a46`；repository.h `cb9532bcc9f02324f3f5dc1ce493bbb57897a11a29894327a943bff03332fe65`；repository.cpp `fc01de85767f8f9429747063bb40a2aef969551024bc275ad032befd0dc3cf92`。

下文Windows路径相对`06_programs/sn100_host_reference/Seerain450_PC/`；A9路径相对正式`06_programs/sn100_host/lis_business/`。B1旧源码只读本目录base。

## 查询开始前后是两个实际阶段

| 触发和原函数 | 输入、顺序与实际变化 | B1可实现的同仓库动作 | 必须留后片 |
| --- | --- | --- | --- |
| `frmSampleMaterial::slotPCGetLisTasks:3973`找架 | 按架名找原对象；没有则addRack；RackTesting时报警并调用原发架函数后返回；RackFinished及以上先删再新建。 | 提供能识别仓库实例、真实架id/孔及修订的取旧值/修改原语；不将同名架视为同一实例。 | 架状态业务分支、自动重建和是否继续发送由B2/B3连接真实任务账；B1不能凭空创建已issued证明或绕过原editable/inFlight。 |
| 同函数4028至4112扫码初始化 | 只遍历holeCode中有对应孔且孔非Testing的项。4098起顺序为：孔sampleCode←本次code；lisCode←空；lisStatus←NONE；item←全新ReportItemData；startTime←本次当前时间。**没有清孔sampleID，也没有清整份旧任务/针/容器。** | 专用扫码初始化方法按目标实际更新这五项并推进本次修订，保留原未改字段。输入应来自同实例真实调用者，不能拿旧控件或当前报告选中项补患者。 | 主控Tt/扫码来源、Testing/重复历史检查及真实时间调用归后片。不能把该阶段直接当QRY已生成或已发送。 |
| `LisCommSetting::QRYMessage:734`筛选 | 仅lisStatus为NONE且孔非Testing才处理。741行对原无效码集合做大小写不敏感的完整项匹配，命中只置MAX并continue；不做T1和第二次item清空。 | 同仓库可准确表示NONE/MAX及阶段变化；不新增条码格式推断。 | 是否需要查询及选择哪些孔由真实查询流程确定，不能以“无查询”掩盖未接传输。 |
| 同函数747至757有效码 | T1 → 第二次当前时间 → 调genLISFromSeverInfo生成QRY及lisCode → 写lisCode → **再次整份item默认** → 放入待发列表。前一阶段与这里的时间可不同。 | 可提供“开始生成查询”和“绑定本次已生成控制号/清item”的分阶段修改原语，保留真实before→after修订。 | B1不生成QRY、不调用发送或自造控制号；由B2/B3按原顺序调用。不能改为入队/写完/ACK后才T1，也不能把两个item重置合成一个而丢失中间事实。 |

原无效集合在`public/systemcommon.cpp:93`为`{"None","none","empty","Empty","Null",""}`，查询处使用CaseInsensitive。不能擅自改为子串匹配，也不能加新的“无效”临床规则。

原重复历史查询中`slotPCGetLisTasks:4069`有实际`send=true`调试替代，不能只据上面注释重新增加“已完成就禁查”的业务。History/原架分支及其差异留B2/B3和Root裁定，B1不查询或改历史库。

## ReportItemData完整默认和未改字段

原`tool/database/report/report_table.h:23`使用默认构造，继承`table_dal.h:17`的id。B1完整纯值item应保留以下原字段名和值；它是孔的报告患者暂存值，不产生持久ID。

| 字段 | 原默认 |
| --- | --- |
| `id`、`row_id`、`sn`、`statue`、`age` | int32的0 |
| `type` | `ReportType::kReportData`，实际数值0；kQualityData=1，kCalibrationData=2 |
| `uploaded`、`is_auto_id`、`visible` | false |
| `date_time` | 默认QDateTime，无效值；不补当前时间 |
| `sex` | `SexType::kUnknown=-1`；Man=0，Woman=1 |
| `sample_hole`、`sample_id`、`name`、`department`、`patient_type`、`application_serial`、`bed_number`、`jinayan`、`checker`、`auditor`、`remark`、`state` | 空串；原拼写是`jinayan`，不能改为另一个数据库字段 |

`liscommreceiver.cpp:224`至269的DSR患者赋值只写床号、姓名、性别、年龄、sn、患者条码sample_id、无格式fromString的date_time、remark、checker、department。其他item字段应取真实旧item原值；仅原扫码/生成查询的整份重置阶段才回默认。不能在每次DSR应用时把未改字段重置，不能从登录人补checker/auditor，不能从当前报告选中行补id/row_id。

年龄、sn、重复次数及时间转换直接复用已接纳A9计划的结果，不再造第二套转换。A9中无效登记时间可继续为无效值；“初始化患者时间”和孔的LIS startTime是不同字段。

## 孔的默认、患者item和孔号/条码分离

原`customwidgets/cussamplehole.h:475`构造设置isAutoId=true、sampleCode空、任务状态S_None、MicroNeedle、MicroCup、TypeSample、revise=true，再按defaultContainerisTube选择Tube/MicroCup。针枚举FullNeedle=0、MicroNeedle=1；容器Tube=0、MicroCup=1。不能只抄成员初始化的bottleType=Tube而忽略构造函数覆盖。

孔还在605至609行独立持有lisCode空、LIS_NONE、默认item、创建时当前startTime、intervalMsecs=60000。`LIS_Status`实际序号为NONE=0、T1=1、R1=2、T2=3、MAX=4；这不是孔任务None/Ready/Testing/Finished/Error状态，不能混用。item.is_auto_id缺省false与孔isAutoId缺省true也不能合并。

| 原值/A9值 | B1来源和赋值边界 |
| --- | --- |
| `ReportItemData.sn` ↔ `A9.after.patient.sampleNumber` | DSR患者赋值阶段无论最终项目是否命中都更新。不要由旧孔内部号推患者旧sn。 |
| `ReportItemData.sample_id` ↔ `A9.after.patient.barcode` | 患者条码在DSR患者阶段更新；可能与孔扫码码暂时不同。 |
| `S_SampleHoleInfo.sampleID` ↔ `A9.after.internalSampleNumber` | 孔内部样本序号，仅原命中且未重复的项目循环内按返回sn改。现Linux的SampleHole.sampleId应作为这类内部号保留，不能再同时承担扫码码/患者号。 |
| `S_SampleHoleInfo.sampleCode` ↔ `A9.after.barcode` | 独立孔扫码码；扫码阶段取真实code，DSR只有实际命中项目时才按返回患者条码更新。 |
| `sampleType/needleType/bottleType` | 按A9的holeAssigned原样应用，不根据患者item“有值”就归零。原命中循环在MSH自定义标记contains("0")时改TypeSample，针改MicroNeedle，容器改Tube。 |
| LIS状态/控制号/起时 | 存在同一孔内，查询、R1认领、应用后MAX及超时是不同事件；纯唤醒tick不是修改孔任务。 |

A9的`OriginalLisHoleValue`须由本次同仓库真实旧孔投影：root/实例/架/孔、完整item中的患者子集、独立内部号/条码、实际类型/针/容器和任务状态。不能只构造默认患者子集充当旧item，也不能把A9默认容器/针当旧来源。

## DSR应用和未知项目矩阵

| 原事实及A9位置 | B1可做 | B2/B3/Root边界 |
| --- | --- | --- |
| 原receiver按MSH-10/架孔顺序首匹配，MAX拒绝、R1重复拒绝；原合法枚举中NONE/T1/T2均可先R1，随后解析/错误可能留R1。 | 提供基于真实实例/目标修订的旧状态读取及明确状态转换；control可为空，原header/该认领处没有非空硬校验，B1不新增非空限制；失败不先写半个患者。 | 第一匹配、profile/连接/peer/真实等待认领归B2/B3；不能在B1自猜必须T1才能R1，也不能把外部Prepared当授权。 |
| 原224至269先赋患者；309起按项目映射，未知项目跳过。A9 plan的tasks保原QString项目名顺序，保存projectName、projectInformationFound、原file/version、id及原repeat标量。 | 专用一次应用入口保留这些值，包括found=false、id0、空rate；不用saveRack的手工enabled/repeats≤99/稀释启用校验替代LIS原规则。 | 真实下发仍须由原项目/稀释/完整参数/试剂/编码守卫决定；B1不降低它们，也不生成设备任务号。 |
| 所有项目未知时，原setHoleTask仍整体替换成空任务，孔None；患者已经更新；孔内部号/扫码码/类型/针/容器沿旧值。A9 application_plan.cpp:241、278至293已明确。 | 必须原子替换空任务，不能保留旧任务，不能把空计划当“无需应用”；按A9 holeAssigned/after保孔旧字段。 | 空任务如何回应设备、全Skipped/报告row等仍待Root裁定，不在B1发Tt0或reserve。 |
| 原402/403改孔资料并setHoleTask后，411置MAX，415才构造/发ACK信号。A9 describeMaxAfterMemory/describeAckRequestAfterMax只是说明。 | 实际同仓库应用成功后可记录真实MAX及本次应用/after修订事实，给后片返回必要值；失败不得提前MAX。 | ACK构造/时间/广播入队/对端结果均不属B1；也不能把内存应用或MAX称报告数据库已保存。 |

重复次数按已接纳A9原OBR-9结果保留，不截99、不截断，也不因id0/空rate擅自改选项目。本轮不新增预算、测试次数或临床上限。B1取旧/校验/一次应用应保标量，不为保存而调用会按repeats展开的sampleTaskStates/sampleHoleTaskStates；实际物化/迭代预算和真实Session消费留Root及B2/B3裁定。保留原inFlight/Testing保护含义，不用清inFlight来让LIS“可改”。

## A9映射源不能由现Linux缓存默认替代

已核Windows `frmsystemmaterial.cpp:2135`的loadProject确实令project_name等于projectBase[project_en_name]；但receiver先按project_name/项目结果lis_code映射，再在`getProjectInformation:426`按base project_en_name首查，仍是两个实际步骤。A9保留name和informationName分别输入，不能因常见值相等而删一步或丢失未找到资料的结果。

原projectResult和project_sample_rate在loadProject:2128至2129调用SqlBase::getData，`customtools/sqlbase.cpp:774`是无ORDER BY的SELECT *；项目总表还另有大小写敏感formula排除、xuhao排序及sendSequence稳定顺序。现Linux项目缓存虽name取project_en_name，但步骤/比例/结果已有ORDER BY，不能默称它就是原A9有序映射输入。

Root追加裁定：**专用原映射源由本B1 Repository作者持有。**在同一仓库中读取同次原INI及全项目原始表，按原formula排除、xuhao/sendSequence项目顺序和无ORDER BY的结果/比例读取规则准备映射，内部调用已接纳A9生成计划，再在原仓库应用。不能直接采用现Linux缓存排序，不能接受外部传来的Prepared作为保存依据，也不修改A9九源或其他任务缓存顺序。不新建患者/任务库；A9给出的命名、是否找到资料、file/version、id0/空rate和重复标量仍完整保留。

## 实例和架孔修订：要识别什么，不能混入什么

| 仓库实际事件 | B1应保留的身份/修订合同 |
| --- | --- |
| initialize重建同根、同架名/id | 旧仓库实例失效；新实例不可仅用root＋id冒充，实例号不得回绕复用。成功新实例与实际snapshot对应，失败不能给旧缓存贴新证明。 |
| saveRack改孔/任务/患者；新增架；删除/clearOrdinary；重建同名架 | 实际目标修订随修改推进，删除后旧票据失效；读取最新版本不能为旧请求续签。需要覆盖实际会改snapshot的路径，不只是LIS专用方法。 |
| updateDeviceTask、setRackInFlight及LIS扫码/查询阶段/患者任务应用 | 实际状态/占用或孔输入变化可被旧票据识别；查询自身产生的before→after由同次操作明确返回，不能要求后续仍等于查询前全部字节，也不能拿任意最新号冒认自身变化。 |
| 项目资料变化 | 除实例/架孔修订，还须核本B1实际读取的同次INI/项目版本；原计划不能在换项目后继续应用。专用映射源按上节Root裁定由本Repo持有。 |
| `updateRackTimes:415`的TB剩余秒数/remainingAtMs | 仅显示进度，**不推进仓库/目标任务输入身份或使旧LIS输入失效**；不纳入A9旧孔、患者/任务摘要或实例/架孔修订。纯load、显示刷新、唤醒tick也不凭时间跳变制造新身份。 |

B1可形成这些同仓库身份和一次应用原语；唯一真实DevicePacket等待、一次归还、账户/配置/连接/关闭取消、原payload/basis保持及现User摘要接线留B2/B3。本片不新增Grant、不会永久false伪装已接通，也不把修订号当仪器无旧任务证明。

## 本片可交与后片必须留下的事项

B1可交：完整纯值item/独立孔码与针/LIS状态；真实旧孔投影；分阶段扫码/查询状态修改；专用原有序映射读取及内部A9调用；实例/架孔修订及一次内存应用；空任务和未知资料原语义。成功发布点补修订记账不得改变原业务规则。交付须写清只是内存结果，不是持久化/ACK/仪器执行。

B2/B3及Root必须另接：真实Tt等待/查询/回复认领；与本B1原配置/映射来源相接的真实时间和消息来源；患者列进入原reserve及报告持久化；元类型、构建和GUI采用；真实QRY/ACK/TX及收尾取消。原Simple/非Simple的报告分组、row分配/覆盖以及全部Skipped行为仍待裁定，B1不修其碰撞、不重排任务、不新增业务上限。

原报告生成函数会在实际预留时再改row_id/type/statue/is_auto_id/sample_hole/date_time/visible/state等，其中Simple与非Simple已有差别；不能把这些后阶段赋值提前塞进B1患者应用或清item默认。本矩阵不批准任何生产LIS查询或绕过真实任务保护。

## Root补充要求的任务替换与revise核对

`customwidgets/cussamplehole.cpp:107`的setHoleTask直接将整份S_HoleTask赋给旧孔，再调用S_SampleHoleInfo::setStateFromTaskState并updateView；后者只发状态变化信号/重绘。`cussamplehole.h:500`起的状态归并只写holeState，不写revise。**DSR应用不能因任务变Ready/None而自动把revise重置true或false；应保留真实旧孔revise。**

原S_HoleTask在231至234行只有sampleTest和calibrationTest两份任务集合；QC使用sampleTest，另由孔sampleType等字段区分，并无可保留的第三份qualityTest。DSR在receiver:277构造全新S_HoleTask，只往新的sampleTest添加本次命中项目，402/403整体替换。因此旧普通/QC sampleTest及旧calibrationTest都被替换；本次全未知项目时两份集合均为空。B1不能把旧CAL/QC执行任务合并回新普通tasks。

同时原holeinfo取自真实旧孔，再由命中项目循环按条件赋sampleType/needleType/bottleType/sampleID/sampleCode；没写的holeNumber、revise、isAutoId、controlName/controlBatch等仍沿旧值。MSH标记未contains("0")时，孔sampleType可保留CAL/QC，即使可执行任务已整体换成新的sampleTest；全未知时类型/针/容器/内部号/扫码码更都保旧。B1按A9的after及holeAssigned应用，不因保留CAL/QC类型就保旧任务，也不因replaceAllTasks就清所有孔级元数据。Linux独立quality/calibration等孔级资料若不在本次赋值计划中应保留；旧SampleTask中随任务存在的执行元数据则随整份任务替换，不借保留资料重新产生旧任务。

原状态归并先None，非空任务先Ready，再依Testing→Idle→Error→Finished优先检查；A9新任务均Idle，所以非空Ready、空None成立。该函数不另限制repeat为99；B1可按计划的标量结果形成同样状态，不为记账展开所有重复。

markReceiving保留原NONE/T1/T2进入R1、R1/MAX拒绝和允许空control的规则；真正MAX仍在本次同仓库实际apply成功后才设。修订只在实际成功发布点记账，不能代替真实状态变化或提前生成ACK。
