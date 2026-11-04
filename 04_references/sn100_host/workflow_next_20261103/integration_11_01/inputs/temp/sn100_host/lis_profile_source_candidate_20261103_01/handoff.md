# LIS同源配置快照候选交接

这片只让通信业务拿到同次设置读取所确定的自动查询开关和原资料目录。它没有接通任务下载，也没有新增查询、保存、确认或上传动作。正式源码、公共主窗、构建、正式计划记录及板端均未修改；由总控统一复核、整合和安排验证。

底稿为687源冻结表 `temp/sn100_host/workflow_next_20261103/integration_10_source_acceptance_01/ascii_b1/source_after.json`，SHA-256 `f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b`。创建候选前实际核了两目标前像，均与该表相同；没有复制或宣称重扫整个正式工程。`base/`保存这两份精确前像，`candidate/`保存两份候选，`baseline.json`保存来源与字节数。旧候选和原消息保持。

| 相对正式工程的文件 | 底稿SHA-256 | 候选SHA-256 |
| --- | --- | --- |
| `lis_transport/lis_connection_values.h` | `c9d47a9f3049470862b7a5b3db88e2473a3acfa5e7217619ab64baa4a63ac1ba` | `41bb19561f6b3996107470107a59b04b01df1310fa1afb5abf16caf9a79b2051` |
| `lis_transport/lis_connection_controller.cpp` | `f403e0470636551fedc2611bb204522306938b493462b9d5fe8d3126407ba1e3` | `7ca09f58d332ac20bf0f7d234c7fc4081a5acddadf646484bba11d81f69c7b61` |

Windows依据仍是只读参考工程 `06_programs/sn100_host_reference/Seerain450_PC`：`communication/commandreceiver.cpp:342`按自动获取开启且LIS实际已连接选择查询；`commlis/liscommsetting.cpp:152`只认自动获取原字符串`true`。`frmmain/frmmain.cpp:277`的`link`只用于原启动连接选择，不替代查询门槛。本片仅补足设置来源，尚无生产查询消费者使用新增开关。

现有正式设置层 `data/lis_settings_store.cpp:401`确认原目录的规范路径；`:153–180`从一次成功的原配置快照取得目录、指纹、自动查询开关及序列号。候选直接保存成功回传的 `settings.originalRoot`、`settings.autoGet`，不重新查目录、不读文件、不增加哈希、不增加Store getter。`receiveCommitted()`在传输配置成功并确定完整配置身份后，同次构造不可变 `ReceiveProfile`；新接口为 `autoGet()` 和 `originalRoot()`，原 `configuration()`、`sequenceNumber()`、`matches()`保持。

`ConnectionState.profile`与最近一次发布的配置状态同行，仅表示来源。发布条件为可信、非配置采用中、非关闭且profile与配置四项一致；不增加opening/stopping条件，以免同步开启发布后永久缺少快照。`state()`仍是最近发布值，转换中可能暂时保留旧状态；它不授权发送。真正排队仍由原 `sendMessageWithReceipt()`核实时标志、配置身份和代次。autoGet没有加入通用发送、接收或保存许可，原编码、分帧和发送门槛不变。

序列号保留原4096个UTF-16单元的上限，新目录文字同样最多4096单元；每单元2字节，非BMP字符可能占两个单元。超限先拒绝采用，不截短序列号或路径。最多16个存活版本，两个文字字段合计逻辑内容最多262144字节，即256KiB；这不包含配置身份文字、QString容量、对象、共享指针及容器开销，不是RSS或整个快照内存上限。弱表按版本对象计数，同一profile在状态、消息、回执中的多份共享引用不重复计版。

同值可信普通loaded继续保留原profile及原来源指纹；成功saved即使值相同仍建立新身份。长度或容量拒绝沿原保守停止路径：先撤本次active，再使配置失信并停止；已交出的旧消息、回执仍持原不可变来源。正常重配置/失败/关闭发布会替换或清空控制器内部state.profile，不新增永久历史队列；关闭不等待外部纯值释放。同步回调可重入时，原operation、stopping/applying/opening及关闭守卫保持。

`lis_profile_source.patch`是仅两源的普通补丁，SHA-256 `7fd8567ece6c4f938f2acc210c2e606abecaae4fdf7b91f9870e3fdd9ad23bdf`。在独立 `ordinary_replay/` 内执行普通Git apply --check及apply，实际均0；重放后两份源与候选字节相同，正式两份仍与前像相同，见 `verification.json`。`source_before.json`、`source_after.json`只列本片两源；`composed_frozen_687_after.json`由原f741表加这两处候选指纹合成，仍为687项，不能当成对当前正式全树的重新扫描结果。

两Agent分别核同源字段/预算与同步重入/关闭；最终两SHA均核对一致，未发现确定的P1/P2问题，结论及实际范围见 `source_review.md`。没有编译、产品运行、模拟报文、网络、RK或真实LIS验证。本片新增接口在正式入口中尚未使用，不能报告真实任务下载已经接通；已有c7的被动软件观察不涵盖本候选。

请总控独立复核这两源后决定整合和编译。后续任务接口与查询闭环的待审内容在 `dependency_contract_revision_02.md`；当前不实施ticket、第二消费者或生产QRY。正式计划与移植记录由总控据实际结果统一更新。
