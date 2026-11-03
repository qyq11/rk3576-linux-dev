# 板B五组串口配置与root访问核对

2026-11-02，用户要求直接用root账户连接并配置，随后明确要求逐路验证实际通信和引脚对应关系。**本目录记录已通过的配置、重启、引脚分配和打开检查；实际排针通信仍待接线，不能把5项打开成功说成通信通过。**用户当前不方便接线，[后续逐路收发准备](../20261102_header_loopback_pending/README.md)单独保存。

## 实际改了哪些地方

对象是板B，硬件序列号`8314b1e8ffd0321f`，通过`root@192.168.100.123`登录，实际用户编号`uid=0`。没有更换SSH认证方式，没有修改`lckfb`用户组，也没有改变应用启动用户。

修改前未发现I²C7/8登记的外设或进程打开`/dev/i2c-7`、`/dev/i2c-8`、`/dev/spidev1.1`。按开发图关闭I²C7、I²C8，释放SPI1第二路片选及其`spidev@1`，启用UART3/7/8/9；保留UART4、SPI第一路及CAN0配置。没有扫描总线或发送外设命令。

启动设置只新增`/boot/overlays/tspi-3m-uart-header-five.dtbo`，并更新同目录`overlays.ini`和`.ubootenv`。新增文件是启动时叠加到原硬件说明上的小配置，不重新烧录整机，也不更换内核或原设备树。恢复所需文件在[板B串口备份](../../../../07_backups/uart/board_b_20261102/README.md)。

## 在RK上核对到哪里

| 串口入口 | 图中发送脚 | 图中接收脚 | 重启后的内核分配 | root打开与读参数 | 实际排针收发 |
| --- | --- | --- | --- | --- | --- |
| `/dev/ttyS3` | 5，`GPIO3_A0` | 3，`GPIO3_A1` | `uart3m0-xfer`，通过 | 通过 | 待接线 |
| `/dev/ttyS4` | 8，`GPIO2_D0` | 10，`GPIO2_D1` | `uart4m0-xfer`，通过 | 通过 | 待接线 |
| `/dev/ttyS7` | 28，`GPIO2_B6` | 27，`GPIO2_B7` | `uart7m0-xfer`，通过 | 通过 | 待接线 |
| `/dev/ttyS8` | 37，`GPIO2_A6` | 40，`GPIO2_A7` | `uart8m1-xfer`，通过 | 通过 | 待接线 |
| `/dev/ttyS9` | 26，`GPIO2_C1` | 29，`GPIO2_C0` | `uart9m0-xfer`，通过 | 通过 | 待接线 |

设备树五组状态均为`okay`，各串口引用的引脚组与表中相符，内核公布的10根引脚实际占用也逐根匹配。`/dev/i2c-7`、`/dev/i2c-8`和`/dev/spidev1.1`已移除，`/dev/spidev1.0`保留。

ARM64检查程序在板B由root执行，实际输出`PORTS=5 PASSED=5 FAILED=0`，SSH退出0、核对脚本输出`verify_exit=0`。程序只打开设备并读取参数，串口业务数据写入和读取均为0；驱动随后显示五路`tx:0 rx:0`。此次读到的默认值是9600、8N1、硬件流控关闭，未据此设置真实外设参数。

RS485查询返回`0x00000002`，其中启用位没有设置；本轮只准备原生串口收发引脚，没有启用RS485软件方向控制。UART3默认RTS会占18脚急诊灯，UART8默认RTS会占28脚UART7发送，仍需按实际转换电路决定方向控制方案。

重启前标识为`72b43693-95d9-4b7e-8620-63f56dfacbc4`，重启后为`2c8da208-9c8a-4b69-98e8-29d2ea1d460a`，证明已通过一次重启生效核对。内核仍为`6.1.99-rt36-tspi`，实时标志为1；CAN0设备存在且保持关闭，其配置文件指纹没有变化。没有新增CAN收发验证或实时性能测量。

板上日期仍不准确，重启后显示`2026-10-12T07:36:32Z`；原输出不改，归档日期采用电脑端北京时间。本轮没有调整时钟。

## 文件与核对依据

- [before_changes.log](before_changes.log)：实际身份、配置前文件指纹及占用查询。
- [device_tree_changes.diff](device_tree_changes.diff)：与板B实际设备树合并前后的差异，只涉及四组串口启用、两组I²C关闭、SPI引脚列表和第二路设备停用，共8处属性变化。
- [configure_result.log](configure_result.log)、[after_reboot_verification.log](after_reboot_verification.log)：实际写入、上传指纹/动态库检查、重启和逐项核对原文，退出码另存相应`*_ssh_exit.txt`。
- [summary.json](summary.json)：上述结果的结构化摘要，实际收发、物理回环及仪器验证均标为未通过验收。
- `*_executed.sh`、`*_executed.c`和[配置源码副本](uart_header_five_applied.dts)：本次执行脚本和输入快照；检查程序维护在[工具目录](../../../../05_tools/tests/uart/uart_access_check.c)，配置维护在[系统工程](../../../../06_programs/rk3576_system/configs/uart_header_five.dts)。
- `overlay_build.log`、`access_check_build.log`：配置和ARM64程序编译输出。首轮电脑命令传递路径失败，未部署旧产物；原失败保留在`build_attempt01_failed.log`，改为直接调用编译器后通过。
- [SHA256SUMS](SHA256SUMS)：除本说明外的证据文件指纹，可在本目录WSL终端用`sha256sum -c SHA256SUMS`核对。

实际过程继续追加[原串口记录](../../../../02_records/20261021_TTL串口调试验证.md#uart-config-board-b-20261102)，后续验收见[计划](../../../../01_docs/plans/development_plan.md#board-b-uart-config-20261102)。
