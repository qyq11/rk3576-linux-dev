# 单项目同步 v2：两项修正的独立只读复核

**原终审两项 P2 在本版源码中均已修正；本次限定修点及受影响链未发现剩余阻断或新的确定问题。**结论仅适用于下面固定指纹，不代表整体功能、正式06整合或仪器验证通过。

本轮只新增本报告，没有改源码、候选、正式工程或旧固定报告，没有编译、运行、读写业务数据库、使用网络或操作板卡。没有重做整套功能审查，也没有接手普通项目/组开关生产端。

## 指纹核对

**2026-11-03 00:35:31 +08:00** 直接读取核对：

- 四目标增量 `delta_from_250b.patch`：9539字节，SHA-256 `68b5f47ece7f544a8200a792ec15ee8baaff2876a9de93911f9ce3139eb9bfd9`。
- 累计 `project_update_intents_v2.patch`：77014字节，SHA-256 `e66eda425bb8097e979161b14d1daf20d05b78ce29c15381b2ba8a750b8162e1`。
- `candidate_manifest.json`：SHA-256 `17a18965d2205911ca2ed979833f708aedeced4cf6d6631b62f64c9a9fb23a4d`。
- 13个候选文件的SHA和字节数逐项匹配清单；13个旧250b源文件逐项匹配 `previous_250b`。实际仅4个目标变化：`original_device_sync.h/.cpp`、`original_sample_store.cpp`、`device_control_shell.cpp`。
- 4个实际对象文件的SHA及字节数逐项匹配清单，包括三个改动CPP对象和 `sample_store.cpp` 依赖对象。本轮未重新编译、验证运行或重跑历史输入守卫。
- 旧250b补丁、65c32清单和旧终审报告d9e036dd均保持原固定指纹，没有以修正文档覆盖旧问题记录。

以下路径以本目录 `candidate/` 为根。

## 明确未attempt的迟到回执

`samples/original_device_sync.cpp:337–359` 保留同intent、同当前连接匹配，新增三项独立事实：是否收到入队结论、是否可能已经发送、unknown是否只因等待入队结论超时。新批次在prepare时重置；连接失效撤掉这一超时证明入口，不借它解除另一连接的真实未知。

- prepared尚无结论，或unknown明确属于“未收到该批入队结论的超时”，才可接权威 `attempted=false,queued=false`。这条否定事实可以晚于10秒；记录 `not-attempted-proof`，没有新保存则停在not-sent，不自动重试旧请求；有新明确保存才取代旧冻结。
- attempted=true先记录可能发送事实；晚正回执保留unknown，不恢复waiting。真实已尝试未知、UP超时或匹配/版本失败后，后来的false不能解除。
- `attempted=false,queued=true` 的矛盾回执被拒绝；错intent、错连接、已知结论和已有可能发送事实均不接受此证明。
- `expireProjectUpdate:385–387` 只给prepared且尚无入队结论的超时打开上述入口。迟到Up的Root期限检查、迟到UP的worker单调期限及回写闭环检查仍在，没有把10秒期限延长。

Root 的 `shell/device_control_shell.cpp:914` 起只在 `not-attempted-proof` 时比较旧unknown的intent、连接、root/文件键、来源、源请求号及前后SHA，全部相同才结清这一来由。该分支不把参数设为确认，不覆盖新保存等待，且立即return；Us/优先使用未知及独立引用待核属性均未被清除。

静态分支已覆盖原反例的两种顺序：计时器先将prepared置unknown后false到达；false先处理但elapsed已经超过10秒。两者都能保留确未发送事实；真正可能发送的unknown仍被隔离。未实际运行这些分支。

## 初始UP提交事实传到刷新层

`samples/original_device_sync.h:14` 为Step增加 `commitSeen`。`projectReply` 129行立即复制DeviceRepo的实际提交事实；即使锁内快照核实失败，返回false时仍保留该事实。成功后调用 `nextProject` 会重置Step，148行再恢复当前UP的commitSeen，覆盖“本项已提交、下一项冲突失败”的窗口。

`samples/original_sample_store.cpp:277–288` 只要commitSeen即尝试重读；成功时更新本地快照，本线程重读失败也发 `originalProjectsChanged` 让其他读者尝试读取。原同步success为false不会因刷新成功转true。295行仅成功且非ignored才续发；299–303行在失败时保留已提交说明、停止等待并发布未就绪，没有自动重试失败的初始同步。

编号变化的引用待核仍在 `projectReply`、`reconcileInitialProject` 与 `referenceChangeState` 中独立保存；`invalidate` 不清掉它。新刷新通知不会被当成UP确认，也不生成新的本地保存意图或Up循环。

## 整合及验证边界

两修均沿原单一同步消费者完成，没有重做生产端或另建恢复系统。仍保留原全文件 `canStart`、已完成写锁内快照结果、实际未完任务检查和其他试剂未知保护；本轮只核上述改动对这些边界的影响，不重新给未改路径签完整验收。

本累计片基于旧captured底稿；Root的06temp已另叠copyv2与CAL桥。是否用增量/hunk保持那些叠片由Root整合核对，本报告没有对Root06整树做适用检查，也不以全文件覆盖替代。普通项目/组开关后续生产端仍按已列分工，不作为本两修的新阻断。

此报告为固定源码修点复核。原四对象编译记录保持原范围；没有新增Windows、RK、真实主控、并发恢复、资源或完整功能通过结论。
