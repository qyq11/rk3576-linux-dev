# 合法配置单孔Mg最终候选：总控独立业务源码审查

2026-11-03，审查者`/root/review_amount`。本次用于核对“在试剂页选中一个已配置孔、确认探测后，实际发哪根针、怎样分别取得动作和液面数据结果”。仅只读最终稀疏候选、直接必要的正式依赖及原Windows源码，另写本报告。没有修改正式/候选，没有合补丁、构建、运行Windows/WSL/RK产品、开业务库、执行SQL、模拟、网络或板卡动作；当前正式687份源码的构建未被本审查触碰。

**结论：发现一个确定的选针错误，以及一项仍需闭合的原输入遍历差异；当前freeze_03不能据本报告放行为“合法配置单孔均与原Windows一致”。** 两项均已通知Root。下方来源租约、异步N和收尾中已经核实的范围仍成立，不把这些问题扩大为全部链路失败。本片不属于当前在构建的六片，不建议向当前批覆盖应用。

本次实际绑定：

| 对象，相对`temp/sn100_host/reagent_single_probe_20261103_01/` | SHA-256 |
|---|---|
| `handoff.md` | `e2f9e234cfd10aa22afc2cf6b14e65e8fcb0ba85af2171b79717d4aa7854ffe3` |
| `delivery_manifest.json` | `4b834e28dd3ee71bd3759b016b34944194498fe442515474086d5e292097e93a` |
| `freeze_03.json` | `1f4b9034f1820a1df127a2b6555bcd845fbd33aac40b87b77268afd9b6795678` |
| `packaging_02/single_probe_on_b688.patch` | `2c3723630dd749cf684d5efacdd852726a6a92526bf9403c65cb24927ab61e4d` |
| `candidate/06_programs/sn100_host/reagents/original_reagent_editor.cpp` | `2490d6c3262cc740d4425d49513d96104483ea677c31a63e05c3ae6802d50258` |
| `candidate/06_programs/sn100_host/reagents/original_reagent_store.cpp` | `bb5289cc9e0ff08084310714383a45954cd350b87b88ca0ef2e7f19085bf6f2c` |
| `candidate/06_programs/sn100_host/shell/device_control_shell.cpp` | `3f27d2c90491aea5b519b5612b301c02d27bace72d0956f7f9fe034e0b23893e` |
| `candidate/06_programs/sn100_host/reagents/reagents_page.cpp` | `1f63788de14e9acb49b2a2d309b5309de89fed95455d1839393c07bf168ecd03` |

其余9份也逐份计算了SHA，最终13/13等于delivery_manifest的`source_sha256`及freeze_03；没有把旧packaging_01作为最终交付。下文候选行号均相对该目录的`candidate/06_programs/sn100_host/`；原Windows根为只读`06_programs/sn100_host_reference/Seerain450_PC/`，依赖正式源码均指`06_programs/sn100_host/`。

1. **[P1] 正常原库左针值被当成右针，不能发送该候选。**

   位置：候选`reagents/original_reagent_editor.cpp:683–690`，尤其第688行。

   实际生产数据链是Editor `load:418–436`调用正式`OriginalProjectRepository::load`得到`projects_`；正式`projects/original_project_repository.cpp:229–236`的`actionFields`只是把数据库原字段`toString()`放进动作字典，第302–303行将其直接加入`advanced.reagents`，第744–775行又直接发布这些ProjectRecords。该链没有把针号原值转换成中文。

   原Windows不是直接对原库值判断中文：`frmsystem/frmsystemmaterial.cpp:2120–2127`读`project_reagent`后，先将原值精确等于`"1"`的字段改为“左试剂针”，其余改为“右试剂针”。随后`frmreagent/frmreagentmaterial.cpp:801–809`才比较中文并生成1/2映射。最终`communication/commandsender.cpp:198–212`按该映射写针号。

   新候选第688行把原值直接与“左试剂针”比较：正常原库值`"1"`不等于该中文，实际会落到`2`，而Windows会发`1`。这是静态代码可以确定的转换遗漏，不依赖实际数据库中两种顺序是否恰好相同，也不是未配置孔/坏包分支。反过来，若库里误存中文，Windows原转换会选右，候选却选左，进一步说明两者消费层级不同。

   最小修正：在纯Mg准备的局部映射中承接原Windows“原库字符串精确`"1"`→1，其余→2”的转换，再按原遍历覆盖；不要为此全局改变ProjectRecords的原字段含义或任务/CAL消费者。新入口第688行必须修；同文件第888–889行的旧设备准备分支有同型比较，需在其未来启用时按同一原数据来源处理，不能因当前只启单孔而将其称为已验证。修后重新固定源、补丁和独立审查指纹；本报告没有代作者改代码。

2. **[P2] 原针号覆盖依赖的输入集合、步骤读取顺序尚未完整承接。**

   位置仍是候选`reagents/original_reagent_editor.cpp:681–688`，及其`orderedOriginalProjects:136–157`。

   项目编号与`sendSequence`的外层排序已经找到直接依据：原Windows `frmsystem/frmsystemmaterial.cpp:715–740`先按`xuhao`，再按配置前缀稳定排序；正式ProjectRepo以`project_base/xuhao`形成`id`并排序，候选读取同一个初始化来源里的`sendConfig/sendSequence`再稳定排序。这部分相符。

   但有两处输入差异：

   - **步骤行顺序。** 原Windows `loadProject:2120`调用`getData("project_reagent", ...)`没有附加排序；`customtools/sqlbase.cpp:774–776`实际构成`select * from project_reagent`并依查询返回顺序append。正式ProjectRepo第302行明确使用`ORDER BY xuhao,rowid`。新Mg直接消费这份已重排缓存，未保留原查询顺序。原`dectLV:803–809`与候选都让同名试剂的后一步覆盖前一步，因此合法项目存在同名试剂、不同针号，且实际原返回顺序与`xuhao`顺序不同，就会改变最后采用的针。正式schema第199–220行只核必要表列，没有约束这两种顺序恒等，也没有禁止这种重复试剂步骤。本人没有开库或执行SQL，不能声称现场原资料已触发；这里确认的是代码已选择不同的读取规则，当前一致性证明没有覆盖该域。
   - **项目文件过滤。** 原Windows `loadAllProject:698–704`实际跳过`dbName.contains("formula")`的文件，大小写规则沿原调用；候选`orderedOriginalProjects`及新Mg遍历未作此消费过滤。现正式ProjectRepo第691–705行只筛`.db`后缀和原有效性，未排除这类文件名。因此若合法已加载项目文件名包含该串，它可能在候选中覆盖针号而在Windows不参与。不能把“缓存来源集合完整”误写成“原Mg消费集合相同”。

   这两处底层缓存差异在当前正式依赖里已经存在，本片首次把该缓存接成真实Mg选针消费者，因此须在本片一致性范围内处理；本报告没有将整个项目仓库重新定性为失败。最小改法是在Mg局部保留原有效项目过滤和原`project_reagent`读取顺序（可在同一后台、同一来源锁与版本核对内捕获专用原遍历值），再执行第1项针号转换；不要全局删除现有任务/编辑用途的排序。若选择先限定成功域，也必须有实际可检查的等价条件并如实标为局部功能，不能据此报告全部合法配置孔完成。

   同时保留`probeProjectFilesMatch:160–178`对**完整缓存/版本/目录集合**的核对。该完整性核对防止新增文件给旧缓存续签，与Mg按原Windows过滤参与选针的项目是两个职责；不能为了过滤业务遍历而缩掉来源证明集合。

3. 本次独立核实的编码、来源和准入范围。

   原单孔活跃入口为`frmreagentmaterial.cpp:827–831→724–763→801–824`：先原动作就绪、对应区域门、询问确认；No不发。候选Page `requestOriginalDeviceOperation:445–488`保留确认并在确认后复核，Root `beginReagentProbe:1300–1324`另核真实连接、原账户、同根配置、孔位/机型/门、任务/事务/读者、未保存输入和独立N运行来源，之后才发一次10秒准备许可；并未仅凭按钮可用进入发送。

   在上述选针输入问题以外，候选复用正式`OriginalReagentRepository::detectionPayload:904–944`：`Mg[1]`之后依次为孔名、试剂名、区域、针号、发送孔号、瓶型、底径、角度。瓶名第一项匹配、无映射的区域后备针、发送孔号来源和底径/角度原文仍可追到原`commandsender.cpp:170–219`；正文最终用Latin1，不写token、lease或本地请求号。正式`appendWire:59–69`会拒绝不可表示文字、括号/控制字符和超长，属于已有严格保护边界，不能称已覆盖原Windows弱转换的全部输入域。DeviceControl第477–503行再核一孔8项、原连接、原准备期限、当前状态和区域门，没有把Mg加进通用配置/维护白名单。

   Editor第368–438行先捕获三份INI同次原字节与解析值，再交内置Repo；读缓存前后核同一`InitializationSource`，`orderedOriginalProjects`不重新读新INI贴给旧缓存。`prepareSingleProbe:647–656`分别以INI原raw SHA和DB/WAL版本方法核对，不混用算法。Root第1337–1353行检查准备INI当前性、批号View、Page原typed孔、Setting版本以及N主题指纹；根目录、连接和已同步配置状态继续受第1229–1232行门保护。本结论没有扩大为任意外部文件替换都能被两把锁阻止，也没有宣称整个原设备同步模块都重新验过。

   SourceLease已独立顺读：Store第201–210行在后台按项目→试剂取原两把合作写锁并核noPending，不在持锁准备中initialize/recover；Editor核完整已消费版本与实际项目集合。Prepared的不可变proof绑定Request、typed全孔、表、正文、全版本、初始化指针、完整批号View；第68–77行以CAS只认领一次。未认领到期先撤可用状态再释放，已认领到期/关闭只撤live而保锁（第95–120行），直到Root `ProbeSourceRelease:27–37`在实际send调用栈退出后排同token worker release。旧/重复token不能释放新lease。该锁范围只证明遵守两把锁的写者到这次send返回为止，不证明整个机械动作期间DB永不改变。

   纯准备只capture/current批号模型，无CAL Default发布、孔位保存、清孔、扣量、退量或任务号分配；Us、Ur、任务N/M和CAL票据没有被变造成探测意图或动作回执。

4. 本次独立核实的Mg、真实N、Page与读者收尾。

   Root `routeReagentProbeReply:1423–1455`接受当前原连接唯一等待窗内非空Mg字段，按原最后字段大于0记动作成功；不虚构设备回显token，也未只认造出来的`Mg[1]`单项格式。错误/冲突/过期回报保留未知，原协议没有请求号导致同连接迟到重包无法绝对区分的限制仍在。N先到不能单独把动作标完成；Mg先成功到仍等待N，而失败Mg不回滚已到的N。

   `shell/reagent_level_shell.cpp:278–342`仍先走原N解码、任务/同步守卫、运行来源和现有ObservationWorker；只对同owner、原孔/试剂/区域/瓶型/针号/发送号、同connection签发具体packet grant。Root自身Page busy和Page `enqueueOriginalLevelPresentation:251–263`只为这个grant作窄例外，未取消其他busy、事务C或其他reader。

   第一轮“自己的专读被当外部reader”对应的修点已经实读：Root blocker第1250–1260行只排除同nPacket/connection且确有登记、没有其他未处理动作/reader/事务/overflow的自己的reader。任务N入口原阻断保留。`processed`、Page采用、实际reader返回分别记录；正式worker第525–526行先发presentationReady再发processed，Root在同Store专用读取后排队哨兵，第399–409行才确认实际槽已返回，未用Page提前拒绝冒充读者返回。

   第一轮“入口先采用过期消息再续时”及第二轮Page提前计时修点也已直接核实：Mg入口第1429–1430行、N入口与grant前、N续时前以及最终成功判定都核Root真实单调60秒；Page `beginOriginalProbe:1617–1619`为该timer选择PreciseTimer。发后重排60秒，只有本孔有效N且原灯为Optioning才续时。精确定时不代表硬实时，实际调度和资源尚未测量。

   `finishReagentProbeIfReady:1522–1549`在有N时等待processed、Page结束和真实reader全部收尾；成功另要求真实提交、后台读回和Page采用。取消/断线/超时/来源失效会停止新grant、保留可能入队的独立unknown，已经派发的事务和读者照实际结果排空；没有补Us/Ur、重发、伪回滚或通用Qj清未知。关闭入口先取消并等待owner归还，未用终止工作线程清忙。此为静态可追链，不是并发/真实仪器已经验证。

5. 后续边界。

   本人仅点核12份前像与当前正式文件：10份仍等候选baseline，`shell/frmmain.h`和`shell/device_control_shell.cpp`因正式B1已变。这与交接所述公共重叠相符，不是重新扫描或重建整个687源表。下一片须以Root当前正式版串行合并这两份文件，保留B1的手动开始/暂停、User来源准备及真实NoCommit收尾；原b688补丁不能直接整批覆盖f741。新增纯值头也需进入后续正式完整源码/构建输入清单。本报告未做该合并，也未替合并后字节给通过。

   作者三轮报告保留原件。本报告中的第1、2项来自本人直接读真实生产值链和原Windows实现，不沿用作者“未见阻断”作为结论。第1项优先修，再窄复核原遍历与转换；不要因只列13文件就停在错误的输入适配，也不要放宽白名单/门/孔数或省掉真实N消费者。

   作者`windows_source_recheck.md`后部列有“使用已有Windows程序实际核”的步骤；它不能改变用户“电脑仅编辑、源码检查和交叉编译，不运行Windows/WSL产品”的授权边界。后续原版依据继续用只读源码、既有截图和用户已有信息；实际功能、真实Mg/N先后、负回报/无N/部分N、取消/断线/关闭、锁竞争、资源与同板影响仅按Root获准范围在RK和真实仪器分别登记。当前所有ARM64编译、RK运行、真实仪器、实体操作、资源和跨进程未知恢复均未因本审查关闭。
