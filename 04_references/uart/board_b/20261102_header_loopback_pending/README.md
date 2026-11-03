# 板B五路实际排针收发：等待接线

2026-11-02，用户明确要求按开发图逐路核对通信和引脚关系，并告知当前暂时不方便接线。**以下五路都没有进行实际排针回环测试，全部待验。**已完成的[系统配置及root打开检查](../20261102_configuration/README.md)与本项分开登记。

## 接好哪些线才能继续

先断电并断开外接串口设备，只在RK板40针排针上短接下表各组的发送和接收脚，各组之间互不相连，接好再上电。这是RK原生信号侧回环，不在RS485的A/B线上短接，也不连接供电脚。

| 串口 | 发送脚 | 接收脚 | 要短接的两个物理针号 | 实际收发状态 |
| --- | --- | --- | --- | --- |
| `/dev/ttyS3`，第一组采集 | 5，`GPIO3_A0` | 3，`GPIO3_A1` | 3—5 | 待接线 |
| `/dev/ttyS4`，样本扫码 | 8，`GPIO2_D0` | 10，`GPIO2_D1` | 8—10 | 待接线 |
| `/dev/ttyS7`，试剂扫码 | 28，`GPIO2_B6` | 27，`GPIO2_B7` | 27—28 | 待接线 |
| `/dev/ttyS8`，第二组采集 | 37，`GPIO2_A6` | 40，`GPIO2_A7` | 37—40 | 待接线 |
| `/dev/ttyS9`，磁珠采集 | 26，`GPIO2_C1` | 29，`GPIO2_C0` | 26—29 | 待接线 |

原始分配依据是[用户开发图](../../../../03_photos/06_设备整体架构说明/20261027_rk3576_interface_development_plan_01.png)。针号是排针的物理位置，须按实物针1标记定位；内核中的GPIO编号不能直接当排针针号。

## 已准备的程序与后续怎么核对

[回环程序](../../../../05_tools/tests/uart/uart4_loopback.c)已交叉编译为ARM64，并上传到板B的`/home/rkshare/programs/uart_setup/uart_header_loopback`。电脑与板上SHA-256均为`500c368602102f1200b6f2186a2f5c9759d4e4ba0cb8b597433454e089bc545d`，动态库检查通过。**没有执行这个程序，也没有向五路发送测试数据。**

程序准备为115200、8N1、无流控，关闭软件回显和UART控制器内部回环，要求数据经外部跳线返回。它逐条打印TX/RX原文与十六进制字节，核对`UART_TEST_0001\r\n`至`UART_TEST_0100\r\n`；每路100条，发送和接收应各1600字节。测试结束恢复原通信参数。115200只是本次回环测试速率，不作为真实外设默认配置。

用户确认接线及上电后，再以root逐路执行，分别保存实际输出、退出码、测试前后驱动收发计数和针号；任一路失败都单列保留，不把其他路或板A结果套用。以下命令是准备好的后续步骤，当前没有执行：

```bash
cd /home/rkshare/programs/uart_setup
timeout 30s ./uart_header_loopback --port /dev/ttyS3 --frames 100
timeout 30s ./uart_header_loopback --port /dev/ttyS4 --frames 100
timeout 30s ./uart_header_loopback --port /dev/ttyS7 --frames 100
timeout 30s ./uart_header_loopback --port /dev/ttyS8 --frames 100
timeout 30s ./uart_header_loopback --port /dev/ttyS9 --frames 100
```

五组排针回环全部通过后，再断电拆除短接线，按实际转换电路接RS485/RS232设备；确认方向控制、速率、校验和报文后验证真实扫码/采集。原生UART回环通过也不能代替转换电路和真实设备通信验收。

本目录的`preparation.json`记录待验状态，`staged_only.log`仅为上传校验与动态库输出，`uart4_loopback_prepared.c`是待测源码快照。编译首轮因C库未导出内部回环标记名称失败，按匹配Linux内核公开定义补充常量后ARM64编译通过，首轮失败保留；尚未进行新版本RK收发测试。`SHA256SUMS`可用于核对这些准备材料。
