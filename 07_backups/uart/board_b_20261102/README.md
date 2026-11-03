# 板B五组串口的配置与恢复输入

2026-11-02，来源为板B（硬件序列号`8314b1e8ffd0321f`）的实际启动设置及本项目编译的串口小配置。用户要求root直接连接后实施；配置、重启和root打开五个串口通过，实际排针及真实外设收发仍待接线验证。[实际证据](../../../04_references/uart/board_b/20261102_configuration/README.md)。

## 保存了什么

| 文件 | 来源与用途 |
| --- | --- |
| `overlays.ini.before`、`ubootenv.before` | 串口修改前，从板B`/boot/overlays/overlays.ini`与`.ubootenv`读取；当时已经启用CAN0。 |
| `overlays.ini.after`、`ubootenv.after` | 串口配置重启生效后的实际文件，包含CAN0及五组串口两项。 |
| `tspi-3m-uart-header-five.dtbo` | 新增的启动叠加配置，已与板上文件核对一致，SHA-256为`9e720d81dfa456d9824403cd6a02e8f6e9899a9eecf375e0d51226e129ea92a4`，按Git LFS保存。 |
| `uart_header_five.dts` | 对应配置源码副本；维护入口在[系统工程](../../../06_programs/rk3576_system/configs/uart_header_five.dts)。 |
| `SHA256SUMS` | 上述六份文件的校验值。 |

本备份只覆盖本轮改动的两个启动设置文件和新增串口叠加配置，不包含整机镜像、分区、内核、业务数据、密码、私钥或登录凭据。因为其他部分没有被本轮覆盖，未额外备份整个环境。

在本目录WSL终端执行`sha256sum -c SHA256SUMS`核对文件。使用前确认是板B，并核查是否有后续启动设置需要保留。

## 需要撤回串口配置时

通常只需以root在板B停用本项，再重启；这会恢复原来的I²C7、I²C8及SPI第二路片选安排，UART4和独立CAN配置仍由其他原设置决定：

```bash
ovtree disable tspi-3m-uart-header-five.dtbo
sync
systemctl reboot
```

若需要逐字节恢复本轮修改前设置，先确认没有后续改动需要保留，再将本目录两个`.before`文件上传到板B临时目录，分别复制回`/boot/overlays/overlays.ini`和`/boot/overlays/.ubootenv`。这两个副本已经包含当时启用的CAN0，不是最初空白启动设置。不要把原CAN事项更早的备份误当成本轮串口修改前状态。

本轮没有执行回退。恢复后还需核对启动、设备节点、引脚分配及实际需要的通信，存在备份不等于恢复功能已验收。
