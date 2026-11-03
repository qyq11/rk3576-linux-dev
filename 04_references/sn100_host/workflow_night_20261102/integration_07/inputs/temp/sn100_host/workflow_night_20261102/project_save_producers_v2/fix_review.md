# 保存生产桥v2：10秒期限修正独立复核

结论：原生命周期报告`f4b6ddc0`的截止时间P2已在本固定v2中修正，所查delta未见新的阻断。同一个绝对单调截止值从来源后台槽开始处理请求时贯通到锁内最终认领，预检读取、排队及仓库验证不会再重新起10秒。已经认领的事务和真实提交事实保持原处理。

本次只新增本报告，没有修改候选、正式源码、06隔离源或旧原件，没有重新编译、运行产品、执行协议程序、操作实际/测试数据库、网络、板卡或Windows界面。结论限于源码、真实依赖文件和已有固定编译证据；不能说真实保存或设备同步已经通过。

## 固定输入与证据

位置：`temp/sn100_host/workflow_night_20261102/project_save_producers_v2`。

| 文件 | 实际SHA-256 |
| --- | --- |
| `project_save_producers_v2.patch`，69135字节 | `c562e94e3e6cbee47fa3a969dab6c254117acbb3f7c5ca69c2676a61855d2426` |
| `delta_from_769f.patch`，17737字节 | `0705de5c94ff2086ecc4fb7f6d65bc55a5222770369220c61adf5d077556dbce` |
| `candidate_manifest.json`，12704字节 | `9de4df264ffb34a57ddccd252b2b2710bdd0894c0362ce6805428da57c4d0b9f` |
| `handoff.md` | `4fdb6a9d39a00abdd3b24b33ff55bcc0b93c218eaef79d222acc77d9d76f5813` |
| `compile_arm64.log` | `9ec3e8d97854e13215fa35f16c8b001f4d15df63faed3bca23c4a42615fe6b9a` |
| `inputs_before.sha256`与`inputs_after.sha256`，各自相同 | `7384f1172feaf4c7e15db4b3e0e1e827356dbdbdb9bcd96912cfe8d128c23cd1` |

实际核对17/17候选目标的SHA和字节数相符；原769f对应的前版源码也相符。两版实际仅8个目标变化，与清单一致：许可头、ProjectsStore头/CPP、ReagentStore头和original_reagent_store.cpp、SampleStore头和original_sample_store.cpp、Root的device_control_shell.cpp。

现有12/12对象SHA符合v2清单；旧769f的12对象也仍符合旧清单，原报告SHA仍为`f4b6ddc0275f4d3c3801b7a4b53aea9c906f79e4af95328633943bc298a1af0c`。旧版真实12份`.d`均依赖许可头，确需全重编；新12份`.d`解析`../`等路径后都指向v2实际候选许可头。三个moc中申请信号/预留槽均包含第三个`qint64 sourceDeadline`参数，和声明、Root的Q_ARG一致，未误用旧签名或旧许可头。

已有编译日志记载9个CPP、3个moc对象，前后输入清单逐字节相同；本复核没有再构建。累计补丁检查仍是明确带`--ignore-space-change`的原CRLF底稿检查0，delta对769f原LF候选的标准`git apply --check`记录0。本复核未重新应用补丁，更未应用正式源。

## 截止时间逐段核对

| 阶段 | 固定v2实际代码 |
| --- | --- |
| 来源取点 | `ProjectsStore::save`进入原库分支首先取`originalProjectSaveMonotonicNow()+10000`，在原权限/资料检查之前；组开关`saveOriginalBatchGroupEnabled`函数开始处理同样取点。两源pending只保存这一次deadline。 |
| 源等待定时器 | QTimer按`max(1, sourceDeadline-now)`安排唤醒，不再固定从预检发出时重新起10秒。若前置检查已经耗尽期限，稍后真实时钟检查仍拒绝，定时器延迟不会延长资格。 |
| Root转发 | 两个原固定来源路由原样接第三参，并以`Q_ARG(qint64,sourceDeadline)`传SampleStore；不读当前时间、不改截止值、不阻塞等后台。 |
| 预算和真实读取 | `reserveOriginalProjectSave`入口要求deadline仍未到；完成真实项目重读和身份/版本核对后、预留前再次核同一值。所有读取和跨线程排队消耗剩余时间。 |
| 预留与构造 | reply原样带sourceDeadline。permit将其保存为const，内部期限为`min(sourceDeadline, now+10000)`，只可能更早。刚预留后构造时若已到期，会立即释放reservation，reply不带permit、不置allowed，不漏一个未登记占位。 |
| 回复确认 | 两源核pending、Admission和permit的deadline完全相同，再核当前单调时间和permit.expired；错ticket/旧身份/root/文件/版本仍按原规则撤未认领许可并保留草稿。 |
| 文件锁内最终认领 | 原仓库/Editor取得锁、完成原版本与业务验证后，原claim回调仍调用这一permit。`claim`在Granted→Claimed比较交换前再执行expired，检查的仍是同一源期限，不会从回复或取得文件锁时重置。 |

所以旧反例“预检9秒后返回，再做1.1秒仓库验证”现在会在最终认领处因原请求已过10秒而拒绝；仍未开始SQL写入，不会把第10.1秒当成另一轮新窗口。

取点准确范围是**来源后台存储槽开始处理请求**，不含点击按钮到该槽之间的GUI/线程排队或参数交付。handoff及新注释已明确这一差异，不能对使用者宣称点击后10秒的硬实时保证。最终时钟检查是软件认领前检查，未给操作系统调度新增实时保证，也不用于跨进程恢复。

## 已认领和提交事实未受损

完整delta只改变期限值、参数转发及到期预留收尾。许可的revoke仍仅Granted→Revoked；SampleStore清理仍不删除Claimed/Committed。没有给已Claimed事务增设到期回滚、角色变化取消或关闭中断；它继续执行原SQL结果，失败未COMMIT则releaseUncommitted，实际COMMIT则保留完整notice，进入统一消费者后才Accepted并释放预留。

Repository、Editor和OriginalDeviceSync与769f实际字节相同，故原事务、写前备份、锁内copy写保护、关闭写连接后的快照核对和commitSeen设置点未改。两个源的commit/post回调未改：提交后重读失败仍发commitSeen=true、verifiedSnapshot=false，不按失败丢失已提交事实。Root只增加第三参数转发，原固定来源、取消、提交通知及CAL桥没有回退；没有增加deviceReady门、发新仪器命令或清Us/任务未知。

本窄修可供总协调按后续实际基线整合；普通保存、试剂组开关、取消/晚回、提交后重读失败及真实Up/UP仍待获准RK和原资料/主控验证。N的下一包装修正属于另一个候选，不混入本结论。
