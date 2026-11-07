2026-11-04，LIS B1第三轮独立候选。用途是让医院系统返回的患者资料和项目在**同一个原样本仓库**完整更新，并保留患者编号、孔内部编号和扫码条码的区别。本片只有值和仓库接口，尚未接真实等待、报告持久化或通信，不能称LIS下载流程完成。

修改精确限`samples/sample_types.h`、`samples/original_sample_repository.h/.cpp`三份；没有新编译单元。三份稀疏前像在任务目录`base`，本轮候选在`candidate/06_programs/sn100_host`，冻结镜像在`review_03_source/06_programs/sn100_host`。前像分别为d93e3039…/cb9532bc…/fc01de85…，完整值见`../before_target_manifest.json`。没有改正式、Store、Session、Root、页面、History、build或样本加号作者文件。急诊06及其全部原件继续冻结。

| 固定产物 | SHA-256 |
| --- | --- |
| 三源后表`review_target_manifest_03.json` | `c881d1748ecd0f3b711a670cbf35e9cace8164ce651cceccc4e729f4d862f202` |
| 累积三源补丁`packaging_01/lis_repository_B1_on_ed4_targets.patch` | `daa6917f22cc68c35d999f62e3c747727a5e6733843c7bd80188d300b47fbb57` |
| 业务/来源独审`reviews/B1_origin_review_03.md` | `1f749bb0c3e4b0fee06ddc642fcaa87721a5d74c62823b3565c709941649c0c5` |
| 生命周期独审`reviews/B1_lifecycle_review_03.md` | `94909acbe2aa997d7fa860c32b3a16babdc9dd9ad0da98118d366760714c7572` |

原行为矩阵`../reviews/windows_B1_behavior_matrix_01.md`为1bb74152c5009f8024ec7781488d87e7848aa3909f4def584eda1ec847c4b1af；原LIS作者补充输入`../inputs/field_field_crosscheck.md`为6ea695af576f9f5e2cfc8ee5b4f6464b45ccc7a8f61926f8ab0216c44ad780e9。Windows证据全部是只读源码，没有Windows实际运行或重新构建；没有使用参考配置连接仪器。

| 修改文件与接口 | 原Windows依据及行为 |
| --- | --- |
| `sample_types.h`：完整SamplePatientItem、独立sampleBarcode/needle及LIS字段、SampleTask来源元数据 | 对照ReportItemData全部默认和未赋字段；患者sn/sample_id与孔sampleID/sampleCode分存，针默认MicroNeedle=1。原容器规则不改。任务保项目名/found/file/fingerprint/id0/空rate和原重复标量，不夹99、不新增数量上限。 |
| `original_sample_repository.h`：目标、私有Prepared和结果接口 | 目标绑定真实仓库UUID/成功初始化序号、架孔修订、项目修订、任务/查询/项目来源摘要。Prepared只能由本Repo内部构造；禁止仓库对象复制、赋值和移动，外层unique_ptr仍可转交同一对象指针。结果kind区分初始化、R1和患者任务应用，不把MAX都当成功下载。 |
| `original_sample_repository.cpp`：取旧及两阶段queryinit | 原`frmSampleMaterial::slotPCGetLisTasks:4098–4102`先扫码码、清control、NONE、全item默认及起时，保内部号/旧任务。原`LisCommSetting::QRYMessage:732–756`再T1/第二起时→生成QRY/control→绑定control/再次item默认。B1将后一阶段分成begin与bind两个值操作，由后片在两者之间生成原QRY；B1不生成或发送报文。 |
| 同cpp：begin、R1与prepare/apply | 原无效码None/none/empty/Empty/Null/空串完整项CaseInsensitive只置MAX；不trim或加新规则。原receiver163先R1，非R1/MAX阶段均可，control可空。A9内部从真旧孔和专用原映射准备完整新孔，按holeAssigned赋孔字段，整份换任务；全未知也更新患者、清旧任务为None，然后实际发布MAX。 |
| 同cpp：专用原映射 | 原loadAllProject/SqlBase::getData/receiver映射链：*.db/Files/NoSymLinks、敏感formula过滤、基础同名键后行覆盖；literal select *、query.next行序；原非稳定xuhao.toInt排序，再同次config.ini/sendSequence前缀CaseInsensitive稳定排序。没有新同号次序、去重、enabled过滤或默认项0/1限制。每文件与整批/INI读前后核版本，不调用含recoverPending的编辑load或重排其缓存。 |

原receiver先写患者再改孔/任务、MAX和ACK，并非整体事务。本片按Root批准的写法，在真实旧值仍对应时完整准备，最终在同worker一次交换snapshot和修订；普通校验失败没有半个患者、半份任务或提前MAX。已真实发生的扫码初始化/R1不因后续失败自动回滚。源码接口能够更新同一内存，并非假成功占位；本轮没有执行这些方法来预填运行通过。

核心调用链为`captureOriginalLisHole`→`initializeOriginalLisScan`→`beginOriginalLisQuery`→后片原QRY生成→`bindOriginalLisQuery`；收到真实消息的首匹配/归属由B3核后，再`markOriginalLisReceiving`→`prepareOriginalLisApplication`→`applyOriginalLisApplication`。Prepared或memoryApplied都不证明消息真实或写入获准，患者成功应用应看结果kind=PatientTasksApplied及其实际before→after。没有构造ACK、占报告/任务号、扣量或接真实Tt。

`../scope_and_identity.md`列完整修改点及摘要字段。真实saveRack发布、新增/删除/清架、占用变更、设备任务状态变化、项目缓存实际改变和LIS自身修改均有修订；失败不虚记架孔发布，组合/TB/纯显示不推进。默认容器代码不变，原手工校验/返回与副作用保持；八项源码比较核了原规则体，saveRack去纯记账两行后原体一致。新患者/条码/项目身份只进入本Repo目标摘要，现共享User/任务序列化还未接新字段，不能偷称全局旧请求保护已闭。

修订历史全部保留：首稿生命周期P2是仓库对象可复制，可能克隆UUID及修订；第二轮禁复制/移动关闭，第三轮只把新增条码/control默认和扫码control重置对齐原显式空串，真实输入仍原样赋值。当前实际父版差异为两源，repository.h与第二轮相同；`freeze_result_03.json`中的parent_delta=1沿用了旧摘要数字，原件未改，实际清单和`freeze_result_03_checked.json`明确为2。不能拿错误摘要替代实际差异。

私有普通Git的init、check-attr、apply --check、apply实际退出码0/0/0/0。重放三份源码逐SHA一致，0不匹配，保留原CRLF；生成的`replayed_target_manifest.json`因JSON排版与PowerShell后表不同，表文件SHA为4e32d13283aeefd61ab065a140aa0e9e865eb95da754babf7e970721ffe12717，但解析后的三项完全相同，不把表序列化字节差异说成源码不同。原阶段输出见`packaging_01/result.json`及各步日志。

虚拟697预期表`review_source_manifest_03.json`为3b7c16130247479756b6228bfe3c5b82629298d4d6f745a0621e37acac078d91，只是原ed4a3eb3025ef0602cdb49d41a6094f35011eb68ac19cf9f36e4f75fb3f6e254表的694项元数据加三源覆盖，**没有实体读取/重放694份，也不是当前正式两页面更新后的后像**。打包时只读点核当前正式三目标仍等原前像；Root采用后须从当时真实工程重新枚举，不用这个旧虚拟表替换现正式来源账。本片不纳当前14批构建。

资源风险和建议另见`resource_and_followup.md`：新路径不物化repeat，不代表现手工/Session/页面物化安全；反复核原项目来源有额外成本，未测响应或RSS。没有为风险加新业务预算、截断或数量门槛，也没有复用编辑缓存伪装原映射。

Root后续接口仍需处理：共享User/任务摘要与新值衔接；同一等待各孔按已记录自身before→after链衔接架修订；Store/Session/profile/peer/route/epoch/原首匹配、负回复/超时、取消关闭及真实一次继续；元类型、全量重新构建及页面采用；患者随Task进原reserve。孔级isAutoId仍未建模，不能用item.isAutoId=false代替；报告时间、Simple/非Simple、旧row/跳过分组留B2裁定，不按二元键统一或按OS切协议。真实QRY/ACK与仪器继续B3/Root统一安排。

验证范围仅源码/字节/限定独审和三源私有补丁重放。未全编译或局部编译，未运行产品/测试、SQL、模拟、网络、板卡、Windows原程序或占共享显示，未部署、清理、提交推送。最终交接、清单和报告SHA见`output_manifest.json`；B1可供整合审查，真实业务与运行结果仍待。
