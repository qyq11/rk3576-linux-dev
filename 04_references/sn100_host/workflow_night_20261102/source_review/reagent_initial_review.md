# 试剂操作第一轮只读核查（2026-11-02）

本轮未修改正式源码、未构建、未运行电脑版本、未访问板卡。工作交给试剂工作聊天继续；原参考工程保持只读。

**复核更正：**初稿把探测写成`MG`、液位/扫码更新写成`MN`/`MA`，均不正确，以下已纠正。探测负载为`Mg`（小写g）；液位报告为顶层`N`，扫码更新为顶层`A`。`MG`实际是关机命令，不能用作探测。本文还补充液位换算和清盘的完整落库区别，不能把控件内字段保留说成数据库所有字段保留。

## 真实执行入口与不可达旧实现

- 原 `frmreagent/frmreagentmaterial.cpp:827` 的单孔按钮调用 `slotDectLVContinue(1)` 后立即 return；后面的旧单孔实现不可达。该 Continue 分支先核设备 actionReady、选中孔位、对应区域舱盖，再确认和 dectLV。
- 原 `frmreagentmaterial.cpp:875` 的整盘按钮调用 `slotDectLVTotalContinue(1)` 后立即 return；后面旧实现不可达。实际分支从 getAllHoles 中只选状态非 R_None/R_UnKnown 的孔，按 mode 与 Times 构造顺序，检查两区域舱盖，再确认。
- `dectLV`（:767）从原项目 projectReagent 的 `needle_select` 建按试剂名映射，左针=1、其他=2，后者覆盖同名之前项；再调用 `getReagentDectCommand`。不能仅按孔区给默认针而遗漏项目选择。
- `commandsender.cpp:170` 的探测命令为 `Mg`，字节为`0x4d 0x67`。构造器:173–174依次追加`PCMoveingCmd='M'`与`PCDection='g'`，常量定义在`commanddefine.h:81/97`。字段依次为孔数；每孔8项：孔位名、试剂名、区域、针号、sendIndex、瓶型、bottom_diameter、angle。无项目针时试剂区默认1、稀释区默认3；瓶型按名称首个匹配，原发送 toLatin1。
- 单孔更换（:637）实际是 fullHole，按新协议先 Us，再 dectLV；不是转盘出口动作。应核 fullHole 后数据和余量语义。

## 按钮实际动作

- **恢复（:1455）没有实际恢复动作。**函数仅定义局部 setDefault lambda，未调用；restoreCalibrationPanel 调用已注释。不能按按钮名新增批量恢复或发送命令。
- 优先使用在 `wgtreagentpanel.cpp:3135`。测试中拒绝，再确认；新协议将选中孔优先级设1，发送 `getSingleReagentUpdateCommand(hole,false)`，不重排其他孔。旧协议则对相同试剂孔递增优先级，分别按两区域 arrangePriority 后逐孔 updateHoleInfo。两分支不可混用。
- `commandsender.cpp:509` 的 Us 保留15字段：孔位、试剂类型、试剂名、批号、瓶型、amount、priority、state、setDate、outDate、warning、out、max、max、isUpdate。第14项确实为 max，不是 used；优先使用末项0。当前 `OriginalReagentRepository::singleReagentUpdatePayload` 已有此编码；`singleReagentSettingPayload` 会强制 priority99，不能给优先使用调用。
- 扫码 `frmreagentmaterial.cpp:978`：未连接拒绝，actionReady后确认，发送 Mh，原等待60秒。构造在 commandsender.cpp:263。原初始化/当前代码 btnScan 禁用，须核实际启用来源与机型，不能自行全机型解禁。
- 转到出口 `:925`：与更换试剂不同，原 btnChange 默认 hide。空闲/主状态为 Warming 或 Ready、actionReady，且选试剂区，确认后发送 `M`+PCRChange+[hole->getId()]，等待10秒；应核目标发行版是否展示。
- 清盘 `:1500`：确认后先 clearPanel，再发 `UC`。清盘并非所有孔分别 Us；控件保留的余量、落库实际清空的高度见下方完整核查，不能直接复用现有`OriginalReagentEditor::clearHole`。
- 校准模式 `:1531`：读取 JSON 标定模式，选择后改变面板，再构造连续探测；依赖大量孔位及校准数据，不能以普通探测替代。

## 完成通知和状态

- 原 `Mg` 回复进 `slotPCDection`（:438），只看最后字段数值>0为完成，否则失败；不是发送成功即完成。函数内已有日志写着“MG消息回复”，该日志大小写不符实际常量，不能照日志构造命令。
- `Mh` 回复进 `slotPCRScan`（:448），首字段>0完成；顶层`A`扫描更新进scanUpdate，顶层`N`液位更新进levelUpdate。`commandreceiver.cpp:61`先取顶层字节：:85的N和:88的A直接发信号；M走:155的pcMoveingCmd，随后按小写g/h/i分别分发，不能把顶层N/A加上M前缀。
- 原出口移动回复 `slotPCRChange`（:457）要求至少2字段，第二字段>0完成。
- 权限 `slotUserChange`（:1368）：工程师及以上显示恢复/清盘/标定；管理员显示更换。当前角色数字映射沿已确认字典。

## 接线约束

当前 `ReagentsPage` 设备按钮统一 showOffline；`DeviceControl::maintenanceCommand` 只允许固定白名单，不能直接发`Mg`参数动作，需总窗口明确接入原试剂业务命令。`sendOriginalConfiguration` 已允许 Ur/UP/Up/Us，但 UC 尚未允许。原单孔保存已有 originalHoleSaved→主窗口 prepareOriginalReagentUpdate→Us→Ur回读链，应复用，优先级不要误走强制99编码。

建议首先完成新协议优先使用：按原 Us(priority1,isUpdate0)，保留当前原库值直到收到对应 Ur，并检查超时/断线结果不明；旧协议重排另切片。探测须先补项目针、孔位sendIndex、舱盖及顶层`N`数据更新，不能只解禁按钮。

## 复核：消息的字节与分发层

| 用途 | 实际负载开头 | 原源码依据 | 不能混用的命令 |
| --- | --- | --- | --- |
| 发探测、接探测完成 | `Mg`，`4d 67` | commanddefine.h:81/97；commandsender.cpp:173；commandreceiver.cpp:162 | `MG`中的大写G在commanddefine.h:107是关机 |
| 发扫码、接扫码完成 | `Mh`，`4d 68` | commanddefine.h:99；commandsender.cpp:265；commandreceiver.cpp:168 | 扫码逐孔更新是单字节顶层A，非Mh完成 |
| 接液位数据 | `N[...]`，`4e 5b` | commanddefine.h:175；commandreceiver.cpp:85；frmMain:402→slotPCLevelUpdate→levelUpdate | `MN`为动作类M加PCServosReagent=N（:134），含义是试剂扫码器移动，绝非液位数据 |
| 接扫描逐孔数据 | `A[...]`，`41 5b` | commanddefine.h:179；commandreceiver.cpp:88；frmMain:403→slotPCScanUpdate→scanUpdate | `MA`中的二级A在:124是样本针二通阀 |

以上是封包中的业务负载字节，不含帧头、长度、校验及帧尾。不能把注释、日志文本或常量英文名当作线上字节。

## 复核：LinuxNew体积字段仍不代替原高度换算

真正可达调用链：`CommandReceiver::slotCommandReceiver`顶层N → 原主窗口`frmmain.cpp:402` → `frmReagentMaterial::slotPCLevelUpdate`（:418） → `wgtReagentPanel::levelUpdate`（:2095）。`levelUpdate_Old`及`updateHoleVolInfo`不是这条信号当前的直接接收处理。

- 试剂报告前8项为孔位名、试剂名、区域、针、sendIndex、瓶型、瓶底参数及角度；LinuxNew在`wgtreagentpanel.cpp:2188`随后读取`ulDect`、`ulHigh`两项，旧协议用最后一项高度。样本报告由:2141的Sample分支另外处理，不应套试剂字段。
- `ulDect`在这条现行实现中只进入日志（:2212），实际量在:2237始终用`getLevelUL(holeName,needle,area,bottle,maxVol,int(height*100))`重新换算。高度单位为毫米，传给容量函数前乘100；不能直接将LinuxNew自带体积作为原版最终显示量。
- 高度非零、JSON换算量为0时，:2239–2246继续按原瓶底直径/角度几何式回退。不要擅自认为任何JSON错误都应使用体积字段，或把新分支当旧规则。
- 落库:2279写`formula_level=高度`、:2280写`amount_uL=换算量`；测试中保留数据库`used_ul`，非测试分支:2335令`used_ul=amount_uL`。控件显示还会限到0与maxVol。`updateHoleBySetting`随后刷新；这些步骤需要一起对照，不能只更新label。
- `manage_reagentCarlibration.cpp:686`的`getLevelUL`在Windows分支:733每次调用`loadCalibration_Json`。后者:595–597明确：机型严格等于`SN450`用`ReagentAmount_450.json`，其他机型（含SN500）用`ReagentAmount_600.json`。面板构造:583及探测结束:2360另用contains(450)选择450/600，相关差异原样记录，不凭机型名称改成不存在的500文件。
- 当前Linux`original_capacity_calibration.cpp:44–47`只允许450及450 - TJ两文件；生产源码中没有调用者，已有独立容量读取能力不等于SN500液位流程已经接好。未补600资料和原分支前，不能将SN500强制读450、改名成450或引用另一板/另一机型标定值。当前只核源码，没有核任何SN500实际容量文件或现场精度。

## 复核：清盘控件与数据库字段有区别

实际链：`frmReagentMaterial::on_btn_clearPanel_clicked`（:1500）确认 → `wgtReagentPanel::clearPanel`（:3207）先保存beforeCalibration副本，再逐孔`clearHoleSet`与`updateHoleInfo`，最后页面发`UC`。

- `customwidgets/cusreagenthole.cpp:560–575`的`clearHoleSet`清空名称/批号/瓶型/日期，设置状态r_none，容量/优先级/阈值清0；**没有改控件的useVolumn和formula_level**。
- `wgtreagentpanel.cpp:3015–3047`的`updateHoleInfo`把未改的`getUsedVol()`写入`used_ul`。因此旧余量没有被强制写0，不能复用现有`OriginalReagentEditor::clearHole:307`中used_ul=0的行为。
- **但“清盘在数据库也保留formula_level”不成立。**`updateHoleInfo`没有往hash放formula_level；`reagent_setting_table.cpp:290`用`data["formula_level"]`取到空字符串，:296调用Update。`TableDal::create_update_bind_sql:160–181`更新所有非id字段，`bind_data:70`确实绑定这个空字符串。因此本条链成功落库时，数据库formula_level被写为空，控件内旧formula_level仍保留；不能只看clearHoleSet就推断数据库也保留。
- 当前Linux`clearHole`反过来没有修改formula_level、但将used_ul清0，这两项均与原清盘的完整落库链不同。清盘应单独实现并核对：used_ul沿控件当前值，formula_level数据库为空，控件内旧高度值直到后续刷新如何变化另验。不要通过调用现有单孔clearHole冒充原盘面清空。
- 本轮为只读源码结论；原Windows程序实际运行和RK数据变化均未执行，原表写入失败、控件尚未从数据库刷新及主控UC后的Ur也须分开记录。
