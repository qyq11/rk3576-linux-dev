# SN100下次任务提示词：板A现场保留，本轮已暂停

当前全部SN100任务已按用户要求暂停。只有用户下一次明确恢复时，才执行下面的接续；不要因为看到计划待办就自行启动。后续测试统一板A，板B用于小车，板名和旧证据不能互套。

工作目录：`C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux`。Root聊天`01a1a05a-73c4-7380-b761-c35a7fdbf76e`。先读根/工程AGENTS、两个项目skill和README，再用rg定位移植记录`sample-order-board-a-paused-20261104`，读板A固定证据README/summary及计划当前卡。保留全部未提交修改/temp/原失败，不清理参考工程，不自动提交推送。不载整本历史或大JSON。

## 先掌握本次结果

- 第15批697源规范f351、完整构建405输入/297对象通过，父正式发布d4501a69。正式程序SHAfd89保持原件。
- 板A检查器只改四行板卡身份，CPP e6116eb4；同批对象编译通过，独立程序SHA fd966bea，57份静态依赖0错误。当前运行的是含检查器的独立验证包`0b0b93787b055f51bba1ac579e0a79488959940284e0a702ab2fa06a1934e743`，不能说是正式程序同SHA。
- 板A实际9项软件检查通过：点加号即时新增并留总览、连续新增、单击仅选中、双击另一未选中卡进入设置、返回仍保留新架、编辑按钮等。随后`choose_other_container_as_unsaved_draft`容器选择步骤超时，check_exit1/completed=false；取消/放弃/第三架/清理未到达。原因未定，不能先归为工具缺陷或产品不标脏。
- 失败后保留本轮S01(id2)/S02(id3)两个空架和当前页面，没有自动删除、放弃草稿、重连、重跑或关闭。实体输入、实屏视觉、真实仪器检测、资源门槛和长稳未验，整个专项未完成。

## 恢复时先只读核板A现场

板A序列`3bf8222fe1417f6a`，ED25519指纹`SHA256:xLQv5sqzTRvKbfD2fNQQLYjCbPN6WOXgkXEoUqV8Yy4`。本次开机标识`559493ce-5319-42b6-9383-bf7b93764a35`；最后GUI5836/启动计数225261/UID1000，原主控4426/196590。IP192.168.100.123只是本次地址，每次先核已知公钥和实读序列，不能接到B或凭旧PID认身份。板A调用器及本次独立known_hosts在`temp/sn100_host/board_a/sample_order_review_20261104_01/`，旧命名的脚本均有拒覆盖和固定旧状态，不直接重跑。

当前run为`/home/rkshare/programs/sn100_shell/runs/20261104_153722_5015`；结果在`reviews/sample_order_board_a_20261104_01`。最后前后同GUI仍活，主程序退出null、VTtty7，主控5000两端ESTAB。用户要求复核后不关闭程序；接续先只读检查，不为取锁/重跑先stop、强杀、重启或开第二份GUI/主控。原监督采用`supervise BASE RELEASE RUN 0 ''`，别换成旧start的30秒失败关闭流程。

A原资料根仍为`/home/rkshare/programs/sn100_shell/runtime/data`。本轮采用用户提供的SN500/5孔主题，旧SN450/10孔主题已有限备份；原config.ini SHA403348a2保持、连接地址192.168.100.123:5000不改，新增两指针指A自己的config/theme。原主控从用户current正确ACA和原配套材料安装到此前不存在的`/userdata/shared`，服务`sn100-original-controller-board-a.service`、Restart=no，未新造模拟端。原包空CSV/校准警告仍待，不能当真实仪器通过。A的sqlite3已作为独立CLI补齐，原系统SQLite库2.7未升级；最初精确依赖安装失败及撤回原件保留。

## 下一次只处理什么

先只读核当前容器选项弹出、可见/启用和检查器命中路径，确认超时原因；已有草稿/空架先核真实身份，不凭架号无条件清理。再按原Windows顺序验证：旧草稿存在时加号先新增，随后Cancel保留新架与旧草稿，Discard只放弃本轮草稿。必要新编译/新验证须Root串行完成具体产物及正常交接，结束继续留屏。不要把最初9项通过扩大成取消分支通过。

沿用既有样本作者`01a1a498-604f-70f0-8eff-0dc019bb22bd`，构建窗口`01a19c13-ad45-72c1-a5fe-736a6660a836`；不重复开功能窗口。其他Task/LIS/ASCII/试剂/状态/告警和全面审计窗口已停，候选冻结。LIS on15只在独立候选重放完成，未采用；B2/B3/共享摘要/急诊主控定义仍缺，算法最后。本次未恢复这些后继工作。

证据：`04_references/sn100_host/board_a/20261104_sample_add_order/`。应用/697源及有限主题恢复输入：`07_backups/sn100_host/board_a/sample_order_20261104_01/`。B首次stage97、file02 false和未完03/runtime均保留历史，禁止改名迁到A执行。电脑只源码检查、ARM64交叉编译和上传，不运行Windows/WSL产品，不做脱机/模拟/回环测试。异常先核实际INI；缺工具可按已有授权补齐，但不能因此改业务或掩盖失败。

---

以下为10:36及更早的历史接续快照：

**11月4日10:36后的实际接续：新版尚未启动，C7保持运行。**第12批完整编译及四文件阶段已0，204固定证据和9恢复实物不改。旧运行链3446的metadata/prepare/preflight均实际0；唯一stop脚本在发任何正常关闭请求前因相机自动重启而79拒绝，不能继续或重跑旧链。先读移植记录`integration-12-runtime-rebaseline-20261104`及`temp/sn100_host/resume_dispatch_20261103/runtime12_rebaseline_reason_01.md`。相机10:30:52无帧退出、系统自动启动新2510511/6498209，非业务INI变化；10:36同boot/C7 669382/2102645、原run/主控5008/3A139148、七配置ad11和三资料指针均保持。新02独立草案在`runtime_batch12_02`，SHA286951d5…，仍false；事实独审ac0c12ec…、Host命名空间独审0861ad11…已完成。原构建窗口正准备全新`runtime_transition_candidate_12_02`，仅改prepare支持完整校验后只读复用已有flat和对应收据二择一，不覆盖旧包或补新链创建事实；作者最终实物、双独审和Root新批准/冻结完成后，才可从新metadata逐阶段接续。新链不能借旧3446的成功收据。旧stop无远端阶段原件，旧run无exit/result，不能说旧程序已关闭。新程序复核完继续留屏。

急诊恢复定义资料按用户要求保持待办，不再重复追问。LIS A9纯计划/ACK候选已双独审，未正式采用或编译；可在本次运行闭合后窄接纳，再协调`shell.pri`、`build.sh`和打包源码目录白名单三个公共文件。参考`runtime12_next_tasks_dependency_review_01.md`；完整Worker等待、实际患者/报告保存和主控任务链闭合前不启生产查询。所有既有窗口沿用，作者独立候选、Root串行公共文件和板卡操作，算法最后。

以下为本轮早先接续快照，保留来源；其中旧链未冻结、未平铺等时态以本页上方和最新实际记录为准，不再执行旧命令：

**11月4日当前接续：第12批已完整编译、独立存包及归档；实际启动过渡仍待审定执行，板端仍C7。**正式688表0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d；新release69220b2eb393408a0c612b43a0fc954e479e8c450a09a48755b3fd2d43b84f72，包SHA2834aaabfb492f0667e55c2f0a97a01be8fbbd3d100dd273ba46eb2f1ecdc687；主程序fb1d42f5a67b0f3663cb3098947ac69d480d4a6784d790bd055ee3dcd5d8ef91，renderer74b8636086d13cefb63d69117fcafdbf9c3fc01c440885d800b5bafd7189c78f。Root cfg d09f…/147证据、source review79b3/progressbd6及五阶段真实0，WORK integration_12_01，204固定证据/9恢复实物完成，禁止重跑已完成阶段。9:47:55仍C7 PID669382/ticks2102645、oldrun20261103_221819_662833，主控5008/118180，相机2046481/5701959及3A139148/847289，七配置ad11/三指针不变。runtime_batch12_01/core_inputs.draft_01.json仍false；Root供审快照runtime_transition_root_review_12_01只固定WIP10代码，未批准生成/动作。实际源码作者最终差异和双独审后，Root另存冻结核心/逐阶段批准、生成再审实际脚本；只正常旧停一次、唯一新版supervise0空session，复核后不关闭新版。用户已把急诊恢复资料列待办，先其他，不重复追问。LIS A9源纯计划/ACK已双独审但未正式采用，不计本688或运行结果。

继续管理RK3576/SN100项目，工作目录：
`C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux`。

先读根AGENTS.md、README.md、`06_programs/sn100_host/AGENTS.md`、两个项目skill、移植计划`resume-dispatch-20261103`当前卡及工程说明。用rg定位移植记录`integration-10-source-freeze-build-20261103`和`integration-10-live-keep-running-20261103`，只读最新段和必要关联，不重载整本历史或大型JSON。当前总控聊天是`01a1a05a-73c4-7380-b761-c35a7fdbf76e`；沿用原功能窗口，多用Agent独审，不新开重复功能聊天。正式源码、公共文件、构建、文档和板卡启停/显示/切版由总控串行处理，作者只写独立候选。

当前板端运行发布`c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e`，应用SHA-256 `50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258`。687份源、395个不同构建输入、291主对象和2报告辅助对象完整编译/依赖/打包、板B暂存及归档/采集/收口实际均0。C7对应源表`temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/ascii_b1/source_after.json`，SHA `f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b`；不要退用685表。Root冻结工具在`integration_10_root_frozen_01/`，实际WORK是`integration_next_02/`，工具03与WORK02有意区分，禁止重跑已完成阶段或复用局部对象。

本批六片是ASCII显示/严格输入、CAL盘面批号、User初始化04、LIS配置来源、留屏观察04、ASCII手动B1。所有未提交修改和temp原件保留；HEAD/origin上次收尾为4fc69a7，当前第12批688源尚未新提交。本轮未新增commit/push或清理，不能沿上次已执行的单次授权再次自动操作。

板B新进程669382、启动计数2102645、22:18:20启动，22:29:50后验同一进程仍在，原主控5008/118180及5000两端TCP连接建立，七配置与三资料指针保持。序列号`8314b1e8ffd0321f`，已知SSH ED25519为`SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`，专用known_hosts在`C:/Users/52400/.ssh/known_hosts_rk3576_board_b`，主机别名`rk3576-board-b`；当前IP为192.168.100.123。启动号当时`0eb94389-f120-42ad-9042-9c21d96a1481`，再次操作须实时核身份，不仅凭IP或旧PID。原主控路径`/userdata/shared/ACA_V1.0OffLineD-500`，SHA `ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff`，不重复启动。

本批被动LIS观察等待就绪6344毫秒、持续15002毫秒，软件1项/0失败，等待器、采集和独立解析0；新应用退出为null，人工实屏未验。实际LIS仍127.0.0.1:3800客户端、未连接，自动获取/发送/模拟均关闭，真实业务pending。不要把这1项扩大为旧26项全部通过；旧25通过/1失败、Qk无完整回包原件保持，不再补发Qk。

复核后保持程序打开。本批仅为载入新包正常关闭旧b31一次，旧APP_EXIT0/clean1只归旧程序。新启动用既有`supervise BASE RELEASE RUN 0 ''`，显式三观察环境，避开旧`start 0`的30秒READY超时TERM；新等待器超时仅退出自己。不要在检查结束调用stop/close/quit或再开第二GUI，也不要为了取锁关闭程序。新的必要切版先完成新包和归档，再由总控串行正常交接，最后继续留屏。监督异常和外部停止仍可能结束进程，不能把0理解成永不退出。

固定文件阶段证据在`04_references/sn100_host/workflow_next_20261103/integration_next_02/`（238份清单），正式程序和源包在`07_backups/sn100_host/board_b/workflow_integration_20261103/integration_next_02/`。本轮139份运行原件和独审在`04_references/sn100_host/board_b/20261103_integration_10_keep_running/`，先看README/summary。运行准备首轮远端79是内核线程D→S被误判，新的运行脚本只修两次PF_KTHREAD/启动身份分类后0；旧模板、九工具、50b配置及原失败未改。不重跑旧切换脚本。

具体后续沿下列窗口分发，先查其最新交接和可实施提示词，不只照模块名字开工：

1. **通讯测试ASCII、维护窗口** `01a19c14-9c1b-7d83-b4d5-488676441f93`：用户要求仪器操作→通讯测试收发使用ASCII；显示/严格输入和B1六正文已在c7，真人输入、控制字节、光标/500块裁剪及实际收发仍待RK，不假登录。Uh作者六源和原补丁保持冻结，不再重做A/B。Root已将六片窄合当前688，独立候选`temp/sn100_host/workflow_next_20261103/root_ascii_uh_candidate_12_01/handoff.md`，patch d89e920d…ba71c15、合成预期0e5c70ef…32ac8d、普通私有check/apply0及双独审通过；它未计第11批，现已按第12批实际接纳且完整688后像一致；下一步冻结第12批来源与构建，作者不追正式复制工程、不联板。只精确Uh，依托原receiver/原页面/原根/登记代次实时能力调用maintenanceCommand，入队与独立被动保存批次分开；保留Mg/B1/User守卫，底层探测期间Uh仍拒绝，不扩其他正文、维护白名单或C参数。
2. **试剂操作窗口** `01a19c11-446b-7e20-a933-ab266d697f78`：freeze04十三源、`handoff_04.md`和原补丁保持不动。Root已将其与B1两份主窗窄合、登记新纯值头，形成15项实际接纳；组合01误挡查询/消音的问题在02修正，两独审通过，正式688/b078已含本片，当前已完整ARM64编译并独立存包，尚未启用。不要再次应用作者旧累计补丁或整树盖回。保留SourceLease、真实N提交/页面采用/reader收尾、期限和取消边界；现运行c7仍不含Mg，不发探测。真实回包/N先后、None/Unknown、整盘/扫码、42/48和仪器条件分别待验。
3. **任务、用户保存和QB窗口** `01a19c11-c651-70e0-8359-97fb01e76b26`：先读`temp/sn100_host/resume_dispatch_20261103/user_emergency_next_prompt_20261103_01.md`及`user_emergency_save_candidate_20261103_01/revision_06/`最新交接。首次未发普通样本急诊的独立18源部件仍基于历史f741+覆盖，不准盖正式688。01预检/取消/发送问题、03取消调用链、04关闭等待调用方缺口分别保留；05本函数guard未解决外层原closeEvent成员引用回栈；06仅改等待说明为Root上下文的延后通知，Root指定回栈独审通过，04业务旧三问题修复独审也已通过。完整9ecb虚拟687来自旧f741的669不变来源+18物理覆盖，不能称06做了物理687重放。用户已把可信主控会话起点和Emergency连续无旧占用依据列为待办；页面保存入口未接、consumerReady未开，不能说急诊闭环完成。不得造Grant/确认按钮或用本地空账/Qj替代来源。原SS盖与Start盖不同，真实COMMIT、页面采用、动作退休、一次Tt[Emergency]分别闭合，真全skip为NotStarted/Tt[Emergency][0]；不走普通None/Ts、不自动退款或重放。Start立即/在测追加/CAL-QC仍后片。
4. **LIS窗口** `01a1a000-817e-7080-b578-b04fbc1ba828`：原三来源文件在c7，同次autoGet/canonical原根两文件已正式接纳进688/b078并编译/独立存包，尚未启用；五份QRY/DSR纯字段仍候选。按Root等待事实审9365071a处理：GUI的dispatched在invoke前置true，不证明Worker认领或SQL开始；basis只是显示前版依据，false不证明未写。未来无写等待保留原值及10秒语义，不跨异步留栈指针/receiptObserved引用、不持SQL或项目试剂锁等网络，不重新冻结续时。原真实Tt→查询→DSR字段应用、保存、原ACK和任务闭环尚未接，query-only不启生产。固定旧profile/request/generation/peers，入队不是送达；无真实对端保持待验，不模拟或旁接第二消费者。
5. **状态/登录窗口** `01a19bf9-e3af-7b11-bd1a-377137f640a9`：P04及本次证据冻结，不重跑旧自动关闭检查器。当前原件已证明本段软件及留驻；真人登录/注销、容器初建和实屏仍待。暂缺板B键鼠，不追问密码，不用模拟身份替代。
6. **构建/归档窗口** `01a19c13-ad45-72c1-a5fe-736a6660a836`：最新交接索引在`temp/sn100_host/workflow_next_20261103/build_archive_validation_11_02/handoff_manifest.json`。9代码已由Root原字节复制到`integration_11_root_frozen_01/`，真实cfg051977…/145证据双审匹配，候选false模板保持。Root第11批完整构建及stage/archive/verify_and_save/finish均已实际0，新WORK统一`integration_11_01/`；先查finish_complete.json和202份固定证据，禁止重跑已有阶段。只独立存包、不写current/不启停；C7、原主控、七配置与三资料指针前后实核。旧第10批工具/50b及旧失败原件不动，作者不自行编译联板。

原当前告警窗口保持冻结，不重发Qk。相机窗口`01a1a01a-6d15-7bd1-8db2-750cb9b2f3e3`已释放本轮操作协调；当时相机服务455358/8080、3A139148，原720p/10帧声明与本次服务身份分开记录。下次切版或资源窗口提前协调，不默认关相机/3A；145个启动资源样本不代表资源/实时控制门槛通过。算法最后。

派工后已确认的边界：急诊首片仍缺“可信会话起点+Emergency连续跟踪”的生产来源，prepared_/普通已发账、本地空集合、Qj新鲜或空报告字段均不能证明设备无旧急诊；不能造永不成功的空资格接口冒称闭环，也不新造确认按钮。第六轮候选冻结，实际准入未闭合；该资料依赖现列待办，先继续其他任务，不重复追问。原不足确认后全任务跳过可能真NotStarted并原样Tt[Emergency][0]，不能伪COMMIT或转普通Ts；其他已reserve未TD的架须按真实issued/inFlight投影，避免预检再算已扣需求。

LIS新两源原交接在`temp/sn100_host/lis_profile_source_candidate_20261103_01/handoff.md`，已由Root正式接纳；它的历史合成687表不能当当前688。root4096与sequence4096、16存活版本最多256KiB只是逻辑文本预算，非RSS；TaskInitialization的原字节SHA与LIS canonicalpath+NUL+bytes算法各自校验，不能直接互比。

Uh预审明确main登记点早于后台start，只做结构登记，发送时才核真实ready；receiver里的consumer_非空也不证明其捕获的原仪器页存活，Root须保留原页QPointer并核仍是当前页。缺/换页只拒主动Uh，不停被动保存后台，不重绑旧消费者或造ACK。

电脑仅编辑、源码检查、ARM64交叉编译和上传；不运行Windows/WSL产品，不做脱机、假包、回环或模拟测试，参考工程只读。RK使用原配置、原资料和原主控；异常先核实际INI，不重复启动。缺必要工具可在已有授权范围补齐并验证。编译、RK软件、真实仪器、实体操作和资源分别登记，不把准备下一片当作关闭当前待验。
