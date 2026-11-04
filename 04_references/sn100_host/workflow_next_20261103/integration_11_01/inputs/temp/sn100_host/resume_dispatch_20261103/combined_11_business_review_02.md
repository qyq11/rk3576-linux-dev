# Root组合11修订02：查询/静音与探测动作门窄补审

2026-11-03，独立审查者`/root/review_amount`。只读`root_combined_candidate_11_02`相对01的两份主窗真实差异和必要调用片段；其余15份只核字节不变，未重审业务正文。未改正式/候选、执行Git、构建、产品、SQL、模拟、网络或板卡动作。仅新增本报告，01原件及其不放行报告保持。

**结论：01发现的交界P2已在02源码中对应闭合，本次定向范围未见新的确定阻点，可供Root继续接纳步骤。** 查询/静音恢复原B1合同；所有本次核到的真实开始/暂停仍受探测在途/未知保护。本结论仅是源码补审，不代表已合入、编译、RK、仪器或资源通过。

实际绑定，路径相对`temp/sn100_host/workflow_next_20261103/root_combined_candidate_11_02/`：

| 对象 | SHA-256 |
|---|---|
| `candidate/06_programs/sn100_host/shell/frmmain.h` | `1250f9e536f874ee97f2f2c5f9ebc6b2ff012481bdd435d06968df6f7f70ad50` |
| `candidate/06_programs/sn100_host/shell/device_control_shell.cpp` | `88ecd1b79de875f4e0b40385df57f077aba231a42de21c23d09ef697a08e7ee2` |
| `packaging_01/mg04_lis_profile_on_f741.patch` | `cabddb143bedcf0602b1a40627b1501a66993c1c989673a215a57eda3dbc8f74` |
| `sparse_after_17.json` | `dc17f3fb585617e3f6c3f34cf099c0cec5980d1ebbd21646f31da1efa1edbdf7` |
| `composed_expected_after_688.json` | `b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767` |

本人逐行比较01/02：实际仅`frmmain.h`、`device_control_shell.cpp`不同。17份实际SHA均等02稀疏清单，其余15份逐字节等01；Mg04、LIS两源、新头登记和原唯一冲突的两条撤销没有借本修订被替换。补审承接01报告`01cf17466ba1f5368d87597f9adb3db74afc7b8a7be43731d7350ea9090bf706`中已核字节/范围，不将01当时的P2回写为首次通过。

1. 通用身份门与动作专门门已分开。

   CPP第153–167行`deviceUserControlBlocker`保留原连接代次/connected、退出/登录交互、关闭、已认证身份及按参数选择的fresh要求，移除了无条件probe检查。新增私有`reagentProbeTaskControlBlocker`只在`reagentProbe_ || reagentProbeUnknown_`时给出开始/暂停拒绝。头文件只增加这个私有声明，没有新增可外调raw发送入口。

   B1的`receiveManualCommunication`精确正文初筛、`manualCommunicationBlocker`及query分支仍沿原管理员/页面/会话/连接检查，且仍仅认Qj/Qk/QH/Ts/Tp[0]/Mz。Qj/Qk/QH到第280行原query，不再经过probe动作门；Mz在共享确认方法中`pause=false`，确认前后均保身份/连接/关闭与手动原请求复核，不加fresh或probe开始专门门。主窗静音仍复用同一路径，原确认No不发。

2. 真实开始/暂停路径仍受同一probe保护。

   - 按钮、QB选择开始和B1手动Ts均到`requestDeviceStart:322起`，确认前第338行、确认后第355行都调用`detectionStartBlocker`；该方法第558–559行先调用新的probe专门门。真正User准备退休后，唯一实际`deviceControl_->start()`在第414行，仍先在第408行重新核同一detectionStartBlocker，没有退回旧prepareDeviceTasks或旧callback直接发Ts。
   - 按钮和B1手动Tp[0]走`requestDevicePauseOrSilence(pause=true)`，第295、310行分别在确认前、后核probe专门门；之后保原pauseBlocker及原请求身份检查才到第315行pause。没有借开始专门的任务/N/C门替代真暂停。
   - QB的实际暂停分支第458–462行显式核probe专门门，再核原pauseBlocker后调用pause；仍沿原主状态/灯态选动作，不因禁用开始而改选暂停，也没有加确认框。
   - 第513、517行暂停按钮可用性和提示使用同门；开始按钮仍使用detectionStartBlocker。按钮显示与实际发送前保护没有分离。

3. 未清未知、未扩范围。

   两文件实际diff没有修改probe unknown、业务/事务unknown的写入/清除、维护白名单、DeviceControl发送门或Mg/N/reader收尾。查询与Mz只是恢复可按原合同调用，不会确认旧探测执行结果。其他15份保持01原字节，因此没有纳入User16、Uh、整盘/扫码或新的None/Unknown单孔分支。

02 acceptance原记录为普通check/apply 0/0及17份回放相等，Root另报告build.sh bash-n 0；本人只读这些记录，未重跑或将它们登记为编译。688表仍是f741叠加本组合的合成预期，不能作为正式已实际晋级或已生成新产物的证明。Root后续应按实际接纳、完整ARM64构建、RK及真实仪器结果分别登记，c7旧结果不转借本组合。
