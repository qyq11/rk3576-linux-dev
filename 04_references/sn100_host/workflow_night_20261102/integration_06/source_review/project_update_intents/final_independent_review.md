# 固定单项目同步候选：独立修点复核

**原两项 P2 已修正；本次所查后续窗口仍有两项 P2，暂不能判定所审范围全部通过。**两项均继续阻止开始，没有发现它们会直接放行旧参数；问题在明确未发送事实的结清，以及初始 UP 已提交事实的刷新传播。

本轮按总窗口分工复核最终固定片，不修改正式源码、候选、旧预审报告或实现边界。没有重编、链接、运行、读写真实业务库、使用网络或操作板卡。以下是源码分支复核，不是 Windows/RK/主控运行通过。

## 固定对象及指纹

**2026-11-03 00:13:51 +08:00** 实际核对：

- `project_update_intents.patch`：72352字节，SHA-256 `250b88f58b0e10b365aa3ae4a0c0005eef2e3619676e6fe659b607aa7822963c`。
- `candidate_manifest.json`：SHA-256 `65c32dbcc8e7858d6c44c197ee5b74911d1610d6eeaa8e54f2d24f2f6639a621`。
- 13个实际候选文件的SHA和字节数逐项匹配清单；12个已有文件的 captured base 逐项匹配；另1项为新头文件。
- 9个实际对象文件的SHA和字节数逐项匹配清单，包括7个C++对象及2个Qt元对象。没有用日志中的名字代替读取实际对象。

本轮没有重跑编译输入守卫或补丁应用，也没有把 captured 重连v2底稿当作已对正式05完成rebase的证明。编译及私有适用记录沿候选原范围；指纹相同不代替运行验收。

下列候选路径以本目录 `candidate/` 为根。

## 已关闭的两项预审问题

| 原问题 | 固定版实际修正 | 本轮判断 |
| --- | --- | --- |
| active not-sent 被新保存替换时跨过未核中间版本 | `samples/original_device_sync.cpp:254–258` 补 `old.committedFingerprint == new.beforeFingerprint` 断言；不等则保留新本地提交、转 unverified，不把旧锚当成连续链放行。普通queued分支仍有同一断言 | 原版本链缺口关闭 |
| 缓存A文件预检通过，Repo刷新后实际提交同内容B文件 | `samples/original_sample_store.cpp:82–100` 冻结已预检的 `fileName` 并传Repo；`original_sample_repository.cpp:285–291` 在刷新、复制目标之后核实际文件仍相同，不符在打开写库前拒绝。当前唯一CAL生产调用必传此值 | 原预检身份缺口关闭 |

## 仍需修正：明确未尝试发送的迟到回执被丢为未知

`samples/original_device_sync.cpp:331–334` 的 `projectUpdateSendResult` 先要求阶段仍为 `prepared`，再按单调elapsed超过10秒调用expire，最后才检查 `attempted`。`expireProjectUpdate:356–361` 会把 prepared/waiting 统一设为 unknown。

可达顺序：worker冻结批次并启动单调期限 → GUI事件处理延迟超过10秒 → Root收到批次时，`shell/device_control_shell.cpp:384–430` 的 `expiresAt` 守卫明确拒绝，**实际未尝试发送**，回 `attempted=false` → worker若已由计时器转unknown，会因非prepared直接丢掉这条回执；即使计时器未先派发，也会先按elapsed转unknown，未处理明确false。

旧冻结意图因而没有被记录为确定未发送。后续新保存看到 active unknown，不能使用已修正的 not-sent 取代路径；同连接继续卡在待核。Root还会把该意图的第一份unknown来由保存在 `originalProjectUpdateUnknown`，只改变worker阶段也不足以结清它。

最窄修正方向：在冻结意图中区分“尚无该批入队结论的超时”与“已尝试/可能发送的未知”。同intent、同连接的权威 `attempted=false` 回执可结清前一种事实，即使这条**否定回执**调度晚到；Root对应未知来由也仅按同一证明结清。不得据此允许迟到Up、迟到UP或清除已经可能发送的真实未知；其他Us/优先使用/引用待核来由不能一起清。这里不是要求放宽10秒确认期限，而是不要丢掉已经知道的“没有发送”。

正常期限内的 prepared 新保存及未attempt回执分支已接上：338–343行可释放未发送旧冻结并保留新意图。本项只针对期限/计时器先到的负回执分支，不能把正常分支通过代替这一窗口。

## 仍需修正：初始 UP 的已提交失败没有带回刷新层

`samples/original_project_device_repository.cpp:196–215` 能在数据库commit后、同锁内回读或核实失败，返回 `commitSeen=true`、`verifiedSnapshot=false` 和false。它已经正确保留提交事实。

但是初始同步 `samples/original_device_sync.cpp:125–138` 把 `written` 留在局部，失败仅按需记编号引用待核、invalidate并返回false；`OriginalConfigurationStep` 没有提交事实输出。`samples/original_sample_store.cpp:275–293` 因而只在 `success && !step.ignored` 时重读/发 `originalProjectsChanged`，false分支只撤ready和报错。Root的 `devicePacketProcessed` 失败分支也只报错，不补项目刷新。

因此初始UP虽已实际写入，本地项目/样本缓存仍可能保持提交前编号或参数。单项目更新分支通过 `saved=written.commitSeen` 有独立刷新路径（Store265–270行），初始分支没有。初始同步中当前UP已提交、随后 `nextProject` 检出下一文件冲突而返回false时，也有同样的“提交事实被整体bool隐藏”边界。

最窄修正方向：用窄输出把当前UP的commitSeen传回Store，独立于完整同步成功与否、也不被下一步Step初始化覆盖；只要已提交，就尝试刷新或通知资料变化，失败继续明确显示已提交、缓存未核实。刷新成功不能替代UP确认，不能恢复ready；新编号引用待核及其他恢复事实继续保留。此处不要求把已失败的初始同步自动重试。

## 已核的受影响关键链

- **锁内生产通知没有CAL直达分叉。** CAL Repo提交后先记录commitSeen、关闭写连接，仍持原写锁回读并形成真SHA；成功和各提交后失败分支都在解锁前调用通知。Store只发Root的intent=0通知；Root统一排到worker，intent>0仅回登记号。旧保存登记ack不能覆盖Root已经接收的新保存。
- **设备回写结果形成于同一锁内。** DeviceRepo关闭写连接后仍持锁重读同文件，核实际参数并复制完整Record/SHA；初始成功和单项目成功都消费 `written.project`，没有解锁后另取任意本地新版本冒充设备确认。第2项是提交后失败事实没有继续传出，不否定这个成功闭环。
- **期限不是只靠QTimer。** prepare冻结单调时钟和到期点，Root入队前后核期限，worker回执、UP入口及回写闭环后也核elapsed，过期不报成功。第1项是明确未attempt的迟到事实未被接纳，不建议放宽发送或UP期限。
- **一份在途不被新保存偷换。** 同文件保存只保留最新未发送项，active冻结完整参数；prepared尚无入队结论与真正尝试发送在正常分支分开。真正可能发送后出现新版本时保留unknown，旧UP受原文件版本保护。
- **纯读取不产生保存循环。** 普通reload、UP回写、曲线保存没有新建明确保存意图；Root的生产通知与登记ack方向分开。不存在通过通用快照自行生成新Up的修法。
- **不靠历史prepared标志永久挡更新。** worker核 `hasUnfinishedIssuedTasks()`，Root核当前设备及正在准备的动作，而非历史 `prepared_==true`。项目通道使用独立批次回执，没有直接借 `invalidateOriginalDeviceSync` 清理Us或已有任务。
- **UP同文件改编号保留引用保护。** 同文件接受实际新xuhao/指纹；若已提交且编号变化，即使快照或完整期限尚未闭合，也记独立identity-review；参数ready不等于允许开始。引用待核不混入试剂未知，也不因普通ready或新连接自行清除。未改写旧草稿/样本任务中的数字引用。
- **初始途中保存不假ready。** 初始等待参数保持冻结，新的明确提交不能直接替换这份等待；版本冲突继续阻断。当前意图、连接、文件及版本匹配条件仍在，原 `canStart` 全项目文件校验保留。

## 范围与交接

普通项目保存、试剂组生产端、copyv2 guard的根窗口叠片已在handoff列为后续接线，不把这些已列待接事项重报为本CAL新阻断。本报告不重新统计原功能覆盖，也不把根05另行取得的RK只读运行结果移用到本temp06候选。

本固定片已匹配上述hash；若修正这两项，应另存增量或新版本并更新清单，保留本片及本报告。后续只核实际改动和受影响路径，不重编或运行来代替逻辑复核。是否正式整合由总窗口决定；目前本报告给出的是“原两项已关闭、仍有两项P2”的源码结论。
