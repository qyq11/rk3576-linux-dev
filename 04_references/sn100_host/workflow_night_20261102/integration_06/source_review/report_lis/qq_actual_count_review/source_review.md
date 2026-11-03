# 实现后回原Windows的只读核对

以下路径以`06_programs/sn100_host_reference/Seerain450_PC/`为原参考根。原工程未改、未构建或运行，配置与数据库未读。本轮只有源码核对；总窗口已有的板B Linux日志不等于Windows运行证据。

| 原依据 | 确定行为及本片对照 |
| --- | --- |
| `communication/commanddefine.h:147,152`、`commandreceiver.cpp:79,207–208` | Q/q进入pcQueryAD(commandChanger(...))，不是新Qq主动请求或机型专用包。 |
| `commandreceiver.cpp:30–57` | 原逐字段字节摘取，QString(QByteArray)转换；额外字段外字节/内部左括号/未闭合尾字段处理不同于现Linux严格readFields。本片不放宽这些公共规则。 |
| `commandreceiver.cpp:1035–1053` | 至少1字段，首项toInt为count；字段数>=1+2*count才取值；循环严格i<count，按channelNum/AD文字成对取，重复只取首次。没有HIL/Base排除、加1、<=循环、SN500或新旧协议分支。额外尾字段不消费。 |
| `frmmain/frmmain.cpp:408` | signalPcQueryAD直接转驱动页slotPcQueryAD，无中间尾项拼接。 |
| `frmdriverset/frmdriverset.cpp:767–794` | 按既有表行完整文字找map，不重建表；空格split前4项原样写，不改精度/单位；短项的余列留旧字，缺通道写“-”，多于4项不显示。 |
| `tool/database/all_dal.h:49`、`all_dal.cpp:94,226–235` | 通道库路径./db/systemInfo/channel.db，TestChannelTable只CreateTable。未从现有库推名字。 |
| `tool/database/channel/test_channel_table.cpp:10–20,64–90`、`tool/database/table_dal.cpp:227–238` | 原test_channel字段id/channel_num(TEXT UNIQUE NOT NULL)/warmup_only/is_open，SELECT*无ORDER；建表/索引源码无通道名字种子。本轮未找到独立业务DataTable填充HIL的源码；UI实际是QTableWidget与TestChannelTable。 |
| `frmdriverset.cpp:862–928` | 依据原库已有行建表；Base精确按Engineer(4)显示，其他名toInt()>channelCount才跳过。HIL若存在则toInt为0，不因回包造行；原Base不可改，其他行工程师可改，Linux暂全disabled的Uc边界不动。 |
| `frmdriverset.cpp:168–173,260–271`、`commandreceiver.cpp:622–626`、`frmmain.cpp:537` | 初始405，reloadUI改450；数量24最后标题空白，否则800；仅试剂位数量变化才更新channelCount并重载。AD本身不改这些。 |
| `public/systemcommon.cpp:38`、`main.cpp:145` | 默认通道数18、启动theme/ChannelCount；本轮不读或修改实际INI/数量。参考展开源码未找到SN500/HIL专用字面量规则，不能由该通用接收器推固件特殊含义。 |

当前Linux调用仍为`InstrumentPage::receiveOriginalAd()`→`parseReadOnlyMessage()`→`parseChannels()`，成功后原表按键显示。候选只改parseChannels字段够用条件，`for(index<count)`、重复首值和输出键不变。

原count转换失败/负数走空map；正count不足原也发空map使UI清“-”。现Linux集合数必须纯数字、非负、<=1024，缺项拒绝并保留最后读数；这些既有安全差异本片保留，不能把本片说成全部边界等价。现严格UTF8和完整连续括号也不改变；真实样本均为合法ASCII且括号完整，不靠放宽编码获得通过。

原Base在本次第26对尾部所以忽略，若在声明前缀内则是普通map键。HIL在第25对所以入map，这只是位置结果，没有“固定25/26行名”业务规则。

源码复查还确认Linux当前代次/连接保护在页面，角色和405/450/24表头在建表方法；本片未改这些函数。实际SN500完整分支、原工作库名字、Windows实际运行结果仍待获准现场条件核对。
