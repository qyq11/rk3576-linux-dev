# LIS下一片B：同一后台等待、内存应用与原报告预留的实施提示词

这份方案用于把LIS返回的患者和任务真正写进现有样本仓库，再沿原流程生成任务与报告。A9指已经接纳的九份纯数据处理源码；B指本方案的后台等待、内存应用和原数据库事务适配；C指随后由总控接入主程序、真实收发和关闭的公共连接。本片只交B方案，B/C都接齐并验证后才有完整下载流程。

文中worker是现有后台工作线程，reserve是现有的任务/报告占号和试剂事务函数。主控的Tt是现有取任务请求；LIS的QRY是查询消息，DSR是下载回复，ACK是接收确认，DSP是下载正文中的项目记录。SQL表示数据库操作；函数、字段和状态名保留源码原名，Prepared仅表示纯值方案生成成功，不表示保存获准或实际写入。

本轮仅整理方案、准确源码落点和依赖，没有创建生产接口或修改正式/A9源码，没有执行SQL、编译、SSH、启停或发包。总控最新交接说明C7正常关闭请求被页面保护拒绝，原界面继续运行；本说明不重试关闭，也不以B的取消设计绕过现页面保护。急诊可信恢复依据仍是后续待办。

本方案接续完整生产提示词47375310…56b693eb、总控A范围42217f35…4ef40b、等待事实审9365071a…344c60及合同rev02。整理期间总控已正式接纳A9及三份公共构建改动：实际697源表 `temp/sn100_host/workflow_next_20261103/integration_13_source_acceptance_01/lis_a/source_after.json`，SHA-256 `ed4a3eb3025ef0602cdb49d41a6094f35011eb68ac19cf9f36e4f75fb3f6e254`，本轮已读接纳元数据并复算表摘要。较早读取的18份相关源码仍与原688表相同，不能因此忽略A9已经接纳。**B实施前仍须总控确认最新源表，点核B目标前像；当前A9已接纳不等于第13批已编译。**不得拿旧688表或A9虚拟697表充当实际实施基准，不复制整树追版本。

冻结A9入口在 `temp/sn100_host/lis_application_plan_candidate_20261104_01/handoff.md`，九源表SHA-256 `df2424cf41da2845da9d05ee0b0548f15b549ca9fd73e85fb8df6f4c82be2501`。B直接使用它的 `buildOriginalLisApplicationPlan()`、`OriginalLisHoleValue`、`OriginalLisApplicationPlan`及独立 `buildOriginalLisDownloadAck()`，不重复或修改A9。

**给B作者的窄范围提示词：**

只在总控新批准的独立稀疏候选中，给现有SampleStore工作线程和原样本仓库补齐可实际使用的内部值路径：原真实Tt通过现来源核验后可挂起；LIS结果可回同一个等待；同仓库能真实更新患者、独立条码、孔任务和LIS状态；继续时由现reserve把患者列与原任务/报告占号一起提交。所有新增状态都是本地寿命和关联记录，不造Grant、保存授权或第二套任务/患者库。

B不接main或新的Tt消费者，不替C选择生产路线，不发QRY/ACK，不使现入口进入半流程。候选内部方法要能按真实输入产生有界值、实际内存应用及真实结束事实；C未接时保持现实际路径，不能放永远false的资格函数或空成功来称已接通。原任务负责人继续拥有样本/Session/公共任务代码，LIS作者提供A9桥接与原行为核对；实际文件归属先由总控登记。

**建议的实施顺序及落点（以下行号是本次读取源，后续按函数核前像）：**

| 顺序 | 源码和实际函数 | B需要完成的内部工作 |
| --- | --- | --- |
| 1 | `samples/sample_types.h:10–75` | 在同一SampleHole中加入原患者item值、独立扫码条码、针类型和LIS运行状态/控制号/起时；SampleTask保留A映射项目名、资料是否找到、资料文件/版本及原重复次数，不能只剩projectId而丢掉id0/空资料的原情况。 |
| 2 | `samples/original_sample_repository.h/.cpp`：initialize 40、editable 119、validateRack 130、saveRack 347、removeRack 369、clearOrdinary 381 | 增专用原LIS查询初始化、取旧值、状态更新和一次内存应用入口；沿同一snapshot，不创建库。加入仓库初始化实例及架/孔修改版本，所有实际改动统一维护。 |
| 3 | `samples/original_task_codec.h:19`，`original_detection_session.cpp::tasksForRack():289` | 每个Task携带同孔的真实报告患者值、内部号和独立条码来源。保留原手工任务路径，LIS继续用该次已应用孔值，不让原Tt旧条码覆盖DSR返回条码。 |
| 4 | `samples/original_detection_session.h/.cpp`：answerRackRequest 630、reserve 505 | 共享必要的只读准备/版本核验；患者列进入原reserve事务。报告分组碰撞只按总控明确裁定准备最小修正，不先改变原行为。不得在已有锁/SQL后等待网络。 |
| 5 | `samples/sample_store.h`及 `original_sample_store.cpp::receiveDevicePacketWithTaskBasis/Impl():379–525` | 同一worker的唯一等待上下文、值回交/消费、应用结果及一次续接/终结。等待点在405–413真实验证通过后、418首个answerRackRequest之前。 |
| 6 | `original_sample_store.cpp::initializeOriginal():13、invalidateOriginalDeviceSync():546`；`sample_store.cpp::release():951、close():976、setSession()` | 重新初始化、换源、主控连接失效、账户变化和正常关闭时，先真实结束/取消等待，再释放原对象；防旧回调访问重新初始化后的同名架。 |
| 7 | `samples/original_task_reagent_receipt.h::sampleTaskInputDigest():45`及已有样本输入序列化调用 | 新患者/条码/LIS项目身份不能不进入任务修订核验。由任务负责人和总控决定最小版本适配，保原非LIS语义，不能悄悄让旧User意图套新孔值。与急诊06的冲突留总控窄合，不修改其冻结候选。 |

可按“数据/仓库及reserve适配→同worker生命周期”分两个候选交付，不能把前者说成已接真实等待。必要的纯值声明可放相邻小头，由总控确认后创建；不为了分层另造服务、存储或线程。HistoryStore、报告编辑页不在B修改清单内，报告负责人只核字段含义及既有刷新合同。

**值表示和内存应用：**

患者item、孔内部sampleId、孔扫码条码分别存。A.after.patient.sampleNumber/barcode与A.after.internalSampleNumber/barcode是两个赋值阶段，不能合并。患者包含A实际写的床/名/sex/age/sn/患者条码/无格式登记时间/remark/checker/department；原未写的patient_type/application_serial/jianyan/auditor等沿真实旧item或原初始化空默认，不从按钮名、当前报告选中项或登录账户补造。

LIS状态是NONE/T1/R1/T2/MAX这套原等待阶段；孔任务状态是None/Ready/Testing/Finished/Error，二者分开。A全未知项目的Prepared仍要整体替换空任务，孔状态None，患者字段更新；类型、容器、针、内部号、孔条码按A.holeAssigned才赋，不因患者item里有新号/码就一起覆盖。A描述MAX/ACK的标志不能当实际应用事实。

先核真实仓库实例、原架id/孔、目标修改版本、来源根及本次等待仍活，再核全部输入和资源，生成完整新孔，最后在同一仓库一次替换。保留editable中的inFlight/Testing保护和Session已issued架保护；不把LIS等待提前登记成“已下发”，也不通过setRackInFlight(false)解锁。失败不先改半个患者，不提前MAX或回ACK。

不能直接用saveRack代替该入口：它的validateRack含enabled、repeats≤99、稀释启用等手工规则，会把已批准的原DSR应用变成另一业务。手工页面/原saveRack校验保持，LIS专用入口保原A映射值及数据结构/来源/在途/资源守卫。A任务可能项目名非空但projectInformationFound=false、id0、空rate，内存层不得丢掉、改选或新增临床拒绝；真正下发仍保现项目、稀释、完整参数、试剂和Latin1/字段保护，失败就记录实际阶段，不能借LIS放宽它们。

原查询初始化要单独提供，不能塞到DSR应用里：Windows `slotPCGetLisTasks():4098–4102`先设扫码条码、清control、NONE、ReportItemData默认item及起时；`QRYMessage():747–756`对有效孔再设T1/起时/control并清item。B提供这两阶段真正操作现仓库的能力，C未来在同一原Tt的真实选孔/生成阶段调用；不得在重复DSR到达时重置item。A.oldHole取这些动作之后、DSR之前的真实值。

查询选架不能直接套answerRackRequest的“已有Ready任务才选架”过滤，否则尚无任务的架永远查不到下载。应对照原slotPCGetLisTasks的实际分支，在已保留prepared及原开始保护的前提下处理普通架的原查找/创建、测试中和已结束分支；空架号/关闭扫码、急诊和CAL/QC等范围由总控划定，未接部分不能静默回空成功。不要解除现无本地任务开始保护或伪造设备请求来绕它。

当前仓库初始化会重新从原架id起点创建，root+架名/id不能证明还是同一实例。增加worker/仓库初始化序号及目标架/孔修改版本；等待号由同worker递增且不回绕，仅作本地关联。B自身查询初始化和逐孔DSR应用也会改变版本：分别保留原起点和由本等待实际产生的before→after修订链，后续只接受预期的自身变化。不能要求继续时仍等于查询前的全部字节而让所有正常应用失效，也不能直接改拿“最新版本”给旧请求续签；外部编辑/删除/重建或项目/初始化变化须可识别并取消。

新患者、条码和项目身份的修订与原User摘要兼容由任务负责人核定；不能把TB剩余时间、纯显示状态或唤醒tick等无关值塞入任务输入摘要，保留现有排除规则。LIS上下文自身阶段可独立记录，不以随时变化的时间值代替任务身份。

**单worker等待与一次结束（接口名仅建议，由任务负责人定稿）：**

| 阶段/拟接口 | 所需真实动作 | 必须保留的边界 |
| --- | --- | --- |
| `beginValidatedLisWait` | 现405–413校验完成后，核原prepared、同步、根、项目/初始化及任务输入；安装唯一私有上下文，再发查询值准备通知并返回事件循环。 | 不能先调用answerRackRequest“探一下”；663–677已有项目/试剂锁及默认批号副作用。不得预reserve、占号、扣量或processed。 |
| `receiveOriginalLisSendReceipt` | 回交该上下文真实QRY逐条入队receipt/拒绝，保存原profile/configuration/request/socket generation/固定peers及孔控制号。 | 三种代次分清：主控connection、Bridge epoch、LIS socket generation。RuntimeBasis没有epoch，C若需要必须显式传；不解析basisToken，不造Grant。 |
| `receiveOriginalLisMessage` | 在同worker先解原头，按仓库原架/孔顺序找第一个控制号匹配，再判MAX/R1并核本等待/原配置/对端，实际设R1后解正文并调用A9。 | 首查含已结束状态，不先筛掉旧孔改选另一个。body/text/profile来自原ReceivedMessage；不用文件导入、MSA-2、OBR架孔或当前新profile替代。 |
| `applyOriginalLisPlan` | 真实版本/旧值仍吻合后，一次改同仓库患者、孔及任务；记录实际after修订，成功后MAX，发带原来源和控制号的Applied/ACK素材通知。 | 不把外部传入Prepared直接保存；A只做值计算。全未知空任务也有实际应用事实；不提前提供ACK时间或把它称持久化完成。 |
| `wakeOriginalLisTimeout` | 检查固化的每孔原墙钟startTime，默认60000毫秒、abs差严格大于期限；T1报警，R1/T2原结束，整架只推进一次。 | C原第三拍仅作唤醒，不重贴身份/起点，不把单调elapsed当原墙钟差。停止/关闭不能靠已停的tick才能结束。 |
| `continueOriginalLisRack` | 整架已结束且阶段从等待原子改成待继续后，只消费一次；重核原实例/来源/修订/撤销，沿原Session及reserve一次继续。 | 使用原payload/basis，不再经GUI route，不新到包号，不重新freeze。不因ACK enqueue false自动新增reserve禁令，也不等WrittenToSocket。 |
| `cancelOriginalLisWait` / 原结束口 | 等待中取消由本worker实际结束并一次processed(false)；已恢复处理则等原真实commit/processed。 | 不把取消请求当回滚；已内存应用、MAX或已入队ACK事实保留，不自动恢复T1、重发、退款或清读者。 |

上下文只保一个活动架：worker实例/等待号、原actionId/packet/主控connection/显式epoch、共享原QByteArray及SHA、原RuntimeBasis值、规范root、初始化/项目/目标任务版本、架孔顺序、profile及有界每孔阶段。它不是另一份仓库，也不是可转让保存许可。原packet仍计GUI的32包/1MiB预算，B不另留历史原包。旧MAX/控制号等必要孔状态留在同仓库生命周期中，结束上下文后释放大正文和profile；不能为全仓库历史孔强持全部旧profile而耗尽live16。

原Impl的taskBasis指向调用栈，committed捕获receiptObserved引用；等待返回后不得再用。恢复同步段时重新创建局部taskReceipt/receiptObserved/committed，并保原值来源。可提取现同步处理/终结段供原路径和继续路径复用，但继续不得再次进入“新等待”分支。原non-Tt/TR、Ur/TD/Tr/同步路径不能重复处理或多发processed。

进入原同步段前先消费一次恢复资格；现Impl:395–400的锁内CommitObserved回调仍按原时点只排纯值，不读库，不延到最终终态。最终结束口先封本地终态，再按实际结果发尚未通知的CommitUnconfirmed、刷新及原任务reply，最后一次原devicePacketProcessed。保现Store:422–428的已提交但外层失败刷新、518–525的CommitUnconfirmed和锁释放后processed；不拿锁内commit通知提前读库/退包。C之后仍由现Bridge等待Page/真实reader收尾。任何emit前不留Action引用跨外调，取消/继续重入按同worker阶段先消费后通知。

936507事实保持：dispatched在invoke前置true，invoke true仅说明已排队；它仍按现标志驱动串行门，不证明worker已处理或SQL开始。500毫秒检查的10秒只提醒，且仅当刻未dispatched撤basisEligible，不新增事务截止、不重置arrived/seed。basis只核原显示前态，false不证明无COMMIT；resume不能借新N/Ur或新basis续签。

初始化/换源/账户/连接失效须明确终结旧等待，再释放或安装新实例。现initializeOriginal:19会release，release:951–958会销毁原对象，close:976–989会直接释放，均需纳入B的真实收尾；不能销毁后让旧回调访问新对象。等待中关闭不得持SQL、文件锁或reader等网络；正常C关闭握手在GUI循环退出前完成，不依赖main退出后的BlockingQueued close往返GUI。保现页面草稿、回读、事务保护；页面guard拒绝关闭时，不借新增LIS取消强行关界面。

**报告/任务字段与原reserve：**

`tasksForRack():382–401`把本孔患者及实际独立条码复制到每个Task；sample是线上样本字段，按原规则有效条码优先、无效码可回退内部号，presetSampleId保内部号，不把sample无条件绑条码。原LIS整架继续调用 `slotSendRackTasks(rackId,rackGet->getScanInfo(),空列表)`（Windows4275），getScanInfo取当时各孔sampleCode（cussamplerack.cpp:283），并非查询前Tt的旧扫码值。B需显式区分普通手工请求和同一LIS继续的数据来源，后者按已应用孔值组任务，不重构假Tt或改原payload SHA。当前tasksForRack:358–363直接取request.barcodes，不能让它把DSR新条码覆盖回旧值。

原reserve仍同SQLite连接、原结果/试剂事务、锁顺序、counter、真实任务/报告号及扣量，保commit失败/未确认处理；不先占号、不拆一笔HistoryStore.savePatient补写。只在原实际任务需要报告时写，空任务或全未知项目不会因患者值已更新而凭空插report_data。

| 持久位置 | B的来源及不能混淆之处 |
| --- | --- |
| report_data.name/sex/age/department/bed_number/remark/checker | 同Task携带的本次真实患者item；checker是送检医生，不填到jianyan。 |
| patient_type/application_serial/jianyan/auditor等原未赋字段 | 真实旧item或原初始化空默认；不从当前选中报告、账号或患者ID猜补。 |
| report_data.sn | 原孔内部sampleId的int32转换，不是A.patient.sampleNumber无条件直接绑定。LIS分支保原toInt规则；其他已验路径的变化须总控单列。 |
| report_data.sample_id | 原孔独立扫码条码，不能直接取患者item.barcode替代两阶段条件。 |
| report_results.sample_id | 仍为孔内部号非空优先，否则条码；不改成task_id或报告主键。 |
| report_data.id / row_id / task_id | 各自保实际SQL主键、兼容报告分组号、设备任务号；A9不生成这些编号。 |
| data_time、is_auto_id、uploaded、statue、sample_hole | 保原实际阶段和已批准差异，不把OBR登记时间或下载ACK当上传成功；sample_hole原空不凭孔号补写。 |

补患者列必须显式按原空TEXT绑定。原ReportItemData默认sex=-1、age/sn=0、字符串空、时间无效，原SQLite显式写空字符串；Qt默认null QString可能绑定SQL NULL。不要靠省列/数据库DEFAULT或全局改Database::execute吞NULL来修，局部患者绑定保空TEXT，数值/其他字段仍按原类型。报告负责人按history_store.h:30–43、cpp:174–186确认含义和现刷新合同，B不改其选中项编辑逻辑。

当前Linux `original_detection_session.cpp::tasksForRack():384`以 `(horizontal ? repeat : 0)+hole*1000` 作临时分组，reserve:570及581–590再把这个数映射成真row_id。横向时孔1重复序号1000与孔2重复序号0都成2000，两孔各1001次仍低于建议4096测试预算，所以次数预算不能排除该键碰撞。这是当前Linux实现的算术问题；不能仅凭此推断Windows原分组完全没有碰撞。

原Windows由 `slotSendRackTasks():3931–3952`按实际isNewProtocal分别调用两个普通任务函数，传入同一总表row_total_count引用，最后3957写总表。原键是每个S_TestBase保存的实际reportRowID，来源如下：

这个分支条件来自设备状态：`CommandReceiver::pcQueryStatus():577`仅在字段存在时按文字等于LinuxNew更新isNewProtocal，断线 `frmmain.cpp:927`重置false，不按上位机操作系统选分支。另两个独立设置不能混同：Windows main.cpp:66从config_Debug.ini读取userOption/SendTaskByRowID（默认true），决定按重复轮次穿插项目还是按项目遍历全部重复；main.cpp:146从theme_config.ini读取theme/ReportTableType（默认0），public/systemcommon.h:91–95规定0横向、1纵向，才决定报告共享行。Linux现Session:268读取前者、272将后者0判横向，不能为规避原Simple问题擅自切协议或改设置。

| 原函数/分支 | 实际分配及计数器作用域 | 与二元键方案的关系 |
| --- | --- | --- |
| `getRackNormalTasksCommand():1520`，非Simple | 每个可处理孔1663从totalRow++取起点；每项目1706重置reportRowInner；新任务1750–1755赋原reportRowID，横向递增inner；2153取max，孔末2176–2177将totalRow推进到max（纵向再+1）。 | 对本片全新Idle任务、reportRowID原为0的初次占号，可用独立孔/重复分组表达其横向/纵向共享关系；实际行号仍由原counter事务分配，不承诺所有跳过/旧任务分支的计数细节已等价。 |
| `getRackNormalTasksCommandSimple():2195` | 2343每孔totalRow++取起点；2389每项目重置inner；2431–2435新任务赋号并横向递增；2816算max、2820–2823改局部行/重复序号，但到2838离开孔循环、2852返回没有把max写回totalRow。 | 原Simple横向也有孔间重叠：孔1两次重复得到R/R+1，孔2从R+1开始。改成独立二元键会改变这个原实际行为，必须单列为有意修正，不能叫无差异移植。 |

对全新Idle、reportRowID=0且未跳过的任务，两个函数的SendTaskByRowID消费方式分别是：true时按每孔最大重复数设m（Simple2345–2351），每轮每项目重置inner（2389）、n=1、index=indexByRowId（2391–2408），横向轮末递增局部reportRowID（2820–2823）；false时m=1，每项目n=全部重复、index=j，横向在每个新任务赋号后递增inner（2431–2435）。两方式都使同孔同重复共行，但都不能弥补Simple缺失的孔末totalRow推进。非Simple对应1663–1755及2153–2177。若旧reportRowID>0则沿用不递增inner，已发/非IdleReset会先跳过，尤其false时跳过第0重复后新第1可能从孔基行号而非基号+1开始；所以raw repeatIndex二元键不直接证明这些分支等价。

建议总控为防止患者串行单独裁定修正上述分组。私有实现最小可行：非CAL准备阶段reportRow只用正占位1作taskFields预检；reserve改用现task.hole与(horizontalReport_ ? task.repeatIndex : 0)的QPair单架键，按原tasks遍历次序分真row_id，赋真号后才生成taskOrigin/rackPayload/issuedTasks；CAL仍row0，不按新map键重排。当前预检消费者不把该占位当真实占号，线上字段也不传reportRow。总控未批准前只整理方案，不实施此改动；更不能恢复Simple跨孔混患者来冒称等价。

目标共享关系为横向同孔同重复各项目共行、纵向同孔各重复/项目共行。同组患者/条码必须同源一致，不能first-wins吞冲突；已有正reportRowID、Idle/Reset/已发跳过、跨架总计数及整数耗尽等原分支需另核，不用这个初次占号方案默称重测/恢复全覆盖。根基仍是原OBR toInt后小于1取1，不截99、不以孔号乘常量作为身份。

两项既有差异留总控裁定，不借患者落库偷偷改：现reserve:572一份当前time且只建首分组行，Windows2754逐任务currentDateTime()+9*index并可Update同报告行；OBR日期只曾写内存，不能拿它当最终持久data_time。原孔isAutoId默认true、占号用孔值覆盖，现reserve硬0；B若要新增真实来源需总控明确批准，不从LIS回应/sn或空号推断。最小B可保现行为并明确未等价，不能宣称这两项已核全。

原整架继续还有sampleCodeCanRepeatTest分支（Windows4160–4275）：按实际孔条码复刻原QueryReportDatasById返回序列并取末项，不自行按日期或主键DESC猜“最新”。原report_table.cpp:207–246先按id ASC读取、放入row_id为键的std::map，再按row_id顺序输出且跳过没有对应结果映射的报告，之后slotRevLisLisTasks:4215才取data.back。实际abnormal_type非空会删相应项目，不能按误导注释倒置；4096行附近的临时send=true不能当成整架最终去重已关闭。去重后仅任务非空且孔内部号为空时，4252–4258才从原样本设置计数取号并递增；不为所有空孔补号、不新造第二计数器。B拟提取同worker、同原结果库的短只读查询/内存调整，由总控明确范围；不持查询等网络，不自动重发旧任务。若首片暂不覆盖，须在C启用前明确实际适用配置及依赖，不能凭默认值跳过。

**ACK阶段和C不能缺的连接：**

B只在实际患者/孔任务一次应用完成后MAX，发Applied事实及ACK素材（原configuration/profile/controlId、等待号/应用修订），不提前取ACK时间或发包。C在真实收到该阶段的调用点新取MSH格式时间，调用A独立ACK函数，再复用唯一controller；不能在拿到Plan.Prepared时预生成，也不等ReportDB commit或仪器接受才生成。

整架待继续阶段要先原子登记，再按同一发送者顺序发本次成功DSR实际产生的ACK素材/请求和整架继续。C先实际尝试这些ACK入队、记录真receipt/拒绝，再把同一等待号的继续排回B；纯NF、其他非OK或超时原本没有成功DSR的ACK，不能因缺素材永等或补造ACK。不等WrittenToSocket/对端ACK，不因enqueue false本身新增reserve禁令。真正身份/配置失效仍应按取消处理，不能和单纯发送失败混为一谈。ACK只描述原接收阶段，已应用内存不会因写socket失败自动还原，MAX重复DSR不重造ACK，未知发送不盲目重发。

| C仍必须提供的真实闭环 | 负责位置和约束 |
| --- | --- |
| 同次LIS路线快照及显式Bridge epoch进入唯一原Tt调用 | 总控持 `main/frmmain/device_control_shell/task_reagent_shell/context`；当前991入口、997–1000 Mg门和32包/1MiB保持，不增加第二payloadReceived消费者。新参数/重载若需添加，由总控窄接同一调用链，不能假称现4参数已带LIS路线。 |
| 固定查询批次→逐条真实QueuedSend/拒绝→完整ReceivedMessage | 复用现controller；receipt需先排回worker再让相应完整消息归还，不能先到DSR却无固定peer事实。先固定全批五取时/T1起点/控制号/正文，后泵不续时。 |
| 有界入站与已投未处理事件的释放回执 | C投worker前就计容量，不能只在worker查16条而放GUI事件队列无限增长；body字节与UTF-16文字单位分开。worker消费/拒绝实际回交后才退账。 |
| 原第三拍唤醒、停止/换配置/主控/根/账户/项目取消 | 不把tick当授权；profile.matches且profile原根等于worker根。旧完整消息按仍活旧等待判断，不换新身份；停后仍能完成真实取消。 |
| ACK素材→A正文→当次广播入队及逐对端结局 | ACK用自身调用的当次已连广播集合，不只回DSR.peer，也不套QRY旧peer集合；无单条撤回API，不随意stop整个LIS伪撤一项。 |
| 整架继续→worker一次消费→原reply/processed/Page/reader/关闭排空 | 现1021–1025退包、1041–1047回应守卫及后续Page/真实reader不绕过，GUI循环返回前收尾。 |
| 元类型、主窗/页面按实际内存修订刷新、正式构建/打包 | 总控/任务及报告负责人；异步load或信号发出不等于界面已采用。A9三公共构建改动不由B重复。 |

**预算与C前的最小裁定项：**

沿现一个活动等待、最多128孔、原32包/1MiB包预算。沿473753方案，额外待发QRY≤128条/UTF-16正文2MiB，DSR含已投未消费≤16条/正文与文字合计4MiB、活动DSP≤4096；实际传输仍64条/4MiB。A9的项目/任务≤4096、代码稀释≤16384及输入/输出各4MiB均保留，是逻辑内容预算，不能推RSS/响应通过。

B还须在任何sampleTaskStates()、sampleHoleTaskStates()、状态列表构造或tasksForRack循环前，按标量安全计算即将展开的实际测试次数与总字段量。A9的4096是项目计划数，不限制repeats乘积；现sample_types.h:52–55会先展开，Session:417结果检查太晚。建议首B每活动架实际展开测试数≤4096，新增LIS患者/任务保留文字全仓合计≤16MiB，另核现协议最大任务帧；这些是待总控/任务负责人定的本地拒绝上限，实施前要写清计费/替换/释放，不截99、不截断、不用永久假资格回避。预算失败要记录未应用或已发生的原R1/内存/发送事实，不造成功ACK。

还需核任务生成的无效迭代开销：现Session:348–350取全架最大重复数再逐孔循环，Windows两个函数是每孔设m。候选可按原每孔范围及原项目次序预整理实际选择项，避免小重复孔也扫描全架最大轮数，且在任何展开前做上述全量准入；必须证明不改真实输出顺序、跳过条件和试剂选择副作用，再由总控批准，不以只有4096个输出就声称计算量或响应已达标。

开始B编码前，总控至少裁定：B目标文件归属及A9接纳后新基准；重复/保留文字物化预算；独立分组键对原Simple重叠行为的有意修正；上述时间/isAutoId既有差异是否本片补；整架原重复检查及首片普通/空架/扫码等真实分支；60秒等待延后后续Tt/TR的既有串行影响。数据生命周期、空任务更新及分组修正候选可以按具体批准独立准备，不等待急诊可信恢复资料；未获分组行为裁定不提前改接口或启生产。

**交付与验证范围：**

B候选交每文件精确前像/补丁/后像、A9调用点与实际同仓库更新及reserve列清单、等待/继续/取消/初始化/关闭的一次终态表、原分支差异和两Agent源码审。代码中不能以空函数/未接发送/永久false许可返回成功。总控安排普通补丁检查、ARM64及受影响验证；本轮只交方案不执行这些步骤。

完整B/C接齐前，主程序仍不发生产QRY求半流程；真实LIS端点、有效INI/角色/编码、板卡及允许操作由总控串行确认验证。条件不足则登记待联机，不模拟、不匿名、不回环、不伪造Tt/Grant/ACK，不靠急诊06未提供资料绕开始门。编译、实际患者内存应用、报告/任务持久提交、ACK本地入队/对端收到、真实仪器执行分别留证；源码数量或存包不证明真实LIS通过。

串口沿原配置分支保留后续依赖；本片不改配置或把串口伪装成网络。缺少真实串口传输/角色能力时明确待接入，不能以“无需查询”或空成功掩盖。B的同仓库数据与字段路径可独立准备，真实串口收发仍由总控另分派。
