# 通讯测试输入：补齐软换行到LF的转换

2026-11-05，按总控 `01a1a62e-3f89-7e62-98fa-d44cffa11f5c` 本轮分工，只制作独立源码候选。目的：原版能把编辑器软换行转换成实际换行字节，当前转换漏了这一步。未联板、登录、启停、发包、构建或运行Windows/WSL产品；没有脱机/模拟测试、提交推送或删除。本窗口未另派任务或发消息。

## 原版入口、转换及当前差异

原 `frmdriverset/frmdriverset.cpp:2515–2523` 的 `on_push_send_btn_clicked` 从 `sendTextEdit->toPlainText()` 取正文，追加到QByteArray后发出 `signalSendCmdToA53`。通讯页导航受原管理员权限控制，但这个槽自身没有编码校验、trim、解释转义、自动附加换行或单次确认/重试状态。原 `communication/commandthread.cpp:333–363` 按既有协议分片，不再次改写正文；空正文的分片循环不发送。

Qt官方源码已按原Windows版本5.14.2和目标5.15.13分别复查，下载的七份源码和完整SHA见 `qt_source_inputs.json`，原文件保留于 `qt_sources/`。两版调用链相同：`QTextEdit::toPlainText` → `QWidgetTextControl::toPlainText` → `QTextDocument::toPlainText`。最后一步对段落分隔符U+2029、软换行U+2028均输出LF（字节0A）；`InsertParagraphSeparator`插段落，`InsertLineSeparator`插U+2028。原QByteArray追加QString通过UTF-8转换，LF仍是单字节0A，没有新增CR或字面转义。[Qt5.14文档转换源码](https://raw.githubusercontent.com/qt/qtbase/v5.14.2/src/gui/text/qtextdocument.cpp) · [Qt5.15文档转换源码](https://raw.githubusercontent.com/qt/qtbase/v5.15.13-lts-lgpl/src/gui/text/qtextdocument.cpp) · [原Qt5.14键盘处理](https://raw.githubusercontent.com/qt/qtbase/v5.14.2/src/widgets/widgets/qwidgettextcontrol.cpp) · [QByteArray追加实现](https://raw.githubusercontent.com/qt/qtbase/v5.14.2/src/corelib/text/qstring.h)。

当前 `prepareCommunicationAsciiPayload` 为保留非ASCII原字符而读取 `toRawText()`，只映射U+2029，随后严格检查每个字符是否在00–7F。软换行U+2028因此被误作非ASCII拒绝。这不是缺少任意命令授权或设备拒绝；补齐这项文档换行转换即可。

## 候选改动及不会改变的流程

只改 `instrument/instrument_page.cpp/.h`：在现有段落换行映射旁增加U+2028→LF；同步一处接口注释和既有输入提示。不改编辑器文档，只转换局部QString副本，因此输入、选择和撤销历史不被重写。

转换之后仍调用原 `encodeCommunicationAscii`：遇到其他非ASCII，清空输出、保留原输入并报位置；调用者仍显示 `encoding-rejected`。合法但含换行的正文，例如 `Q`、换行、`j`，编码结果应为 `51 0A 6A`，**仍不等于精确Qj**，提交时按已有名单得到 `unsupported`，不会删除换行后发送。空输入、128000字节整条预算、精确 `Qj/Qk/QH/Ts/Tp[0]/Mz/Uh` 名单、权限/连接/请求冻结和全部Uh保护均未改。没有状态、配置、数据库或队列新增；不自动trim、不解释文字形式的 `\xHH`。

下表是源码推导的预期，不是实际运行结果：

| 编辑器原始文本中的内容 | 原Qt toPlainText结果 | 当前候选处理 |
| --- | --- | --- |
| 段落换行U+2029 | LF，0A | 继续映射，原有行为 |
| 软换行U+2028 | LF，0A | 新补映射，消除本次确定遗漏 |
| 已有ASCII空格、LF、TAB、CR或NUL | 这一步不替换 | 既有逐字符ASCII转换保持，不新发命令 |
| 字面文本 `\x0A` | 字面文本 | 保持 `5C 78 30 41`，不当作LF |
| 不换行空格NBSP，U+00A0 | 普通空格，20 | **仍拒绝U+00A0**，不静默改空格 |
| 中文、重音字母、表情或U+0085等 | 无上述ASCII替代 | 继续严格拒绝，不放宽到Unicode空白 |
| 富文本框架标记U+FDD0/U+FDD1 | Qt转LF | 本片仍拒绝，未扩到富文本框架转换 |
| 嵌入图片/对象U+FFFC | 保留对象替代字符 | 仍拒绝，不忽略对象后发送余文 |

**明确的规则边界：**原Qt确实把NBSP改成空格，这与当前“保留原字符做严格ASCII拒绝”的既有处理不同。本轮明确要求保留严格ASCII拒绝，因此不换回 `toPlainText()`，也不新增NBSP替换；不宣称NBSP已与Windows完全一致。原Qt对FDD0/FDD1也有替代，但原通讯框粘贴富文本框架的实际使用、预期ASCII规则尚未核清，本片保留拒绝并交后续核实。两类差异分别记录，不把未知情况当已兼容，也不因它们停住确定的软换行修复。

## 基线、补丁与源码检查

本轮唯一基线是 `07_backups/sn100_host/board_a/resume_20261104_02/source_snapshot.json`，表SHA-256为 `d24ee5f9ab0786ff15528c3958f997e47181caab9fbfe5373abc36f2833d218d`。制作前后实际复读697份正式源，全部匹配该表且保持不变，不用旧021d表冒充本次前像。保留11月4日已采用六源，以及现有文档和归档未提交修改。

| 文件 | 前像SHA-256 | 候选后像SHA-256 |
| --- | --- | --- |
| `instrument/instrument_page.cpp` | `17fb989dd9dffe9a9de85fa3fd80ef3be64f28df4dc1d957b3a19b62eb8739a2` | `e50bc5411ff63c6f21ebf137415d7d03d145371bf9715d176f33a3a56d1c6ac0` |
| `instrument/instrument_page.h` | `0215157511c2cf74cb3240c11a30a2c12d293728ca27a6b840837c7f9e313e11` | `0ef5ef331ff17ecb49556d321f8bf4b08c30208fecbdea8146b025c21375c087` |

普通窄补丁 `soft_line_break.patch` 相对SN100工程根，SHA-256为 `9bab0c719d108f8dc5e75fd833fc51b69f7338b2f871c5533d3c80bc754778ce`。前后源在 `before/`、`tree/`。私有副本补丁检查/应用实际0/0，应用后逐字节等于候选；正式源只做补丁可应用性检查，实际0，未应用。三项stderr均空，CRLF保持；WSL挂载模式仅在补丁头按Git实际100644表示。详见 `patch_results.json`、`source_checks.json`，不是功能测试。按本轮限制由作者自行核读窄差异，未启动额外Agent审查。

**Windows核对只有只读源码对照。**候选完成后再次回查原槽、Qt两个版本及原分片入口；没有运行Windows原版或重建原工程。ARM64编译、RK输入/实屏、真实仪器和资源验收均未执行。新增一次字符替换扫描，不新增线程、定时器、网络请求或持久化；资源影响仍待板A测量。

## 交总控的最小板A验收

1. 总控采用、交叉编译后，在原配置、原资料和同一主控条件下安排板A验证；正常管理员登录由用户方便时完成，不假登录，不占板B，不为本片改INI或切其他版本。
2. 在通讯输入框用实际键盘分别插入段落换行和软换行（常用Shift+Enter，实际平台组合键/输入事件须记录）。核输入仍保留原排版，`communicationAsciiInputValid=true`、无U+2028错误；如能只读观察 `prepareCommunicationAsciiPayload`，段/软换行均须得到0A。该读取接口本身不发送。
3. 输入 `Q`、软换行、`j`，核准备正文准确为 `51 0A 6A`。在总控获准操作范围内提交时应为 `unsupported`，不得trim成Qj或产生本次手动发送。持续后台状态查询另计，不能把TX计数变化直接当作手动输入已发；无需为本片发送维护动作。
4. 输入含NBSP和其他非ASCII时仍报对应字符，准备输出清空、输入保留；删除该字符后可恢复合法状态。软换行夹杂NBSP时，不能因前者已映LF而放过后者。字面 `\x0A` 应保留四个ASCII字符，中文/表情和富文本对象仍拒绝。128000/128001边界及原无换行精确正文的既有处理保持，不为凑通过开放新正文或实际发送。

上述均为待执行步骤，无本轮RK/Windows运行通过证据。当前不能登录只阻实际界面核对，不影响交付这份独立候选。总控可先核补丁和接口，其他待核项继续登记。
