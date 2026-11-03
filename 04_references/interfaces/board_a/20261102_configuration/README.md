# 板A五组串口和CAN0配置结果

2026-11-02，用户重新接入板A，要求把刚才板B的串口和CAN配置在板A再做一遍。**已完成板A配置、一次重启生效、12根引脚分配和root打开五个串口的核对。CAN三帧本机发送确认通过；关闭本机回送后接收超时。实际排针串口收发、CAN收发器及真实外设通信仍待验证。**

**过程入口：**[这次重新配置板A，是按什么顺序完成的](../../../../02_records/20261021_TTL串口调试验证.md#board-a-configuration-process-20261102)，按步骤解释为什么这样处理、具体用了哪些配置、如何核对和还缺什么。本目录保存当时证据。

板A硬件序列号为`3bf8222fe1417f6a`，SSH ED25519主机指纹为`SHA256:xLQv5sqzTRvKbfD2fNQQLYjCbPN6WOXgkXEoUqV8Yy4`，与原Qt板记录一致；不是板B的`8314b1e8ffd0321f`。内核为`6.1.99-rt36-tspi #1`，实时标志1。板端时间原文保留，日期按电脑端北京时间登记。

## 改了什么

配置前仅UART4启用；I²C7/8及SPI1第二路片选占着计划中的串口引脚。未发现这两组I²C登记的外设或相关设备被进程打开，也没有运行SN100主程序。将本次两份叠加配置与板A实际设备树合并检查，只有9处预期属性变化。

写入范围：新增`/boot/overlays/tspi-3m-can0-m2.dtbo`、`tspi-3m-uart-header-five.dtbo`，更新同目录`overlays.ini`、`.ubootenv`。板A原先没有配置工具，另外安装官方SDK已经编译的ARM64静态程序`/usr/local/bin/ovtree`，用于维护这两项设置。没有烧录分区、替换内核或覆盖Qt应用及业务数据。两个叠加配置的指纹与板B相同，板A原设置单独备份在[本轮恢复目录](../../../../07_backups/interfaces/board_a_20261102/README.md)。

登录时普通用户密钥被拒绝，用户在本机窗口输入密码建立临时连接；当时主目录及SSH文件权限读到777，普通用户密钥仍不生效，本轮未改SSH服务设置。为重启后继续核对，临时在原先没有授权文件的root目录加入电脑公钥，全部核对后已删除该文件，见`temporary_access_cleanup.log`；没有归档认证凭据。普通用户长期密钥登录问题另行处理。

上段是串口/CAN配置阶段的收尾状态。随后用户另外要求长期直接进入root，已重新配置并保留远程root授权，WindTerm当前会话名为“lckfb-板A”；当前登录方式见[后续root登录记录](../../../../02_records/20261021_workspace_setup.md#board-a-root-login-20261102)。本目录原日志和校验清单保留当时范围，不改写历史结果。

## RK实际结果

| 入口 | 40针发送脚 | 40针接收脚 | 重启后引脚组 | 结果 |
| --- | --- | --- | --- | --- |
| `/dev/ttyS3` | 5 | 3 | `uart3m0-xfer` | 引脚分配、root打开及读取参数通过；实际收发待验。 |
| `/dev/ttyS4` | 8 | 10 | `uart4m0-xfer` | 同上；历史UART4回环不当作本轮复测。 |
| `/dev/ttyS7` | 28 | 27 | `uart7m0-xfer` | 引脚分配、root打开及读取参数通过；实际收发待验。 |
| `/dev/ttyS8` | 37 | 40 | `uart8m1-xfer` | 同上。 |
| `/dev/ttyS9` | 26 | 29 | `uart9m0-xfer` | 同上。 |
| `can0` | 11 | 7 | `can0m2-pins` | 引脚分配和3帧发送确认通过；独立接收及外部通信未通过验收。地为9脚。 |

串口检查输出`PORTS=5 PASSED=5 FAILED=0`，SSH和核对脚本均退出0。没有串口业务数据读写，五路驱动计数均为`tx:0 rx:0`；读到9600、8N1、无硬件流控，仅是当前默认值。RS485启用位未设置；转换电路和收发方向仍须按实物确认。I²C7/8及`spidev1.1`已释放，`spidev1.0`保留。

重启标识从`42f798fc-e78c-4d67-8c9e-a8064e6ddb0a`变为`046347e9-a651-4392-af42-61fc37d00bcd`。用户确认排针没有接外设后，在板A用临时500000 bit/s普通CAN及控制器回环检查：三帧内容一致，来源均为`tx-confirmation`，即驱动发送确认，退出0；关闭软件本机回送后，第一帧等1秒超时，退出1，清理退出0。共发送4帧，硬件计数TX=4、RX=0。最终`can0`为`DOWN/STOPPED`、回环关闭；500000不是已确认的外设通信速率，也没有开机自动收发服务。

## 如何核对材料

`preflight.log`是修改前状态；`device_tree_changes.diff`是合并差异；`configure_result.log`是安装和启用输出；`after_reboot_verification.log`逐项核对设备树、引脚、设备入口和串口打开结果。`can_echo.log`、`can_no_local_echo.log`保留两次CAN测试的实际TX/RX及退出码；`final_state.log`保留最后状态。`summary.json`列出通过和未验范围，`*_exit.txt`记录SSH实际退出码，执行源码及脚本按原样另存。

准备过程中的路径传递失败、重启期间首次SSH超时和最终可选进程查询限制见`preparation_notes.md`；没有把这些情况改写为通过。CAN程序和串口程序均已结束并返回上述退出码，没有在电脑运行功能测试。

在本目录WSL终端运行`sha256sum -c SHA256SUMS`核对除本说明外的证据。恢复目录另有自己的清单。实际事项继续追加[串口记录](../../../../02_records/20261021_TTL串口调试验证.md#uart-config-board-a-20261102)和[CAN记录](../../../../02_records/20261102_can.md#can-board-a-20261102)，下一步见[计划](../../../../01_docs/plans/development_plan.md#board-a-interfaces-20261102)。
