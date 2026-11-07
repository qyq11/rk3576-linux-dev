# 通讯测试：恢复收发时间戳颜色

本片是供总控采用的候选，未写正式源码。用户已恢复后续移植；按新总控 `01a1a62e-3f89-7e62-98fa-d44cffa11f5c` 分工，只在本目录工作，不编译、运行产品或联板。后续验证用板A，板B旧工具保持冻结。

## 可采用的一处修复

原 Windows `frmdriverset/frmdriverset.cpp:1510–1549` 的 `slotShowSendData`、`slotShowReceiveData` 使用 `darkgreen` 显示时间戳和旧分隔符 `"  :  "`，正文在 `</font>` 后。正式 Linux 的 `appendCommunicationLine` 全部插入普通文本，遗漏原时间颜色。

候选仅修改 `instrument/instrument_page.cpp`：用 `QTextCharFormat` 给当前时间戳设置原 `darkgreen`，正文显式恢复插入前格式。沿用已合入的 `TX/RX/hex` 文本结构，不恢复旧分隔符，也不把设备正文当作HTML。底部跟随条件、文档局部光标、500段上限、计数、名单过滤及全部发送保护保持。无新增信号、设备接口或运行依赖；总控只需采用这一个普通补丁。

| 交付 | SHA-256 |
| --- | --- |
| 正式前像 | `1b887ccea0f7d5f83d15a54bd3421f4e89bccd567103c50baa62bc15bd3955e1` |
| `tree/instrument/instrument_page.cpp` | `17fb989dd9dffe9a9de85fa3fd80ef3be64f28df4dc1d957b3a19b62eb8739a2` |
| `timestamp_colour.patch`，相对SN100工程根 | `49269b83c1b55e14e1d579d71827d89ce06ed726267558f33d253068ceec89f8` |

## 实际核对与边界

- 基线提交 `021d4734915e4dac863155876851f974e365c92f`。以板A正式合入归档 `formal_merge_20261104_01/application/source_snapshot.json` 为路径及指纹依据，697份正式实体制作前后逐项复读一致；见 `source_check_summary.json`、`formal_before.json`、`formal_after.json`。
- 私有副本补丁检查/应用实际0/0，应用后与候选字节相同；正式仅 `git apply --check --directory=06_programs/sn100_host`，实际0、无正式应用；保留CRLF。独立Agent只读审查未见阻点，见 `independent_review.md`。
- WSL挂载把私有文件表现为100755，首轮补丁头因此与正式Git的100644不同。只按实际Git模式修正补丁头，源码未改；首轮补丁及回执保留。最终私有检查/应用仍有挂载模式警告、均0；正式检查0且无警告。`normalize_patch_mode.py`末行笼统写“最终无模式警告”不准确，以 `patch_results.json` 和 `patch_mode_note.json` 的实际字段为准。
- 本窗口启动第一次检查Git为干净；制作候选时已有README、主计划、主记录三处其他窗口修改，已保留，未写任何正式/公共文件。目录仅新增本片源副本、补丁、审查及源码工具材料，不清理旧原件。
- **Windows核对仅为只读源码核对**，制作后再回查上述原函数及权限/清空/显示开关入口；没有运行Windows程序。本轮未编译，未运行Windows/WSL产品、脱机或模拟测试，未连接板卡/主控/仪器，不新增任何运行通过结论。
- 板A待验：真实已获准收发下时间戳颜色与正文颜色；HTML样式文字/连续空格显示；停在中间阅读、选中文字时新消息追加；清空后及500段裁切后格式。无需为本颜色修点额外发送维护命令。响应、内存和长期运行未测；本片每条显示只增加两个值格式和至多两段文本插入，不据静态判断声称资源通过。

## 同范围源码现状

| 项目 | 当前结论与入口 |
| --- | --- |
| ASCII收发显示 | 已有逐字节转义和完整hex；正文不改网络字节。`communicationAsciiDisplay`、`appendOriginalCommunication`，首轮原名单优先级/空项/NUL构造保持。本片只补时间颜色。 |
| 手动输入 | 已有严格ASCII、空输入不提交、整条128000预算、不trim或解释文字形式的转义。`prepareCommunicationAsciiPayload`、`requestManualCommunication`。并非原自由正文全部兼容。 |
| 已接精确正文 | `Qj/Qk/QH/Ts/Tp[0]/Mz`及后来已合的`Uh`保持；`device_control_shell.cpp:266–342`及页面双侧校验。本片不重复实施、不开放其余正文。 |
| Uh主动请求 | `attachOriginalChannelReadback`绑定同一receiver/页/资料根；`requestReadbackBlocker`检查真实能力；精确Uh分支发送前后复核同一身份/连接/绑定，不把入队当完成。被动回包保存和页面更新独立运行。本次核对为现有源码存在，不补发、不制造确认。 |
| 其他正文 | 仍需逐项业务接线，保持未兼容。配置读写、任务、参数化机械动作和C测试正文等不能通过扩大白名单代替。 |

另记一项后续待核：原 `on_push_send_btn_clicked:2515–2519` 用 `toPlainText()`；当前为了严格检查不换行空格等字符改用 `toRawText()`，只映射段换行U+2029。Qt5原转换还映射软换行U+2028。该差异已有Qt源码线索，但本片不放宽输入；待独立切片核具体输入事件、原行为与允许正文条件。原依据可见 [Qt5.14.2 QTextDocument](https://raw.githubusercontent.com/qt/qtbase/v5.14.2/src/gui/text/qtextdocument.cpp) 的 `toPlainText`。真实ASCII人工输入、TX/RX及Uh回包保存/刷新仍待总控统一RK验证。

## 总控追加：样本下拉框只读意见

已另核正式 `samples/cuscombox_base.cpp/.h`、`sample_page.cpp:955–966` 与原 `customwidgets/cuscombox_base.cpp/.h`，未改样本源码。原版首次展开将菜单与阴影改挂主窗口，作为普通子控件；当前菜单有 `Qt::Popup`，阴影仍是普通子控件。当前菜单按屏幕坐标移动，随后把相同 `menu->geometry()` 直接赋给阴影的父坐标，两个坐标系不一致，属于确定显示风险；改成主窗子控件后也必须把屏幕位置转换为该父控件的局部坐标，并用同一坐标系裁剪。

Qt的弹层打开会登记活动弹层并尝试抓取键鼠，真正隐藏时撤销；父控件隐藏子树时会跳过独立窗口。因此当前只靠失焦隐藏菜单、菜单Hide时隐藏阴影，不能证明切页/重建/关闭时一定清掉仍存活的弹层。修复应统一菜单/阴影所属窗口与关闭路径，核切页、表格重建、注销和销毁时两者都隐藏/释放。保留选择后的容器值和真实 `itemSelected` 路径，不用假脏标记绕过选项点击。当前容器接收槽只改值和调用 `markDirty`，未见该槽立即销毁菜单；但 `emit itemSelected` 后继续访问 `this->menu` 的通用生命周期风险仍应由作者在修片中检查。以上依据 [Qt5.15 QApplication弹层处理](https://raw.githubusercontent.com/qt/qtbase/v5.15.13-lts-lgpl/src/widgets/kernel/qapplication.cpp) 的 `openPopup/closePopup` 与 [QWidget显隐处理](https://raw.githubusercontent.com/qt/qtbase/v5.15.13-lts-lgpl/src/widgets/kernel/qwidget.cpp) 的 `show_helper/hide_helper/hideChildren`，为源码推断，**不是EGLFS实际抓取成功或本次全界面无响应的根因证明**。目标板实际弹层、焦点/键鼠抓取、屏幕命中、外点/返回关闭与销毁恢复交总控/样本作者验证；本意见不替样本候选放行。

补充风险：`CusCombox_Base::setParent`仅重挂menu/frame，未重挂本体；当前样本容器路径未发现调用此函数。若后续修复使用它，需要保持一致所有权，防止外部父对象先销毁后本体析构再次删除裸指针。`qMax(50, available)`在下方不足50像素时仍会越界，显示位置/反向展开应按实际屏幕核对，不能仅删Popup标志就认定修复完成。四份直接对照文件指纹见 `popup_review_inputs.json`。
