# 本机工具与独立硬件实验

本目录的安装包/工具本体只在本地保留。正式应用源码在06_programs；这里的tests用于独立验证某个接口或系统能力。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [tests/](tests/README.md) | GPIO、UART和实时内核实验源码/脚本，随Git保存。 |
| [drivers/](drivers/README.md) | 电脑侧驱动相关本地工具。 |
| [flash/](flash/README.md) | 烧录工具。 |
| [terminal/](terminal/README.md) | SSH、串口和终端相关工具。 |
| [virtualization/](virtualization/README.md) | 虚拟化相关本地工具。 |

本次只为工具目录放行README；安装包和解压内容仍按根忽略规则留在本机。恢复RK所需的正式镜像、配置和必要包放[07_backups](../07_backups/README.md)。
