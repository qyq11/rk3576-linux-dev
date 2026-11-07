# LIS B1：查询初始化、患者与孔任务的原版对照补充

本补充供原任务作者实现B1时逐项核源码。B1只在现有样本仓库补患者、独立条码、LIS阶段及修订记录，并在完整应用计划生成后一次更新内存；本窗口只写这份说明，不成为第二代码作者。

总控限定候选为 `samples/sample_types.h`、`samples/original_sample_repository.h/.cpp` 三份源；不改手工saveRack业务和新孔默认容器，只允许成功发布后的纯修订记账；不接Store/Session/页面、SQL报告预留、真实Tt或发送。Tt是主控取任务请求；QRY是LIS查询，DSR是下载回复，ACK是接收确认。A9是已接纳的九份纯数据源码，Prepared只表示值计划生成成功，未证明真实接收、内存应用或保存。

读取基准为实际697源表 `temp/sn100_host/workflow_next_20261103/integration_13_source_acceptance_01/lis_a/source_after.json`，SHA-256 `ed4a3eb3025ef0602cdb49d41a6094f35011eb68ac19cf9f36e4f75fb3f6e254`。本次三目标源和A9相关源均按该表核字节；开始实现仍由总控确认前像。旧B文稿及A9保持冻结。本次补充服从总控最新要求：不把旧B的预算建议带入B1，不新增数量上限、截断或动作限制。

以下Windows路径相对 `06_programs/sn100_host_reference/Seerain450_PC/`，正式路径相对 `06_programs/sn100_host/`。行号只对应本次已读字节。

**两次查询初始化必须分开，任务并不在这里清空。**

| 原入口与条件 | 原实际顺序和保留值 | B1核对点 |
| --- | --- | --- |
| Windows `frmsample/frmsamplematerial.cpp::slotPCGetLisTasks():4028–4036、4098–4102`；本次扫码列表含该孔，且孔非Testing（测试中） | 设孔扫码条码 → 清lisCode → LIS_NONE → 整份ReportItemData默认值 → 当前startTime。内部sampleID、旧任务、类型、针和容器均未在这五句改动；4105–4109补内部号的代码已注释。 | 提供这一阶段的真实仓库值更新，不把清患者改成清任务，也不因条码无效而跳过这次已发生的初始化。 |
| Windows `commlis/liscommsetting.cpp::QRYMessage():732–756`；遍历整架，LIS_NONE且孔非Testing | 按孔扫码条码判断。有效时：LIS_T1 → 再取当前startTime → 生成QRY及控制号 → 保存控制号 → 再整份清患者item。 | 第二阶段扫描范围不等于第一次扫码列表；时间取于生成前，不在排队、重连或唤醒时续期。B1只提供值操作，不在仓库新增发送者。 |
| 同函数 `:741–745`，条码无效 | 仅设LIS_MAX并continue，没有第二次清item、T1、新起时或新控制号，也不删除旧任务。 | 无效码按原整串、不区分大小写比较，不trim、不按子串判定。原列表见 `public/systemcommon.cpp:93`：None/none/empty/Empty/Null/空。 |

第一阶段不额外要求旧LIS状态必须NONE；它本来就是新查询的初始化。第二阶段才要求NONE。不要把任一阶段挪到重复DSR或应用失败后，造成旧患者/控制号被重新清除。无效码不查询，也不等于“成功收到空任务”。

选架/发报仍待后续接线：原 `slotPCGetLisTasks():3978–4019`按原架顺序查找/创建，测试中的架走原下发后返回，已结束架删除重建；不是只找已有Ready任务。前段重复条码检查虽有条件，4069实际强制send=true，不能据注释恢复新的禁止查询规则。B1不自行接管这些主程序动作。QRY控制号由 `liscommsetting.cpp::genLISFromSeverInfo():669–672`生成，使用孔号和原时间值；701查询内容用孔条码，不是孔内样本号。

**患者、孔内样本号、孔条码是不同的值。**

Windows成功DSR在 `commlis/liscommreceiver.cpp:224–269`直接写患者item；随后映射项目，最后402–403分别设置孔信息和整份任务。B1使用冻结A9的结果，不重新解析、映射或转换。

| B1需要保留的来源/含义 | A9及原版依据 |
| --- | --- |
| 患者床号、姓名、性别、年龄、样本号、患者条码、登记时间、备注、送检医生、科室 | `OriginalLisApplicationPlan.after.patient`；正式 `lis_business/original_lis_application_plan.cpp:203–214`。原性别M→0、F→1、其他→-1；年龄保原atoi的可表示十进制前缀转换，超出32位整数范围时A9拒绝，属已知安全差异；样本号保Win32转换。登记时间可以无效，不补当前时间。checker是送检医生，不能填作jianyan（检验医生）。 |
| 孔内部sampleId与独立扫码条码 | 对应 `after.internalSampleNumber`、`after.barcode`，不能用patient中的号/码无条件覆盖。原 `liscommreceiver.cpp:370–382`只在有效且不重复的映射项目循环内、值不同时修改。 |
| 孔类型、针、容器及任务状态 | 按A9的 `holeAssigned` 和 `after`；实际命中循环中MSH-16含0才普通类型，针为微量1、容器为管0。原全未知时类型、针、容器保旧；孔任务状态按整份替换后的任务计算，空任务为None，不能保旧Ready。现有新孔默认容器配置不改。 |
| 项目任务 | 按A9 `tasks` 的QString项目名顺序保留projectName、projectId、projectInformationFound、originalFile/Fingerprint、defaultRate和repeats标量。原项目代码未知会忽略；已匹配项目名但基础资料查不到仍建任务，保infoFound=false/id0/空rate，不能混为未知项目而过滤。 |
| 现有非本次赋值字段 | 从实际旧孔副本保留revise、quality、calibration等；不新建默认SampleHole后顺手清空。患者patient_type/application_serial/jianyan/auditor等原接收器未赋字段沿真实旧item或两次初始化的默认值，不从账户、当前选中报告或按钮名补造。 |

A9已处理名称首匹配、无名称时结果代码末匹配、基础资料名另首查和第一默认稀释项，见正式 `original_lis_application_plan.cpp:187–197、227–282`。原映射不用基础project_lis_code作替代，不预滤enabled；次数来自OBR-9整串toInt后小于1取1，不用DSP count、不截99。任务可以保压缩次数，不能为B1内存更新提前展开全部状态。

**映射必须读原版顺序，不能直接采用现编辑缓存。**

总控已指定由原Task作者在同一Repo、工作线程及连接内，唯一实现专用的原映射只读快照；本窗口不另写读取器，不修改当前项目编辑缓存，也不向作者提供无法核实的外部Prepared。原“raw步骤”在这里指未经编辑缓存重排的原始表行。原链及顺序依据如下：

| 原读取位置 | 查询、容器及真正影响映射的顺序 |
| --- | --- |
| Windows `frmsystem/frmsystemmaterial.h:23`；cpp `loadAllProject():689–715` | 原根下project/，QDir筛*.db、Files/NoSymLinks，文件名含小写formula则跳过；逐文件loadProject后放QList。715用非稳定的std::sort按projectBase[xuhao].toInt升序，无同号次序规则，不能声称同号时保目录顺序或全链稳定，也不能新增按名字/id/文件名去重。 |
| 同cpp `:716–741`；Windows `main.cpp:163–164、182` | config.ini以UTF-8读取sendConfig/sendSequence，默认空QStringList。非空时，对上述项目列表stable_sort：项目名以列表中首个前缀开头者优先，前缀比较不分大小写，未匹配为INT_MAX；同优先级保前一次排序结果。这不是config_Debug.ini的SendTaskByRowID，也不是报告横/纵设置。 |
| Windows `frmsystemmaterial.cpp::loadProject():2092–2138` → `customtools/sqlbase.cpp::getData():762–807` | 对project_base、AlgParam、project_reagent、project_results、project_sample_rate、project_mda均调用不带where/order参数的getData。实际SQL为select * from表名，按query.next依次append进QList；这些调用没有ORDER BY，不能把源代码未保证的顺序说成固定rowid/xuhao/result_id顺序。行内为QHash，但映射靠指定字段取值，不靠QHash遍历。 |
| 同loadProject `:2104–2115、2128–2135` | 基础name/value逐行insert，同名基础键后行覆盖；project_name在2135取projectBase[project_en_name]。结果取每行lis_code，稀释取sample_default_option.toInt和sample_rate；保原表行顺序。不能用project_base.project_lis_code代替结果lis_code，也不把默认项先按xuhao重排或限制为enabled。 |
| Windows `liscommreceiver.cpp:280–305、336、351–359`；`frmsystemmaterial.cpp:399–402、426–435` | getAllProjectInformation原样返回完整排序后QList。匹配项目名立即结束外层；匹配结果代码只结束内层，后面的项目仍可覆盖；随后getProjectInformation另按projectBase[project_en_name]首查。默认稀释取原列表首个default项，不是编辑缓存排过序后的首项。 |

当前正式编辑读取确有不同用途：`projects/original_project_repository.cpp::readProjectData():288、302、304`分别给稀释/试剂加ORDER BY xuhao,rowid、给结果加ORDER BY result_id,rowid；`:264–273、294–297、314–323`还有严格字段校验，默认项仅接受0/1，而原toInt后的非零值均可选为默认；`readProjects():700–706`拒重复编号/名称并按id排序，未承接上述sendSequence。原映射快照需保完整来源/字段和实际原排序步骤，不能修改这套编辑行为来适配LIS。没有ORDER BY及非稳定同号排序带来的平台/资料顺序风险应明列给总控，不能偷偷补新排序或宣称已在真实资料验证等价。

现正式 `OriginalProjectRepository::load():749`还会调用recoverPending（处理未完成复制），并读单位和试剂目录，不能把整个load当成没有其他动作的原映射读取器。INI来源可对照正式 `reagents/original_reagent_editor.cpp::orderedOriginalProjects():136–157`：同初始化source的normal()内解析sendConfig/sendSequence并按原QStringList处理；这里只对照来源和前缀排序，不复用探测/余量读取器自带的数量或业务门槛，也不把新配置补给旧请求。

**全未知项目仍是一次真实的空任务替换。**

原 `liscommreceiver.cpp:321–330`忽略未知或重复映射，但402–403始终整份设置新holeTask。`customwidgets/cussamplehole.cpp::setHoleTask():107–113`替换后重新计算孔状态，`cussamplehole.h:500–518`先设None，有新Idle任务才Ready；403旁边的“会Ready”注释不覆盖空任务的实际None。

因此成功DSR没有可用项目时，B1仍应用患者值、整份清空旧任务、最终孔任务状态None；内部号、孔条码、类型、针和容器依A9保旧。不能因 `tasks.isEmpty()` 返回而留下旧任务，也不能保旧任务却报成功。此处的孔任务None和LIS结束状态MAX是两个状态，不可合并。

**一次应用的失败边界，不是把整条原流程说成原子操作。**

Windows先按MSH-10控制号找首个孔（`frmsamplematerial.cpp::slotSendQRYtoSample():4608–4634`，按架/孔原顺序，含旧MAX/R1）；接收器再判MAX/R1并返回，其他状态先在 `liscommreceiver.cpp:163`置R1。非OK在186置MAX、无患者任务应用及成功ACK，其中只有NF在177–183报警；OK少于五段在189–194返回，保留已发生R1。原版并未额外限定只有T1能进入R1。上述选择和接收仍由后续真实链提供，B1不成为第二DSR消费者。

原R1、患者逐字段写入、孔信息、任务替换、MAX、ACK分别在163、224–269、402、403、411、415发生，没有整体事务或回滚。B1“先检查完整候选、再一次替换目标内存”是本仓库明确采用的安全写法，应保持原可接受输入的阶段和最终值，不能倒称原版已整体原子。

具体核查边界如下：

- 从同仓库实际旧孔构造A9输入，核实际根、仓库/架实例、目标孔及预期修订，再准备完整新孔；保留旧孔未赋字段。所有可失败的核验在替换前完成，不先写一半患者或清旧任务。
- A9为Rejected时，不读取其默认after当有效值，也不把失败改成“全未知成功”。本次应用不写入、不给MAX成功事实或ACK；早已完成的查询初始化/R1仍如实保留，不自动回滚或重新起时。
- Prepared只给纯值，不证明真实消息、首匹配、连接/对端或等待仍有效。B1从同Repo真实旧孔及专用原映射快照调用A9，不以外部Prepared或编辑缓存代替；真正route/epoch/Tt/ACK、唯一继续及取消关闭等待后续合同，不造新的保存许可或假成功占位。
- 修订结果应明确原根、仓库/架实例、孔及before→after，区分实际数据更新与阶段变化。现 `OriginalSampleRepository::initialize():62–77`会重用架id起点；同名同id不能充作同一实例。自身初始化/R1/应用/MAX的合法修订要可衔接，不能改拿最新值给旧请求续签；删除重建或外部编辑要能识别。TB剩余时间和纯显示刷新不进入任务内容身份。
- 不通过手工 `saveRack()`存LIS计划：正式Repo `:149、157–160`的enabled、1–99和稀释启用规则是现手工规则，B1既不把它们新增到DSR，也不改手工校验来迁就LIS。总控仅允许saveRack成功发布后的纯修订记账，校验、默认、保存数据和返回结果不变；真实外部变化及删除重建也须纳入，防止旧身份误认新对象。保留现有在途/Testing保护，不用清在途标志解除保护。候选只更新同一snapshot，不另建任务或患者库。

**资源和后续边界。**

本补充不新增测试次数、总文字量或其他拒绝门槛，不截项目/重复数，也不限制原Windows按钮动作。A9冻结代码现有的资源拒绝是实现事实，不是Windows业务数量规则；若遇到，只保留真实Rejected及原因交总控，不截短重做或伪造空任务成功。现 `sample_types.h:45–64`及Repo editable `:125`会展开重复状态，资源/响应问题另列待核，不把“标量计划成功”说成物化或性能通过。

B1结束只证明候选具备同仓库内存字段和修订能力。患者随Task进入原报告事务、Simple/非Simple/旧row及跳过分组是B2；真实收发、首匹配与等待寿命是B3/公共接线。两者不在本补充实施范围，尤其不统一按“孔＋重复序号”宣称原版完全一致。本轮仅只读源码及文稿核对，没有编译、SQL、产品运行、联板、LIS通信或下载保存通过结论。
