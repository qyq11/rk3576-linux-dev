# 资料与长期验证证据

这里混合保存板级参考资料和开发过程中需要长期保留的证据。先按事项找子目录，再读对应操作记录，避免只看某个日志名就判断当前版本状态。

[返回上级说明](../README.md)

**图片位置（2026-10-27整理）：**照片和截图现按内容放在[03_photos分类目录](../03_photos/README.md)。本目录的历史清单、日志及文件指纹保持原文；其中若仍记着`03_photos/文件名`，按[文件移动对照](../03_photos/文件移动对照.json)查新位置，再与原指纹核对。这里只调整图片位置，不改变历史验证范围。

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [interfaces/board_a/20261102_configuration/](interfaces/board_a/20261102_configuration/README.md) | 板A：五组串口和CAN0配置、一次重启、12根引脚及root打开检查；实际排针及外设通信待验。 |
| [can/20261102/rk_result.json](can/20261102/rk_result.json) | 板 B：CAN0配置/重启、引脚、发送确认与关闭本机回送后的接收结果；真实硬件收发仍待验。 |
| [system_image/20261030/](system_image/20261030/) | 电脑编译：新板镜像来源、编译和文件核对；是否已烧录板 B待确认。 |
| [system_image/wsl_move_20261102/](system_image/wsl_move_20261102/) | 系统镜像编译环境迁到D盘的整盘指纹、源码与工具前后对照、ARM64编译及设备树编译结果。 |
| [RK3576_Linux6.1_SDK_Note.md](RK3576_Linux6.1_SDK_Note.md) | 厂商Linux 6.1开发包相关说明。 |
| [RK3576_40Pin工程资源分配表.jpg](RK3576_40Pin工程资源分配表.jpg) | 本项目40Pin引脚用途分配图。 |
| [泰山派3M_40Pin引脚图.png](泰山派3M_40Pin引脚图.png) | 板卡40Pin引脚位置图，用于核对物理针脚。 |
| [RK3576_立创_野火_资料网站汇总.xlsx](RK3576_立创_野火_资料网站汇总.xlsx) | RK3576、立创和野火相关资料网站入口汇总表。 |
| [Rockchip_Linux_RT_report.pdf](Rockchip_Linux_RT_report.pdf) | Rockchip Linux实时性参考报告。 |
| [Rockchip RK3576 TRM V1.2 Part1.pdf](Rockchip%20RK3576%20TRM%20V1.2%20Part1.pdf) | 芯片技术参考手册；本地资料，不随当前Git规则保存。 |
| [preempt_rt/](preempt_rt/README.md) | 板 A：实时内核配置与测量；含电脑构建日志和外部参考。 |
| [sn100_host/](sn100_host/README.md) | 已有RK部署与验证为板 A；电脑构建与历史测试另计。 |
| [uart/](uart/README.md) | `20261021/`为板 A；`board_b/`为板 B。 |
| [samba/](samba/README.md) | 板 A：共享文件读写证据。 |
| [workspace_setup/](workspace_setup/README.md) | 电脑工作目录与终端配置；历史RK操作为板 A。 |

## 资料、证据、备份的区别

参考资料解释设计；证据说明某次实际执行；[07_backups](../07_backups/README.md)保留重装/回退所需输入。不要修改既有证据包及其校验清单来迎合当前结果。新索引README放在事项上层，不改固定版本批次的内容。
