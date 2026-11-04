# 公共生命周期组合修订02定向复核

只读复核 `temp/sn100_host/workflow_next_20261103/root_combined_candidate_11_02` 相对01的两个主窗口文件，以及必要的真实发送入口。01报告和原候选保留；本报告不覆写01发现的事实。没有修改正式或候选源码，没有执行补丁、构建、产品、SQL、模拟、网络或板端操作。

结论：01中“单孔探测的通用动作门同时挡住通讯查询和静音”的P2已在02源代码中关闭。此次增量未发现新的确定阻点。结合01已完成的生命周期和构建登记审查，可按本次固定后像继续由总控安排源码接纳。这里仅为静态源审通过，未完成编译、RK运行或真实仪器验收，也未验证当前留驻程序的运行状态。

实际改动与门的位置如下：

- 通用 `deviceUserControlBlocker()` 第153—168行删除了探测条件，恢复原连接代次、已连接、交互、关闭、登录和按需fresh检查。没有为了放行查询而删除真实身份、连接或关闭保护。
- 第170—176行新增私有 `reagentProbeTaskControlBlocker()`，只读取探测owner/Unknown并返回开始、暂停的阻止原因。头文件第286行有唯一匹配声明；没有新增发送、清理、确认或复位动作。
- 手动 `Qj/Qk/QH` 仍走原 `manualCommunicationBlocker()` 再调用 `query()`。该blocker最终仍检查通用身份/连接门，但不再遇到探测门。手动和顶部 `Mz` 仍走同一暂停/静音入口中的 `pause=false` 分支，两个新增探测检查均要求 `pause=true`，所以不会挡住静音。原DeviceControl发送层保留Qj/Qk/QH/Mz例外，Unknown事实不会因查询或静音被清除。
- `detectionStartBlocker()` 第556—559行改为调用新动作门；开始确认前、确认后以及真实User准备退休后的最终发送都仍调用该blocker。因此普通开始、实体键选中开始/恢复、手动Ts和准备期间出现探测均受同一门约束，没有只禁按钮却漏掉最终发送。
- 共享 `requestDevicePauseOrSilence()` 第295和310行在暂停确认前后各核一次新动作门。确认框中的嵌套事件循环若启动了探测，确认返回后仍拒绝旧暂停；Mz不套用该条件。发送尝试仍在真实发送调用前登记，false仍可能表示已入队，没有改成“确定未发送”。
- 实体键真正选择暂停的分支第458—460行单独核新动作门，再核原pauseBlocker；实体键选择动作的原机型/状态条件没有变化。当前shell中没有发现这份CPP之外另一个 `deviceControl_->start()/pause()` 调用入口。
- 第511—518行把同一探测原因用于暂停按钮禁用和提示；开始按钮继续使用detectionStartBlocker；静音按钮的原启用条件保留。

相对01只改动 `shell/device_control_shell.cpp` 和 `shell/frmmain.h`，其余15份后像实际SHA完全相同。01已核的原请求号/会话代次、取消顺序、User04真实finished后退休唯一出口、探测租约的调用栈末尾归还、N真实保存/页面采用/reader返回、构建头登记均未被此次修订触碰。新动作门也不修改这些所有权或完成事实，因此没有新增相互假结束或误清新请求的接线。

本次独立重算了17份候选、17份私有回演后像，均与02清单相符；已读取包装的真实命令和退出原件，普通 `git -c core.autocrlf=false apply --check` 与apply均为0，未在本审重新执行。02预期688表等于f741的687表覆盖本片17项后的字典，仍只是合成预期表。本次核到16个受影响正式前像仍等f741，正式新头仍不存在；不把这些局部读取说成正式688全源扫描。

| 对象 | 实际SHA-256 |
| --- | --- |
| 02组合CPP | `88ecd1b79de875f4e0b40385df57f077aba231a42de21c23d09ef697a08e7ee2` |
| 02组合frmmain.h | `1250f9e536f874ee97f2f2c5f9ebc6b2ff012481bdd435d06968df6f7f70ad50` |
| `sparse_after_17.json` | `dc17f3fb585617e3f6c3f34cf099c0cec5980d1ebbd21646f31da1efa1edbdf7` |
| `packaging_01/mg04_lis_profile_on_f741.patch` | `cabddb143bedcf0602b1a40627b1501a66993c1c989673a215a57eda3dbc8f74` |
| `composed_expected_after_688.json` | `b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767` |
| `packaging_01/acceptance.json` | `da3573244966cc061d8f7f284edd500280fc1564d4396026c9f361cb3eddcba0` |

下一步的实际验收应分别保留：ARM64完整编译；RK探测中/Unknown时查询与静音仍可按原权限使用、开始与暂停被拒绝；确认期间进入探测不漏发暂停；手动Ts真实后台退休后只尝试一次发送；真实Mg与N提交、页面采用、reader返回、取消/断连/关闭及同板资源影响。没有真实条件的项目继续待验，不以本报告代替。
