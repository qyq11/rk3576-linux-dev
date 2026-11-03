# 重连来由记录丢失：v2修复交接

原 `device_reconnect/` 的686f补丁、67eb清单、对象和记录保持原样。本目录只修独立审查指出的P2，不改正式4e、未知保护或恢复业务。没有运行产品、数据库、网络、模拟端、RK或主控。

固定交付：累计片 `99dd8eda52f99e7e533e4fe5b3b0a95004c01dc703a68474bee16e228fef97c6`（33610字节）；相对686f两目标增量 `3ee3bfd51f8073ef485a12b08d9c378c2a5e9f6febc40956e715e8e97027a79d`（5709字节）；清单 `0aa51283b822814cb8ca5eac7f7b1062368f0e4cd115358f704be4a37c82fefc`。累计对正式、增量对686f候选的 `git apply --check --whitespace=error` 均为0，见 `patch_checks.log`。shell对象编译退出0，前后输入指纹不变，最终小端AArch64对象SHA `87bd1ecf92824a2641e872966ede49ed3dff1b2d5fad948117fda62086d735f2`。

确定问题是：断线先降低动作就绪时，会同步结束优先使用并清空请求/孔位/阶段/报文；随后后备路径先写只有旧代次的记录，挡住后来的完整封存。原来未知标记仍阻止业务，但承诺的可追溯来由丢失。

修复只有两个目标：`shell/device_control_shell.cpp` 与 `shell/frmmain.h`。新增普通私有方法 `rememberReagentRecoveryContext()`，统一写第一份完整受限来由。在 `finishReagentPriority(resultUnknown)` 中，先登记旧未知代次，立刻封存仍完整的请求、孔位、阶段及摘要，然后才清状态。`invalidateDeviceTaskContext` 与跨代次后备路径调用同一函数，不再写残缺占坑记录。已有记录不覆盖；成功结束或Preparing且明确未可能入队，不新建未知来由。

源码顺序已核 WaitingReply、SavingReply、WaitingPage 的断线、超时、状态失效及页面取消，均先进入统一结束函数再清字段。未知布尔值、后台一次许可撤销、10秒期限、业务阻止、代次规则和不重发行为不变；没有新增解除入口。所有原686f未完成恢复范围继续适用。

`device_reconnect_v2.patch` 是相对原正式底稿的9目标累计片；`delta_from_686f.patch` 是相对686f候选的两目标修片。总窗口按当前基线选择其中一个，不能两个重复应用，不能整文件覆盖。`candidate_manifest.json` 同列正式底稿、686f版本和v2候选指纹。

仅重编 `shell/device_control_shell.cpp` 的ARM64对象。新增方法不是Qt槽/信号，不改类字段布局或元对象接口；其他686f对象仅按固定指纹保留，不冒称重新编译。对象编译不代表通知重入、界面或真实仪器通过。实际这些行为仍待获准RK/主控验证。
