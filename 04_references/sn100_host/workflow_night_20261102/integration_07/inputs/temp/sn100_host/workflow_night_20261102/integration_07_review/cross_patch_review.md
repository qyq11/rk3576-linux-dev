# 第七批隔离树：N03与保存生产桥跨片窄复核

结论：前两片三份共享文件没有新增确定交互退化，N的专用读取/互斥/失败续调与producer的预留期限/真实提交通知均保留。后补确认的原N03关闭目标P2已由独立第4片修正，第3片只补真实关闭判据日志。最终四片的固定源码并集未见本范围剩余阻断，可进入总协调的完整构建；没有把本地保存伪报设备就绪，也未新增重放N或盲重发Ur。此结论只是源码与固定字节复核，合并源码尚未由本复核编译、链接或运行。

本次只新增本报告，没有修改stage、replay、固定候选、正式源或06review，没有应用补丁、构建、运行产品/协议程序、打开业务数据库、访问网络或板卡。N03与producer v2各自全审不重复；其他文件仅核固定片边界、字节/行尾及必要关联保护。

## 固定来源与无漂移核对

整合作者已明确冻结`stage_from_06`、`replay_from_official_06`和attempt04既有输出，仅补外层交接元数据。本复核按其给出的最终源码快照及三共享SHA核对，读审后再核收口。

| 对象 | 实际SHA-256 |
| --- | --- |
| 官方06源码包 | `c1a21586e8b9a1adaaefd9ac1743f8b0f74c04a45f37001450de61b42402369b` |
| 官方06 `source_snapshot.json` | `48c4010b1e4b74fc3ff0664cb16d6981c2e6c7fe117aaaf54cf39790ae3bb213` |
| N03累计片 | `88bdc78486bfa009c41badc9b20df4085df52b383f52c0ac556679d2ca8c7edb` |
| N03最终清单 | `d366019603ea10f6c95709a287792773005747f25e9827fe42eaaf1d782edd44` |
| producer v2原累计片 | `c562e94e3e6cbee47fa3a969dab6c254117acbb3f7c5ca69c2676a61855d2426` |
| producer v2清单 | `9de4df264ffb34a57ddccd252b2b2710bdd0894c0362ce6805428da57c4d0b9f` |
| 实际接在N03后的attempt04派生片 | `e5a292199e28ed3ef2eeb6bbb3e47454ddf0c3bb92ca2dad353f68729655fcd9` |
| `attempt04/final_source_snapshot.json` | `a0133d69e287fe6c12f6cb00b4893712964b57bd8d62d92408c8a4a24e527d4f` |
| `merge_inputs_07.json` | `deecec44c5c44166b9f952d234c5614af2b1380fa3db30e5eda9ef65af79debf` |
| attempt04行尾/并集记录 | `9c01f995d8050c49e962bae733d2878bc0acdb5054898e8e0aef02d4676f3fa0` |
| attempt04独立回演结果 | `e1cfb9ee6e459268e9d407e08a0e0054a7964ba982d5b09723d5f52f84569dcf` |

实际stage源码文件集合恰为663，没有多/漏项；663/663 SHA符合最终快照，独立官方06回演目录663/663也符合并与stage逐字节一致。当前正式06的649/649仍符合官方06快照，未被本次隔离整合替换。根、stage、replay的`.gitattributes`均为`5de2ac8a8bf53f9c7775f129eac002896013a7d2415ae731a679616852a600a1`。

原plain producer因行尾检查失败1、旧attempt03新增文件补丁元数据失败1均保留，不把它们改称通过。实际N03检查/应用、attempt04派生检查/应用和官方06独立重演两片的检查/应用，记录的actual_exit均为0；其参数未用ignore-space-change。本复核只读这些固定记录及实际最终文件，没有重复执行。

## 三共享最终字节与边界

| 共享文件 | 最终CRLF原字节SHA-256 |
| --- | --- |
| `reagents/original_reagent_store.cpp` | `f23d4693fda500786627872a5fe9c5cb03942fe2217bac71dab6004f80c14341` |
| `reagents/reagent_store.h` | `b2ad67baeac4de589669ce7b88fe197c90a74507ac414ce93f4d38ef87dec1af` |
| `shell/device_control_shell.cpp` | `2ed6ee83c0b1285e77f864d039c01cb8111e11867e0f071d2b85d3673bd8547d` |

三份文件都符合作者冻结值和最终快照，去除CRLF差异后与`overlap_analysis/three_way`保存的三方文字并集完全相同。对固定N03作差，新增处只属于已审producer：Store增加预检/认领/真实提交链，头增加类型和槽/信号/成员，Root增加两源接线；N既有专口与Root互斥没有被覆盖。

其他目标边界也实际核对：14份producer独有目标只归一CRLF后与固定producer候选完全相同；22份N独有目标原字节与N03候选完全相同；624份两片都不触及的官方06文件仍原字节相同。派生片对16份既有producer目标保留其CRLF，新许可头保留原LF。没有用忽略其他空白或人工重写业务来凑并集。

## 共享处的行为与顺序

1. **读取不会冒充保存。** `loadOriginalLevelObservation(root)`仍在ReagentStore所属线程核同root，按项目→试剂两锁、noPending后调用loadOriginal，成功只发原profileReady/loaded。producer的`originalProjectSaveCommitted`只在其实际组开关writer提交回调发出；N专用重读、普通reload、原Ur保存和其他试剂资料不会因此新发项目notice或originalHoleSaved，不会误接Us/Up。

2. **组开关许可与N读口并存。** 新头同时声明N专用读取槽、producer申请/Ready槽及第三参sourceDeadline；没有复用一个业务请求字段承担另一个业务。组保存仍原originalMayEdit，预检每源一个pending、唯一ticket/身份/root/file/版本、同一个绝对deadline，最终在原锁内认领。未COMMIT失败仍释放；COMMIT后重读失败仍保留commitSeen和未核实快照，不被N失败回执或关闭伪取消。ReagentStore的原session/close路径与固定producer归一文本一致，没有漏掉未认领pending取消。

3. **统一接线只建立一次。** Root仍先attach priority/N，再由`if(deviceTaskStore_) return`保护消费者绑定；N自身也有已attach判断。两源的申请/取消按固定source过滤并以QPointer转到SampleStore，deadline第三参原样排队；Ready以原源QObject为接收者排队，consumer销毁后关闭源预检。普通项目和组保存都走`acceptOriginalProjectSave(notice,0,true)`，保留既有CAL同一路；没有多接一份发包回调或持项目锁BlockingQueued。

4. **真实提交先于页面归还busy。** producer在原项目锁内先排真实notice，再发原源完成/失败回执；group本地提交提示仍只是页面缓存通知。Root收到notice先使旧参数ready失效，再排统一消费者登记。同一源完成和后续loaded归还Page busy发生在这份通知之后；N03的busyfalse续调因而不会把本次提交当成已确认参数。即使页面或队列较晚收尾，原版本/notice/intent守卫继续决定是否发送，不由loaded生成新保存。

5. **Ur/N互斥仍是双向的。** Root同步生产口先看N的Root/worker/Page busy；异步begin之前先登记started、connection/root、baseReady=false并关N写门。真实纯Ur发送前记独立等待，匹配处理成败、断线及失败仍按原原因收尾。N接收入口仍据这些事实拒绝抢占；producer预留和本地提交没有删除或提前清这份Ur等待。N gate继续和真实Qj观察分离，未借group权限或参数聚合ready禁止Testing。

6. **失败续调不会变成重发。** N03的processed、成功/失败Page ACK，以及busyfalse后的0毫秒续调都保留在其独有Shell文件。producer组保存成功/失败也会改变Page busy，所以可能多调用一次原同步口；该口每次仍重核Nbusy、连接/新鲜状态、身份/运行态、页面草稿、资料根、旧任务未知和每连接一次启动。新调用不直接置ready、不重放N、不给旧Qj续期、不清Us/任务未知，也不绕过既有prepared/intent/期限发包检查。

7. **并发版本变化保持可解释的失败。** 两片写入仍共用原项目写锁；N还持试剂锁并复核项目集合/版本，producer也锁内核目标版本。互相修改依赖时，旧观测/旧草稿只能因锁或版本变化拒绝，已经提交则保留事实并把页面标待核，不能把另一片的保存当自己的成功回执。没有为合入删除copy pending拒绝或用统一DB最大量补造runtime。

## 后补确认：原N03的外层关闭目标P2

该问题不由三共享合并引入，位于原N03独有`shell/reagent_level_shell.cpp`。当外层`ShellViewport::closeEvent`（主入口`:231`）因内层N仍在途而被拒绝时，外层窗口保持开启。N处理结束后的processed收尾（前两片固定源`:294`）安排的是内层`frmMain::close()`；它可以关闭/隐藏内层页面，却不会重新关闭外层窗口，无法完成原退出意图。

这是源码可达的确定P2。此前本人的N03窄复核漏看了实际窗口包装目标，现补记，不能拿旧报告`9ad2e991`表示这一退出分支已经闭合。根已要求在前两片之外另交独立第4片，将该延后回调改为`window()->close()`并说明原因，保留所有状态、等待判据和返回值；这与原红退出及既有关闭重试的外层目标一致。

根还要求独立第3片只在`requestReagentLevelShutdown`成功返回true前记READY日志。READY只说明此N关闭判据的active/workerBusy均为false，不是共用ReagentStore线程已经STOPPED，也不是已运行在途N验证。3/4片按下列独立阶段核对；前两片a0133快照及原件保持，不将合法后补片当成候选漂移。空闲关闭重试也不能证明真实在途N排空分支已经跑过。

### 第3/4片最终窄复核

| 阶段 | 补丁SHA-256 | 全663源快照SHA-256 | 唯一改变的`reagent_level_shell.cpp` SHA-256 |
| --- | --- | --- | --- |
| 前两片 | 前文N03与attempt04派生片 | `a0133d69e287fe6c12f6cb00b4893712964b57bd8d62d92408c8a4a24e527d4f` | `ba5c765f6bf6e535c029a16fc635e3edcba5bcac40567bfdd033103af26b6011` |
| 第3片bool日志 | `db780ecb4f7a8281d435d704415e6dd4371f9c9dfdd6743d8812e89dc997a05b` | `9e8a23ec164596de8da453286e9576ba3b8ce277bf4813458660c5260ef8378c` | `209bfc60b47defae3560588f5c82b1d797fb73329e006ced47cfe7d569bd8c5c` |
| 第4片外层重试目标 | `682cfafa0826b3df837be3b893899e1a4c8ca35bc73ccd15127d403d59fdd503` | `760d81b3fe64f375e9600ed3deb71d6c67699cf2d9f53a82cc03d434a259af26` | `21e84ccaa973adee76521fb2b3841bd01a40c2d47f7c0304b1e566e5559806c0` |

实际核过第3片602字节、第4片981字节的补丁和对应快照SHA。第3片`stage_with_shutdown_log_02`的663/663符合9e8快照；第4片`stage_with_outer_close`的663/663符合760快照。每一阶段相对上一阶段只改这一个CPP，其他662份及三共享最终SHA不变；前两片stage仍全663符合a0133。最终`replay_four_patches_from_06`全663也与760快照逐字节一致，正式06的649份再次核仍符合官方快照。第4片目录的8份回演check/apply记录及自身check/apply均actual_exit=0，本复核没有执行它们。

第3片最终代码只在原false guard之后、return true之前加一行qInfo，输出`bool(reagentLevelActive_)`及线程安全`isBusy()`的实际值。已避免将`std::shared_ptr`直接流入Qt5 QDebug的类型问题；使用原有QDebug include，不引入依赖。closing赋值、revoke、busy判据、false/true返回完全未动。已进入closing且许可已撤销，Root不再接新N，READY不是根据预设常量伪写。

第4片最终只把匹配processed、归还active且worker已不busy后的0毫秒回调，从`close()`改为`window()->close()`，另加一行中文原因。QObject回调上下文、条件、状态清理、提交事实和调用顺序不变；外层因此重新走原ShellViewport/frmMain关闭守卫及Uh排空，不直接退出事件循环或终止线程。该源码P2可按本修正关闭；真实N在途情形仍未运行复现，定时/空闲退出成功不能冒充该情形已验。

最终result记录：`shutdown_observation_02/result.json` SHA为`0760227091221233fc111ebab5ff8cf44f01af556f6363f9225d27b67d2e0d5e`；`shutdown_outer_close_01/result.json` SHA为`24ffa9579f79e96c314d9cbc6bdd50ecc00d6479710d59d4c39ecaf219453afc`。前者只记录三片日志阶段，后者记录四片并集，不混用旧的非bool日志或前两片快照。

## 正式06保护及验证限制

CAL锁内writesAllowed、target/expectedFileName、实际commitSeen/锁内notice/close后SHA仍保持；初始/单项UP的新DeviceRepo签名与copy guard不在这次producer重写范围。DeviceSync的68b5两修（晚到的确未尝试结论只解释其对应未知，以及初始UP真实commitSeen跨nextProject/重读失败保留）和Root原身份/期限/发送来由检查均由固定producer边界及官方06未改文件保留，未回旧250b。Repo完整局部发布、只读过滤与写守卫也未放宽。

本报告没有把各候选原对象当成当前合并树已编译：共享头布局和接线已成为新并集，需要总协调随后以这663份固定输入完整构建/链接并核输入覆盖。原数学、资源硬预算、42/48、容量标定批次及任务预留后可信runtime等已列未闭合项不因本次合入关闭；没有RK、真实仪器、界面交错或资源实测结论。
