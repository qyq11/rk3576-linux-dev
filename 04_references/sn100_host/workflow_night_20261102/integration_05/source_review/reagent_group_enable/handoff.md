# 试剂批组启用：本地开关候选交接

候选补丁为`reagent_group_enable.patch`，SHA-256：`210dd7deca9c75c5020df485d6fd68e60dd592fdd3317a0afedf769febb8ce85`。这是本地页面、事务保存和缓存通知切片；没有应用到正式源码，不是设备同步或完整批组功能验收。

详细原版对照见同目录`contract.md`。候选保留原批组对话框和复选框位置、文字；不改`.ui`、颜色、布局、main或构建清单，不重做当前已有ISI和正常测定值编辑，不碰容量、优先使用和项目复制候选。

## 已实现的操作

选项目后，复选框显示原`project_base`的`is_related`，点击即保存字符串0/1；保存期间不能关闭对话框或并发编辑。开关不等待下方“保存”，下方“取消”也不撤销已提交开关。存在未保存批组草稿时，要求先保存或取消草稿再点击开关。

启用条件按原函数：原`SELECT * FROM project_results`返回的列表排除最后一行，剩余`is_used`求和须大于1；组表须存在该项目一行，其组名不是Default或PTFbg。这里没有把None额外排除。原查询没有ORDER BY，候选沿用相同查询形状，不使用迁移项目结果模型的result_id排序；跨SQLite版本的未指定顺序仍须用获准原资料在RK核对。

Default/PTFbg的行隐藏按原分支恢复：PT关闭时可见Default，启用时隐藏；非PT隐藏Default；PTFbg始终隐藏。没有自动造组、删组或改组，不修改ISI、校准点和曲线。

写入沿用既有项目锁→试剂锁、全部已读资料指纹和`BEGIN IMMEDIATE`；核对根、稳定编号、项目名、实际文件名、旧指纹、旧值，再只更新现有`is_related`一行。关联资料提交前再核版本。未知、缺失或重复开关拒绝，不补建字段；错误不作为保存成功。保留既有登录、权限、检测态守卫。原函数没有这些运行保护，本片没有删除现保护去追求放行。

原失败启用会尝试写0，且原`updateProjectBase`没有把实际SQL失败传给返回值。本片有意保留最后已提交值并显示实际失败，避免错误改写和假成功。

原还把开关连接到`toggled`，刷新项目时的`setCheckState`也可能触发写库、失败回0或Up。本候选只接受用户点击，刷新阻断信号；已有1而条件不满足的资料不在读页面时自动改0。此处是明确的保护差异。

## 提交、重读和后续同步

`ReagentStore::originalProjectGroupEnableCommitted(ReagentProject)`只在实际COMMIT后发出；保留原根、编号和文件名，清空指纹，开关值是本次已提交值。此时仅证明本地提交。

页面保留这份待重读身份。重读失败时，复选框半选、禁用，提示“数据库已保存，但重新读取失败”，不把旧显示当成0；取消不清除已提交事实。后续成功重读后，页面从新项目快照按原根、编号和文件名定位，再发`ReagentsPage::originalProjectGroupEnabledSaved(ReagentProject)`，其中指纹和开关是实际新快照值。如果期间外部程序又改了文件，该通知描述实际重读版本，不能当作设备已经收到本次点击的证明。

唯一shell接线在`catalog_shell.cpp`：收到后只在项目页ready、非busy、无草稿时调用现有`reload`。现有项目快照信号随后更新试剂、样本和校准消费者；项目页忙或有草稿时保留草稿，不自动弹窗，不强行刷新。通知携带的身份和新指纹可供下一片统一同步消费者使用。

原新协议本应发`Up`（`0x55 0x70`），其中包含`is_related`；旧协议只本地保存。本片按明确范围不发送`Up`或其他仪器命令。当前整体仍缺“保存版本→匹配Up→重新就绪”的统一消费链，同连接的同步启动标记不会因本次保存重置。`OriginalDeviceSync::canStart`的全项目文件指纹守卫保持原样；这可能让本地更改后的任务继续被拒绝，不能删检查放行。本片没有调用`invalidateDeviceTaskContext`，没有清除单孔结果未知等既有事实。

## 修改范围和恢复

8个候选文件：`reagent_types.h`、`original_reagent_editor.cpp/.h`、`reagent_store.h`、`original_reagent_store.cpp`、`reagents_page.cpp/.h`，以及`shell/catalog_shell.cpp`。正式文件没有写入。完整底稿和候选逐字节SHA在`candidate_manifest.json`。

事务未提交时，失败回滚。已经提交后，需要恢复业务状态时重新点击原开关值并重读；这不能代替设备重新同步。本片只更新项目文件里的现有一项，不备份整机、不改无关库。没有产品运行或真实资料写入。

## 验证范围

- Windows普通`git apply --check`退出0；正式8个底稿SHA与候选清单相等。补丁应用到本目录私有底稿后，8/8文件与编译候选逐字节相等，没有应用正式源码。
- ARM64对象：原资料编辑器、原Store、页面、catalog接线、既有`reagent_store.cpp`支持副本、正式main支持对象，以及Store/Page两个moc，共8个。日志为`compile_objects_revised_arm64.log`。正式main沿用已有Qq、QC、LIS等接线，SHA为`52335c8d9f5048c91a48db71d63c634698114446fed82a6f93f3722189ba759f`。
- 私有应用检查发现catalog接线文件LF/CRLF与Git实际输出不同；只调整候选换行格式，并仅重编catalog对象，日志为`compile_catalog_byte_alignment_arm64.log`。其余7对象依赖的源码与头文件未变，交接核对会逐项验证。早期`compile_objects_arm64.log`是uic路径写错的失败记录，不作为通过依据；中间`compile_objects_final_arm64.log`是两阶段通知补充前的对象记录，也不作为最终候选依据。
- 编译前后全部正式源码、候选和编译脚本清单逐字节相等，数量和SHA见最终清单；两个moc和各对象`.d`均确认实际使用候选头文件。新增元对象槽已编译。结构体追加字段需要后续整合完整重编，不能把04旧对象与新头混用。
- 只编译对象，未链接、未运行产品、未测试数据库、未联网、未连接RK或仪器。原屏幕外观、点击、取消、保存重开、权限变化、运行中阻止、事务失败、外部改库冲突、提交后重读失败恢复、跨页面缓存更新和实际设备同步均未实测。

获准后验收应覆盖：PT/非PT的Default/PTFbg显示；满足/不满足两个启用条件；关闭不需要校准条件；批组草稿取消不改开关，开关已提交后取消不撤销；写失败保持原值；提交后重读失败显示未知，重读后恢复；项目页有草稿不覆盖；旧任务和旧设备参数仍被文件版本守卫拒绝。设备同步链未补齐前，不能把这片称为已可联机完整使用。

固定核查结果：完整8对象一轮的693输入前后相等，清单SHA为`45630dd0d2e11f6d97a0e732c647442ae29a41fa416dc426470071cb6214e0b7`；catalog换行对齐补编一轮的694输入前后相等，SHA为`7bb76627a6b11f21b2e5d0669b9f25ded2bea4dc3d2a00b1fa3f655eb48449e7`。其中680项正式输入在编译后仍逐字节相等。两轮之间唯一源文件变化是catalog的LF/CRLF，已验证其余7对象所有源码和头未变；catalog新对象SHA仍为`38161e5266d2f0e56ffeca9bcefbf6c5aaa0bf87ceec94fddebda15378688f7c`。Windows普通应用检查退出0，私有实际应用结果8/8字节一致；WSL因正式文件CRLF使用`git apply --check --ignore-space-change`，同时以正式底稿SHA保障输入没有混用。

最终`candidate_manifest.json` SHA-256：`ef21e33c48b608181478de42f9fae2106b25ee94f945380ac792e2cdd793e0ca`。候选源码和补丁已固定，不再继续编译或改动。
