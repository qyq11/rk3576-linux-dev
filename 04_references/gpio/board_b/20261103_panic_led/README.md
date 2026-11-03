# 板B一次内核崩溃、心跳灯观察及恢复证据

2026-11-03，北京时间。用户明确要求实验，确认设备未在动作，并授权协调总管理聊天预留板B。硬件序列号`8314b1e8ffd0321f`。本次仅一次受控`panic`，先同步磁盘，临时设置崩溃后等待60秒重启，不改永久系统设置。

**现场结果：用户反馈“崩了就没闪了”，恢复后反馈“已经重新闪烁”。** 未明确反馈崩溃时是常亮还是熄灭，也未明确反馈Power灯状态。完整过程、前后状态和验收范围见[原GPIO事项记录第8节](../../../../02_records/20261021_gpio_output.md#board-b-panic-led-20261103)。

| 文件 | 用途与核对方法 |
| --- | --- |
| `before_state.log`、`after_state.log` | 前后身份、启动标识、心跳设置、七份配置指纹、版本指针、进程和文件系统状态。两次SSH均退出0。 |
| `trigger_ssh.log` | 临时`kernel.panic=60`、同步完成、触发前时间及SSH因崩溃失联退出255。触发前后的输出可能来不及通过网络全部发出，实际崩溃以保留内核日志核实。 |
| `recovery_monitor.log` | 逐次连接超时，以及16:18:27进入新启动后的成功读取。 |
| `panic_proof.log` | 从保留内核日志中读取的本次SysRq崩溃记录，以及恢复后的系统、挂载和版本指针。 |
| `recovered_led_samples.log` | 下载后两端崩溃日志指纹，以及30次亮度状态读取。软件状态有0/1变化；光学现象由用户另行确认。 |
| `comparison.txt` | 前后七份配置文件的指纹文本比较结果为True。 |
| `preexisting_pstore/console-ramoops-0` | 实验前旧的保留内核日志，防止本次新日志覆盖此前重启线索。SHA-256为`3c6e9c361c88efe063768f71a003993998ec29fa0e5731c97788fe72ba20af37`，与取回前板端一致。 |
| `recovered_pstore/` | 本次重启后取回的`console-ramoops-0`、`dmesg-ramoops-0`原文件，逐份指纹与板端一致。 |
| `capture_state.sh.txt` | 前后只读采集脚本的原文。 |
| `trigger_once.sh.txt` | 本次实际发送的崩溃脚本原文，含板号、旧启动标识及业务空闲检查；仅为过程证据，请勿作为日常操作运行。 |
| `SHA256SUMS` | 本目录其余文件的SHA-256清单，供后续确认内容未变。 |

恢复后`kernel.panic`恢复0，其余已核参数和七份配置保持；当前程序仍为08版，实验前后的业务程序都未由本聊天启动。板A未操作。总管理聊天已收到恢复结果和交还通知，后续部署与业务验证由其按原范围继续。

机制依据：[Linux内核SysRq文档](https://www.kernel.org/doc/html/v6.1/admin-guide/sysrq.html)说明`c`用于触发崩溃；[内核panic参数说明](https://www.kernel.org/doc/html/v6.1/admin-guide/sysctl/kernel.html#panic)说明正数为崩溃后等待重启的秒数。操作前还核对了本地厂家内核中的实际实现。
