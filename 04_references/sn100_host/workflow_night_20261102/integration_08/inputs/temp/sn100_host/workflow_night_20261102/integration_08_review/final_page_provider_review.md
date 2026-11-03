# 任务试剂量：最终页面、运行来源与接收守卫只读复核

2026-11-03。本次只读检查已冻结的Root增量。发现一项确定的P2：提交未确认时，长期保留的显示遮罩只登记任务前已有可信运行量的孔，漏掉同盘原本没有可信量、后来被真实回读确认的孔。该问题尚未由本报告修复，不能给本片整体无阻断结论。

本次只新增这份说明，没有修改候选、正式源码、07、早审报告或其他交付；没有执行补丁、编译、运行产品、打开数据库、构造测试资料、联网或连接设备。下述触发顺序是源码中可达的调用链，不是仪器复现记录。

## 输入及读取范围

源码根为 `C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/task_reagent_root_candidate/candidate/06_programs/sn100_host/`，下文短路径均相对于这里。

| 固定输入 | SHA-256及核对范围 |
| --- | --- |
| `packaging_01/task_reagent_root_on671.patch` | `5cf43ed58e3f7378cb80e886ab87d4d8afd3e923dee5f65a2c49ba24a4101c6f`，读取前后相同。 |
| `packaging_01/manifest.json` | `6299638325a01786b91c0dd719175ac9d9e9d4371b5d34587e1e5e2e4e2d6c84`，14改、3新，共17目标均与实际候选SHA相符。 |
| `packaging_01/final_source_snapshot.json` | `cb86ca5fa03914db8598c2c477a4a641cb0bff34cc216c24944c9f3a437df77f`；实际回读candidate、patch_source、replay_on671三树各674项，所有项均与该清单相符。这里没有重新运行check/apply。 |
| `arm64_objects_02/compile_result.json` | `53cc1475303dbaafad2ab3c087ad318e2ebf702b997ad214c3b4f06369d5e271`；只核固定文件及其38对象、4970输入、inputs_unchanged=true、对象exit均0的汇总，不冒称本审者编译或重新验收全部对象。 |

`handoff.md`、`final_review_page.md`、`final_review_source.md`只用来了解作者声明与其他窗口审查范围，以下结论由实际源码独立核对。阅读中与收尾回读的17目标及相关旧文件均无指纹漂移。

## P2：事务待核范围被“可信显示前版”缩小

位置：[`shell/task_reagent_shell.cpp:87`](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/task_reagent_root_candidate/candidate/06_programs/sn100_host/shell/task_reagent_shell.cpp:87)至94行，及197至203行。

这里的“事务待核”（作者简称C）表示无法确定原事务是否提交，或无法确定它属于哪一个原动作。它不同于“运行量显示缺依据”（作者简称B）；后来读到的液量能建立新显示来源，但不能证明此前事务已经核清。

当前代码存在以下完整路径：

1. `RuntimeProvider::freezeTaskBasis`在`reagents/original_reagent_level_task.cpp:165–174`逐孔登记available，最后仍可将整个basis标为available。盘中A孔已有可信运行值、B孔尚无可信运行值，是合法的冻结结果；basis包含两孔，B的available为false。
2. 原SQL业务并不以显示basis.available决定选瓶或扣量。`samples/original_detection_session.cpp:299–409`的reserve保留原SQL流程，333行的captureBefore明确只核显示凭据。因而B没有可信GUI运行来源，并不能证明本次事务不会写B。
3. 后台返回真实`CommitUnconfirmed && !committed`时，`task_reagent_shell.cpp:412–419`登记C并调用foldTransaction。87行仅在缺全局basis、冲突或既有unscoped时设整盘；88–93行只把逐孔available=true的孔加入fact.holes。有A时，94行不会将空范围升级为整盘，于是这笔C只记A，漏掉B。
4. `syncTaskReagentDisplayUnknown:197–203`只将该C的已登记孔发给页面。活跃动作的208–210行同样只收可信孔；动作结束后则继续使用已归档的subset，并非临时现象。
5. C仍允许原真实Ur继续核实来源。`reagents_page_level.cpp:303–315`处理实际确认；`original_reagent_level_runtime.cpp:402–432、445–449`能为原来无可信运行量、但字段齐全的B建立新的ConfirmedUr运行值。此路径不要求B曾有可信前版，也不需要把DBmax补造成来源。
6. Source没有删除C，这一点正确；但B从未进入该C遮罩。`reagents_page_level.cpp:437–454、486–503`只跳过遮罩内孔，随后会显示已确认B的量，并在既有次数来源满足时显示其次数。这样同一笔未确认事务仍挂在C里，B却已恢复为确认显示，而没有证据证明原事务不涉及B。

B可以因非N失败的来源撤销而暂时不可用，后续真实单孔或整盘Ur再确认；上述路径不依赖`failedOriginalLevelHoles_`中的失败N孔，也不依赖任何假造信号。Root对新Tt/N的全局C阻断仍存在，这不补足C显示范围的缺口。

**最小建议：**对提交未确认、归属冲突等无法证明具体写入范围的C，直接保留整盘待核；或用能够独立证明的完整实际活动孔/事务范围登记，不能用`HoleRuntimeBasis.available`筛范围。该标志只说明能否继承任务前显示值，不能证明SQL是否影响该孔。C范围不应因真实Ur、手工确认、角色变化或B恢复而缩小。本报告不修改业务，也不提出新退量、补偿或重放规则。

## 早审的三个来源与身份问题

### 独立任务身份已与正常状态、N观察许可分开

- Root的`sameIdentity`在`task_reagent_source.cpp:37–45`只比较真实连接、工作根、机型协议、主题/容量来源、孔数和容量标定状态；`taskReagentContextChanged:285–350`另核实际配置与主控值、两个原资料根以及六项账户身份，再推进独立revision。mainStatus、actionReady、N许可号、Setting库SHA都不参与这一身份比较。
- 六项账户身份见`task_reagent_shell.cpp:19–22`和Page的`reagents_page_task.cpp:30–34`，两处相符；roleLabel只是显示文字。Page `setSession`仍按原规则处理actionReady和设备按钮，不将它变成任务身份换代。
- 原Qj观察上下文仍在`reagent_level_shell.cpp:17–24、178–205`按原mainStatus和真实字段续签；新签的N permission没有贴到旧任务basis。任务seed在`task_reagent_shell.cpp:351–359`保存原到达身份和原mainStatus，后续冻结不补抓新包身份。
- Page的`originalTaskIdentityMatchesBasis:186–194`核独立身份；`originalTaskReagentContextMatches:465–481`核原会话、Provider实例、三库原行、项目版本和原Provider前版。正常Ready→Testing不会仅因mainStatus变化撤任务basis。

所读范围未见把TaskIdentity.revision与N观察permission混为同一代次的剩余阻断。

### 真实换算语义变化已撤销旧运行资格，未重算最大量

早审指出Provider没有保存N原Context、不能只清视图。本冻结版本采用保守失效方案：

- `reagents_page_task.cpp:47–62`把连接、根、机型、协议、主题、容量文件/存在状态/指纹、孔数和容量标定状态定义为运行换算语义；不把账户、available或普通revision当物理换算变化。
- setter在`261–281`先取消旧basis；已有完整旧语义且这些实字段变化时，269–270行调用同一Provider的invalidate。随后清全部视图，未标记的旧N视图也不能被重新贴到新身份。
- `original_reagent_level_runtime.cpp:306–316`保留原数值，撤available、将maximumSource置Unknown、通过put推进逐孔revision并清旧转换缓存。`observeSnapshot:319–381`在同根普通重读时不会恢复已撤销的资格。新冻结的165–169行仍要求current.available和明确maximumSource，不能把保留的旧数值签成可信量。
- 原N准备过程在`original_reagent_level_model.cpp:214、227–269`沿用先前maximum，只按真实Context更新amount、条件used和state。maximum原来源仍是瓶型初始化、已确认Ur或手工来源；不能把它说成“N容量换算出的max”。纯角色变化只清视图、取消采用，不擅自改这些物理数值。

本版本关闭了“真容量/协议等身份改变后，旧N amount/used/state仍获新basis资格”的早审问题。结论以已经由Root登记的真实TaskIdentity改变为条件，不宣称外部文件的任意修改都能被瞬时检测。

### 晚到提交及未确认专口只撤原签名、同实例、同孔版本

- Page `rejectOriginalTaskReagentCommit:572–592`分别认证活跃或有界取消完整basis，并核Provider生命周期token及原工作根，不用重建后地址相同或数字revision相同冒认旧实例。
- Provider的basisSignature在`original_reagent_level_task.cpp:84–95`包含完整seed、三库、逐孔原行与运行字段；`rejectTaskReceipt:234–257`只接受已登记活跃/取消签名。实际撤旧要求原根与逐孔原revision仍相等；新Ur/N/手工来源经put推进revision后不受旧结果影响。
- `markTaskUnconfirmed:217–231`要求原事务真为CommitUnconfirmed、committed=false、合法动作、非空回执、根及原包SHA一致。没有将未知改成Committed，也不以未知after补成功采用；完整原basis不符不会消费当前签名或撤新值。
- Root `observeTaskReagentCommit:395–424`还核原expectedAction、actionId、连接、根、原payload SHA及完整basis，再保存事实。Page只在真实processed后排读（`advanceTaskReagents:515–536`），不会把锁内callback到达当Source两锁已释放。
- Provider成功采用在`261–350`核完整前后行链与三库，只采用真实after used，保留可信maximum来源、maximum、amount及state。重复凭据不重复推进版本，旧凭据不会覆盖更新来源。

除前述C范围问题外，本次所读未见晚到真COMMIT或Unknown专口撤销后来新Ur/N来源的确定路径。

## 新N关门与原观察、读取及遮罩

- Worker `setTaskAdmissionFence:189–213`只关闭新准入，不撤permit、pending或运行快照；开门后仍等新真实Qj。`submit:335–357`在解码前后分别持mutex核门及token变化，旧包不会趁解码窗口进入。
- `currentLocked:394–400`不含taskFenceBlocked/observationAdmission；已受理N仍按原permit、真实新鲜期限、readback门和同一运行来源收尾。真实mainStatus或资料变化仍可按原语义使它失效，本片没有承诺在所有真上下文变化下强行完成。
- `setTaskObservationFacts:216–268`在匹配原连接和任务令牌时更新真实Qj事实，保持新N准入关闭；`isContextCurrent:271–275`不看新N门。Root `updateReagentLevelContext:185–205`确实走该入口并同步Page观察上下文，因此任务门本身不阻止真实Ur取得其原观察身份，也未借setReagentReadbackPending(true)充当任务门。
- `taskReagentNBlocker:232–242`持续阻断C、B、原任务及nReaders在途；`reagent_level_shell.cpp:334–375`在Page成功排N专读后向同一Store排内存哨兵，仅实际槽返回后归还nReaders。任务冻结及关闭仍等待该记录，不以页面超时冒充实际读者退出。
- Page `setOriginalTaskReagentDisplayUnknown:300–327`只接受当前任务身份、根和页面会话，将非法范围升级为整盘；仅影响显示，不变更SQL、账户、Provider或N许可。renderer同时处理孔量、详情和相关项目次数，次数页重建后再次遮罩。C与B记录在Root结构中分开；Source恢复路径没有删除transactions的语句。唯一所见缺口是上文C最初登记范围过窄。

## 源码指纹与结论边界

除17目标已逐项对照manifest，本次深入读取的关键文件如下。前后SHA相同；短路径均是前述实际候选根内文件。

| 文件 | SHA-256 |
| --- | --- |
| `reagents/reagents_page_task.cpp` | `26deafea7b4bcd3f6571fe203500d40fdaa1daa07d17048230d0709a4edd99d8` |
| `reagents/reagents_page_level.cpp` | `0e76757c4cc7743c1101c77b0ff5c2ef6c02b907edceff9bac145697d8df77a7` |
| `reagents/original_reagent_level_task.cpp` | `fe180cfb1985d7023d3f58082f222720a0fe56d21a4de6988bc49c620400d4b6` |
| `reagents/original_reagent_level_runtime.cpp` | `9637a3513c79a6ea134b523cc25cc436f84300df48293175d39c7c9beeeea22e` |
| `reagents/original_reagent_level_model.cpp` | `0d83d1fb4cb7b466b6fc056a20d7cc13789b390f88e173802fa75e877a865f25` |
| `reagents/original_reagent_level_worker.cpp` | `eb54a1b10f8b01b157a68fd91c245ed46d0ba9ea3247a82ae79f334aabf3ce89` |
| `shell/task_reagent_shell.cpp` | `ed3d788aff3519ac7ab13c869c6d87a9aa2d4025eef13dad07d1cc635452af10` |
| `shell/task_reagent_source.cpp` | `9871b1967f0312302812bb57a02849082b471ee1b165f3395ce1b3432063c768` |
| `shell/task_reagent_context.h` | `29adc84dbb0af7ba48ccaca99c99615749a4c1453afaf41d427d705e5311eda2` |
| `shell/reagent_level_shell.cpp` | `a75781e4b83db0294f8c850227b550286c8db09118c001976e62b9c370f62bc4` |
| `shell/device_control_shell.cpp` | `7f87cadc6fa8d68b6f8d68339644de0854b23d80b0097c351fc56c84837024e5` |
| `reagents/reagents_page.cpp`（原消费者） | `bfc9a6908885d25c4a4a6ea14fbbcc7913b82c483584368b2153cff7f5d8af3a` |
| `samples/original_task_reagent_receipt.h`（冻结纯头） | `3d749c53cb9001a40a0167ba2d0cc16a36399db53f9a62c3ecabfc523e206119` |

本报告只对冻结5cf43ed5补丁及上表读取范围给出结论。当前进程的C记录没有持久化，不能称跨重启已核清或恢复。无新COMMIT的重复TR弱刷新、原g6/g15精度与remaining净差账的已声明差异保持待办；没有在本片发明新退款或扣量规则。RK、真实仪器、量/次数交错显示、真实提交未确认和退出恢复均未由本审者验证。
