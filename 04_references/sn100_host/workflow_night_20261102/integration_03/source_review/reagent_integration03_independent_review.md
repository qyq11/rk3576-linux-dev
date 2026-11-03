# 试剂首批与统一程序接线独立复核（2026-11-02）

本次只读复核首批试剂候选，并只在`temp`生成构建接线候选；没有修改正式源码、首批候选或优先使用操作候选，没有运行产品、访问网络、连接板卡或打开数据库。

## 适用范围和结论

首批固定补丁为`reagent_candidate.patch`，SHA-256为`da7acc5946a71e0046df0c59d3c3f986fb3f110248aa88c028830b332902c589`。独立核对时，清单中13个候选文件的字节哈希全部一致；10个已有正式文件仍与保存的底稿字节一致，3个新文件当时尚未存在。普通`git apply --check`及忽略空白的同一检查均退出0。总协调随后已合入该首批；这不改变这里记录的合入前核对范围。

建议把首批与本报告的窄接线补丁一起纳入`integration03`的统一构建。这里的建议只针对源码和构建接线，不表示设备动作、界面、保存重开或仪器业务已经验收。最终对象编译结果单列在下方；统一程序完整链接、RK实屏、真实仪器和资源结果由总协调另行登记。

## 默认设备动作是否会执行

- `reagents_page.h`中`deviceConnection_`初始为0，`deviceOperations_`初始为空；`reagent_store.h`中的操作许可初始为空。首批没有为主程序添加任何开放设备操作的调用。
- `ReagentsPage::requestOriginalDeviceOperation`先检查原资料已就绪、账户及仪器状态，再检查连接号、能力清单和实际信号接收者；缺少任何一项都在确认框、资料改写及发命令前返回。确认后再次检查条件，工作线程另需领取当前单次许可并在事务提交前复核。
- 单孔/整盘探测、充满、优先使用、清盘均只接到上述受保护的请求入口。恢复和容量标定只说明当前不能执行；扫码保持禁用，转至出口保持隐藏。主程序本次不连接`deviceOperationRequested`，不调用`prepareOriginalDeviceOperation`。
- 用量窗口打开时内部发出`QH`请求意图，申请码和添加码分别产生`QI`及`J[...]`意图；页面转发仍需对应能力清单、当前连接和真实接收者。当前均未开放，因此不会进入网络。未知次数显示问号，不能把它当成设备返回0次。

这里的“禁用”指动作执行被阻止；部分按钮仍可点击以显示原因，并非所有按钮均变灰。账户身份变化、失去可操作状态、检测占用、连接代次变化及等待超时会撤销许可。本次没有增加任何设备回包、执行成功或添加成功的伪造通知。

## 现有主程序可以直接承接只读界面

`shell/main.cpp`已经建立`ReagentsPage`并调用`attachCatalogPages`。`shell/catalog_shell.cpp`第58至73行已经把项目快照变化转给`ReagentsPage::setProjects`。候选原库分支只使次数估算失效并请求重读，不使用早期本地项目编号覆盖原项目。忙或有未保存草稿时，`reloadOriginal`延后；草稿关闭、保存后解除忙状态和离页等入口会恢复延后读取。

因此本次无须修改`main.cpp`或`catalog_shell.cpp`，也不应从旧候选覆盖它们。现有`Qq`检测通道接线、质控/校准接线和新LIS接线均保持正式原文。优先使用设备全链由另一个工作聊天继续处理，本补丁没有接触其协议或完成判断。

## 构建依赖补齐与去重

独立试剂入口与统一程序都将引用新增`OriginalReagentUsageDialog`，必须把源码、含`Q_OBJECT`的头文件和UI列入构建。该窗口还使用`samples/material/qtmaterialscrollbar.cpp`及`qtmaterialscrollbar_internal.cpp`，后者及前者各有需要moc处理的头文件。

统一`shell/build.sh`原本已列入这两个滚动条实现，所以只增加新用量源码、头文件和UI。独立`reagents/build.sh`补齐用量源码、两个滚动条实现与两个moc头；其现有试剂头文件通配符和UI通配符已覆盖新增用量头及UI，无需再重复添加。

`reagents.pri`用`*=`列入用量文件和滚动条依赖，覆盖独立试剂及现有引用它的工程。`samples_core.pri`仅把原有相同两个源码和三个头文件从`+=`分离成`*=`，保证统一程序仍各有一项；没有改样本、质控或校准业务。

窄接线补丁在`reagent_integration_wiring/wiring.patch`，SHA-256为`da023a80c2df061ded1d82aa121dafaa8a972cacecd630d7141c7f55c2a1c3c6`。共4个文件：

- `06_programs/sn100_host/shell/build.sh`
- `06_programs/sn100_host/reagents/reagents.pri`
- `06_programs/sn100_host/reagents/build.sh`
- `06_programs/sn100_host/samples/samples_core.pri`

对应底稿及候选哈希见`reagent_integration_wiring/wiring_manifest.json`。固定后普通`git apply --check`退出0。未更改参考工程、数据库、配置、正式主程序、首批试剂补丁或优先使用设备协议候选。

## 验证和仍待验范围

只用qmake解析构建清单的两项检查均退出0：独立试剂清单与统一引用清单均输出`USAGE_AND_SCROLLBAR_INPUTS_UNIQUE=1`，实际确认用量源码、QObject头和UI，以及滚动条两源码和QObject头各有一项。输入在`reagent_integration_wiring/qmake_inventory/`，没有调用生成Makefile中的编译或运行目标。

独立脚本原有`"$module_dir/"*.ui`和`"$module_dir/"*.h`分别覆盖新`maskdlgreagentusage.ui`与`original_reagent_usage.h`；其后uic循环生成界面头，moc循环对含`Q_OBJECT`的头生成并追加cpp。最终对象轮会输出这两个实际展开命中项。这里没有重复显式添加同一头或UI，避免同一moc被追加两次。

滚动条源码的`"qtmaterialscrollbar_p.h"`、`"qtmaterialscrollbar_internal.h"`和`"lib/qtmaterialstyle.h"`按本源码所在目录解析；用量源码的`"samples/material/qtmaterialscrollbar.h"`按原有`-I program_dir`解析。没有新增全局包含路径，也没有更换已有Material公共实现。

最终独立对象脚本为`reagent_integration_wiring/compile_objects_final_arm64.sh`，日志为同名`.log`。脚本只生成ARM64对象，不链接、运行或上传。生成补丁期间的旧轮15个对象都已编成ARM64，但整份输入快照因正式首批合入和temp接线清单变化而不一致，整体退出1；旧日志保留在`compile_objects_arm64.log`，不当作固定输入通过。

最终固定轮已完成并停止重编：脚本退出0，15个对象均为AArch64可重定位对象，包含首批6个cpp和3个moc、当前正式`main.cpp`与`catalog_shell.cpp`、滚动条2个cpp和2个moc。697项输入的`inputs_before.json`与`inputs_after.json`逐字节一致，清单SHA-256为`31e13e0dcd16bebe4feb2381c0ff86fecca8158ec54dd48279f7afd405d9d035`。两个构建脚本语法检查通过；日志实际记录了新用量UI与QObject头在独立入口通配符中的命中。各对象哈希及正式首批13文件和固定候选的换行归一后相等结果见`reagent_integration_wiring/review_result.json`。

最后重新核对`wiring.patch`仍为上述`da023a80...`，普通`git apply --check`仍退出0。未发现阻止本轮四文件接线进入统一构建的问题；后续若总协调更改源码，完整构建仍须以其新的冻结输入为准。

尚未验证：统一程序完整链接、RK实屏上的用量弹窗与滚动条显示、实际资料中的测试次数与原机逐项一致、原资料保存重开、主控耗材收发及条码写入、任何机械/液路动作、响应/资源和同板控制影响。首次联机前仍需按总协调获准范围核实际INI、机型及孔位数，不能因本次代码可编译而开放设备能力清单。
