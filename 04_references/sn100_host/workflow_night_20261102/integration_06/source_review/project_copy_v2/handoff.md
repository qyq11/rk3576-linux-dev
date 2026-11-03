# 项目复制v2：共用读取、写守卫和持久未提交证明

v2修的是复制失败后应用内的真实恢复缺口：试剂、样本、历史和项目同步都复用ProjectRepository，v1的默认pending门槛会把本来应当可读的原项目也挡住；一次失败读取还可能把先读到的克隆留进调用者半份缓存。v2统一返回过滤后的只读原文件，整次读成功才发布快照，并持久保存完整核实过的未提交事实，避免后来正常定标复用编号又把读取变成unknown。

**当前只有候选和ARM64对象。**没有改正式05、v1、Root的06审阅副本或task_error在途SampleRepo/Store，没有运行产品、测试库、模拟端、真实数据库或网络/板卡操作。目录沿11月2日晚同轮保存，跨日继续不更名。源码和补丁已固定，独立复核见`independent_recovery_review.md`。

## 交给Root怎样串叠

Root的06尚未应用copy，推荐用下面分片；不能把重叠候选整文件覆盖到task_error新签名或试剂组v3上。

| 分片 | 用途 |
| --- | --- |
| `copy_core_and_ui.patch` | 11份项目源码/头/UI，包含v1本地三库复制、原命名对话框、默认定标/权限/保存前备份保留，以及v2共用读取/证明。 |
| `wiring.patch` | 与v1相同的8份构建接线，仍未接网络共享。 |
| `external_writer_guards.patch` | 只给ProjectDeviceRepo项目写锁后加noPending，以及ReagentEditor的project/写入口在两锁后加noPending；其余task_error/组开关逻辑保持各自版本。 |
| `owner_cal_guard.patch` | **必需Root后叠**：task_error最终saveCalibrationParameters项目锁后、开写库前加`projectRepository_.writesAllowed(message)`；本目录没有修改那份SampleRepo。 |

`candidate.patch`组合13份源码改动，便于总审；`v1_delta.patch`展示相对v1的5份变化，不是对当前未接v1正式目录直接应用的包。不要重复应用组合和分片。

对当前05，组合candidate+wiring、分片core+external+wiring+owner CAL均已只读`git apply --check --whitespace=error-all`退出0；没有实际应用。owner CAL要在真实06/task_error最终函数上再核窄hunk及编译，未接它之前不能宣布项目写路全部覆盖。ProjectDeviceRepo对象也基于05旧签名，只证明窄守卫可编；与task_error新结果/锁内通知签名合并后仍需真实06重编，不能替代他的回执核对。

## 实际读者和可用范围

生产只有Repository.readProjects直接枚举project/*.db。SampleRepo.refreshProjects、HistorySource.refresh、ReagentEditor.load、ProjectDeviceRepo.load，以及ProjectsStore辅助读取都复用它。启动loadSystemProfile只读通道/字典/结果检查/告警，不枚举项目。具体可达失败链、main就绪前退出条件和写者索引见`reader_boundaries.md`。

public load默认允许**已经完整证明并持续可核的未提交克隆**被排除后读取原文件。多个恢复读者用进程内mutex串行，随后项目→试剂QLock均try0、不等待；写入口只查pending，不隐式恢复。没有锁等待环。

readProjects使用局部完整records/files，public load暂存项目/单位/试剂等输出；任何失败都不发布半份列表，保留上次有效缓存并返回false。unknown仍拒绝原资料源，不能把保留旧缓存当本次读取成功。当前main遇初始化unknown仍可能退出，这是不能证明数据时的明确拒绝，不承诺任意损坏/外部换库下继续可用。

原项目保存/复制/删/导入、单位修改、设备UP保存、is_related开关及后叠last_cal_para都仍因marker拒绝写入。新证明只允许读者排除历史克隆，不授权写、删、搬或计数器回退。已经完成同步的原文件仍可只读复核；新连接要保存设备项目返回时会明确拒绝pending，不能声称项目同步已完成。

## 为什么持久记录“已经证明未提交”

首次仍必须完成v1整套核对：SQLite正常热日志恢复之后，在两把QLock与SQLite核对事务持有期间，确认同一自有完整源克隆、旧编号/名称、源身份/版本未变、新名及预留校准号无新关联，才原子登记`uncommitted-proven`及完整清单证据SHA。

发布后显式核同一自有文件描述符的fsync/close返回、路径身份、父目录落盘，再重读完整marker及目标内容；不依赖QSaveFile通常的内部同步。任何落盘或核验失败不排除文件。以后逐次核私有proof完整性、同一自有目标身份、完整初始clone、旧ID/name、附加库身份和新名无关联；目标缺失/替换/修改或证明不符仍unknown。

已证事实不再因别的合法CAL后来复用预留编号而失效，源当时版本已在证明中绑定；后续真实源仍走正常读取和版本保护，不能把克隆用作缺失源或新项目。完整条件、落盘失败和并发边界见`proof_contract.md`。代码没有自动删除任何项目DB、关联行或计数器，也没有用proof完成共享对端同步。

## 原业务保留与验证边界

v1原规则保留：前缀+1至4位后缀、max xuhao+1、仪器project_id不另发号、只改原复制四键/结果名、默认定标/AlgParam/last_cal_para及其他表整份保留。每条校准绑定各自复制base/data和默认试剂组，只有试剂组而无校准绑定的行不额外复制，厂家三库不扩入。

Root已有实际只读统计是6条正编号绑定各有唯一base，0号data为0，默认20组中15组无对应校准绑定；这些15组按原版本就不复制。缺base仍明确拒绝，不造空记录或有效曲线。文件头1/1仅说明rollback格式，不能证明连接FULL同步、实际复制或崩溃恢复通过。事实及原链保留在v1说明与正式sqlite3归档，本轮未重读实库。

最终6个对象为helper、Repository、ProjectDeviceRepo、ReagentEditor、HistorySource及未改的正式SampleRepo读接口，均ARM64可重定位对象。编译包含14份直接输入前后SHA相同，并从实际.d确认使用v2 Repository头。History的cpp/h只是等字节编译支撑，不进入源码补丁。没有完整链接、产品执行、实际恢复、CAL号复用、界面或资源通过结论。

编译过程如实保留：最初helper编好后因WSL缺rg中止依赖检查，改grep后接续其余5；核过该helper原输入与完整批一致。独立预审补出marker需显式文件fsync后只重编helper；后来补读取整次发布后只重编Repository，其余源/头未变。旧日志/输入/对象SHA留revisions，最后log是Repository增量，最后objects清单覆盖6个最终对象。

`review_manifest.json`固定13份源码、8份构建、19份正式底稿（均未变）、6对象、6patch及owner后叠条件；`patches.sha256`给补丁SHA。最终helper为`d3eba28ceffc9708928e0a1f580529f4eb356d4efb5992394b756bce61396de1`，Repo cpp为`39fbb96c10f09fd9f51e31a046125b005413ca1cc2a5c6602e2958402898b34f`，Repo头为`187e3fc8d4e8db6b29e2ee6e46040657d9169b4dd6e029445e5473c72bb0e615`。独立复核已完成，无剩余确定阻断，CAL/DeviceRepo串叠前置继续保留。

下一步由Root将分片与task_error/组开关真实版本串入06，接CAL守卫并完成真实目标编译，再按获准板卡、实际INI和主控范围验证复制、失败/重开、三态、旧任务/历史/同步、权限与草稿、资源和仪器功能。电脑或对象编译不替代这些结果。本轮不改公共README/计划/记录，不自动提交Git。
