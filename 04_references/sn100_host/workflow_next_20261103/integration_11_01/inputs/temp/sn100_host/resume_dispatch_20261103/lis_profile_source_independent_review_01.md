# LIS同次设置来源两文件候选独立审查

本轮按实际两源及补丁未发现确定的P1/P2阻点。新增自动取任务开关、原资料根和ConnectionState.profile均属于纯值来源补足，没有启用生产查询、ticket、患者保存或新的发送许可。此结论不等于已合入当前687/c7，也不等于编译、RK或真实LIS验证通过。

本轮只用Windows读取候选、必要正式源码与摘要，只新增本报告；未修改作者候选、正式源码、公共入口、构建或板端，未运行产品、SQL、模拟、编译、网络或RK。没有复用作者审查结论代替源码判断，也没有重放作者的Git应用动作。

## 实际版本与补丁范围

|文件|实际SHA-256|
|---|---|
|`lis_profile_source_candidate_20261103_01/handoff.md`|`4b1b6350202a13f0b71b23da5a606fa2002bd5935537029331de0a21be0b7823`|
|`lis_profile_source.patch`|`7fd8567ece6c4f938f2acc210c2e606abecaae4fdf7b91f9870e3fdd9ad23bdf`|
|候选 `lis_transport/lis_connection_values.h`|`41bb19561f6b3996107470107a59b04b01df1310fa1afb5abf16caf9a79b2051`|
|候选 `lis_transport/lis_connection_controller.cpp`|`7ca09f58d332ac20bf0f7d234c7fc4081a5acddadf646484bba11d81f69c7b61`|

实际补丁只有这两份源，改动为ReceiveProfile两个const字段/getter/构造参数、ConnectionState追加profile、根路径长度预算，以及publish来源赋值。原send、接收、编码及身份算法没有补丁改动。

两份 `base/` 与读取时正式源逐字节摘要相同，也符合作者记录的f741局部前像：

- values.h：`c9d47a9f3049470862b7a5b3db88e2473a3acfa5e7217619ab64baa4a63ac1ba`。
- controller.cpp：`f403e0470636551fedc2611bb204522306938b493462b9d5fe8d3126407ba1e3`。

另实读作者 `ordinary_replay/` 两后像，摘要分别与上述候选相同。作者verification记录普通apply检查/应用均0；本审查只读该原证据，没有自己执行Git或认领这两个0。`composed_frozen_687_after.json` 是由原f741表替换两个键合成，不能登记为本轮对正式687全树的实际扫描结果。本轮也只核两份正式前像，没有重扫全工程或把大型JSON装入上下文。摘要点核工具块为 `99944d`，交接摘要读取块为 `8a8902`。

## 自动取任务、原目录、序列号确实来自同次settings

现正式设置层 `LisSettingsStore::initializeOriginal:394–408` 先按原workingRoot规则核目录，再以 `QFileInfo(root).canonicalFilePath()` 设置originalRoot_。`readStored:153–180` 用一次config.ini的OriginalIni::Snapshot取得fingerprint及字段，result.originalRoot直接等于这个已确认根，autoGet只在原字符串恰为`true`时成立，sequenceNumber也从这一snapshot取出。

正常load成功回传这份settings；save成功在原目录下必须重读真实保存的config.ini，再emit saved（`:480–488`）。控制器没有从界面草稿、另一个getter或后来一次文件读取抓新增字段。

候选 `receiveCommitted:129–203` 在transport.configure成功返回、operation/closing复核、新ConfigurationIdentity确定之后，用同一个const settings引用一次构造：

`ReceiveProfile(configuration_, settings.sequenceNumber, settings.autoGet, settings.originalRoot)`。

三个业务来源值与本次configuration一起进入不可变对象。构造函数仅friend控制器可调用，字段为const、复制/赋值删除，读者只持 `shared_ptr<const ReceiveProfile>`，因此不会把旧对象原地改成新root/开关。它不持QObject、Store、文件句柄或线程，也没有新增IO、路径规范化、hash或Store getter。

## 原四项身份和三种摘要含义保持

ReceiveProfile::matches仍逐项比较revision、effectiveKey、transportToken、sourceFingerprint，未缩成只比revision或连接代次。新增autoGet/root没有混入一个新的身份算法：

- effectiveKey仍为原controller `effectiveKey:89–101`，其中原本就包含originalRoot、originalValues、autoGet、sequenceNumber等生效设置。本片没有新增或重排hash输入。
- sourceFingerprint仍直接取本次settings.originalFingerprint，即原config.ini读取指纹，不改成目录或effectiveKey的摘要。
- transportToken仍由原transport.configure算法计算端点/编码/原指纹等值，socket generation继续独立，不用原目录替代它。

`latestReadFingerprint_` 在普通读取时更新，不会写入旧ReceiveProfile。可信且同effectiveKey的普通loaded在`:136–137`直接返回，旧configuration/profile/sourceFingerprint保留；新增root和autoGet本就在key中，改变这些值不会误走同值分支。成功saved不走该提前返回，即使可见值相同也经过configure并建立新revision/profile。已有的这一差异没有被新增getter改变。

## 旧消息和发送回执继续持自己的来源

配置采用期间，旧profile不会在transport.configure的同步旧回调之前被换为新对象；configure先结清旧会话，controller只在返回后生成新configuration和profile。接收回调先把当时profile/configuration复制进ReceivedMessage，再emit；已交出的旧消息仍持原const对象，不会调用新增getter去读取当前全局设置。

原QueuedSend、PendingSend和SendOutcome的profile链未改：发送时固定configuration/profile/generation和真实targets，结果按原pending请求取旧值。这片没有把ConnectionState.profile反贴到旧消息或旧pending上，也没有通过autoGet重新解释编码、权限、ACK或保存结果。

## 4096与16存活版本的边界

候选 `canAdoptProfile:104–126` 保留sequenceNumber最多4096个QString UTF-16单元的原门，增加originalRoot最多4096单元，超限拒绝、不截短。它在transport.configure和新profile构造之前执行；拒绝沿原保守路径撤本次active、置不可信并停止旧连接，不能把“超限没有采用”误记为“完全没有停止副作用”。

这项准入只在原资料、TCP、非simulation的配置上调用；其他情况不能绕过后成功建立未受预算的profile：非TCP或simulation会被原transport.configure拒绝，独立本地settings成功配置后也在创建ReceiveProfile前返回LocalSettingsOnly。

profiles_仍是weak对象表，清理expired后若已有16个存活对象即拒绝增加。ConnectionState.profile、message/profile、pending/profile和业务值的多份引用若指向同一对象，不重复计一个版本；新State强引用会延长该对象实际存活期，因此外部保留旧状态会更容易触发第17版拒绝，这是既有“不丢存活旧来源”的正确保守结果，不能忽略或静默淘汰它们。

两段文字的逻辑内容上限为 `16 × (4096 + 4096) × 2 = 262144` 字节，即256KiB。它不含ConfigurationIdentity文字、QString容量/终止单元、对象/容器/共享指针及外部状态副本开销，也不是RSS、所有回调队列或整个ReceiveProfile的内存上限。代码/交接没有用这个计算冒称实际资源测量通过。

## ConnectionState.profile的发布条件与同步开启

候选publish`:358–367`每次先写configuration/generation/peers/state/detail/closing，再在 `trusted_ && !applying_ && !closing_ && profile_ && profile_->matches(configuration_)` 时附加profile，否则清为空。这是最近发布状态的来源，而非“已连接”判断。

不额外要求 `!opening_` 是必要的：原transport.openConfirmed的server listen可在controller.opening_仍为true时同步publish Listening；原client beginClient也同步publish。若在此屏蔽profile，开启动作返回后没有保证额外再发一次相同状态，会造成状态来源长期空。本候选允许这次同步发布携带可信来源；连接是否建立仍看原真实socket状态/peers，不看profile是否非空。

不额外要求 `!stopping_` 同样符合“来源”含义。明确stop并未撤销已采用的可信设置，因此Stopped状态可以带同一profile；它不会自动重开，也不等于现在可发送。设置失败先trusted=false，失败/关闭发布会清state.profile；正常重配置发布用新profile替换它。控制器只维护一份state_，没有新增强持历史列表。

`state()`是缓存拷贝：转换中的事件可能尚未publish，读到的closing/profile等可暂为旧状态。消费者不能仅凭缓存profile非空或autoGet=true发送。原 `sendMessageWithReceipt:246–278` 最终仍在控制器所属线程实时检查closing/applying/stopping/opening/trusted、revision、实际generation、接收输出、请求编号和profile四项匹配，然后才交transport；实际transport还核armed、代次、真实已连接目标与队列预算。

因此在同步Listening/Stopping状态的stateChanged接收者中重入send，即使profile非空，opening/stopping仍会在原发送门拒绝；applying时读旧缓存也会被applying拒绝，closing或settingsError时由实时closing/trusted拒绝。发送成功后仍沿原“没有额外可重入检查/emit再返回false”的入队回执链，新增state.profile没有增加send内回调。

## 关闭和额外强持的实际影响

关闭先 `closing_=true, trusted_=false`；若正applying/opening/stopping，由原最外层转换和operation守卫收尾，不能在中间假报stopped。正常最终transport停止publish会使state.profile为空；drain还要求转换均结束、sendNotificationDepth为0、transport不活跃、pending空，才清active profile/weak表并emit stopped。

外部已取得的ConnectionState、ReceivedMessage、QueuedSend或SendOutcome可以继续持原纯值；关闭不等待这些引用归还。清weak表发生在controller永久closing后，不会借清表在同一控制器中重开绕过16版门。新增强持不包含QObject或活动socket，因此不会让外部profile引用延迟QObject线程关闭，但其纯值释放时点仍由外部最后一个引用决定。

这并不证明实际关闭时序/内存/界面响应已经测过。变更涉及公共纯值结构，后续整合仍需Root的完整ARM64构建及真实配置下验证，不能用c7旧版的被动观察代替本候选验证。

## 尚待推进的范围

源码窄审范围内可继续交Root决定后续独立整合；当前687/c7正式源码和留驻程序均未改变。原两文件前像仍吻合，但以后采用前须重新点核并登记真实apply/after，不把作者合成687表当正式扫描。

本片没有查询消费者、下载/保存、ticket、ACK或自动发送新增开关消费者。生产query-only仍未获启用，任务等待合同仍是合同；新getters和state.profile不能把这些后续工作自动判成已授权或完成。
