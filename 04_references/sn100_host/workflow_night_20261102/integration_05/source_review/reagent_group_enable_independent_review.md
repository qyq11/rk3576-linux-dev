# 试剂组本地开关：独立只读审查

本轮按总窗口分工，只审 `reagent_group_enable` 固定候选的明确新问题和指纹。没有修改候选、正式源码或既有记录，没有编译、链接、运行程序、读取真实数据库、使用网络或操作 Windows/RK。下述场景为源码路径推导，未实际执行。

审查版本：

- `reagent_group_enable.patch`：`210dd7deca9c75c5020df485d6fd68e60dd592fdd3317a0afedf769febb8ce85`。
- `candidate_manifest.json`：`ef21e33c48b608181478de42f9fae2106b25ee94f945380ac792e2cdd793e0ca`。

两者实际 SHA-256 均与分派值相同。8 个候选文件、8 个底稿文件逐项匹配清单；现有 `apply_check_tree` 的 8 文件与候选逐字节相同，当前正式 8 文件与底稿逐字节相同。8 个既有对象文件 SHA-256 也与清单相同。本轮没有重新应用补丁、重编对象或重跑原构建输入守卫；字节核对不代替历史编译过程或运行验收。

候选代码位置以下以 `temp/sn100_host/workflow_night_20261102/reagent_group_enable/candidate/06_programs/sn100_host/` 为根；原版位置以 `06_programs/sn100_host_reference/Seerain450_PC/` 为根。

## 需要修正的三项 P2

### 1. 目标项目在指纹核对到 SQLite 写事务之间仍可换版

候选 `reagents/original_reagent_editor.cpp:383–393` 的公共 `mutate` 先持项目/试剂双锁、核全部文件指纹，之后才打开数据库并 `begin()`。`OriginalBusiness::Database::begin` 在正式 `original_files/original_business_database.cpp:112–114` 确认为 `BEGIN IMMEDIATE`；SQLite 排他只能保护事务开始之后，不能回补此前的间隙。

新 `saveBatchGroupEnabled` 回调752–759行只核 `is_related` 旧字符串；更新后793行附近的提交前版本循环又明确排除了目标项目 `file`。因此，不使用本程序 QLockFile 的外部写者若在旧 hash 核完、BEGIN 成功前提交同项目其他字段，而保持 `is_related` 不变，新入口仍可对与 `requested.originalFingerprint` 不同的目标版本提交。若改的是项目名，开启检查还会按旧 `project->name` 从旧 `state_.batchGroups` 找组，可能用另一个版本的关联放行。

双锁能阻止遵守相同锁的程序写者，但不是 SQLite 外部写者的事务屏障。该缺口来自旧 `mutate` 的间隙，本次新增可写路径依赖它，不能因此把本入口的目标版本保护说成完整。

最窄修正：在**新开关回调起点、已取得 BEGIN IMMEDIATE 后且任何更新前**，再次核目标完整文件指纹与 `requested.originalFingerprint` 相同；不必在本修正中泛改其他 `mutate` 调用。后续关联文件的提交前核对继续保留。

### 2. 待重读提交会使后续其他保存失败覆盖草稿

候选 `reagents/reagents_page.cpp:1157–1162` 新增的 `operationFinished` 回调，只要任意操作失败且 `pendingOriginalGroupCommit_` 非空，就无条件调用整个批组表单的 `refresh()`，没有 `dirty_` 保护。

可达顺序：

1. 开关实际 COMMIT，随后重读失败，待重读身份保留，界面显示未知。
2. 使用者继续编辑另一个批组的批号、ISI 或正常测定值，形成尚应保留的草稿。
3. 该批组保存失败，通用 `error` 发出 `operationFinished(保存批号,false)`。
4. 新回调因旧开关 pending 仍在而调用 `refresh()`；1119行附近可把 `current` 重新赋为旧快照，1135–1138行重填输入框，覆盖刚才应保留的草稿。

现有 `snapshotChanged` 的表单刷新1305–1308行已用 `!dirty_` 保护；新增失败分支绕过了该保护。最窄修正是失败时只刷新开关状态，或至少禁止在 `dirty_` 时整表刷新，不必扩大成新的表单或同步系统。

### 3. 提交后回读确认仍依赖可被 UP 改写的旧序号

候选 `reagents/reagents_page.cpp:258` 以新记录 `project.id == pendingOriginalGroupCommit_.id` 作为结清待重读提交的必要条件，同时才核 root、文件名及非空新指纹。

但当前 `ProjectRecord.id` 来自原 `project_base.xuhao`（`projects/original_project_repository.cpp:263`）；有效 UP 匹配明确跳过 `xuhao`（`samples/original_project_device_repository.cpp:149`），随后允许保存设备返回的新序号。它不是跨 UP 不变的文件身份。

若开关 COMMIT 后首次回读失败，在恢复前同文件的 `xuhao` 被合法 UP 或原程序修改，后续成功重读即使取得同一 root/文件的真实新指纹，仍因旧 ID 不等而无法清 pending、无法发 `originalProjectGroupEnabledSaved`。没有下一次新提交覆盖 pending 或把序号恢复时，普通成功 reload 本身不能完成这次恢复。

最窄修正：提交前的 ID/名称/文件/版本校验照常保留；**提交后**按稳定的原工作根及原文件名定位已重读记录，采用实际新 ID 和新指纹。不能把数值序号永久不变当作恢复前提。若还需要比较原项目名，应独立校验并明确报冲突，不能假称同一文件没有读回。

## 已核且未发现另一确定问题的指定部分

- 原开启门槛使用无 `ORDER BY` 的 `project_results`，排除最后一行后求和大于1；组计数只排除 `Default/PTFbg`，没有额外排除 `None`。没有改成按结果名识别校准或按 `result_id` 排序。
- 曾在局部回调发现它先校验所有行0/1、再排除末行，疑似新增末行门槛；完整追前置后，`OriginalReagentEditor::load:306` 调用的 `OriginalProjectRepository::readProjectData:303–322` 早已对全部行的 `is_used` 严格校验0/1。因此“末行NULL但正常载入后点击”的局部反例在当前正常链不可达，本报告不把它列为独立新问题，也不借此放宽原有读取校验。原 Windows 的末行排除与现有 Linux 资料读取边界仍分别保留事实。
- 写入只更新已有 `project_base` 中的 `is_related` 字符串 `0/1`；核旧值、重复行和受影响行数，不补键、重建表、改 ISI、校准点或其他列。
- 项目锁先于试剂锁，现有全资料指纹检查保留；新增回调也在提交前复核其他关联文件。第1项只针对目标文件取得 SQLite 锁之前的版本间隙，不否定已有双锁和关联核对。
- `ReagentProject` 在 `reagent_types.h:137` 已声明 Qt 元类型，`original_reagent_store.cpp:35` 注册；跨线程槽/信号用值对象，不传 GUI 或 SQL 连接。
- Store 的 COMMIT 通知清空旧指纹，只证明已提交；正常重读后再从新 `projects_` 取得真实指纹发已重读通知，不把旧输入指纹冒充新版本。正常回读失败保留 pending，开关半选且禁用，没有假显为0；第2、3项是这条恢复链新增的特定缺口。
- `catalog_shell.cpp` 在项目页忙或有草稿时不自动重读，避免覆盖项目草稿。不能把“跳过重读保护草稿”当成缓存已经更新。

## 不重复报为新问题的已声明范围

本候选仅本地保存、不发 Up；明确保存后等待匹配 UP 再就绪的整体消费者缺口已在 `CAL_sync_review` 合同和总进度登记，本报告不重复列为新 P1。

候选用用户 `clicked`、刷新时阻断信号，替代原 `toggled` 在显示/切项目时也可能写库、发 Up 的副作用；原失败开启尝试写回0，候选保留最后提交值。两者已有声明，本轮没有把它们重新包装成未声明漏项。

本报告没有实际运行开关、错误恢复、并发、跨线程或设备操作。原8对象编译记录保持原范围；本报告、指纹相等和源码推导均不能代替 Windows、RK、真实主控或资源验证。
