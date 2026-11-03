# 项目复制第二版：恢复证明与实际读写入口独立审查

已完成作者最终固定版本的漂移和清单复核：本次恢复证明、并发读取发布及读写守卫窄片未发现剩余确定阻断，可交总协调串行整合。CAL“上次浓度”写守卫仍须由总协调在 task_error 最终函数上叠加；这是明确的整合前置条件，不能把本候选单独称作所有写路已覆盖。此前过程审查发现的文件同步项和作者随后修正的快照发布项已在最终源码中关闭，固定范围见末节。

本次只读检查 v2 新恢复证明、锁顺序、项目读者和直接写入守卫；不重复 v1 完整复制原行为审计。只新增本文，未改作者候选或正式源码，未编译、运行、创建/读取业务数据库、联网或连接板卡。

## 开始审查的输入和期间修正

开始审查时 helper `candidate/projects/original_project_copy.cpp` 为 `0960a785f7d06631ef03f9fe1558e8a510680753a9ca75c997facaf08daa453a`；Repository 头为 `187e3fc8d4e8db6b29e2ee6e46040657d9169b4dd6e029445e5473c72bb0e615`。v1 相邻目录原件不动。

已发现并通知作者：首次把清单发布为 `uncommitted-proven` 时，`writeMarker()` 仅依赖 `QSaveFile::commit()` 和目录 fsync，随后重读内容。没有现有证据证明本 Qt5 实现会可靠把文件 fsync 失败作为 commit=false 返回，因此不能据此称作已明确检查成功的文件落盘证明。

随后已读到修后 helper `d3eba28ceffc9708928e0a1f580529f4eb356d4efb5992394b756bce61396de1`：`syncMarkerFile()` 先取得原身份，再打开同一自有、单链接普通文件描述符，核设备/inode/用户，显式 fsync 并检查返回值及 close 返回值，最后核路径仍同身份及目录 fsync。所有 `writeMarker()` 都以它收尾；已存 proof 每次也先经此函数再 markerMatches，避免一次落盘失败后下一次仅凭可读内容放行。目标复核与事务/两锁的顺序仍保留。最终源码及修后对象均已固定，此项源码缺口关闭；不是 fsync 故障实测通过。

作者随后自行收窄了另一个并发发布问题，本次已独立核修后 Repository `39fbb96c10f09fd9f51e31a046125b005413ca1cc2a5c6602e2958402898b34f`：原实现直接清空/追加调用方输出。若 marker 空快速扫描之后活动复制才创建克隆，目录排序又先读到克隆，随后遇源的重复旧编号而失败，可能已将半份克隆列表留下给 SampleRepository 成员。现在 private readProjects 使用局部 records/files，全部成功才发布；public load 暂存项目、单位和三类选项，任一步失败恢复 Repository 内部旧文件/排除名单，并保留调用方上次有效输出，仍返回 false。末尾 SQL 迭代错误也拒绝发布。这一窄漂移合理，未观察到把失败伪装成功或发布半份 clone 的新路径；最终源码与修后 Repository 对象已固定。

上述修正关闭后，最终新增逻辑未发现剩余确定误分类、死锁或绕过项目写守卫的问题。另有一处 CAL 浓度保存写守卫由总协调在 task_error 候选之后串行叠加，明确未在本候选硬接，见后文；它是整合前置条件，不能称当前所有最终生产写入口已完成覆盖。相邻 v1 的 19 份候选/接线文件及 6 个已有对象均与原 manifest 匹配，未见原件漂移。

## 新证明的时序与三个结果

`recoverPending()` 没有清单时直接返回；有清单时先取进程内恢复 mutex，再重新枚举清单，随后按项目→试剂顺序 `tryLock(0)`。每份清单都在两把 QLock 仍持有时进入 `recoverOne()`。

恢复对目标库可写打开，并 ATTACH 原校准/默认试剂库，让 SQLite 自己完成热 journal 恢复；核日志/同步模式，再 `BEGIN IMMEDIATE`、quick_check 和完整事实核对。没有裸看中间主库字节便认定事务未提交。

| 事实 | 当前判定与限制 |
| --- | --- |
| 完整提交 | 非 `uncommitted-proven` 阶段先执行原 fullCommit：最终项目完整摘要、新名下各关联全部行内容/身份、校准点集/额外点数量和计数器事实共同证明，不能仅靠行数。只清对应清单，保留已提交资料。 |
| 首次证明未提交 | 必须是 prepared/ready，目标同一自有普通文件、完整初始克隆指纹及内部旧 ID/英文名相同；来源同身份/版本、新名无关联、所有本次预留校准号在三个校准表为空。都成立后才构造新证明。 |
| 已存未提交证明 | 每次仍核私有清单、完整 proof 内容与证据 SHA、目标身份/完整初始克隆/旧 ID 名称、外库身份、新名无关联。克隆缺失、内容改变、被替换或出现新名关联均拒绝。不能凭 proof 授权写入/删文件/改关联。 |
| 无法证明 | 保留现场并返回失败，默认读者不继续交出可用快照；不猜未提交、不自动删数据库、不回滚他人后来写入。 |

`uncommittedEvidence()` 把完整源/目标清单绑定为摘要，proof 还核格式、首次阶段、检查项目集合及 UTC 时间结构。首次证明的发布、显式文件/目录 fsync、重读与目标复核发生在 SQLite 核对事务及两把 QLock 仍持有时；事务结束失败也拒绝返回可过滤名单。显式文件 fsync 修正见上文。

已提交目标不符合完整初始克隆与旧 ID/名称，因此不能仅因 fullCommit 后来未能证明而退成未提交。`new_id != source_id` 也是标记结构前提。对于已存证明，即使不再尝试 fullCommit，只要后来目标变成新项目或字节改变，初始克隆核对仍失败，不能被过滤为旧克隆。

后续读取故意不再要求预留校准号为空，也不再要求源项目保持首次旧版本：首次完整证明已经记录“当时没有提交”。这样合法 CAL 后来复用旧预留号，或原源项目后来有合法变化，不会单凭这些事实把所有原读者重新挡死。新名仍须无关联，外库仍须同文件身份，克隆仍须完全不变；因此该放宽不是凭校准号猜归属，也不允许删除后来 CAL 资料。首次尚未成功持久证明时，预留号已被复用仍返回待核，不借后补 proof 猜历史。

## 并发和写守卫

恢复路径是 mutex→项目 QLock→试剂 QLock，QLock 全部 tryLock(0)，不持 mutex 阻塞等另一把 QLock。写路径持项目/试剂锁时只执行纯 `noPending()`；它不调用 recoverPending，不产生锁内递归恢复。复制自身在已持两锁下直接 recoverOne，不再重复拿恢复锁。未观察到 mutex 与两把 QLock 的循环等待；真实锁占用仍可明确失败/稍后重读，不能称所有并发必定成功。

默认 `OriginalProjectRepository::initialize/load` 会恢复核对清单，再把已证明自有未提交克隆文件名过滤掉，返回原项目资料。unknown 或证明核对失败会返回 false，不能把有待核清单的目录直接当普通项目目录。`copyRecoveryPending()` 与 store/page 信号保留原项目只读提示，后台写保护不依赖页面按钮是否禁用。

实际项目文件列表生产入口集中在 Repository 的 `readProjects()`。只读检索与调用核对如下：

| 生产读者 | 当前共用路径 |
| --- | --- |
| 项目页、当前报告格式 | ProjectsStore 持 Repository；公开 load 默认允许过滤已证克隆；格式快照复用同 Repository |
| 样本、校准预设/检测任务 | OriginalSampleRepository::refreshProjects 复用其 Repository |
| 报告/历史原格式 | OriginalHistorySource::initialize/refresh 复用 Repository |
| 试剂批组项目选项 | OriginalReagentEditor::load 新建 Repository 并 initialize/load |
| Up参数读者 | OriginalProjectDeviceRepository::load 新建 Repository，先取得过滤后的项目再读各已知文件 |
| 校准/质控页面 | 主程序接收项目快照后传入相关 store，不额外枚举 project/*.db；loadSystemProfile 不列项目文件 |

上述“可读”指经过核对的原项目快照，并不把整个应用的其它业务写入一并禁用。例如合法 CAL 可分配后来编号；原写权限、运行态和其它库的版本保护仍各自保留。

直接原项目写入入口：

| 入口 | 待核复制期间的后台阻断 |
| --- | --- |
| Repository 保存/复制/删除/导入 | 取得项目锁后，private readProjects 默认 allowVerifiedPending=false，调用 noPending；不沿公开过滤读取放行写入 |
| Repository 单位新增/改名/删除 | 原入口写前检查之外，v2 已加项目锁内 writesAllowed 复核 |
| UP项目回写 | OriginalProjectDeviceRepository::applyDeviceProject 项目锁后、load/SQL前 noPending |
| 批组启用 is_related 等项目文件写 | OriginalReagentEditor::mutate 两锁后对 project/ 文件 noPending，目标指纹/事务内版本守卫仍在 |
| CAL上次浓度 last_cal_para | 当前正式 OriginalSampleRepository::saveCalibrationParameters 仍缺锁内 writesAllowed；总协调已明确待 task_error 完成后叠 `if (!projectRepository_.writesAllowed(message)) return false;`。本候选未覆盖，不得遗漏该整合条件 |

原目录之外的导出也经 private readProjects 守卫，没有把未提交克隆打包成普通项目。未见另一条生产代码直接枚举 project/*.db 后绕过 Repository 的列表；测试源码、文件选择对话框和单个已知文件指纹读取不当作额外生产目录枚举。

## 最终固定核查和未验范围

作者给定并实算匹配的最终 manifest 为 `41f974820c01e7fed2002b0317dbaf86e45851dc8acc3083f24f91d7a5b34d3c`。helper、Repository cpp 与头分别仍为上述 `d3eba28c…`、`39fbb96c…`、`187e3fc8…`。21/21 份清单源码/构建候选、19/19 正式底稿、6/6 已有对象、6/6 补丁指纹均匹配；编译前后 14 项直接输入均匹配且清单逐字节一致，清单 SHA 为 `b31e9118774aef6da57b94f382360680e1bae0cc5b3a69217c99f279718e1ca6`。

修后 helper 对象为 `aa206f09549c57f07c1f9a9b223fc44f199a7c2e100098fad5b7839c162bcf58`，修后 Repository 对象为 `7049a9edc4e5cfff404e171363ebd3dbdd7ba41fa2763067a25f9166de311306`；只核作者既有对象/编译记录和输入，没有重编、链接或运行。其余对象不借旧 helper/Repository 的成功替代此次修后对象。

固定源码累计片 `candidate.patch` 为 `474641f8196bd5035e59729d6219813a3d5fdcaf14007331fabf0f8c26eec110`；core/UI 分片 `a4ab1ade63b6bb558bb856874f36718ffb1ef65eaad7ad11af1b8de322656ef8`；外部写守卫 `6c49a885f73671b5f732c38a734121dab011b27ed229c8b658f598ea23e7001b`；构建接线 `9e57b4817616ae7d7220dcf47c6c4e419312787c195ab12465e540a8ce011e76`。必需另叠的 `owner_cal_guard.patch` 为 `724d469b40fe8f6842129670921d9f6250e1cfa245e2806fa2c7df334cd944b7`，清单明确 `applied_in_this_candidate=false`。

只读 `git apply --check --whitespace=error-all` 核累计源码+构建+CAL 守卫，以及 core/UI+外部守卫+构建+CAL 守卫两种组合，均退出 0。没有应用补丁。DeviceRepository 还会与 task_error 的新函数签名/回读通知相遇，必须只叠外部守卫窄 hunk；CAL 守卫也须串入 task_error 最终函数，不能整文件覆盖。随后由总协调在统一基线上完整编译。

Qt/SQLite 多库崩溃恢复、文件系统 fsync 失败、重启后已证克隆过滤、合法 CAL 复用编号后的真实页面加载、权限/草稿及资源仍未在 RK 验证。本次没有故障注入或业务库实验，不宣称恢复已实测。
