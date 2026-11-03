# 校准审核后应用第二版：修点独立复核

本轮只读复核目标批组绑定前态修订，不修改候选或正式源码，不应用补丁，不新增编译、运行、数据库、网络或RK动作。旧`c76a`候选及`calibration_permissions_independent_review.md`审查原件保留。

**结论：先前目标G由B并发改绑C后被续接覆盖的确定缺口，已在本版源码中关闭。本次接口和修点范围内未发现新的确定阻断。**这只是源码结论，不是并发运行或RK功能通过。

## 固定版本与补丁检查

- 相对正式的六文件累计补丁：`294c0861db8aa452ab76cccc749ba95ac2e4c46745da4d2c6a31b442876a6b4b`。
- `source_manifest.json`：`256e88418d602b5cf7ec18b95c3d87ac7cb64e3b53ae963178d8e038a79d8752`。
- 相对`c76a`的最小差异：`7147feac12dfdc5c83c9e2182828d14ae87bb751570b3f9f96e0cf9531afa2b6`。

三份指纹均与总协调指定一致。六份候选、六份`base_current`副本及六份正式文件均与manifest各自指纹匹配；旧`c76a`五候选源、旧补丁及旧清单仍未变。

在仓库根对累计补丁执行`git apply --check`退出0；对最小差异显式使用`--directory=temp/sn100_host/workflow_night_20261102/calibration_application_permissions_v2/base_c76a`检查，也退出0。未实际应用。04应选择本版累计补丁替代旧累计补丁，不重复叠加；新增记录/页面布局仍要求完整重编依赖对象。

## G由B改绑C的路径怎样被拒绝

以下行号对应本版`candidate/06_programs/sn100_host/calibration/`。

1. `calibration_original_store.cpp::readOriginal()`约149至163行按项目读取绑定表的完整`SELECT *`原行，包括其他曲线编号及未知列。读取结果按项目缓存、按值共享；`originalBindingsLoaded=true`与空列表分开，失败或超4096行整次返回错误，不把部分资料当作完整前态。
2. `calibration_page.cpp::targetBindingBefore()`42至57行只从完整原记录中摘出目标G的原行；封套固定原根、项目、批组和完整行列表。`applyOriginalCurve()`683至697行在显示审核确认前取得该封套，待续接成员只保存此前态，不用审核后的新记录替换。
3. 审核后`continueOriginalApply()`717至727行先从新记录取得G的当前行并与保存前态比较。G从B变C会使完整行中的`calibration_id`不同，立即停止续绑，提示审核已保存。无行变有行、原行被删、目标重复或原行其他字段变化也不通过。
4. 即使变更发生在页面重读之后，五参数后台入口仍先取得原写锁和`BEGIN IMMEDIATE`，再在同一事务内`SELECT *`重读目标G（`calibration_original_store.cpp`约319至344行）。实际行与确认前原行不相同便回滚本次绑定事务；不会凭刚取得的新revision接受G的新前态。
5. 已审核或`auto_apply`分支直接应用也传确认前封套；`Default`使用相同比较，不绕过。第二步拒绝、回滚或读取失败不撤销第一步已提交的审核。

此前的问题来自只比较A自身的已应用组名；本版补了“目标G原来绑定谁”的独立前态比较，旧问题不再仅靠A的组名列表或新revision判断。

## 新旧接口和排队类型

新写槽为`applyOriginalCurve(qint64,qint64,qint64,QString,QVariantMap)`。前态采用Qt内建`QVariantMap`，没有引入需另行注册的新自定义类型。当前Qt5的`qmetatype.h`已将它定义为内建类型8；嵌套的`QVariantList`同样内建。

Page两处排队调用（703、731行）均明确使用`Q_ARG(QVariantMap,bindingBefore)`，头文件声明和实现第五参数完全一致。已生成的`arm64_compile/moc_calibration_store.cpp`172行以`QMetaType::QVariantMap`记录第五参数，206行的分派实际调用五参数重载。该检查针对Qt运行期排队识别所需的名称和元类型，不以C++对象编译代替它。

四参数旧重载在moc中另有分派，但实现只发缺少前态的明确错误，不重新查询或写入，不会从新版本补造前态。候选Page已没有四参数写入调用。全工程搜索到的历史四参数调用位于`quality_control/tests/original_qc_calibration_rk_driver.cpp:119`；本轮未改或执行该旧驱动，其旧结果不能当作本版绑定已验证。

`CalibrationRecord`仍通过既有`CalibrationRecords`值对象注册和跨线程信号传递，新增成员是Qt容器的按值快照；无需为内部前态再注册一种自定义名字。后续完整构建必须使用本版类型头，不能混用旧结构布局对象。

## 空前态、重复和编号0

- 封套须恰好四项，原根/项目/组名的类型和值均核对，`rows`须为列表；缺字段、额外字段或类型不符被拒绝。原无绑定明确为`rows=[]`，与“未完整加载”分开。
- 确认前或事务内出现多行目标绑定均拒绝，不任选一条。单行通过完整前态比较后只更新`calibration_id`，未知列保留；确认前及事务内都无行才插入。
- 是否已有行使用`!actualRows.isEmpty()`，不再拿编号真假判断。合法`id=0`仍走`updateFields(...,0,...)`，不会误插新行；编号转换无效则拒绝。该判断也没有擅自新增原编号必须大于0的业务规则。

## 其余已过分支的未漂移核对

本版`calibration_store.cpp`与旧`c76a`候选逐字节相同，因此Engineer人工应用独立门槛、checked/autoApply不附加power1、原资料canEdit保留认证/power1而只去除actionReady的判断未改变。旧独立分支及设备/任务保护不在本次修改中。

最小差异仍保留旧确认、取消、操作者变化清续接、曲线原字段及逐点map比较、新revision、审核与绑定分开提交和提交后失败诚实提示；只增加目标前态的保存、清理和双层比较。未重复扩大保存、复制、隐藏删除的既有power1差异，也未改算法或后台自动应用。

实际数据库锁、审核后并发改绑、元对象排队的实际运行、失败恢复与RK页面仍待统一验证。已有3个C++对象、2个moc以及零编号补丁后的单对象编译不代替这些结果；本轮没有重新编译或执行。
