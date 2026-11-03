# 板A排针12脚普通GPIO的有限恢复输入

2026-11-03，目标板A，硬件序列号`3bf8222fe1417f6a`。仅关闭排针12脚`GPIO0_B5`原来的PWM通道，供程序申请普通GPIO输入或输出。一次重启和板端软件核对通过；电压、外部输入变化及485实际通信未验证。[操作及结果](../../../02_records/20261021_gpio_output.md#board-a-pin12-gpio-20261103)。

| 文件 | 用途 |
| --- | --- |
| `overlays.ini.before`、`ubootenv.before` | 从板A读取并核对指纹的本次修改前启动设置；已包含原五组串口和CAN0配置 |
| `overlays.ini.after`、`ubootenv.after` | 本次启用12脚配置并重启后读回的实际设置 |
| `uart3_485_gpio_pin12.dts`、`tspi-3m-uart3-485-gpio-pin12.dtbo` | 本次实际配置源码及编译文件；后者在板A`/boot/overlays/`启用 |
| `gpio_output.c`、`gpio_output.arm64.gz` | 增加12脚映射后的匹配源码及ARM64程序压缩包；解压后的程序指纹为`6d6905d02687b631af852030340e100c1bdb759c99f7168eb9a79ec6a5ac3585` |
| `SHA256SUMS` | 本目录文件指纹清单，清单自身不列入 |

本次原有PWM没有使用者，也没有旧GPIO程序被覆盖，因此只保存上述受影响启动设置及本次新增配置/工具；没有整机备份、认证凭据或板B恢复文件。

需要恢复12脚原来的PWM安排时，先确认仍是板A，并退出申请12脚的GPIO程序。在板A管理员终端执行：

```bash
ovtree --lang zh disable tspi-3m-uart3-485-gpio-pin12.dtbo
sync
systemctl reboot
```

这会停用本次小配置，其他启用项继续保留；重启后再检查PWM节点状态、GPIO占用及实际用途。恢复原PWM节点不代表已经输出PWM波形，波形仍需对应程序设置。

若需逐字节回到本次修改前，先确认后来没有新增启动配置，再把`overlays.ini.before`、`ubootenv.before`分别恢复为`/boot/overlays/overlays.ini`、`/boot/overlays/.ubootenv`并重启。有后续变更时优先使用前述单项停用方法，不能直接覆盖后续设置，也不能把这两份板A原件用于板B。**本轮只准备了恢复输入，没有实际执行回退。**
