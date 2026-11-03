# 任务提交后续接试剂剩余量：候选交接

本片仅在本目录实现页面、试剂存储和运行数据的专口；没有Root业务接线，没有新设备命令。正式源码、07最终副本、冻结N03和后端纯值头原件均未修改。不能据本片称整条任务流程、设备执行或实物验收通过。

## 底稿及修改范围

唯一底稿为`integration_07_review/stage_with_outer_close`：最终说明`2b4730874ad721db6b66ed44195e2f202adac4e95ee697b23f5062c0b2599526`，663份源码清单`760d81b3fe64f375e9600ed3deb71d6c67699cf2d9f53a82cc03d434a259af26`。`prepare.py`逐文件核对后建立本目录完整副本，后端`OriginalTaskReagents`纯值头逐字复制，仍为4314字节、`3d749c53cb9001a40a0167ba2d0cc16a36399db53f9a62c3ecabfc523e206119`。未混旧06 Store头。

最终源清单、每项改动/新增/补丁指纹及隔离回演结果以`packaging_03/manifest.json`为准；根目录首轮和`packaging_02`均保留为历史，不能取代03。03完整片含16目标（10修改、6新增，其中一项是只读依赖头）；已有冻结纯值头时用`packaging_03/task_reagent_display_consumer_only.patch`及`consumer_manifest.json`，只15消费者目标，不重复新增生产端的同一个头。两片均标准check/apply后核669份最终文件逐字节一致，二选一，不叠加。主要位置：

- `original_task_reagent_display.h/.cpp`：页面采用值回执和纯值凭据检查；不改已有RuntimeBasis/Commit。
- `original_reagent_level_task.cpp`及运行提供者头：冻结完整原依据、登记单槽、任务专用整盘采用、幂等及当前版本核对。
- `reagents_page_task.cpp`、页面头和构造入口：异步专读、页面自有账户/快照/项目核对、三事实和逐孔回执、取消及10秒只读等待收尾。
- `original_task_reagent_readback.cpp`、Store头/原存储入口、Editor头/实现：两锁专读、旧缓存副本认证后整体交换；原纯Snapshot投影抽出共用，未换一套扣量逻辑。
- `reagents.pri`、独立试剂及统一程序的`build.sh`：只增新CPP/头构建项；不改Root动作接线。

## 实际行为和证据链

只处理已有真实提交的Tt预留和大写TR退未用。`used_ul`是任务预扣后的可分配剩余量；SQL实际写孔与整盘运行刷新孔分别核对。即使本次没有used UPDATE，可信前版孔仍采用同次after的实际used。最大量来源、最大量、液量和状态原样保留；不按delta重算、不夹到液量、不重新选瓶或调用分配。无可信来源保持Unknown。当前活动面板孔名由真实控件相对面板可见性取得，排除隐藏旧20位子树；不把配置42当作表有42行，原42位主题实际还含4个SD孔。

Store在项目→试剂两锁内，纯`noPending`，核真实根/原选择孔表及三库前后版本；三库版本沿`OriginalProjectRepository::databaseFingerprint`的DB本体和存在的WAL算法。`postCommitVerified`绑定后端完整typed after及真实三库版本；消费者再核完整typed列/行/原值/NULL/实际typeof链、已知Snapshot投影逐字段及当前三库同版本，覆盖未知列的依据是完整后端链加整库指纹，绝不把17列投影称全行。不是消费者重演writer SQL或从绑定猜SQLite亲和转换。项目/ISI缓存文件或项目集合变化就拒绝，不普通load、initialize或recover；全部成立才交换完整Editor缓存。

Page另核自己冻结的账户身份、原快照、项目已知值/版本/顺序；Provider另核登记的完整basis签名、当前全局/逐孔revision和原身份。当前SHA相同也不跳过这些检查。先构造整盘，确认全局revision余量再发布；成功ACK读取真实current/revision。失败只撤仍属该冻结前版的来源，晚到结果不覆盖更新的Ur/N来源。

首轮独立审发现“先取消、晚到真实COMMIT”不能撤旧值，02增加Provider单槽取消签名：只授权按原完整依据和未变revision撤旧，永不授权采用，处理后消费。真实TR即使只提交results错误事务、used没有UPDATE且Setting SHA未变，仍走同一认证/整盘规则；不能以SHA没变继续信任旧N派生used。完全缺依据时无法安全判定当前源归属，不能猜撤或事后补冻结；Root保留本action的N显示未知门。新Qj和裸N不能解除它，须明确的新Ur/手工独立来源流程；这也不解除CommitUnconfirmed或旧任务身份未知。

02另加真实单调截止检查，回读处理前超过首次入队10秒就拒收；重复排入不续期，PreciseTimer只作事件唤醒兜底。03仅将任务提示变为固定一份，避免连续任务向同一tooltip反复追加文字；未改变量、证据、门和回执。

原空非NULL文本可按原显示转换为0，但保留raw类型与说明；NULL、blob、其他非法或非有限值不补正常0。目标SQL是否允许空量仍由原writer决定，本片不放宽。非COMMIT的原TR弱刷新、较早重复TR等既有差异未接，不伪造committed。小写Tr无试剂SQL，不制造采用。

## 给Root的接口和必需时序

页面公开入口：`freezeOriginalTaskReagentBasis(RuntimeBasis seed, QString*)`、`enqueueOriginalTaskReagentCommit(Commit, QString*)`、`cancelOriginalTaskReagentBasis(token, reason)`；结果信号`originalTaskReagentAdopted(RuntimeAdoption)`。Store槽`loadOriginalTaskReagentCommit(Commit)`，专用结果`originalTaskReagentReadback(Commit, verified, Snapshot, Projects, message)`，不发普通loaded。页面构造时注册三种值元类型并连接专用信号。

1. Root以独立任务门阻N新受理与Qj重签，保持原Ur门；已在途N及展示须真实收尾。不能`setOriginalLevelContext(0,0)`暂闭或在SQL锁内等GUI。保留Page真实连接/许可/来源后，在原Tt/TR入后台队列前冻结。
2. Root填原actionId、arrivalPacketId、connection、root、原包SHA、唯一basisToken、permissionRevision及协议/主状态/实际和配置机型/主题/容量语义。Page填三库/完整原孔投影和可信运行值。actionId须单调递增；本片保持一份待处理basis，不能覆盖。冻结头没有Action枚举字段，Root须以原actionId的到达类型再核Commit.action，不能互换Tt和TR。
3. 无basis或显示未核不阻止原合法任务；Root保留N未核阻断。窄桥的`taskReagentCommitObserved`可先在锁内只排纯值，Root须等Session返回、两锁已释放的原`devicePacketProcessed`收尾，再交Page启动Store专读，避免专读tryLock(0)抢本事务锁。原任务响应照原安全路由发送，绝不等显示ACK才响应。显示ACK不证明加液、扫码或其他设备动作完成；Unconfirmed的transaction原样保留，不能说成committed或已回滚。
4. `committed`为数据库事实；Store的`verified`为专读证明；`snapshotAccepted`为Provider完整链已接到新快照；`runtimeApplied`仅当至少一孔有可信basis，且全部可信basis孔成功采用。Unknown逐孔报告；partial可为快照已采用、部分运行量待核。逐孔`changed`指本次发布或撤销了运行版本，不是SQL写孔标志，也不表示数值一定变化。
5. 入队失败、没有新COMMIT、原动作结束、断线、账户/资料切换或关闭，Root均按原token cancel。freeze到receipt前Page不自行计时，须Root有界收尾；收到receipt后的专读等待上限10秒。单槽last ACK和单槽已取消basis有界；最近回执重复不推进，更早重复安全拒绝，不能承诺都标duplicate。取消后晚到只读结果不套新动作。
6. 采用失败保留真实提交、显示/N待核，不重放、不退款、不清旧Us/Ur未知。成功收尾也须新的真实Qj重新签发N资格，ACK自身不开放N。

## 原Windows对应与验证范围

本片回Windows**只读源码核对**，没有启动既有Windows产品、没有重建Windows程序，不称Windows运行通过：

- `frmmain/frmmain.cpp:541`连接`signalRefreshReagentAmount`到试剂页；样本普通任务构造末尾`frmsamplematerial.cpp:2851`发刷新。
- `frmSampleMaterial::slotTasksErrReturn:4398`找到原任务后调`restoreRgForSingleTest:5743`；后者更新used并于5759发刷新。
- `frmReagentMaterial::slotRefreshRemainder:148–168`遍历全部设置行，在当前盘找到对应控件才调用`setUseVol`并刷新选中详情。`cusReagentHole::setUseVol:483`只改运行used并重绘，不动max/amount/state。原`restoreRgAvaliableTestTimes`处在`#if(0)`，未当活跃入口。

原试剂页CPP SHA `557290538e062e14151dc49749da41f8f8dfcf038779fb3dab921cac90106e79`；原样本页CPP `f9455d9f89b56cc69a86a268b5a0304468f064d7d8e36e84671cee4755fff244`；原孔控件CPP `4301784315c7986174d6c3ff47f14d91837530eefcd3cfcd08253fd45cc75364`。原参考工程、配置和数据始终只读，未用于连接仪器。

ARM对象结果见`arm64_objects_03/compile_result.json`及它回指的02、01：01实际12对象退出0、4903输入前后一致；02重编6个受四源修点影响的对象，逐实际依赖证明另外6个对象原字节复用，4916输入前后一致。03只重编提示修点的页面任务对象，其余11个逐实际依赖证明复用；实际退出码、输入数和SHA以最终文件为准。每阶段都保真实`.d`，不是手填依赖；02/03的Qt头用现有Linux磁盘缓存，`qt_header_equivalence.json`逐项证明等01固定的ARM Qt原头，所有业务头/表单/生成物均来自本自有完整副本，不借旧06 Store或正在构建的正式树。未链接或运行电脑产品、协议/DB测试、模拟数据、联板、部署或占用共享显示。Root接线、完整链接、Windows实际运行核对、RK/真实主控、真实仪器、锁竞争/超时恢复、UI/次数一致性、资源与长稳均待总窗口统一安排。整行/列数与字节规模的时间/内存预算未实测，10秒是显示等待策略，不是运行耗时或资源验收门槛；冻结N03原守卫和资源待验未关闭。
