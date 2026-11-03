# 第09批LIS唯一失败的只读归因

2026-11-03。仅读本地收集结果、实际检查器源码和正式UI/接线。没有修改正式源码、检查器或原结果，没有运行产品、编译、网络或板卡动作；当前留屏程序没有被本审查触碰。

**结论：本项失败由检查器的递归控件查询范围错误解释，未发现正式LIS状态接错控件的代码证据。**检查器把顶部LIS状态图标和设置页中同名的LIS导航按钮一起计数，再要求只有一个，因而对正常页面结构报“缺失或不唯一”。这个源码归因不等于LIS图标实际显示通过，更不等于真实LIS通信或业务通过。

## 原失败保持

本地已收集`results/result.json`属于正式发布`b31f581fd240e09ee4c0a4a4eb6c4cbdd674fa9cf8897fcd170e9d0ca058d993`，SHA-256为`a14ee95878e63230695eacc80219064eddf5877da98a871f00cb90bc40c9c757`。这里只给本地收集副本指纹，不把它冒作板端未脱敏原件指纹。

- 实际26项，25通过、1失败；failed为1，失败名为“原LIS配置来源与实际连接状态可读；无对端不冒称通信通过”。
- LIS快照status为integration_error，原reason保持“正式LIS图标对象缺失或不唯一。”，software_state_passed为false。
- configuration_matches_requested_context=true、connection_and_settings_source_fingerprint_equal=true；实际已提交值仍为原资料、TCP client、127.0.0.1:3800、link=true，autoGet/autoSend/simulation均false。
- 真实状态为Connecting与ReconnectWaiting交替、0个已连接对端；app.log保留Connection refused及最终Stopped。无真实对端不能记LIS连接/收发通过，也不能据此说INI发生变化。
- 原始回包独审退出文件仍是1；本次不改成0，不重新计算或补造其提前终止后未形成的逐项通过结果。当前告警Qk已发一次但无回包的待验范围也不因本归因改变。

## 确切调用链

检查器`lis_connection_observer_09.h:129–131`使用：

```cpp
const auto buttons = shell_->findChildren<QWidget *>(QStringLiteral("btnLis"));
if (buttons.size() != 1)
    return integrationError(QStringLiteral("正式LIS图标对象缺失或不唯一。"));
```

本地配套Qt的qobject.h:180明确该findChildren重载默认FindChildrenRecursively，即搜索全部后代；没有按当前可见页面过滤。正式源码至少有以下两个同名QWidget后代：

| 位置 | 类型 | UI中的父子路径与用途 |
| --- | --- | --- |
| `shell/frmmain.ui:409` | CusBadgeButton | frmMain/frmBg/statusArea/btnLis，顶部LIS连接状态图标。 |
| `shell/frmuserset.ui:188` | QtMaterialFlatButton | frmUserSet/usersetFunMenu/btnLis，进入LIS设置页的导航按钮。 |

`frmmain.cpp:81–82`将frmUserSet构造为stkCenterAdptive的子页并加入主窗口；`frmuserset.cpp:15–21`执行该页setupUi，之后38–57行把其btnLis纳入设置导航。QtMaterialFlatButton继承QPushButton，也是QWidget；CusBadgeButton也明确继承QWidget。因此对shell做上述递归QWidget查询会把两个不同用途的控件都纳入。原结果没有保存buttons.size，不能把源码推导的两个写成“板端实际记录了数量2”。

观察器先写配置及来源匹配字段，再在这里返回integration_error，尚未读取顶部按钮的提示。`status_login_review_09.cpp:565–577`把integration_error视为已评估失败，所以即使原配置与来源均匹配，仍将这一项记为失败。若没有这个查询歧义，0个真实对端原本会在observer:150–151返回pending；它不是当前这条“控件不唯一”失败的原因。

## 正式UI接线核对

正式`main.cpp:1445–1446`把真实LisConnectionController的stateChanged接到`frmMain::setLisConnectionState`，错误细节和初值也调用同一setter。`shell/lis_shell.cpp:53–79`在该setter中直接操作`ui->btnLis`：更新顶部图标、on/off文字和结构化提示。这个ui是Ui::frmMain，不通过全树同名查询选择控件，不会指到Ui::frmUserSet的导航按钮。

上述六份正式源逐SHA匹配本次05完整构建的source_snapshot。实际检查器头及CPP也分别匹配prepared_inputs的模板表和execution_09_result中的lis_observer_sha256/checker_source_sha256。因而本归因针对本轮实际物料，没有拿另一个候选或旧版本的源码替代。

## 留给下次的最小修正

只在新的检查工具副本中把查询限定到顶部状态图标，例如显式包含`shell/widgets/cusbadgebutton.h`并按CusBadgeButton类型查询，或先定位statusArea再查询其直接子项；同时记录实际数量、类型和父对象名称便于诊断。不能只对shell直接加FindDirectChildrenOnly，因为顶部图标也不是shell的直接子项。无需改名正式UI、改LIS业务或改INI。

本次只归因，不修改被冻结的检查器、原failed/退出码或重新运行case。以后要证明工具修正及实际图标提示，须由总控在新的独立attempt中另行获准观察，保留本次26/25/1及独审1原件。真实LIS对端、串口、下载/上传和业务确认继续待验。
