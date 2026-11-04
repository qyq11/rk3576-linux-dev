# 用户来源修订04：调用者、生命周期及旧路径独立窄审

本次对象为 `temp/sn100_host/workflow_night_20261102/user_task_origin_init_revision_04_20261103_01/` 的送审20文件片。固定送审表 `review_source_manifest_01.json` SHA-256为 `bbf468e1de179161da7ecf9a188435dde2f7d15b8ae29ca2849f33a906a2250a`，补丁为 `6cb11db873125f81badaab385775aafe344be112b8c8cfbbf047740b52746eec`，均已实际核对。它不是最终晋级版；不能把后续字节变化自动算入本结论。

**本审查范围未发现新增确定阻点。** 已见同一初始化来源从构造样本仓库之前传入样本/内置试剂/Session，并在ready前复核；真正User准备要求三方持同一份非空来源。03的实际无写开始、真实完成归属、取消/超时/换身份不误发Ts，以及CAL实际setter与锁链在合并中保留。快照内部解析由另一Agent主审，本文不替代其结果，也不单凭这些接口就宣布整个初始化问题已通过。

没有修改正式或候选，没有复制/回演整树，没有编译、运行产品/报文/模拟、执行SQL、访问网络或板卡。这里检查源码、调用范围和局部实际字节，不作RK或真实仪器结论。

## 初始化来源如何到达真实调用者

候选 `samples/original_sample_store.cpp:13–43` 保留原所属线程及pendingQuality拒绝，先由同一初始化入口capture，再创建局部SampleRepository。新来源明确传入：

1. `OriginalSampleRepository::initialize(root,message,原容器默认值,configurationSource)`，样本普通/急诊孔数从这份theme取；它还核同份normal里的容器设置与main已经传入的值一致。
2. `OriginalDetectionSession::initialize(repository,message,originalPageMetadata_,configurationSource)`，沿CAL同一盘面拥有者，再将同一configurationSource传给内置ReagentRepository。内置试剂盘20/42表名使用该份theme，不在读完旧表名之后另抓新指纹。
3. Session消费同份normal/debug/theme，结束前核current；SampleStore在同步对象初始化完成后、安装局部对象及任何就绪信号之前再核current。任何一段失败不发布ready。仓库成功后才记自己真正消费的来源，而不是读完旧缓存再补一个证明。

主程序仍在移入工作线程前给Store和SamplePage设置同一个原容器默认值、普通/急诊布局及CAL共享拥有者（`shell/main.cpp:584–602/1053–1072`）。修订04没有改这些公共入口；若当前INI中的容器/布局已与早先main参数不同，新Store拒绝混用，不自行换默认值或刷新UI。此来源只证明本次三对象的初始化，不替main、LIS、其他页面或原同步对象证明所有配置缓存一致，头文件注释已明确其范围。

## 真正User准备仍由原链实际消费

`OriginalDetectionSession::prepareUserOperation:468–496` 现在额外要求：

- 自己的configurationSource非空；样本仓库和内置试剂仓库的source指针均与它相同。
- 已核初始版本恰好三份，并与User登记的configurationVersions相同；当前root/文件仍符合这份原来源。
- 已保存样本输入摘要和项目原文件版本仍匹配；原prepare前后各核一次。

没有从当前磁盘重新capture给旧缓存续签，没有仅以三个相同字符串代替“三个消费者确实用了同一份解析值”。无proof的旧调用仍可做旧prepare，但不能进入新User入口。

新GUI登记及退休核对改用 `OriginalIni::fingerprint`，与capture保留的原字节摘要口径一致；项目文件仍用原ProjectRepository指纹。与03的 `task_reagent_shell.cpp` 实际diff只有新增INI头和两处INI摘要调用替换，动作表、实际finished归属、取消/退休逻辑未改。`device_control_shell.cpp`、frmmain.h、task_reagent_context.h等关键User文件仍为03的实际相同后像。

保留的调用链为：按钮/QB原确认与守卫 → GUI登记真实User请求 → 原桥排队 → SampleStore所属Worker核账户/连接/根/同步 → Session验证同源后无写prepare → 真实None/NotStarted finished → 先核完整来源和basis、再标processed → 无写退休 → 原finishDeviceTaskPreparation再核request/交互/资料门 → 原Ts。类型/回调存在没有冒充真实User写事务已接通。

| 生命周期情况 | 当前源码结果 |
| --- | --- |
| prepare失败或缺proof | Store仍在实际Session返回后发匹配的finished，success=false；不占号/扣量、不生成COMMIT，不发送Ts |
| 错误来源finished | 仍只保留可疑事实/未知保护，不标当前动作processed、不清等待；真正匹配finished才能收尾 |
| 10秒超时/取消 | 未dispatch可撤；已dispatch保留原动作等实际finished，不中断Worker、不假报完成或回滚；cancelled/epoch及原request挡迟到Ts |
| 换账户/连接/root/配置 | 账户权限、连接代次、原根/原输入/配置版本在派发及退休再核；原上下文撤销递增request/epoch，不把旧成功给新上下文续签 |
| 关闭及页面专读 | 原真正后台/reader/adopted各自收尾条件保留，None不排Page专读，未新增清reader/杀线程/假完成逻辑 |
| 立即下发配置 | 读取这份已核debug的SendTaskImmediate；true仍明确拒绝，不能回退Ts称Emergency已下发 |

User写入的事务准入、reserve生产消费者及实际Page采用仍未接；TaskSendDelete、多瓶、无本地任务/LIS未接等原拒绝没有放开。

## 默认接口及LIS、Us、N等旧真实调用

三个initialize增加的是带默认空值的来源参数。实际直接调用仍能省略：SampleRepository的原杯默认false保持；ReagentRepository默认读原theme；Session优先复用样本仓库真正已有的source，没有source时按旧方式读取三个INI，不会在旧仓库之后自行capture出一个“可信User证明”。没有找到正式非测试代码对这三个initialize取旧成员函数指针的调用。本次不保证旧二进制布局兼容，后续须同版完整重编。

实际必要旧调用已直接核：

| 调用者 | 变更后的实际边界 |
| --- | --- |
| `reagents/original_reagent_editor.cpp:360` | 仍省略source走原theme读取，CAL既有metadata绑定独立保留；没有被强迫提供User proof |
| `samples/original_sample_store.cpp:572–585` 原Us准备 | 真实保存孔位后创建临时ReagentRepository，仍省略source读取当前原库并编码；没有因新参数而关闭真实生产Us链 |
| `reagents/original_reagent_level_repository.cpp:225–227` N保存核对 | 同样默认初始化，再核当前状态；原N自身context/来源合同仍在，不借User来源代替 |
| `reagents/original_reagent_level_worker.cpp:443–446` 原来源读取 | 保留默认仓库路径、原量来源及实际读回检查；没有添User授权或清N等待 |
| `shell/device_control_shell.cpp:943–945` 优先级Ur格式核对 | 此临时verifier只用既有priorityReplyMatches，并未调用新增初始化，不受proof影响 |
| `data/lis_settings_store.cpp:154/242/483` | 仍用未修改的OriginalIni::readVersioned/patch及保存回读；没有切成User初始化快照，也没有重贴LIS配置来源 |

OriginalIni::read的签名、输出清空、UTF8/BOM/整行注释兼容、私有临时副本及QSettings读取流程从旧实现提取到readBytes/parseBytes复用；旧read仍执行同一流程，没有换默认编码、键规则或写回原INI。`original_ini_write.cpp` 未改，readVersioned仍做原bytes前后校验及自身version口径。新增OriginalIni::fingerprint只用于新初始化/User核对，没有改LIS原Snapshot指纹算法，也没有改Us/N旧INI读取入口。

## 失败与ready、现有等待的边界

Store安装三个新局部对象和ready_置true均在最终current复核之后；失败只报initializeOriginal错误，不发User finished、devicePacketProcessed、Page采用、COMMIT或退款。项目仓库初始化可能含原有恢复动作，这次失败没有把其实际文件恢复说成回滚，也没有改SQL/旧事务事实。

入口原有release()保留：它会撤旧内存对象和原项目准入、置ready=false，代码不是这次新增的“事务退款”。当前正式main的调用仍来自原初始化ready串行链；本片没有把reloadOriginal或普通加载接成运行中任意重建SampleStore，也未新增调用release来结束User等待。Root桥的dispatched/reader与实际finished不会因capture失败被标完成。

这不等于已经支持“运行中随时重新initialize仍保留所有在途内存任务”：原release本来会销毁旧Session。若以后增加运行中重建入口，必须先在Root处理真正已发任务/动作和来源撤销，不能拿本次proof或初始化失败当自动恢复机制。本次未扩大该旧边界，也未以源码审查关闭真实关闭/重载时序待验。

## 四处CAL合并的实际保留

stage_result.json实际记录四次git merge-file退出0，而不是仅三次：Session.cpp、Session.h、original_sample_store.cpp、sample_store.h。对应stderr原件均为空；stage_revision.py记录的命令是对当前7cad、原b066、User03执行 `git merge-file -p`。这是作者实际命令原件，不是本Agent重新执行的退出。四文件未见冲突标记；零文本冲突不代替以下业务核对。

- Session initialize保留CAL metadata参数、pageMetadata_及内置ReagentRepository绑定，再加第四参数configurationSource，没有用初始化source冒充CAL票据。
- 原prepare保留局部capturePageMetadata，User调用它时不publish永久Default。真实Tt来源检查放在CAL任务编排之前；原项目→试剂锁、load/capture、tasksForRack、publishDefaults仍在真实Tt中，分配失败后已到达setter事实没有被吞掉。
- reserve仍核 `pageMetadata_->current(work.pageMetadata)`；Tu/reagentConfiguration仍用当前有效批号副本，未从User准备重新取票给旧编排续签。
- Store initialize仍向Session传同一originalPageMetadata_；真实Tt返回后仍发originalReagentPageMetadataChanged，包含setter已到达后失败的情况。sample_store.h的配置同一拥有者、成员和通知，与新增User slot/finished并存。

没有把CAL片17文件整套覆盖成User03整文件，也没有把原锁/批号生命周期改成新的User事务。编译、实际CAL跨请求/失败及同板交错仍待实际验证。

## 20文件和局部实际指纹

实际前像表SHA为 `7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e`。前后表元数据各685，差集20改、0增、0删，等于review_changed_files_01.json。只对这20份实际base/candidate/formal读SHA：base均等对应前像，candidate均等送审表及changed_files，审查时正式这20份仍等前像；不据此声称完整现场仍只有7cad，也未把其他窗口新LIS片覆盖进本候选。

新增修改范围相对原User14为6份：OriginalIni h/cpp、OriginalSampleRepository h/cpp、OriginalReagentRepository h/cpp。其余14仍是原User范围，四处已合CAL；补丁文件头恰20对，无新增/删除/main/build。所核实际后像如下：

| 文件，相对源码根 | 实际SHA-256 |
| --- | --- |
| `original_files/original_ini.cpp` | `e45f125c8c13ca360651600fc5e256c768783aacabc26b66faf9b74e9ea960fd` |
| `original_files/original_ini.h` | `afc2cc5b460cf3d5d89d8de8dfdcb20ab227de9fd483392c182e1b0c918a02b8` |
| `reagents/original_reagent_level_task.cpp` | `38fcd40d5c6a2b7b7de8382a6f977708adf632cc0caab92058669b19656d28f6` |
| `reagents/original_reagent_repository.cpp` | `9d4919c0f8f16aa4287cd8750a6280e3b174c87d2bb562490e2e4976b145ae2b` |
| `reagents/original_reagent_repository.h` | `3f1a259773a28acd2fadc0b27417548b19bc1721e2130c0f042a8d37f6861821` |
| `reagents/original_task_reagent_display.cpp` | `307321947e6d271836999a8f72232a2c337e983198105f315f2affdecc5b8f00` |
| `reagents/original_task_reagent_display.h` | `235bc321627466e819c839edc7e464b64c3f6b6cad07c1509511f5aba7412ed8` |
| `reagents/reagents_page_task.cpp` | `7972b9b5addc9aa0e70e4e55a19f9df999ffb99ba6988d4b08bec8a293f9a611` |
| `samples/original_detection_session.cpp` | `711ba37e24558f16f8acbb134829ddebab3b05b064c48b5fb34fec599a788149` |
| `samples/original_detection_session.h` | `ee5a9af6601b8009eda62af59d35bdc13af735b5ab6805928de7df8d426d7a8b` |
| `samples/original_sample_repository.cpp` | `fc01de85767f8f9429747063bb40a2aef969551024bc275ad032befd0dc3cf92` |
| `samples/original_sample_repository.h` | `cb9532bcc9f02324f3f5dc1ce493bbb57897a11a29894327a943bff03332fe65` |
| `samples/original_sample_store.cpp` | `8630fd6147c938e061cb4260f85b117423b89d74dc7b3c84623bb37c8f7ef208` |
| `samples/original_task_reagent_receipt.h` | `14f6a358ea6f486ab506998dd78b2681e9a8773778ebe83168c04bb8f9d74d39` |
| `samples/original_task_reagent_receipt_writer.cpp` | `f83e639896ea617c83f35561ae1f496b090046a2c0f4d064cad5181e1ab128da` |
| `samples/sample_store.h` | `8d996cc9770ec27528e2677cbc1e302dd9ace2719b88920cef02cc5a781b65ef` |
| `shell/device_control_shell.cpp` | `eadccb771fc651cac7e6d26f52386ce8b570cbfc092910745356b129d7a331d4` |
| `shell/frmmain.h` | `efae044127aecd82e7953e461e742b9537f624556c2e729fabe7ac5ff0803352` |
| `shell/task_reagent_context.h` | `84ee2a3b004b486a8618fad5c6146f256895e345c47c10271c0214120054b05a` |
| `shell/task_reagent_shell.cpp` | `0340e945c9f7664b4d266f9a53322a4791ba1b79469bb089e2c46d88d49b7224` |

本次没有执行普通Git apply或全表回演，不给它们新填0。下一步须合并另一Agent的快照内部审查，作者封口最终补丁/清单后由Root复核字节，再决定正式合片、完整ARM构建和原配置RK验证。初始化变动中失败、重载、实际排队/迟到完成与退出、Us/N和真实Tt/TR仍需针对现场条件分别验收，不做电脑模拟替代。
