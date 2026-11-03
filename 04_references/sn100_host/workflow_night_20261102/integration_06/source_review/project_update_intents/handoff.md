# 校准明确保存后的单项目同步：候选交接

本片只准备temp候选，基于固定重连v2的overlay，相对窄补丁交总窗口rebase。正式05正在另行晋级的avg/theme/reconnectv2/groupv2不覆盖、不重置；没有修改正式源码、main、构建或计划，没有运行产品、数据库、模拟端、网络、UI、RK或主控。

固定片：`project_update_intents.patch` 72352字节，SHA-256 `250b88f58b0e10b365aa3ae4a0c0005eef2e3619676e6fe659b607aa7822963c`；`candidate_manifest.json` SHA-256 `65c32dbcc8e7858d6c44c197ee5b74911d1610d6eeaa8e54f2d24f2f6639a621`。13目标含1个新头文件；对captured重连v2 overlay的适用/空白检查退出0，见 `patch_apply_check.log`，不宣称已对05完成rebase。最终9个对象均小端AArch64；各记录编译退出0，输入前后指纹一致。源码从此固定，后续修点另存增量/新版本。

## 这片实际补了什么

已接的真实生产入口只有校准小窗保存原 `last_cal_para`。原链和字段证据沿 `CAL_sync_review/contract.md` f7990d97及 `save_intent_contract.md` 3cb5bb0a；原Up确含此字段。本片复用完整原Up、UP解析/发送字段比较及写回事务，不改算法、浓度字符串、编码、精度、曲线计算或审核规则。

正常已同步LinuxNew连接：校准参数真实commit → 写连接关闭、原写锁仍持有时核提交后同文件快照/SHA → 锁内排明确通知 → Root立即撤旧ready → 同一worker读取并冻结完整Up参数 → 单项目Up和UP查询有序入当前连接 → 10秒内匹配全部已发字段、同原文件及请求版本 → 原事务保存 → 读回同文件实际编号/确认SHA → 更新原确认基线。没有其余待核项目时恢复参数ready；开始仍保留原全文件canStart校验。

设备回写Repo也返回commitSeen/verifiedSnapshot及同文件完整快照：写连接关闭后、同一个项目写锁释放前完成回读，初始和单项同步只使用这个快照。不能释放锁后另取任意新SHA当作设备确认，否则另一保存可插入并被错记为已发版本。若UP已经commit但快照或完整确认期限失败，明确保留已提交事实、触发刷新并保持unknown，不说回滚。

写前失败不造保存意图。commit后读取失败通过commitSeen=true、verifiedSnapshot=false报告“已经提交、设备未确认”，不说回滚、不填猜测hash、不发Up。Repo先复制目标身份，刷新后不用旧容器指针。容量/已知文件名在提交前预检，刷新后再断言仍是预检的同一文件；A改名B而id和SHA相同也在开写库前拒绝。

## 文件和接口

- 新值对象头 `projects/original_project_save_notice.h`：保存来源、源请求号、root、原文件名、该次id/名称、前后SHA、提交/快照状态；阶段回执另带本地intent、原连接、确认SHA和实际id。root+filename是稳定定位，id只是提交版本断言。
- `OriginalSampleRepository::saveCalibrationParameters`：新增结果、锁内提交回调、预检expectedFileName。回调在原锁内只发Qt值信号，不执行SQL，不跨线程带连接或函数。
- `SampleStore::saveOriginalCalibrationParameters`：写前已知文件/预算预检；CAL回调排intent=0生产通知，保留原本地保存回执和刷新。`enqueueOriginalProjectUpdate`是统一消费入口，intent>0回执不会再次产生生产意图。
- `OriginalDeviceSync`：保留初始同步和全文件版本检查，增加每已读文件一份最新未发送意图、全局一份冻结在途；旧意图明确“取代”不算成功。初始/单项回包都按原文件读回实际id和SHA。
- Root：以明确通知撤ready、按来源/请求/前后SHA/intent关联阶段；旧ready、旧编号回执和旧连接不能清新保存。新通道使用独立批次与实际入队回执，不走会牵动检测会话的invalidate入口。纯reload、设备UP回写、曲线保存不生新intent。
- `sample_calibration_page.cpp`只把原提示说明白：参数本地保存、设备确认、样本架保存及曲线未计算是不同状态，没有添控件或改布局。

新类型均有Q_DECLARE_METATYPE，在SampleStore::registerTypes注册；Q_ARG使用全局准确类型名OriginalProjectSaveNotice。批次QList<QByteArray>也明确注册；moc中有对应槽/信号。源码/对象核对不等于Qt排队实际运行通过。

## 多保存、忙和失败

所有生产者须在同一个原项目写锁内先排producer notice再释放锁；Root统一排队，避免CAL直达而其他源绕GUI造成版本倒序。后台给旧保存分配intent的回执只更新同一保存，不覆盖Root已接的新通知。

未发送连续保存核SHA链，普通queued及active not-sent两条分支都要求旧after等于新before。链有未核中间版本则留unverified。已可能入队的旧参数不替换；新版本留待处理，旧UP不能覆盖新值。Prepared尚无入队结论时等待回执，确认未attempt可取代旧冻结版本，真正可能发送才留unknown。

运行、未知控制、试剂动作、未完已发任务或初始同步尚未完成时只留明确意图；Root真实状态允许才推进。CAL生成的样本架草稿不用先保存才同步参数。既有优先使用在途先按原owner完成，新项目Up等待，不因撤项目ready反过来把这条Us撤成未知。

未尝试即守卫拒绝记not-sent；可能已发送后失败、超时、不同参数或版本冲突记unknown，不因下条Qj重试或续时。相同连接的TD/Tr/TR和旧检测结果仍按原后台链处理，本片不清在途、报告号、试剂预留或任务集合。prepared_是历史准备标记，不能拿它永久阻挡完成一轮后的参数更新；worker核真正未完issued任务，Root核真实状态及正在准备的请求。

期限由prepare时启动的单调时钟冻结，不随流量更新。批次带同进程单调到期点，Root实际入队前核；worker处理发送回执、UP入口和回写闭环后也核真实elapsed。只靠QTimer晚派发不能放行迟到批次/回包。单次计时器使用PreciseTimer；线程忙导致晚确认仍保守留待核，未验证为实时期限。

断线停旧等待，保留当前进程的已提交/未确认记录，不将旧意图直接转发新连接。干净新连接按原初始同步读当前磁盘文件，实际确认才结清旧文件待核事实。若旧任务未完，仍按重连v2阻止业务恢复。跨进程台账未实现：退出后文件提交保留，内存意图/来由不构成持久恢复证明，不能把重启当完成对账。

## 预设引用待核：独立于试剂未知

UP若实际改变xuhao，项目参数仍按同一文件保存并记录新id/hash；另记 `originalProjectReferencesNeedReview` 和后台专门标记，显示“参数已确认、预设引用待核”。参数ready不等于开始允许。标记不混入priority/Us未知，不改普通/急诊架、任务号或旧id映射，不因新连接、普通ready或之后一次无编号变化保存清除。本片没有解除入口；必须后续有原版依据、核清现有引用并实现专门迁移后才能解除。

若UP已commit且改了编号，但提交后快照或10秒确认尚未闭合，也先留下独立的identity-review（实际新编号已由成功提交的UP确定，未知SHA不猜）。不能等新连接只见新编号时漏掉旧草稿引用风险；完整参数确认仍按unknown处理。

实际消费者：`sample_types.h`的SampleTask.projectId及SampleCalibrationPreset.projectId仅存数字；`original_calibration_preset.cpp:45,115,141,155`把项目id写入预设/任务并按它匹配；`original_sample_repository.cpp:27,132,246,254`按id查项目、验证校准预设及逐点任务；`sample_calibration_page.cpp`确认后用打开时捕获的source生成草稿。不能把改变的xuhao当成永久文件身份，也不能凭旧数字批量重写引用。当前正常id不变闭环先交；初始同步中属于明确保存文件的编号变化也保持此保护。无明确保存通知的全局历史引用迁移不在本片验收范围。

## 尚未闭合的分支和后续生产端

初始同步的冻结等待不会被新保存替换；并发改写导致冲突时保留commit和意图、停止ready，不能假装全同步完成。需要新一轮合规初始同步的恢复条件仍由重连v2负责，不能拿普通reload重试。

普通项目保存与210dd7de组开关尚未接生产通知。总窗口须在各真实commit点提供同形Notice、写前已知文件/预算检查、冻结filename断言及锁内通知；稳定文件仅支持既有清单，新增/删除/改名清单不冒称支持单项更新。未经这套约束的外部已提交通知只保留阻断/unsupported，不属于可靠排队承诺。不能直接把通用保存成功、项目reload或UP回写当入口。

Root的接法是已知生产者lambda固定source，调用acceptOriginalProjectSave(notice,0,true)，再由同一SampleStore排队；CAL已有同形链。Repo结果必须区分未提交与已提交未核实。不同生产者不能在释放写锁后才补发通知，不能一源直接登记、另一源绕GUI。对应预检/提交锁窗口由后续生产端实现，不能用普通页面快照代替。

项目复制v2另要求CAL写保护：在saveCalibrationParameters的QLock成功后、Database.open之前叠加 `if (!projectRepository_.writesAllowed(message)) return false;`。当前正式旧头没有此API，本片未伪造声明或编译它；待复制v2固定后由总窗口/本窗口做独立单对象增量，不覆盖本片固定指纹。

复制v2作者已另给设备UP写回的两行窄guard：include `projects/original_project_copy.h`，在applyDeviceProject取得QLock后、load/SQL前核 `OriginalProjectCopy::noPending(root_,message)`。它只扫marker、不恢复或拿另一互斥量，由总窗口按hunk串入，不能用整份设备Repo覆盖本片新增签名、锁内结果与快照。

## 验证范围

只做原源码对照、候选对象及moc的ARM64编译、指纹和相对captured重连v2底稿的窄补丁检查。7个实际变动cpp、2个moc通过；后续每次修正只增量编真正变动对象，各轮输入前后指纹相同。没有链接、运行、数据用例、网络或主控验证；既有SampleStore的未用函数和聚合初始化警告未借此清理。

新增资源是一只worker单次10秒计时器、每已读文件一份最新未发送记录、一份在途冻结参数、Root一份每文件最新状态，以及独立的第一份未知/引用待核来由。没有新线程、后台重放或无限历史队列；真实RK耗时、UI响应、SQL排队、资源和同板影响待获准验证。正式05rebase/全量构建及真实Up/UP、失败/断线/迟到/多保存验证不在本次编译结论内。
