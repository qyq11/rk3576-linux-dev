# 实时内核安装与回退输入

这里有正式镜像、模块和此前受影响部分的回退材料。具体适用分区、版本和恢复步骤必须结合[实时内核记录](../../02_records/20261022_preempt_rt.md)，不提供脱离条件的一键烧录。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [archive_manifest.json](archive_manifest.json) | 归档范围、来源、文件列表及SHA-256。 |
| [SHA256SUMS](SHA256SUMS) | 本批原有归档输入的校验清单。 |
| [images/](images) | 6.1.99-rt36-tspi等该事项镜像、模块与构建配套资料。 |
| [flash/](flash) | 板卡烧录配置TaishanPi-3M-RK3576_Linux.cfg。 |
| [test_tools/](test_tools) | 指定rt-tests安装输入。 |
| [board_baseline_20261022_104356/](board_baseline_20261022_104356) | 原启动分区、模块/overlay与内核配置等当时基线。 |
| [board_environment_20261022_111741/](board_environment_20261022_111741) | 当时选择范围内的环境清单、配置和程序，不等于整机磁盘镜像。 |

性能原始数据另在[04_references/preempt_rt](../../04_references/preempt_rt/README.md)。本次只加目录导航，不对镜像、校验值或板端做任何修改。
