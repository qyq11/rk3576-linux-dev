# LIS原配置快照与固定对端入队回执：三文件候选

> 最终接续裁定：交付前再核正式源真实退出1，漂移记录原样保留。总控已指定最新最终表为`integration_10_source_acceptance_01/cal/line_ending_correction_01/source_after.json`，SHA `7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e`，685份。三个transport前像仍相同；root明确负责把本片与7cad组合并核新全表，作者不再复制/重演整树。下述747/684副本及成功回演仅是准确历史时点，不能当当前685表，也不能采用首轮失败的`cal/source_after.json`。详见`root_latest_baseline.json`及`final_recheck_observed_drift.json`。

这片用于解决两个来源缺口：解码旧消息时能取得同一配置版本的原序列号；查询一经传输层接受，就能得到当次精确固定的目标对端，不必等到发送完成再猜。它不执行下载、保存、业务确认或仪器动作。

## 已完成的ASCII阶段回演与最新接续

总控在开发期间合入了ASCII界面的两份instrument源码。本片已接续这个新表，不把旧09底稿继续写成当前正式源：

- 该次回演的历史输入表：`temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/ascii/source_after.json`，SHA-256 `747ae955c12b4bc39e6b76ab80b282445c07500d92b08a364861333ddad5f913`，684份。
- `active_base/`和`active_candidate/`是ASCII747阶段的准确副本。与上述表相比只修改下列三份；新增0、删除0，仍684份。两份`instrument/instrument_page.cpp/.h`保持总控ASCII后像。
- `base/`、`candidate/`、`base_manifest.json`及早期prepare/edit工具是开发开始时的09历史前像和过程，原样保留；不能整树回拷它们覆盖新ASCII源码。最终补丁和active副本优先，早期编辑工具不作为修后源码的重建入口。
- `source_before.json`是该次输入表的相同字典，因JSON序列化格式不同，文件SHA为`26b218d395f68eeaf7c8279afbb0abf79c99fdf350fff839101c63630564cbe7`，不能与747的原文件字节SHA混用。
- 该次ASCII组合完整后表`source_after.json` SHA-256 `2584bd0ec6aba4197eccce738cb078abc5464af7f42a0e5d37cde470808f543a`。

精确范围均相对`active_candidate/`：

| 文件 | 本片内容 | 最终SHA-256 |
| --- | --- | --- |
| `lis_transport/lis_connection_values.h` | 不可变ReceiveProfile、QueuedSend；原ReceivedMessage/SendOutcome追加快照指针和元类型声明 | `c9d47a9f3049470862b7a5b3db88e2473a3acfa5e7217619ab64baa4a63ac1ba` |
| `lis_transport/lis_connection_controller.h` | 保留旧发送原型，新增具名回执接口；当前快照、弱引用表和pending中的快照 | `468c0418413ae295e73b6c3b347de94a382b960954df3f1646280bfdb11fd151` |
| `lis_transport/lis_connection_controller.cpp` | 同次已提交值绑定、有限寿命、入队同步回传及旧逐对端结果来源 | `f403e0470636551fedc2611bb204522306938b493462b9d5fe8d3126407ba1e3` |

补丁`lis_profile_queue.patch` SHA-256：`5090d167d455d8f89c6c8a589da0fc4df911fbf26891b682d93b733aab5e18f5`。从ASCII当前684前像普通Git check/apply实际均0，684份回演与active候选逐字节相同；原CRLF保持，没有忽略空白、模糊匹配或三方覆盖。命令及真实退出见`verification.json`、`01/02/03`原件。

747回演当时的前后正式684份均等于747字典；之后最终再核检测到总控新合入，真实退出1已保留。最新7cad的三文件前像已单独核同，最新685整表由root组合核对。旧五份纯解码源码及其19份交付原件SHA全部不变。本片不改main/build/Store、低层TCP、任务、公共文档或任何冻结旧交付。

## 原版和已有源码依据

原Windows `commlis/liscommsetting.cpp:31`读原配置，`liscommreceiver.cpp:53`以`getLisSequenceNumber().contains(MSH发送方)`判断消息；查询控制号和业务阶段仍见冻结的下载字段交接。本片不改原字段、顺序、编码、角色或TCP/link规则。用户ASCII需求只限通讯测试页。

正式来源链已由两Agent只读复核：

1. `data/lis_settings_store.cpp:153–176`从同一个OriginalIni::Snapshot取得原文件指纹、所有原LIS键及`sequenceNumber`；缺键原样为空，重复大小写键报错，不猜值。
2. `:480–488`保存后成功回读才发`saved(settings,ports)`，因此快照直接取这次`settings.sequenceNumber`。没有另读INI，不携带`originalValues`或认证正文。
3. 控制器先保持旧身份完成`configure()`内部的旧发送结清，再取得最终revision/effectiveKey/transportToken/sourceFingerprint，用新身份和序列号一次构造快照；不先放空token再原地修改。
4. 低层`lis_tcp_transport.cpp:247–268`的sendMessage在校验、编码、预算及固定Connected对端后入队，填`acceptedPeers`再返回true；这段没有emit、socket.write或事件循环。控制器沿此窗口登记pending后返回纯值，没有改变广播路由。

相关现行源指纹见`source_references.json`。本轮没有读取实际INI、端点、患者库或原程序运行状态。

## 调用合同

`ReceiveProfile`的构造仅控制器友元可用，内容为private const的完整ConfigurationIdentity和原sequenceNumber，实体不可复制；业务只持`std::shared_ptr<const ReceiveProfile>`。析构只释放普通值，不含QObject、timer、SQL、controller指针或依赖其线程的deleter。

`profile->matches(identity)`逐项比较revision、effectiveKey、transportToken和sourceFingerprint。未来解码入口须先核非空及matches，再从该profile取sequenceNumber；不能把当前Store全局值补给旧消息。旧五文件解码候选不改：其中完整ReceivedMessage的默认值复制会自然携带新增指针，但目前没有生产调用或profile校验消费者。

新接口为：

```cpp
bool sendMessageWithReceipt(quint64 expectedRevision, quint64 expectedGeneration,
                            const QString &body, QueuedSend *accepted, QString *error);
```

旧五参数`sendMessage(...,quint64 *request,QString *error)`的成员函数类型原封保留，内部委托新接口。旧空request仍拒绝并清0。新接口拒绝时先清空QueuedSend；校验失败、原配置缺失/不匹配、编号耗尽或传输拒绝，均发生在传输返回true之前。已经分配的发送编号在拒绝后也不回退复用。

成功返回值包含：

| 值 | 来源及含义 |
| --- | --- |
| configuration/profile | 同一次采用的完整配置身份及原序列号；四项一致 |
| request | 该控制对象生命周期内递增的本地发送号，不是线上MSH控制号 |
| generation | 调用时核定、传输实际接受的连接代次，不事后改读state |
| peers | 本次传输同步返回的精确固定目标集合，不是connectedPeers数量，也不是之后新连接的对端 |

传输返回true后只填固定集合、insert pending和复制输出，没有新增信号/外部可重入调用、后置校验或普通false路径。没有捕获内存耗尽再伪装成可回滚的false；分配器异常不在本片普通入队失败保证内。

`QueuedSend`是可复制、可构造的本地入队回执，**不是任务授权、socket已写完、对端ACK或患者保存成功**。业务仍须绑定真实Tt、本地查询ID、原控制号/架孔/项目和阶段。没有生产查询发送者接入本接口。

每个SendOutcome仍按pending的原configuration/profile发布，逐peer删除结果后才通知；不从当前active重贴旧来源。原sendNotificationDepth、operation换代、停止/配置/开启守卫及最后回执关闭补核保留。

## 配置变化、容量、失效和关闭

| 情况 | 本片处理 |
| --- | --- |
| 首次原TCP配置成功 | 尚未发布Configured前建立同身份快照；它只表示配置采用，不表示已连接 |
| 普通同值loaded | 保留同一快照对象和旧完整指纹；不因latestReadFingerprint变化重贴来源，不分配新版本 |
| saved，即使值相同 | 沿原策略换代并重开；旧发送先用旧pending/profile结清，新快照随新完整identity建立 |
| failed configure、串口、独立本地设置、模拟配置 | 不建立可供通信采用的新快照；旧已交值仍拥有原事实，不转成新身份 |
| Store.error | 仍按前片策略停旧、禁新发送，保留原来源事实，不生成保存成功快照 |
| 快照准入长度/数量不足 | 在configure前明确失败，经原settingsError停止旧连接并标待核；先释放本次active再同步通知，避免回调成功重读后被外层误清；旧pending/消息各自持原ptr |
| 同配置重连及旧generation完整消息 | 共享同一配置快照，但保留消息原generation/peer；不是据当前代次一刀丢消息，也不据快照使查询续命 |
| 明确stop | 保留同配置active供原确认开启使用；不能用它证明旧查询仍有效 |
| shutdown | 实际传输/pending结清后释放controller active和弱表，不等外部纯值全部释放才发stopped；外部旧值可自然跨线程最后释放 |

资源限制是本地候选预算，不是原协议限制：同时存活的快照最多16个版本（含active），单个序列号最多4096个UTF16字值。空序列号仍合法保留，不用空串代替超长值；不截断，也不传全INI。满额按保守方式先拒绝、停旧待核，不为腾位置淘汰仍被旧消息/回执持有的快照。

控制器仅强持active，历史表只有weak_ptr。每次新配置准入先移除expired弱引用，仍被外部持有的版本计数不减少；消费者长期保留旧值会形成明确背压。若释放active后它无人外持，下一次成功重读可收回该位；若16个仍全部外持，继续拒绝至有值释放。

本片没有按墙钟TTL使仍被持有的原事实过期。查询60秒超时是后续业务身份规则，不能据此销毁profile或改读当前值。最后强持者释放后，弱引用过期只回收元数据；不提供旧ID查取或“找不到就用当前配置”的兜底。收到空profile或四项不匹配须明确失败；不得把旧消息改成当前回复。

16版本只约束本控制对象创建的不同配置快照数量，不约束外部消息队列、payload或整个进程RSS。每条ReceivedMessage/QueuedSend/Outcome只是共享同一版本对象，未复制全INI。RK资源和响应未测。

## 独立复核、检查和待接事项

两个Agent分别核原配置来源/分支及入队同步顺序/重入/寿命，固定结论见`source_review.md`。初审发现一条P2：准入失败先发settingsError，返回后reset会误清直接消费者新采用的profile；已改为先reset再发通知，两方窄复核通过。另将配置编号耗尽检查提升至所有新配置的configure/++之前，防止任何分支理论回绕。只关闭源码窄审，不写成编译通过。

实际只有源指纹、旧五文件冻结核对、标准Git check/apply和该次ASCII684全表回演；未构建、未执行产品/消息/模拟、未网络或联板。值对象布局新增字段，整合时应同版重编使用处，不能混旧对象。现有签名虽兼容，不表示二进制布局可混用。

本片未改五文件纯编解码，也未把它们加入构建。接收侧的profile验证/取序列号、查询在途表、固定peer归属、唯一真实Tt等待、架孔/项目失效、患者/任务真实保存、原ACK时点仍待后续root协调。也不改端点、编码、原TCP/link、开始门、结果上传或串口。

当前正式软件仍由总控管理；作者未启停、切版、关闭留屏程序、启动主控或提交推送清理。请总控安排新Agent独立复核这三文件，再决定整合/编译及下一项底稿和归属。
