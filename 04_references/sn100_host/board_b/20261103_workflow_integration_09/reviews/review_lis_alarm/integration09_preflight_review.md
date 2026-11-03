# 第09批预合并的LIS与当前告警复核

2026-11-03，只读检查总控的`integration_09_review_01/preflight/06_programs/sn100_host/`，未写正式或作者候选，未应用补丁、编译、运行、测试、访问SQL或操作板卡。

**结论：所审LIS、当前告警与公共接线范围没有确定阻断，可晋级到正式串行合入和完整ARM64构建。**这不关闭扣退量/QB其他独立审查问题，也不代表板端软件、真实LIS/告警、实体操作或资源通过。

## 本次绑定

- `staged_source_manifest.json`：684份，SHA-256 `f947c2ac3f95609da27c53c88b20fd7293734fff15a465d74ae107996df180ec`。
- `staged_patches.json`：10片，SHA-256 `bb3896e9115038344fdf691a48890a25fa39a55fb24aa26b1df04d37d370ca68`。
- 将684份清单逐一与预合并文件实际SHA比较，缺失0、指纹不符0。没有整份JSON输出或装入聊天。

## 逐项结果

| 核对什么 | 实际结果 |
| --- | --- |
| LIS业务 | 6目标逐字节等于冻结作者候选，无额外变化 |
| 告警模块 | 9目标逐字节等于冻结作者候选，无额外变化 |
| LIS公共文件 | 只在`main.cpp`、`frmmain.h`、`build.sh`出现其他片预期变化，其他公共目标逐字节一致 |
| 告警公共文件 | 只在`main.cpp`、`frmmain.h`、`build.sh`、`device_control_shell.cpp`、`shell.pri`出现预期窄合并变化，其他公共目标逐字节一致 |
| QB v3保护 | `frmmain.cpp`、`login_shell.cpp`逐字节等于作者v3；`device_control_shell.cpp`到最终只有新增告警include和唯一Qk守卫，QB开始/暂停守卫和原主状态选择未改 |
| `shell.pri` | 原112处CRLF/119处LF，最终114处CRLF/123处LF；告警两行沿原CRLF插入，扣退量三份头登记仍在，未整份归一行尾 |
| 构建登记 | LIS控制CPP/Q_OBJECT头/值头，告警三个CPP/观察Q_OBJECT头，扣退量三头均在手工ARM入口的输入出口之前；相应`.pri`登记仍在 |

`main.cpp:706`的当前告警启用与`:1441`起LIS控制器创建/状态、消息及退出核查都保留；LIS候选到预合并main的唯一变化是前者五行启用代码。`frmmain.h`同时保留LIS状态接口、当前告警观察成员/默认10000毫秒、QB用户交互与开始确认守卫。

## 唯一Qk与实际范围

预合并告警按钮仍有两个既有连接位置，但条件互斥：`alarm_shell.cpp:59`在有观察对象时走`queryCurrentAlarms()`；`device_control_shell.cpp:105`仅在`!currentAlarmState_`时保留旧只读查询。新链唯一排Qk在`alarm_current_shell.cpp:89`，发送前保留有效连接/新鲜Qj/灯状态及本地等待守卫，没有从告警窄合并删掉QB路径。

`package_arm64.py:133`新增`current_alarm_scope`与实际代码相符：明确Qk查询、Qk/QK接收、不由查询写历史、不把QK当Qk确认、本地等待10000毫秒、无自动重发、无复位/蜂鸣器确认、真实告警未验。LIS元数据附片同时保留原资料首次link/保存开启及串口/业务ACK/下载上传未完成的说明；未把两种通信范围混为08仅Qj检查。

## 仍须由总控完成

正式合入时重核前像和每片指纹，保留原第07告警公共补丁拒绝结果及此次窄合并来源。随后执行完整ARM64编译、moc/链接、完整输入与依赖核对、上传SHA和RK原条件验证。

原674表外`tests/test_shell_arm64_inputs.py:271`旧LIS发布scope断言在本预合并源表之外，本次未把它认作已更新或已通过；总控如维护该源码核查合同，仍应保留并更新明确预期。运行新LIS版本前仍须核实际INI端点/角色/编码及当次获准动作；10秒等待只是本地提示，真实消息分支不足保持待验。
