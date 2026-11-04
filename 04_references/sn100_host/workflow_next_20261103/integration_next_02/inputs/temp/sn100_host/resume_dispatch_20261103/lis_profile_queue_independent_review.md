# LIS原配置快照与固定对端回执：总控独立源码窄审

2026-11-03。本次读取 `temp/sn100_host/lis_profile_queue_candidate_20261103_01/handoff.md`、最终 `active_candidate/lis_transport/` 三文件、`lis_profile_queue.patch`，并直接核必要Store/低层传输代码和旧五份纯编解码。代码与补丁按19:38:33 +08:00实际字节固定；交接文字仍可能收口，不能用文字SHA代替三份代码和补丁身份。

**结论：所审三文件未发现确定阻点。** 同版原配置及序列号快照、旧发送结清归属、同步固定目标回执、重入/停止/寿命边界可作为后续真实查询的来源接口。这里只完成源码审查，不表示接收者已经核profile、查询已发送、患者/任务已保存、LIS已确认或RK运行通过。

没有修改作者、正式、旧五份候选或公共文件；没有复制/重演全684/685树，没有编译、运行产品/样例报文/模拟、访问网络或操作板卡。未把作者两Agent结论或Git回演替代本次源码审查。

## 同一次原配置与完整身份

已直接核正式 `data/lis_settings_store.cpp:153–180`：原文件指纹、原LIS值和sequenceNumber取自同一个 `OriginalIni::Snapshot`；不是先读序列号再取当前文件指纹。保存入口 `:480–488` 成功回读后才发saved，控制器使用回传的这份settings，没有再读INI、选当前全局值或传出originalValues正文。

候选 `lis_connection_values.h:23–45` 的ReceiveProfile只有控制器友元可以构造，完整ConfigurationIdentity和sequenceNumber均为private const，实体复制被删除；外部仅持shared_ptr<const ReceiveProfile>。matches逐项比较revision、effectiveKey、transportToken、sourceFingerprint，没有仅凭revision或文件SHA比较。析构只释放QString/普通值，没有QObject、SQL、计时器、控制器指针或要求指定线程的deleter；外部已交值不依赖控制器寿命。

控制器 `cpp:149–184` 的顺序是：标applying并撤可信发送 → transport.configure同步结束旧连接/发送 → 返回后才取得最终transportToken及完整新identity → 一次构造不可变profile → 发布Configured。configure内部的sendFinished回调仍读原pending中的旧identity/profile；没有先发布空token，再原地修改profile。

| 分支 | 实际代码和结论 |
| --- | --- |
| 普通同值loaded | `cpp:125–130` 更新读取诊断指纹后直接返回；effectiveKey不含无关INI键的文件指纹，因此不把新读指纹重贴给旧采用身份/profile，也不借loaded重开明确停止的连接 |
| saved，即使值相同 | 绕过上述no-op，沿原策略换revision并重新配置/开启；旧pending先按旧来源结清，随后才发布新快照 |
| configure失败/serial/simulation | 低层 `lis_tcp_transport.cpp:63–119` 明确拒绝相应分支；控制器不构造新通信profile、不生成保存/连接成功事实 |
| 独立本地设置 | 控制器 `:175–180` 发布LocalSettingsOnly后返回，未建立可发送的原资料快照 |
| 同配置重连 | 不重新调用receiveCommitted，保留同一配置profile；消息的原generation/peer仍独立保留，不把profile当查询续命凭据 |
| stop | 保留同配置active供原显式openConfirmed；不会因此证明旧查询仍有效或恢复业务授权 |
| shutdown | `:338–347` 等传输不活动、pending空、通知深度及转换守卫均收尾后释放active/weak表再stopped；不等待外部纯值全部销毁，也不强制释放它们 |

## 16版本、4096字值和拒绝时的重入

`cpp:102–119/134–147` 在configure、新revision增加和新profile构造之前检查版本耗尽及原TCP快照准入。sequenceNumber最多4096个UTF16字值，空串保留为合法原值，不截断；weak表先清expired，仍存活版本最多16（含active），没有强持整本历史或静默淘汰外部仍持有的值。

满额拒绝采用保守策略：先拒绝本次新配置并撤active，再经原settingsError停止旧连接/禁发送，标待核。旧pending/消息持有的旧profile仍在。这可能要求释放无外持的旧active后重新读取才能有空位，交接已明确；没有假称它能在16个外持版本都存活时继续采用新版本，也没有墙钟TTL销毁仍有效的事实。

重要修点已在最终代码成立：版本耗尽和容量失败两条路径都在调用settingsError**之前**reset本次active，调用返回后直接return，没有第二次reset。settingsError `:294–308` 用operation及stopping守卫保护同步stop/通知；其stateChanged直接消费者若成功重读并采用新profile，外层operation检查不再覆盖/清掉这个新值。configure中的旧sendFinished即使要求stop/shutdown，也先使旧operation失效，外层不继续发布/开启旧意图。

这里限制的是本控制器产生的存活配置版本和单个序列号长度，不是全进程RSS、全部payload/消息队列或真实RK资源实测。控制器已有的effectiveKey计算仍会处理传入settings，不能把此profile预算夸大为整个设置读取/散列过程都只处理4096字值。

## 固定对端的同步入队回执

旧 `sendMessage(quint64,quint64,const QString&,quint64*,QString*)` 原型及成员函数类型保留；新方法有不同名称sendMessageWithReceipt，避免改变旧取成员函数指针的调用。旧空request仍拒绝且先清0；新拒绝先清空QueuedSend。两者均先核所属线程、状态/版本/传输代次、有效输出及编号上限，新接口还核profile非空及四项身份相同。

已直接核低层 `lis_tcp_transport.cpp:247–268`：核generation/id → 精确收集当次Connected peers → 编码及预算 → 入队 → 填acceptedPeers → true。这段没有emit、socket.write或事件循环。controller `:256–270` 在调用前固定request/generation/configuration/profile；低层true之后只有把其原peers赋给pending/QueuedSend、insert pending和复制输出，没有后置校验/普通false、信号或读取“现在”的对端数。编号在传输拒绝后也不回退复用。

因此成功返回能同步交付当次固定peers及同一来源，不必等SendOutcome后再猜，也不会按后来新连接广播。普通分配器异常不在此片“普通false可回滚”的保证内，源码没有catch后伪装为false。

`receiveSendResult:273–291` 先按request/generation移除对应peer，复制原pending的configuration/profile，必要时擦掉该pending，再发sendFinished；直接消费者停止、配置或关闭时不会通过迭代器重访旧map。sendNotificationDepth让最后一项通知返回后才补核关闭排空。旧回执用旧值，不用当前active重标；不匹配结果只诊断，不能制造业务成功。

**QueuedSend只能证明本地传输接受及固定目标。** 它可复制、可构造，不能充当真实Tt、任务授权、socket已写完、对端ACK或患者保存凭据。后续业务仍需独立保存本地查询ID、原线上控制号、架实例/孔/项目版本、主控原包及各阶段结果；本片没有生产查询发送者。

## 与旧五份纯编解码兼容，但校验消费者仍待接

本片给ReceivedMessage/SendOutcome追加共享指针，原字段及语义保持。旧五份中 `original_lis_download_codec.cpp:92` 是完整 `result.source=message`，第二阶段 `:134` 完整复制Header，因此自然携带新增profile，不会只摘旧五字段而丢它。类型注册同步增加ReceiveProfilePtr和QueuedSend，默认空指针仍可表达未取得快照。

旧五份实际SHA均与此前独审相同：QRY两源仍是原c7afa继承；types为daf03fc7…，codec.h为9fe4f07d…，codec.cpp为4162c5f3…。本次没有改其解析或字段/顺序/编码/单位，也没有把LIS改成通讯测试页ASCII。

兼容不代表消费者已实现。旧decodeOriginalLisReplyHeader仍接受外部sequenceNumberSnapshot参数，本身不会强制检查profile。后续真实入口须明确核 `message.profile && message.profile->matches(message.configuration)`，再取该profile的sequenceNumber；不得继续传当前全局Store串，或手工改写Ready结果冒作可信身份。查询归属、超时/碰撞/原首匹配、真实Tt等待、患者/任务保存及原ACK时点仍是后续独立业务工作。

增加值对象字段后须同版全量重编使用处；旧发送签名兼容不等于二进制布局可混用，不能复用清理前的旧.o。

## 精确三文件与补丁来源

实际核对active_base/active_candidate各三份、正式各三份，以及源表中的必要三条，没有重算或重放全树。历史source_before/source_after路径均684，仅三改、0增、0删；三份前像在b066、ASCII747、当前Root指定7cad表中完全相同，审查时正式实际三份也仍等前像。

| 相对源码路径 | 实际前像SHA-256 | 最终实际候选SHA-256 |
| --- | --- | --- |
| `lis_transport/lis_connection_controller.cpp` | `59ec9aed15986f713a9686d740ec3b18092fc6e2359c62c16a7ee075171625cf` | `f403e0470636551fedc2611bb204522306938b493462b9d5fe8d3126407ba1e3` |
| `lis_transport/lis_connection_controller.h` | `f04d50c44fde199168add15a9c979827f7d108ebb13bc7dce3d83534958804d3` | `468c0418413ae295e73b6c3b347de94a382b960954df3f1646280bfdb11fd151` |
| `lis_transport/lis_connection_values.h` | `a02a0dc81c8fb9646e2f808a831d3bd9f92c3d378f097cc1a5f861a5d138d31e` | `c9d47a9f3049470862b7a5b3db88e2473a3acfa5e7217619ab64baa4a63ac1ba` |

实际补丁SHA为 `5090d167d455d8f89c6c8a589da0fc4df911fbf26891b682d93b733aab5e18f5`，恰含这三文件差异，无main/build/Store/低层TCP/任务修改。直接使用的5份正式依赖指纹均与source_references.json一致。读到的handoff SHA为 `5e54ceadec0c5750e8d724df2197740820bf7d8b34865681e4aa4d7216e55162`，source_review说明SHA为 `5af92eac6cfc3e5dccccb1227726bfe740dcfce0138c0a9c3ae493facd75fdf0`；后两份如仅收口文字应另留新指纹，不据此改写冻结代码身份。

Root当前指定表 `integration_10_source_acceptance_01/cal/line_ending_correction_01/source_after.json` 的实际SHA为 `7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e`、685项。作者747/684回演是当时成功原件，最终全684再核退出1是Root后来合CAL产生的已知漂移，不是这三份源码缺陷；不能改成0、覆盖原件或再整树回拷。Root负责在7cad上组合本片及其他已审差异并核新全表，不采用已知失败的首轮cal/source_after.json。

本次只完成独立源码、接口/重入推演、局部实际指纹和补丁范围核对。没有新增编译、QObject实际排队、配置/重连/停止实测、RK软件、真实LIS、真实主控/仪器或资源/同板控制结论；当前留屏程序没有被本审查操作。建议Root在同版合片后做必要完整ARM构建，再按获准原配置现场条件分别登记新接口、真实联机业务与留屏结果。
