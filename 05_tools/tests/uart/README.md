# 串口状态检查与UART4回环实验

读取串口状态，并在接好排针跳线后逐路验证发送和接收字节。配置/打开检查与实际引脚收发分别登记。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [uart4_loopback.c](uart4_loopback.c) | 默认/dev/ttyS4，可用`--port`选择其他待验串口；115200/8N1，关闭回显和控制器内部回环，逐条打印并比较实际TX/RX。必须先接好对应排针跳线；本次新版本仅ARM64编译及上传通过，尚未运行。 |
| [inspect_uart_pinmux.sh](inspect_uart_pinmux.sh) | 只读检查开发图中的UART3、4、7、8、9：读取板卡身份、设备节点、实时设备树、引脚占用和访问权限；不打开串口、不发送数据、不改配置。 |
| [uart_access_check.c](uart_access_check.c) | 打开五个串口并读取当前通信参数，不收发业务数据；已在板B由root执行，5项打开通过，不能据此认定实际引脚通信通过。 |

只读脚本可由电脑通过SSH传入板端运行；`--sudo-login`仅用于在交互窗口现场输入管理员认证，以读取受限状态。没有`dtc`时直接读取实时设备树属性，不为检查安装工具或挂载调试目录。2026-11-02已在[板B实际执行](../../../04_references/uart/board_b/20261102_initial_audit/README.md)，SSH和采集标记均为0；这只表示该次状态读取完成，不表示串口通信已通过。

接线、编译及历史收发见[串口记录](../../../02_records/20261021_TTL串口调试验证.md)。本机回环和Windows到RK串口通信是两个不同验证范围。

板B目前的[系统配置与root访问](../../../04_references/uart/board_b/20261102_configuration/README.md)已核对；用户当前不方便接线，按开发图逐路确认实际通信的[五组跳线与待验表](../../../04_references/uart/board_b/20261102_header_loopback_pending/README.md)继续保留。板A历史回环结果不套到板B。
