# 试剂批组开关v2交接

v2修正独立审查确认的3项问题，仍只保存本地开关和刷新缓存，不发设备命令、不改算法。正式4e源码未动，旧`reagent_group_enable`目录、补丁`210dd7de…`和清单`ef21e33c…`全部保留。原行为合同、已声明的clicked保护差异与设备同步未接范围继续适用。

固定交付：

- `reagent_group_enable_v2.patch`，完整8文件补丁，SHA-256 `775d4cf0bdd679af087ab40210852d3a5971b26bbe7c9dfa668877a9e0d5569a`。用于当前正式04底稿，替代尚未应用的v1完整补丁。
- `v1_to_v2.patch`，只改2个cpp的修正，SHA-256 `8c08e93d4d4c1545f7af79dc436af86a2b771e114613e7162b4c191472e82e32`。只用于已经应用v1的副本；不能与完整v2补丁重复应用。
- `candidate_manifest.json` SHA-256 `1709f240ef3efc1dee7b8bef4342a5ccca94766e700d524c11baa3ab1c361946`。

## 三项修正

1. `reagents/original_reagent_editor.cpp::saveBatchGroupEnabled`在`BEGIN IMMEDIATE`已经取得SQLite写锁后、任何本次更新前，重新比较目标项目完整指纹与请求版本，关闭此前文件指纹到写锁之间的间隙。其余双锁、全部资料旧版本核对、旧开关值、行数及关联文件提交前核对保持；只修改这一新入口，不泛改其他`mutate`调用。若SQLite打开/恢复导致版本变化，同样保守拒绝并要求重读。
2. `reagents/reagents_page.cpp::openBatchEditor`的失败回调只响应“保存试剂组开关”及其待重读状态下的“读取原试剂”失败，并遵守`!dirty_`。整个refresh也先检查草稿；旧开关pending不能让后来的批组、ISI或正常值保存失败重填旧快照，未保存内容保留。
3. 提交后pending按原工作根和实际文件名定位。`xuhao`是该版本的序号，UP允许改写，不能当跨回包永恒身份。成功重读采用该文件实际的新序号、值和指纹，再发已有本地已读回通知。批组对话框项目行同时保存根/文件名，重读时更新当前序号和名称，避免pending已解除而表格仍困在旧序号。若同根/同文件不存在，显示未知并拒绝保存，不转而使用碰巧同序号的另一项目。有草稿时不重填。

独立审查曾提示“末行NULL被新回调阻止”，完整前置链已证实：现有`OriginalProjectRepository::readProjectData`载入时早已对全部结果`is_used`严格0/1。该正常反例不可达，不作为新P2，也不在v2放宽既有loader。原末行排除求和及现有读取收窄分别保留。

## 验证

只重编上述Editor/Page两个实际变动cpp。两个对象均为AArch64：

- `original_reagent_editor.o`：`7d820c95a2bff936f72ba7dce9539376445bd4f9805ce251994f8cf3a20fd58c`。
- `reagents_page.o`：`dec5f07d1e5bc3111fe457b4ee50b16938fb0ab6096dafd8fdeac7b5e88fcded`。

日志`compile_delta_arm64.log`退出0；693个输入前后逐字节相等，清单SHA为`292ec1868ef690e677145f488a241a312ee870ce3e43762c090ca54d44a0d89f`，其中680项正式源码仍相等。`.d`确认两对象使用v2候选头。全部头文件及v1其余6对象对应源未变，6对象实际SHA与旧固定清单相等；没有重复编译它们。没有改变moc接口或main已有Qq/QC/LIS接线，后续整合仍须按正式流程完整构建。

完整v2的Windows普通`git apply --check --whitespace=error`退出0。完整补丁和v1增量分别实际应用到temp私有副本，两份应用结果各8/8文件与编译候选逐字节相等，没有应用到正式文件。所有底稿、v1、v2及继承对象指纹见清单。

仅源码和对象编译，没有链接、运行、测试库、网络、RK或仪器操作。待获准验证：外部程序在旧指纹到BEGIN之间修改目标但不改开关时拒绝；COMMIT后重读失败，再编辑批组并保存失败时保留草稿；同文件UP改xuhao后成功重读可解除pending并更新项目行；根/文件不匹配继续未知；取消和权限/检测态保护保持。不得把对象通过称开关、同步或真实仪器功能完成。
