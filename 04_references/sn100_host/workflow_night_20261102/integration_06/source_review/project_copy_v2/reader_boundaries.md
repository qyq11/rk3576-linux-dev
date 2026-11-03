# 待核项目复制：实际读取入口与写入边界

这份说明来自只读生产源码检索，不来自运行或真实库测试。范围排除了tests和thirdparty，没有把备份目录、参考原件或共享对端传输算作本轮应用内读者。

## 实际入口与v1故障路径

生产代码只有`OriginalProjectRepository::readProjects()`直接枚举工作根`project/*.db`。下面几个入口复用同一Repository，并非各自另写一套目录枚举。

| 实际消费者链 | v1在已证明未提交克隆下的影响 | v2共用处理 |
| --- | --- | --- |
| `SampleStore::initializeOriginal`→`OriginalSampleRepository::initialize/load`→`refreshProjects`→Repository.load | initialize先识别并排除克隆，但refreshProjects调用load默认false又拒绝pending；样本初始化失败，main在sampleReady前可exit(3)。普通样本/校准草稿后续重读也被挡。 | load默认返回过滤后的只读原项目列表，旧任务按原项目ID/文件/指纹继续核对；不会从克隆补一个假项目。 |
| `HistoryStore::initializeOriginal`→`OriginalHistorySource::initialize/refresh`→Repository.load | 历史当前格式读取失败，未就绪时可使主入口退出。 | 报告格式只读原项目，指纹仍按原文件真实内容计算。 |
| `ReagentStore::initializeOriginal/loadOriginal`→`OriginalReagentEditor::initialize/load`→Repository.initialize/load | 试剂项目选择读取失败，reagentReady前main可exit(3)，样本初始化又依赖试剂ready，可能更早阻断。 | 继续提供已过滤的原项目及版本；直接写project/is_related另有锁内pending守卫。 |
| `OriginalDeviceSync::reagentReplySaved`→`OriginalProjectDeviceRepository::load`→Repository.initialize/load | 读取完整项目参数失败，配置同步不能继续；不能说只影响项目设置页。 | 只加载真实原文件，按各自文件/指纹核参数；不把克隆当成新项目下发。设备UP返回要修改项目时仍受写守卫。 |
| `OriginalDeviceSync::canStart`→当前ProjectDeviceRepository.load | 已同步项目的重新核对也被默认false拒绝，可能阻止旧配置开始。 | 已完成同步的当前原文件仍可做只读一致性核对；新连接的项目回读保存不借只读放行，仍可因pending明确拒绝。 |
| ProjectsStore.read、完整结果格式查看、项目页面load | v1只有页面专用true入口可读，其余只读辅助入口过度阻断。 | 默认只读过滤统一；所有保存仍过原权限和pending写守卫。 |

`OriginalRuntime::loadSystemProfile()`只加载通道、用户字典、结果检查和告警，不枚举project目录；main早期这个profile加载不是重复xuhao的来源。QC/校准页面沿既有目录/样本项目快照取项目，没有找到另一套生产目录枚举。

v1的重复xuhao保护本身仍有效：若未过滤克隆进入readProjects，会拒绝整次读取，不按目录顺序挑一个。此次实际恢复缺口主要是过滤之后的默认读取门槛及跨线程恢复互抢；不是据此证明旧任务已误引用。v2仍只允许**已完整证明未提交**的克隆被排除，unknown继续拒绝该原资料源，保留文件和旧草稿。

另补了实际缓存发布风险：原readProjects边读边改调用方列表和m_files。若marker空检查后活动复制才创建克隆，文件名`10 ...db`可能先于源`2 ...db`被读，重复xuhao最终虽报错，半份输出却已包含克隆；SampleRepo把成员projects_直接传入，失败后getter仍可能留下这个错误文件绑定。v2只在完整扫描成功后发布records/files，public load的项目、单位、试剂等输出也整次成功才发布；失败保留上次有效快照并返回false，不把旧快照冒充本次新读取成功。这样不靠每个调用者自行清半份缓存。

## 谁还能直接写原项目

检索原project表更新和`project/`读写打开后，实际写者如下；匿名新库的project_base不是原name/value文件，不混入这项守卫。

| 写入口 | pending守卫 |
| --- | --- |
| Repository.save/copy/remove/import | 既有项目写锁内private readProjects默认false；有任意复制清单就拒绝，仍保留原权限/版本/备份。 |
| Repository.addUnit/updateUnit/removeUnit | v1入口已查writesAllowed，v2再在项目锁内复核，防检查后复制失败留下pending。 |
| ProjectDeviceRepository.applyDeviceProject | v2只加include及项目锁后、load/SQL前的noPending。与task_error扩展的锁内结果/快照回调必须按窄hunk叠加，不能覆盖整文件。 |
| ReagentEditor.write的project/文件（当前is_related） | v2只在已取得项目→试剂两锁后查noPending；原试剂库读写不因这个项目文件守卫一并禁止。 |
| SampleRepository.saveCalibrationParameters的last_cal_para | **由Root在task_error最终函数上再叠一行**：项目锁后、打开写库前`if (!projectRepository_.writesAllowed(message)) return false;`。本目录不改他在途SampleRepo/Store；未接这行前不能声称所有写路都已覆盖。 |

`writesAllowed()`只核工作根和pending清单，`noPending()`只列清单；二者不恢复、不进恢复mutex、不发设备命令。持久未提交证明没有解除任何写保护，仍需后续获准的明确处置，不能据它删除数据库或关联。

## 恢复互斥顺序

`recoverPending()`在没有marker时直接返回；有marker才取进程内恢复mutex，随后重新列marker，按项目→试剂顺序`tryLock(0)`。两把QLock都不等待；失败明确返回忙，不持mutex等另一线程放锁。函数返回时QLock先析构，再释放mutex。

多个初始化读者不会各自抢同一恢复锁而误报失败：它们先由mutex串行。写者取得项目锁后只查pending；发现marker就提前拒绝，不带锁进入恢复。正常无marker的锁内load走快速路径。即便外部不配合地同时写目录，QLock也是try0失败返回，没有“恢复mutex等待项目锁、项目锁等待恢复mutex”的环等。

正在复制或其他写者实际持锁时，读取仍可返回忙，请重读；本轮没有承诺无等待开销或运行中外部任意换库的一致性。待核克隆已经证明且当前可核时，各实际只读消费者能从同一入口取得原文件，不再因v1默认false而被阻断。效果仍待完整应用/RK验证。
