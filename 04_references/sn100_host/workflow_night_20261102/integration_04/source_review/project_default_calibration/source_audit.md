# 项目默认校准参数：原调用链和本片边界

这些设置决定以后新建校准任务采用哪种稀释方式、重复几次、取几个点，以及为新曲线记录哪种方法和坐标变换。已有某条曲线的方法、审核和应用由校准页面维护，不能用它代替项目默认值。本片只补编辑和保存，不计算曲线。

以下原版路径以`06_programs/sn100_host_reference/Seerain450_PC/`为根；参考工程只读，未启动产品、读取运行数据库或连接端点。

## 原入口与六个实际键

`frmsystem/frmsystemmaterial.cpp::on_btnCalibrate_clicked()`（2719行）切换`stkPanel`到第3页。原`.ui`的“校准参数”按钮、该页六个下拉框仍完整存在于当前工程，两个`.ui`的SHA-256同为`bc08c2e42ef9893b196c6d13d0777bdb58dbb056af1515e998f2f82cef04eaaa`。候选复用这些控件，不移动、改名或改配色布局。

| 原`project_base.name` | 页面内容 | 可选原串 |
| --- | --- | --- |
| `calculate_select` | 计算方法 | 直线法、一元三次曲线法、插值曲线法、折线法 |
| `x_change` | 浓度变换 | `x`、`log(x)`、`1/x`、`ln(x)`、`log(log(x))`、`ln(ln(x))` |
| `y_change` | 结果变换 | 对应上述六种`y`形式 |
| `repeat_number` | 重复次数 | `1`至`4` |
| `calibration_mode` | 稀释方式 | 手动稀释、自动稀释 |
| `point_number` | 手动稀释点数 | `1`至`8` |

来源是原`.ui`的2286至2660行和原保存函数的2501至2506行。`calibrateMethod`不是这里的数据库键；`repeat_number_max`也是另一个原参数，不能代替本表重复次数。“双线回归法”出现在单条曲线的方法设置中，不在本项目页的四项列表里。

原`slotCalModeChanged()`（2460行）只在“自动稀释”时隐藏点数及其标签，未清点数，保存仍写该值。原点击浓度启用框时，`slotResultUsedChanged()`（2404行）联动`wgtCalibration`使能；原加载时不统一用浓度勾选重新设定这个区域，保存也没有据此拒绝六字段。本候选恢复该点击联动，不添加方法/点数组合判断或新的全页保存条件。

## 加载、初值、保存和失败

- `loadProject()`（约2092行）从原项目DB读取`project_base`的`name/value`。没有给所有项目补统一业务默认值。`.ui`初始首项依次为直线法、`x`、`y`、重复1、手动稀释、点1，它们只表示控件初值。
- `ProjectSelectChanged()`（2190行）重新读取项目，再交`refreshProjectBase()`（867行）填控件。后者只处理原表已有键；缺键可能残留上一个项目显示。遇到完全无法匹配的旧串会选首项，原保存可能将它覆盖。
- `on_btnCancel_clicked()`（2466行）重新读取当前项目。`on_btnSave_clicked()`（2471行）先调用旧版本备份，再把本表六个`currentText()`原串交`updateProjectBase()`。原基本参数写入与后续表写入并非完整原子事务；`updateProjectBase()`（485行）还未正确用返回值反馈所有SQL失败。
- `customtools/sqlbase.cpp::updateDataAuto()`的列表重载（528行）实际是`UPDATE ... WHERE name=...`，不新增缺键，也不核受影响行数。因此候选缺键禁用该项并不补造键，避免将残留显示值误认为已保存。
- 对未知旧值，候选在下拉中显示原文；不编辑就原样保留，改动后仅接受上表原选项。这是对原“可能显示首项并覆盖旧值”的明确保护差异，不声称原版已经这样处理。

## 默认值怎样进入以后任务

原`frmsample/frmsamplesettingmaterial.cpp`约989行读取项目默认稀释方式、重复数和点数。原`frmsample/frmsamplematerial.cpp`约3263及3638行把项目`calculate_select/x_change/y_change`复制为新曲线的`calculate_id/x_change/y_change`。改变项目默认值不会回写已有曲线。

当前工程对应消费者是`samples/original_calibration_preset.cpp`（47至70行）和`original_control_tasks.cpp`（111至116行）。它们读取重新加载后的`ProjectRecord.originalBaseValues`。候选仍在保存成功后走既有重新读取和`snapshotChanged`链，没有让尚未提交的草稿提前影响任务。

当前`calibration/calibration_original_store.cpp::setOriginalMethod()`只修改`result/calibration.db`中选定曲线的`calibration_base.calculate_id/x_change/y_change`，该方法表有第五项“双线回归法”。它不修改项目DB的本表六字段；本片不改该模块、算法、审核或应用条件。

## 权限证据范围

原项目页`slotUserChange()`（809至827行）按`permission >= Engineer`设置保存、取消等按钮；本文件未使用`actionReady`。外层另核了原`frmmain/frmmain.cpp`：约76至81行创建分析项目按钮，202及213行绑定页面5；构造接线约454行把`menuButtonGroup::buttonClicked(int)`直接接到页面切换，约460行重新加载项目。开始/暂停按钮的`setBtnDisabled()`约563至585行及Qj状态处理约768至889行没有使项目按钮、页或保存按钮禁用。全局`CustomEventFilter`约48至82行只处理触控键盘。在全部项目对象/按钮引用及父控件使能链的源码检索范围内，未找到检测态另挡项目保存的链。原实际发行运行未验证；该局部/外层源码证据交总协调核权限差异，本片不放宽权限。

候选继续使用现有`ProjectsStore::canEdit()`的已认证、运行态允许编辑、工程师或管理员三个条件，页面与后台不另开通道。失去权限/运行态条件后，六下拉按既有守卫立即禁用；后台正式执行保存时再次检查会话。本片只补默认值字段，不修改这条既有规则。

原点击浓度启用框后对整个校准容器的使能会残留：关闭浓度后再取消，或切到别的项目，原加载未重设这个父容器，可能继续灰显。本候选仅恢复该原点击链，也保留这个源码行为；不把它说成新的持久化错误或擅自在本片修原规则。后续RK核操作时应单列，必要修正另由总协调确认范围。

## 本片存储范围

候选新增`ProjectRecord.defaultCalibrationValues`作为六项可编辑草稿，保留只读的完整`originalBaseValues`。仓库加载时只摘出实际存在的六个键；保存时要求键集合不变、只校验有变化的值，只更新变动项。原目录、文件身份、旧版本指纹、写锁、事务及保存前自动备份全部保留。缺键、重复键、外部修改、失效目录或SQL失败沿原仓库当前错误链返回，页面保留草稿供核对或重读。

不修改项目新增、带关联复制、原报文字段、算法或曲线表。原新协议的命令发送只使用原发送表：`repeat_number_max/repeat_number/point_number/x_change/y_change`存在，而`calibration_mode/calculate_select`的发送行是注释；本片没有为它们补造字段。现有全局同步流程不在本片重写。
