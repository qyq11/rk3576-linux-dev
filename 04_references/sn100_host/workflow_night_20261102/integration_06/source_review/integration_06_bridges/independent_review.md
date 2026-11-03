# 第六轮项目复制写保护桥：独立窄审

结论：当前第六轮隔离副本的复制写保护桥未发现新增确定阻断。CAL、UP回写和批组开关都在已有写锁内、打开写库前阻断待核复制；新同步签名、提交事实、同文件快照和通知链没有被旧复制候选覆盖。此结论仅覆盖这组桥和受影响链，不能作为同步意图 v1 整体放行；其已知两项 P2 仍由作者 v2 修正，本报告不重复上报。

本次只新增本文。没有修改任何候选、隔离源码或正式文件，没有编译、运行产品/解析器、访问业务库、联网或连接板卡。正式 05 的已有板端 20 项结果不属于本文所审源 06，也不能代替本桥运行验证。

## 输入与实际差异

隔离根为 `integration_06_review/06_programs/sn100_host/`。读取时同步片仍为 `250b88f58b0e10b365aa3ae4a0c0005eef2e3619676e6fe659b607aa7822963c`，本报告不声称已收到或验证作者后续 v2 delta。

根桥 `copy_cal_guard_after_intents.patch` 实算 SHA 为 `eb048b9d6bd69b445256cf7e3a012aecffe5251e35980ddd0ab20c0d5b8cc351`。直接比较隔离副本与同步候选：

- `samples/original_sample_repository.cpp` 只有项目锁成功后新增 `if (!projectRepository_.writesAllowed(message)) return false;` 一行。
- `samples/original_project_device_repository.cpp` 只有复制 helper include、锁后的 noPending 及解释注释；没有换回旧函数签名或删除新提交/重读代码。
- `samples/original_sample_store.cpp`、`samples/original_device_sync.cpp`、两个上述 Repository 头、`shell/device_control_shell.cpp` 和 `shell/frmmain.h` 与同步 v1 候选正文相同。因此本桥没有另改发送/确认意图、重连或 Us/任务未知状态代码；不是对那些代码完整正确性的重新背书。
- 与已审复制 v2 的 13 项源码/UI 比较，除有意组合新同步代码的 DeviceRepository cpp 外，其余 12 项正文均一致。部分新文件及头的 LF/CRLF 格式改变，故字节 SHA 不同，已核不是漏失证明或读者保护逻辑。不能把它们的上游 SHA 直接冒充第六轮字节 SHA。

此次实际关键文件 SHA：

| 隔离文件 | SHA-256 |
| --- | --- |
| `samples/original_sample_repository.cpp` | `e7cff37b2371fe76cf5ae935737dcddb08cb1e00f5a12e835d82a4101eeae1ae` |
| `samples/original_sample_repository.h` | `4812de050c17ea1a8ac9258b5688dbc3f7590d6abf6c0e2ebe46a21497a09062` |
| `samples/original_project_device_repository.cpp` | `53b80ec7646820eb65350c2685cd2f42d1e4356b117f1da384db14bc193da1eb` |
| `samples/original_project_device_repository.h` | `dee0cbf17b93060e27efd5ff51071c3df404faa4967f7a45e201ffcf2ac442bf` |
| `reagents/original_reagent_editor.cpp` | `56e59889bffb141aebc9e4dd6ae44a28d2f155af6dc946e2cd71a3d0c4732855` |
| `projects/original_project_copy.cpp` | `5aad871cee2f93d693c595f4071b9f69c797ab42893b7509b0385806d59d227e` |
| `projects/original_project_repository.cpp` | `39fbb96c10f09fd9f51e31a046125b005413ca1cc2a5c6602e2958402898b34f` |
| `projects/original_project_repository.h` | `b3dfba7b306052b2de32182d31b454073f557bd1ea88dda6af7a1d8ba488844a` |

## CAL桥保留了什么

`OriginalSampleRepository::saveCalibrationParameters()` 仍保留新 result、committedNotice、expectedFileName 参数。它先读取完整项目，核预期版本，把目标 ProjectRecord 拷贝到局部 `target`，再核原文件名，避免刷新容器后继续用旧指针。

项目锁成功后新增 writesAllowed，纯检查工作根和 pending 标记；有已证未提交或 unknown 清单都返回 false，尚未打开本次写库，`commitSeen` 仍 false，也不会发已提交通知。它不会借“可以只读过滤克隆”放行写入。

无清单时原流程保留：写事务内再核目标原文件完整版本，只更新 last_cal_para，真实 commit 成功才置 commitSeen；写连接先退出作用域关闭，然后仍持项目锁重读真实项目 SHA。按同一原文件名找回目标并核 ID、名称、实际浓度文字，成功才置 verifiedSnapshot 和 committedFingerprint。已提交后重读失败仍在锁内通知“已提交、未核实”，不因返回 false 把提交事实丢掉。

SampleStore 仍传预检的实际文件名、接收完整 notice，并由锁内回调排提交通知；桥没有改变这一顺序或改成 GUI 先后猜版本。Repository 的局部完整发布/失败保留上次有效输出与此相容：重读失败会明确 false，不能把残留旧 Record/SHA 当新提交快照；成功则是该次完整同文件快照。项目锁保留到通知完成，正常合作写者不能在此间替换刚确认的版本。

## UP与批组写路

`OriginalProjectDeviceRepository::applyDeviceProject()` 保留 expectedFileName、OriginalDeviceProjectWriteResult 新签名。其项目锁后 noPending 在 load/SQL 前，pending 拒绝时 result 仍为初始 false，不能报已提交或确认。

无 pending 时，名称、请求指纹、原文件名、xuhao 冲突、事务内完整指纹仍核。真实 COMMIT 后先关闭写连接，锁仍持有，再 load 完整已提交项目；按目标同文件/名称取回，核实际 wire 参数后，才同时交出 `project.record`、真实指纹及 verifiedSnapshot。原读者过滤逻辑没有改成用可变旧 ID 去捞另一文件，Repository 也没有把失败时的半份文件列表交给新 ack。

`OriginalReagentEditor::mutate()` 与复制 v2 正文相同：项目→试剂两锁成功后，仅对 `project/` 文件检查 noPending；原版本、事务内目标复核及写入失败保护仍保留。它没有顺带禁止不涉及项目文件的其它试剂库业务，也没有因本地开关保存直接制造 Up ready。

## 生产写入口与恢复锁相容性

| 原项目相关写入口 | 当前 pending 保护 |
| --- | --- |
| Repository 保存/复制/删除/导入及导出枚举 | 项目锁内 private readProjects 默认禁止 pending |
| Repository 单位新增/改名/删除 | 入口检查及项目锁内 writesAllowed 复核 |
| UP回写 | 新 DeviceRepository 项目锁后 noPending |
| is_related 等项目文件写 | ReagentEditor 两锁后、project/ 分支 noPending |
| CAL last_cal_para | 本根桥新增项目锁后 writesAllowed，补齐前次明确未硬接的入口 |

`noPending/writesAllowed` 仍是纯检查，不恢复、不进恢复 mutex、不写证明、不发命令。默认公开读取先完整证明后过滤自有克隆；unknown 返回失败。此可读快照不改变 private 写门槛。

已经持写锁的新 CAL/UP方法调用 load 时，正常路径先经纯 pending 拦截；无清单的 recoverPending 快速返回，不会带着项目锁再去等恢复 mutex。若不合作外部操作在这时添加清单，锁竞争只会明确拒绝，保留提交/待核事实，不靠递归拿锁或删标记放行。恢复证明、显式同文件 fsync、合法后来 CAL 复用编号及局部发布仍与已审 v2 正文相同，本次未扩大重审。

桥本身没有调用 invalidateDeviceTaskContext、清空试剂未知、重发 Us/Up 或把失败写成 ready。CAL 在守卫拒绝时不发已提交通知；UP失败仍交原同步错误/待核路径，接收与任务的已有未知事实没有被本桥新代码清除。源同步 v1 中其它状态处理的已知问题仍归作者 v2，不能因“桥没有新增清理”就称整个同步状态机已通过。

## 后续只需核什么

作者同步 v2 delta 串入后，应只核这三处锁内守卫、新签名、commitSeen/verifiedSnapshot、同文件 SHA/通知链仍在，以及复制 v2 证明和局部发布没被覆盖；不需因此重开完整复制原行为审计。第六轮统一编译、RK正常保存/失败/重读及 pending 场景仍待总协调获准验证；本文没有新增任何运行通过结论。
