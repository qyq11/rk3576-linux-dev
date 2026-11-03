# 安装与回退所需的正式归档

这里保留恢复应用或受影响环境所需的输入。每个事项的恢复范围不同；存在一个包不等于有整机备份，也不等于业务数据库能够一致恢复。

板B2026-11-02新增[登录开关恢复输入](root_session/board_b_unified_login_20261102/README.md)及[共享配置恢复输入](samba/board_b_root_20261102/README.md)，按板B身份使用；密码、无线认证配置及用户文件不归档。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [samba/](samba/README.md) | 板A共享登录账号修改前后配置及有限恢复脚本；不保存共享密码或用户文件。 |
| [interfaces/board_a_20261102/](interfaces/board_a_20261102/README.md) | 板A五组串口与CAN0的原设置、重启后设置、两份叠加配置及新增的官方配置工具；非整机备份。 |
| [uart/board_b_20261102/](uart/board_b_20261102/README.md) | 板B五组串口小配置及两个启动设置的修改前/生效后副本；修改前已包含CAN0，只覆盖本轮串口配置的恢复范围。 |
| [can/board_b_20261102/](can/board_b_20261102/README.md) | 板 B：CAN0小配置及两个启动清单的修改前/生效后副本；非分区或整机备份。 |
| [system_image/](system_image/README.md) | 电脑编译的新板镜像、源码和配套输入；是否已烧录板 B待确认。 |
| [preempt_rt/](preempt_rt/README.md) | 板 A：原板配置备份、实时内核镜像和匹配模块；烧录步骤见原记录。 |
| [qt_environment/](qt_environment/README.md) | 板 A相关Qt依赖及电脑构建输入；按各包范围恢复。 |
| [sn100_host/](sn100_host/README.md) | 已有RK部署包对应板 A；另含电脑检查点及源码修改前副本。 |
| [home_path/](home_path) | 板 A：主目录变更的受影响配置。 |
| [root_session/](root_session/README.md) | 板A最新密码登录、此前自动登录及WindTerm会话的有限恢复输入；原2026-10-22自动提权字段单独保留，不含密码。 |
| [sn100_host_sample_discard_feedback_20261026/](sn100_host_sample_discard_feedback_20261026) | 电脑侧源码修改前副本，非板卡环境备份。 |

## 常见文件

`README.md`说明来源与恢复范围，`source.json/origins.json`记录来源，`SHA256SUMS`保存校验值，`source_snapshot.json`把源码与应用对应起来，镜像/压缩包按Git LFS保存。先读事项说明，再按原记录恢复；不把患者数据、密码、私钥或认证资料放进归档。新增上层README不会改写既有包和校验清单。
