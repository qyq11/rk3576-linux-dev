# 安装与回退所需的正式归档

这里保留恢复应用或受影响环境所需的输入。每个事项的恢复范围不同；存在一个包不等于有整机备份，也不等于业务数据库能够一致恢复。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [preempt_rt/](preempt_rt/README.md) | 实时内核镜像、匹配模块、烧录配置和既有回退输入。 |
| [qt_environment/](qt_environment/README.md) | Qt开发/运行依赖、调试工具及必要配置。 |
| [sn100_host/](sn100_host/README.md) | SN100匹配应用/源码发布包和受影响文件修改前副本。 |
| [home_path/](home_path) | 主目录/共享路径调整的受影响配置；子目录README说明边界。 |
| [root_session/](root_session) | 终端会话调整的受影响配置；子目录README说明边界。 |
| [sn100_host_sample_discard_feedback_20261026/](sn100_host_sample_discard_feedback_20261026) | 样本放弃草稿反馈修复前的有限文件副本，不是整套环境备份。 |

## 常见文件

`README.md`说明来源与恢复范围，`source.json/origins.json`记录来源，`SHA256SUMS`保存校验值，`source_snapshot.json`把源码与应用对应起来，镜像/压缩包按Git LFS保存。先读事项说明，再按原记录恢复；不把患者数据、密码、私钥或认证资料放进归档。新增上层README不会改写既有包和校验清单。
