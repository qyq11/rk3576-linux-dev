# N最终包装03：原P2与四处后续修正独立复核

结论：本次所查修点未见剩余阻断，可进入总协调的第七批隔离整合。原复制待核误计项目、Ur与N互相抢占两项P2防线保持；02中完整项目名大小写折叠过严已修正，失败Page回执及较晚归还页面忙状态也都有实际继续同步调用。仅关闭这些源码缺口，不代表N全链、整程序链接、RK运行、真实仪器或资源测量已经通过。

本次只新增本报告，不修改候选、正式源、06隔离源、旧包装/报告，不编译或运行任何产品、协议程序、数据库、网络、RK或Windows界面。原数学、精度和字段行为沿用既有独立审查，不重新全审。

## 固定身份与真实文件核对

唯一最终入口是`N_consumer_candidate_v2/packaging_03/n_v2_manifest.json`。02及根目录失败包装仍保留，不能用于最终整合；03中间phase_01也不代替最终结果。

| 对象 | 本次实际SHA-256 |
| --- | --- |
| `revision_handoff.md` | `bd54c303b5863cd3970b2fac44a55848f36145cf2c9e5c2c4f92b7198143833f` |
| `packaging_03/n_v2_manifest.json` | `d366019603ea10f6c95709a287792773005747f25e9827fe42eaaf1d782edd44` |
| 累计`n_v2_cumulative_on06.patch` | `88bdc78486bfa009c41badc9b20df4085df52b383f52c0ac556679d2ca8c7edb` |
| 增量`n_v2_delta.patch` | `ca3d1e214a5cc95e9036489f0a88b8ac5d8277bbdbfaa87404259705c1371104` |
| 独立`n_v2_store_guard.patch` | `ef07df1c0298eb71688a1da847af4d575f1e94a83a3f594af84eb2c02f93c349` |
| 最终4对象编译日志`compile_followup_02.log` | `6012c4fab1d87667657081ff48911610c926bc8ac8b9955e374818775d3648a7` |
| 最终379项before/after输入清单，两份实际相同 | `2e301fcabfe5da34810439b94355ee6a46569f47ee9f1e2e4a384c67df522368` |
| 实际依赖报告 | `fe34117f123b5a522a12cb3e2a3382887d5e2eaedafa9c0615102537f0616038` |

25/25当前候选源码SHA与03清单相符；所有有实体路径的03补丁SHA相符。累计私有实际应用副本`package_checks_20261102T173841Z_63068f18/n_v2_cumulative_on06`的25/25目标SHA也相符。清单记载其私有check/apply退出0并核最终字节；本次没有再次应用，也未把只读检查称为正式已应用。

相对02固定私有累计副本，实际只4个CPP改变，其余21个源码目标SHA相同：

| 实际改动文件 | 03源码SHA-256 |
| --- | --- |
| `reagents/original_reagent_level_model.cpp` | `0d83d1fb4cb7b466b6fc056a20d7cc13789b390f88e173802fa75e877a865f25` |
| `reagents/original_reagent_level_projects.cpp` | `c566a72b82c9afa5a1c3fd0c34eb96625712f29bf2022c7916e3bf4e572351c0` |
| `reagents/original_reagent_level_repository.cpp` | `d1b2c49adc7e59e318d7f0cb393118a62c371a00847679e5660f17c1d4f11b6d` |
| `shell/reagent_level_shell.cpp` | `ba5c765f6bf6e535c029a16fc635e3edcba5bcac40567bfdd033103af26b6011` |

16/16现有对象SHA相符，恰好上述4个业务对象变化，其他12对象与02相同。最终日志只记4个CPP重编并以`COMPILE_ONLY_EXIT=0`结束；完整对象集合仍为12业务对象+4moc，不能称03重新编了16个。02旧4份CPP/对象/.d共12个归档文件实际SHA均符合03清单的保留记录；中间phase_01另存，不混用其输出。

379项输入不仅before/after原字节相同，当前实际文件SHA也全部相符。实际16份`.d`均解析并规范化路径后与清单依赖集合一致；四个关键消费者的`.d`仍指向实际06的`projects/original_project_copy.h`，该头实际SHA为`6631707ba3d486f75989092068ff0d79f732c1f86eff9bcf9c431c622c401efe`。本次没有执行打包或编译脚本。

## 原复制待核P2：防线保持

- `readProjectSnapshot:31/115`读项目前和完整清单/版本复核后都noPending；任意copy pending都拒绝，不把已证自有clone当真实项目，不自行恢复/删除。数据累积在局部loaded，完整成功才发布，失败不漏半份项目或orderVerified。
- `projectsMatch:116`包含noPending；`saveObservation:214-219`先持项目→试剂两锁，再通过versionsMatch在SQL前、BEGIN后、UPDATE前及COMMIT前复核（`:219/224/240/255`）。没有只挡loader而漏掉检查后新建clone的写入竞争。
- worker提交后的后台重读仍先取两锁并noPending（`:399-407`），Editor读出局部snapshot/choices，后续再核source-order版本；Page仍走`loadOriginalLevelObservation(root)`专用读口（Store`:68-82`），核当前root、两锁、noPending后才复用Editor。应用内合作锁边界下，不由N进入有清单的复制恢复路径。
- Page提交展示、撤pending和读失败路径保留：失败只报error/待核，不发布旧详情为本次结果；项目次数未核实会标待核，失败孔不会复用旧N数字。普通重读/Qj/sourceReady不把失败N变为显示成功，需新有效N被接受。

03未改这些worker、Store和Page文件，当前及03私有累计副本SHA与固定02一致，因此没有为了修名称而削弱原copy guard或已提交事实。

## 完整项目名身份：02过严条件已修

三个项目CPP相对02只有5处移除`toCaseFolded`：model`:83/86`、projects`:76`、repository`:133/137`。现在精确QString/QSet名称身份与06的ProjectRepository规则一致；不同文件、不同xuhao的`PT_a`和`PT_A`可分别通过。精确相同完整名称或重复整数xuhao仍拒绝，不能仅靠不同文件名放过clone。

`projects.cpp:103-109`仍用原`startsWith(..., Qt::CaseInsensitive)`计算sendSequence前缀rank，再stable_sort。只恢复完整名称身份，不改Windows原排序、不重算旧观测；没有将“前缀排序忽略大小写”误用成“项目完整名称忽略大小写”。

## 原Ur/N交错P2：双方生产口与真实Qj观察保持

N与初始同步的双方排他仍在：`updateOriginalDeviceSync`开始先看Root/worker/Page Nbusy；异步begin入SampleStore前先登记started、connection/root和baseReady=false，再关N写门。N接收入口同步看已请求Ur、恢复查询、已登记未完成初始同步，不靠GUI包队列暂时为空放行。

实际纯Ur发送前设置独立连接等待标记，匹配processed成败均收尾；发送失败、断线/上下文失效按原因清理，未processed的超时保守等明确重连。不能由迟到ready布尔提前开N门。

关闭的是N写门，不抹真实Qj观察：`setReagentReadbackPending`清worker运行缓存、submit和current核门；`isContextCurrent`仍仅核原Qj语义/期限，供泛Ur和priority的原arrival Context认证。合法Ur不会因自己的等待把registry清掉而被迫超时。只有blockers为空且新的真实Qj才开N门，处理回执和ready信号不续Qj、不复活旧job。

03没有改这些既有02 Shell/worker保护，原“发Ur等回包→N先占用→泛Ur被Nbusy挡掉”的次序仍被阻断，未用删除泛Ur互斥凑通过。

## 失败收尾后的同步续调：03已闭合

1. worker `processed`仍匹配本条packet/许可/完整Context后归还Root等待并调用原同步。若Page还持展示pending，原同步仍按Nbusy返回。
2. `originalLevelPresentationFinished:313-321`现在无论shown成功或失败，都在非关闭状态调用`updateOriginalDeviceSync`。失败先按原同代/同许可条件重核来源，不伪报显示、不重放N。
3. 新`busyChanged:209-215`只对false且非关闭排`QTimer::singleShot(0,this,...)`，回调再次核关闭状态后继续原同步。这样pending已提前撤销，而专用读取随后才失败/归还busy的分支，也能在本轮事件收尾后继续，不依赖将来另一条Qj恰好补调。定时回调以Root为上下文，销毁后不会访问旧对象。

这些调用都重新进入原`updateOriginalDeviceSync`，其Nbusy、当前连接、真实新鲜状态、角色、资料root/版本、页面busy/草稿、旧任务未知及每连接仅一次启动守卫未删。若期间又来新N或换连接，续调仍看当时真实状态；它不直接置ready、不清Us/任务未知，不自行发送N/Mg，也不把重复续调变成重发Ur。

Testing继续走状态5的N被动观察判断。新续调沿用原初始同步的actionReady/运行态条件，可能按原规则暂不启动同步，但没有把该返回结果反用成N许可门；N本身仍按实际Qj、机型/20或42孔、文件/独立runtime和copy保护判断，不因角色或聚合Ready整体禁止。42/48和任务预留后缺可信runtime的拒绝保留。

## 交接与未验范围

03所核源码修点可并入07隔离树，完整06上用累计片；已接好06+N1才用增量加Store守卫，不能重复叠同内容。producer v2与N共享文件的最终拼接、整程序链接及输入覆盖由总协调下一步核，不能以本报告代替跨片复核。

本次没有新增资源硬上限：项目计数限制仍不等于DB大小、单字段长度、累计字节或RSS上限；Qj3秒期限不等于CPU/内存/响应时延保证。容量标定批次、42/48实际资料、任务预留后可信runtime和真实页面/主控交错仍按交接列为未闭合或待验。现有对象退出0只证明对象编译；未把它说成完整链接、RK运行、真实仪器或资源验证通过。
