# ASCII A2修订01的总控侧独立窄审

2026-11-03。读取本轮实际完整补丁、tree/before两文件及当前正式前像，比较A1、A2相关源码。另只读核Qt5.15.13官方文本追加实现，没有运行产品、模拟数据、编译或联板；未改正式、候选或公共文档，只写本报告。A1原报告保留。

**结论：A1两项确定过滤阻点已关闭，强制把使用者光标/选择送到末尾的路径也已去除；在本次两文件范围内没有发现新的确定阻点，可进入总控的正式前像核对、合入及ARM构建。**这只是源码审查结论，不是RK显示、输入或仪器通过。

本片仍是已有收发的ASCII呈现，加上严格输入检查/编码准备；没有B手动发送、C参数命令或自测接收注入。不能用本审关闭这些未接任务。

## A1阻点关闭依据

### 旧过滤正文及匹配规则恢复

候选appendOriginalCommunication现在只在RX分支使用原有的`QString::fromUtf8(payload)`重载构造filterText，再按旧QString::contains匹配。没有改成payload.contains，没有改用完整显式长度重载，没有把ASCII转义后的展示串交给过滤器。

因此本地配套Qt5在该QByteArray重载中使用qstrnlen的旧NUL截断语义保留；只在首个NUL后出现的词，不会进入过滤文本。Show不能凭这个词放行，Mask也不能凭它屏蔽。这里是代码推导，未生成或运行示例数据。

Show仍先于Mask；发送不进过滤分支；原QStringList、循环顺序及空项contains语义未变。没有名单ASCII预校验，所以Show空项与非ASCII词同时存在时，空项仍能让原逻辑匹配；非ASCII词也继续使用原UTF-8过滤语义，不会触发全RX暂停。

A1新增的activeCommunicationFilters、communicationFilterError、过滤错误属性/tooltip和诊断行均已删除。清空按钮、显示开关处理以及整个applyOriginalRuntime函数，与当前正式前像源码段一致；原清空及计数归零后不会再重新追加A1诊断。

### ASCII展示与过滤明确分开

过滤通过后，最终正文由完整原payload逐字节生成ASCII可见表示。保留旧过滤NUL截断不意味着展示截断；反过来，展示能看到NUL及后续字节，也不意味着重定义原过滤。

显示辅助与已审A1函数逐字一致：unsigned字节遍历；可打印ASCII保留大小写/空格，反斜杠双写；控制、DEL及高位字节生成固定两位大写hex的`\xHH`。它不按UTF-8合字，不把高位字节当Latin-1扩展字符，不用C字符串长度截断NUL。反斜杠独立处理使字面转义串与真正控制/高位字节可区分。

仍直接从原payload/bytes生成hex辅助；保留的fromLatin1只处理toHex产生的ASCII字符。原QByteArray是const输入，没有重编码回写、改帧或更改自动查询。原资料TX仍用现有sentDecoder、RX仍取原payloadReceived；fallback仍是原wire层，层次没有互换。

## 光标、滚动与选择

追加助手已移除setTextCursor和ensureCursorVisible。它创建局部文档光标，用begin/endEditBlock包住追加；只有追加前竖滚动条已在底部，才在追加后滚到新底部。使用者回看时没有新增显式滚尾或主动替换当前选择的命令。

只读对照的[Qt5.15.13 QTextEdit实现](https://raw.githubusercontent.com/qt/qtbase/v5.15.13-lts-lgpl/src/widgets/widgets/qtextedit.cpp)在只读状态下同样先判断是否到底，再决定跟随；[底层追加实现](https://raw.githubusercontent.com/qt/qtbase/v5.15.13-lts-lgpl/src/widgets/widgets/qwidgettextcontrol.cpp)使用局部文档光标与编辑块。候选与这些结构相符，未再看到A1那条确定的强制移动路径。

不据此声称交互完全实测等价。Qt原append还显式继承当前UI光标的字符/块格式，候选是在文档末尾插入；本页现有纯文本路径未见由此必然触发的颜色/字体错误，但原格式继承、回看选中复制、水平位置及500块裁剪都应在RK实屏确认。500块裁剪可能删除选择所在的旧内容，不能保证被删内容的选择仍存在。

纯文本insertText不把尖括号解释成HTML，连续空格和转义反斜杠可见；这属于ASCII呈现的明确做法，不是原HTML猜测路径的运行通过证据。

## 输入准备及发送边界

- encodeCommunicationAscii、communicationAsciiDisplay、prepareCommunicationAsciiPayload和refreshCommunicationAsciiInput四个函数，与已审A1代码段一致。
- 输入先从toRawText取文档字符，明确将文档段分隔符映为LF，再严格检查0x00..0x7F；不trim、不自动补换行、不解析字面转义或hex文本，失败清输出并保留编辑器内容。此处不能恢复进入QTextDocument之前的原文件行结束字节。
- 输入检查成功不代表可发送；空串在编码上合法不代表协议可发。新接口不检查会话、连接代次、业务状态、命令、长度或额度，B以后仍须独立核这些条件并当次重新取输入。
- disableDeviceActions、updatePermissions、updateDeviceButtons与正式前像函数段一致。新增连接只有sendTextEdit的textChanged到输入检查，没有为push_send_btn/C参数/自测按钮新增clicked或解禁，没有新增DeviceControl发送、连接、关闭或仪器动作调用。
- A1新增的两个过滤状态数据成员已删除，当前头文件只新增include和成员函数声明，未新增数据成员。仍须Root用匹配源码/头构建，不从本审推定旧对象可直接重链。

## 两文件补丁与指纹核对

本次没有重复Git check/apply或684树回演：

- tree实际仅.cpp/.h两份；before字节与当前正式两文件完全相同，after SHA与changed_files的A2_r1字段相同。
- 独立从两份before/tree生成unified diff，仅在内存里统一换行比较，与完整communication_ascii_A2_r1.patch文本一致。
- before与after均保持CRLF：CPP为1081→1173个换行，H为105→110个换行；实际字节数分别56707→60797、4788→5154。
- formal_before、formal_after、candidate_expected均684项；前两份元数据相同，expected恰好替换这两份after SHA，不增删其他键。没有重算其余682份当前文件，也不冒称根完整前像已再次验证。
- 头文件与A2实物逐字节相同；A2→本修订差异是新增QScrollBar include和追加助手的窄修正。作者原检查/失败资料未改，本审没有把其退出码替换成自己的编译或运行结果。

## 留给Root的RK项

先核完整正式前像并按唯一流程编译/部署；作者不关当前留屏程序，不复跑旧自动退出检查器。

- 用实际原主控已有获准流量核页面正文、原字节/hex、显示开关、清空及计数，不为覆盖控制/高位/NUL分支造包或发额外动作。
- 原Show/Mask空项、非ASCII词、NUL前截断等源码分支本次已读到保留；没有真实数据条件时仍列待实际输入，不报完整收发覆盖。
- 回看、到底跟随、选择复制、水平位置、字体/颜色、长行及500块裁剪须RK实屏核对。
- 输入错误提示、管理员真实入口和键鼠条件分别待验；严格字符检查不是业务发送或权限验证。
- 逐字节正文最坏4倍、hex仍在，500块不是内存/响应预算；实际CPU/RSS/PSS/界面响应及与相机、主控并行条件另记，不判长稳或实时控制通过。

本审没有发现需要再改ASCII A2_r1核心代码的确定阻点；上述实际验证与B未接范围继续保留。

## 本报告绑定指纹

完整补丁SHA-256：`952523f3614dcd758f46af5d7c40848961cf376a261425897ff134575fd67937`。

| 文件 | 正式前像SHA-256 | 本修订后像SHA-256 |
| --- | --- | --- |
| `instrument/instrument_page.cpp` | `ffffde2772495de2257fbf71a53953cc9b5c1610f5e535c2be7498a026b8112b` | `40c307d5f01a04a2408370b377382a3c2483c87520d1030e69d516716e550edb` |
| `instrument/instrument_page.h` | `00f8907b361863e3b0907c663f949d5b6930d4bae35bee9879b7fbcd4c0fa7d0` | `859b8447f8c1567619e837240aa42fb6124205c7f678b24785b1e7d1175ccf66` |
