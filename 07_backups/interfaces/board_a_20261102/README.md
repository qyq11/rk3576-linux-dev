# 板A五组串口和CAN0的有限恢复输入

2026-11-02，从板A（硬件序列号`3bf8222fe1417f6a`）读取原设置，再安装与板B相同的两份叠加配置；配置和一次重启核对通过，实际外设通信仍待验。[执行结果](../../../04_references/interfaces/board_a/20261102_configuration/README.md)。

| 文件 | 用途 |
| --- | --- |
| `overlays.ini.before`、`ubootenv.before` | 板A本轮修改前的两个原文件；原来没有启用CAN0或五组串口配置。 |
| `overlays.ini.after`、`ubootenv.after` | 重启生效后从板A读回，包含CAN0和五组串口。 |
| 两份`.dts`及对应`.dtbo` | 本轮实际配置源码和编译结果；编译结果与板B相同，但恢复时仍核对板A身份。 |
| `ovtree.gz` | 官方SDK中已有的ARM64静态配置工具，解压后与板A`/usr/local/bin/ovtree`一致；该路径本轮前不存在。 |
| `SHA256SUMS` | 上述文件校验清单；在本目录WSL终端用`sha256sum -c SHA256SUMS`核对。 |

只保存受本轮写入影响的启动设置和新增文件，没有备份整机、分区、Qt应用或业务数据，没有认证凭据。工具来自专用WSL的`/home/qyq/TaishanPi-3-Linux/app/ovtree/ovtree`，原始程序SHA-256为`96461f74b561a6b35565d07f95039325581b388a3316e409ef9a3cc46b826868`；解压输入是该文件的逐字节副本。

只停用其中一项时，在板A管理员终端运行`ovtree disable tspi-3m-can0-m2.dtbo`或`ovtree disable tspi-3m-uart-header-five.dtbo`，随后`sync`并重启。停用串口项会恢复I²C7/8和SPI1第二路片选的原安排，其余设置由原设备树及仍启用的配置决定。

如需逐字节恢复本轮之前，先确认是板A且没有后续启动设置需要保留，再把两份`.before`文件分别恢复到`/boot/overlays/overlays.ini`和`.ubootenv`；两份新增`.dtbo`及本轮新增的`ovtree`可在确认未被后续工作使用后移除，随后同步并重启。不能用板B的启动清单代替这两份板A原件。

本次没有实际回退。文件指纹和备份读取通过，仅说明恢复输入齐全；恢复后的启动、接口和真实收发还需另验。
