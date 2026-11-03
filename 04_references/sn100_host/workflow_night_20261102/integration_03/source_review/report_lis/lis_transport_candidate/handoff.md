# LIS TCP基础收发候选交接

本切片用于为后续医院系统的任务查询和结果上传提供TCP字节收发能力。当前正式Linux程序已有LIS设置保存和完整DSR报文文件导入；文件导入只保存/查看原报文，不代表已经联网下载或应用任务。本候选只补连接、分帧、发送结果和停止接口，没有补造HL7字段、业务确认或模拟对端。

所有改动在本目录`candidate/`，正式源码与只读参考工程未由本窗口改动。总窗口决定何时合入、完整编译及联机；本窗口不部署、不接管RK或共享显示。公共README、计划和总记录由总窗口统一更新。

## 原版实际调用链

以下路径相对只读参考根`06_programs/sn100_host_reference/Seerain450_PC/`。

| 入口或规则 | 原函数和实际行为 | 候选处理 |
| --- | --- | --- |
| 启动 | `frmmain/frmmain.cpp`构造/初始化先加载配置，约277行在`getAutoLink()`为真时`openComm()` | 当前主入口只准备已提交配置；实际端点未核，不恢复自动打开 |
| 设置保存 | `frmuserset/frmlissetting.cpp::on_btnSave_clicked()`写可见键，Tcp只更新ip/port，串口只更新commname/baudrate；随后无条件发`signalConnectLis`；`frmMain::slotConnectLis()`直接`openComm()` | Store保存链保留，只有`saved`成功快照才配置候选。保存失败/草稿不改端点；本片保存后仍不连接 |
| 模式和端点 | `commlis/liscommsetting.cpp::openComm()`重新加载INI；只有type精确为tcp才走TCP；tcptype精确为client选客户端，其他值包括空值选服务端；服务端监听Any、支持多个客户端 | 只实现TCP；保留client/其余server选择。串口明确报未接入，不回退到TCP；没有默认替代地址 |
| 字节编码 | `LisCommSetting::msgToMllp()/mllpToMsg()`：原始`lis/lisCode`精确等于false选UTF8，所有其他串值选原生UTF16；包括帧头尾一起编码，不带BOM | 原资料模式使用`originalValues`的原始串；保留非false分支。明确输出Windows/RK小端UTF16：`0B00 ... 1C000D00`；UTF8为`0B ... 1C0D` |
| 接收边界 | `LisCommThread::slotReceiveLis()`追加字节，按字节找头尾；找到头后才去掉前导杂字节；半帧留待下次；完整消息入队 | 每个真实TCP对端各有缓存，保留同一对端的拆/粘包规则。UTF16仍按原始字节搜尾，没有擅加转义/字符对齐规则 |
| 调度 | `LisCommThread::slotCommandTimerTimeOut()`约200毫秒三拍：发一条、处理完整接收、业务超时/模拟心跳；同一拍约600毫秒一次 | 保留三拍次序；本片第三拍只发`businessTimeoutTick`通知，不修改样本状态或生成任务 |
| 心跳 | `LisCommThread::linkProcess()`虽注释“不启用”，实际在moni和link为true时排入`LisCommSender::getPcLinkCommand()`的裸ASCII `AAAAA` | 明确不实现模拟链，simulation配置拒绝；不发送AAAAA，不造心跳HL7或业务ACK |
| 连接/写入期限 | `customtools/tcpbase.cpp::openTcp()/tcpWrite()`阻塞等连接1500毫秒、写入1000毫秒 | Qt5异步socket；连接用不提前触发的单次计时器；写入1000毫秒预算通过200毫秒轮询观察，可能晚报，不是硬实时期限 |
| 重连 | `TcpBase::tcpReConnectTimerOut()`只重新建立socket；`SystemCommon::reconnectTimer/reconnectTime`来自原`userOption/ReconnectTimer/ReconnectTime`，主程序把小于2000毫秒的值钳到2000 | 使用当前已加载的原运行配置；只重建连接，不重放业务消息。写完socket不能标成LIS已确认 |
| 任务/确认 | 原`genLISFromSeverInfo()/genLISToSeverInfo()/ACKMessage()`、`LisCommReceiver`和样本页共同处理任务及结果归属 | 本切片未移植此业务层，不能拿通信对象或原报文导入页替代 |

## 必须保留的差异和待核

1. 原Windows页面把`lis/link`显示为“应答模式”，启动时据其自动打开，但保存后不论该值都会打开。现有程序中应答模式勾选与顶部LIS off并存，见`windows_review.md`。本候选不把该键当作传输层总开关；主入口一律不自动打开，是实际端点尚未确认时的当前边界，不能报原自动连接已经移植完成。
2. 原UI允许端口0至99999，`TcpBase`把`QString::toInt()`结果传给16位TCP端口，服务端端口0还可能选择临时端口。候选连接接口仅接受1至65535的十进制端口；不改写原保存值，也不依照截断结果连接。0、超范围、非数字原值的等价处理在放行真实端点前待核，不能说所有原端口行为已一致。
3. 原UTF16解码以`size()/2`忽略奇数末字节；候选明确报错并丢弃该完整帧，连接保留。这是新增的异常输入拒绝，尚未得到真实对端核对；不能写成原版已有校验或全异常兼容。有效偶数字节的编码及字段没有改变。
4. 原服务端合并所有客户端的接收字节，可能跨客户端拼半帧；候选按对端分别分帧。发送仍向目标客户端广播，但目标在排队时固定，新接入客户端不会收到旧业务排队消息。断线或停止时，每个旧目标都有明确结果。
5. 原代码完整帧/队列缺少容量上限，缓存超限可能直接清空；候选超限报错并结束相应连接或拒绝新发送。下方数值是本地资源预算，不是原协议规定。原`openTcp`可能连接/监听失败仍返回true，关闭/切换可能留重连；候选如实区分发起、监听、已连、失败及停止。

## 文件和接口

新增`lis_transport/lis_mllp_codec.cpp/.h`负责原边界和编码；`lis_tcp_transport.cpp/.h`负责异步TCP及生命周期；`lis_transport.pri`仅增加已有QtNetwork依赖。三处窄接线是`shell/main.cpp`、`shell/build.sh`和`shell/shell.pri`，不改设置页/Store、不安装QtSerialPort、不把LIS带进报告渲染辅助程序。

候选main基于`924d5fb5...`保留本切片基线；交接核对时总窗口已将Qq接线合入正式main，SHA为`f200237e...`。本补丁对该正式main的`git apply --check`仍退出0。只能应用窄补丁，不能把整份候选main复制回去覆盖Qq或其他窗口改动；合并后的完整main仍由总窗口重新编译。

传输对象创建在GUI线程；所有公开方法必须在该线程调用，不移动到其他线程。socket与三个计时器依靠同一事件循环，不使用阻塞wait、processEvents、忙等或额外线程。主入口对象名为`originalLisTcpTransport`。

1. `configure(settings,reconnect,reconnectMilliseconds,error)`只接受Store已提交快照，先结束旧连接/在途请求，再准备模式、地址、编码和配置令牌。调用不会连接；`loaded/saved`信号都只走这里。
2. 总窗口先核实际生效INI、TCP角色、真实对端和允许动作，再以当前`configurationToken()`调用`openConfirmed(token,error)`。当前main没有该调用；更换配置使旧令牌失效。返回true只表示异步发起或已监听，不等于Connected。
3. 后续业务层先订阅`wireBytes/messageReceived/sendFinished/messageDiscarded`，再允许打开。收到正文带原代次和对端标识，不直接入样本/结果库。原始正文及解码文本都提供，业务层仍须核原HL7字段、消息归属、确认与重复规则。
4. `sendMessage(id,expectedGeneration,body,error)`使用本对象生命周期内严格递增的非零请求号；断线前排队的目标不会移交新连接。仅返回排队结果；每个目标另回报`WrittenToSocket`、`NotSent`或`OutcomeUnknown`。`WrittenToSocket`没有业务确认含义；部分字节已提交后断线/停止归`OutcomeUnknown`，禁止盲目重发。
5. `stop(reason)`先禁止新动作，停止接收监听、连接期限及重连，再结清在途和排队发送；半帧、完整待交付消息被明确报告丢弃；未处理入站socket也被关闭。Store关闭和应用退出已接该接口。普通断线保留已经收齐的旧消息及其原代次供业务判断，不跨连接拼接。

主入口目前只接状态/错误日志；`wireBytes`提供具体RX及本地接受的`TX_SOCKET`字节，但尚没有持久化/显示接收者。实际TX/RX记录、容量/轮转/敏感字段处理须随正式业务接线落实，不能报告已具备完整通信日志。

## 资源和响应范围

本地已接入并处理的对端最多16个；待接受入站队列另设16个上限，这不代表整个进程只有16个socket。每对端读缓存64KiB、分帧缓存/完整单帧最多1MiB；每次读最多64KiB、一次最多256帧。待发送队列64条/4MiB，完整待交付接收队列256条/4MiB。广播目标先全部登记，再逐个写入；通知导致停止/换配置后不继续操作旧队列或推进新会话阶段。

上述两个4MiB是各队列预算，不是整个模块或进程内存上限。每个对端另有半帧、当前帧和socket缓冲，临时编码/解码和日志也占空间；跨线程慢槽还可能积压Qt信号。当前没有RK响应、内存、CPU、长稳或同板控制实测，不能据这些容量宣布稳定性通过。

## 核对与交接状态

- Windows：已实际打开现有原程序的设置入口并核显示，详见`windows_review.md`；字节收发、业务变化、报错和恢复仅核源码，仍待真实条件。
- 编译：旧8616f9批四个对象编译退出0，证据保留在`revisions/8616f9_before_utf8_nul/`；本轮UTF8修正只重编编码器，退出0，其余三对象沿用并核旧指纹。四对象均为AArch64小端，8份候选输入在本轮编译前后指纹一致。最新证据为`compile_slice_arm64.log`、`compile_inputs_before/after.sha256`和`candidate_manifest.json`。旧传输编译有Qt5旧`QAbstractSocket::error`信号弃用告警；本轮单对象未产生告警。不是完整链接或可部署程序。此前`*_initial/pre_*.log`是中间版本；`*_invalid_snapshot.log`中WSL缺rg导致指纹未正确生成，不能用它的末行作指纹证明，最终脚本已改用明确文件清单。
- 源码独立复查：已核编码/边界、广播回报、停止/换配置重入、旧代次及三处窄接线，结论见`independent_review.md`；没有借此新增运行结论。
- RK/真实LIS/串口/仪器：均未在本窗口执行；缺真实对端、业务接线、打印机或仪器条件时不安排模拟服务。总窗口核板卡身份后统一安排。

本切片可交付的是受限基础候选。正式任务查询、任务应用、结果报文、确认回复、业务超时和重复处理另切小片；曲线算法、报告空回调、告警和机械动作未在本片改写。

## 本轮修正：UTF8正文遇NUL时保持原版转换

总窗口独立审查发现，旧候选把原`QString::fromUtf8(recLisData)`改成了显式长度重载，导致UTF8正文含NUL时可见文本不同。只读参考源码`commlis/liscommsetting.cpp:248`与实际编译所用Qt5.15.13的`QtCore/qstring.h:711–712`共同确认：QByteArray重载先用`qstrnlen()`确定长度，遇首个NUL停止。对于字节`41 00 42`，原文本为`A`；旧候选显式长度会保留`A\0B`。这些是源码推导，未执行报文测试。

现仅将候选`lis_mllp_codec.cpp`改回`QString::fromUtf8(body)`并说明原因。原始正文QByteArray、分帧、编码选择、头文件、传输和主入口均不变，没有新增字段校验。旧补丁`8616f992...`、旧清单、源码及编译证据保留在`revisions/8616f9_before_utf8_nul/`，`base/`未动。

本轮使用`compile_slice_arm64.sh codec-only`仅重编编码器对象，8份候选输入仍核编译前后指纹；另三份对象沿用前批，清单脚本逐一核它们的旧指纹，并检查候选仅编码器cpp改变。最新清单分别登记重编范围和前批证据，不把这次单对象编译说成再次完整编译。新补丁/指纹以当前`lis_tcp_foundation.patch`和`candidate_manifest.json`为准，旧8616f9不再作为最新候选。

本修正回Windows只读核了原对应解码函数与本地Qt5头文件，没有再占用原程序导航、输入报文或新启动/重构建Windows。20:40的设置入口实际运行证据范围不扩大；本次NUL转换、RK收发和真实LIS仍未运行验证，由总窗口按真实条件安排。
