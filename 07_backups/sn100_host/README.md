# SN100应用与源码归档索引

有些子目录是可恢复的应用包，有些仅是少数文件的修改前副本。优先读子目录已有README或清单，再确定能恢复什么。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [original_db_20261027/](original_db_20261027/README.md) | 原项目DB、单位DB与INI兼容接入的固定源码及后续匹配应用；不含原患者/账户资料。 |
| [combination_mode_20261027/](combination_mode_20261027) | 组合模式修复前受影响源码副本 |
| [instrument_offline_20261024/](instrument_offline_20261024) | 仪器页/人工维护离线检查点的匹配应用与源码 |
| [lis_raw_inbox_20261027/](lis_raw_inbox_20261027) | 原报文收件箱接线前的有限源码副本 |
| [lis_settings_20261024/](lis_settings_20261024) | LIS本地设置检查点的匹配应用与源码 |
| [local_features_integration_20261026/](local_features_integration_20261026) | 本地功能整合前受影响文件副本 |
| [local_results_20261026/](local_results_20261026) | 原值结果存储/入口变更前受影响文件副本 |
| [offline_followup_20261026/](offline_followup_20261026) | 后续修复前按事项保存的文档/源码副本 |
| [offline_four_windows_20261025/](offline_four_windows_20261025) | 该轮整合的匹配应用/源码与有限修改前副本 |
| [password_login_20261024/](password_login_20261024) | 密码登录检查点的匹配应用与源码 |
| [printer_application_20261027/](printer_application_20261027) | 打印动作接线/修复前的有限源码副本 |
| [printer_settings_20261024/](printer_settings_20261024) | 打印设置检查点的匹配应用与源码 |
| [qc_history_integration_20261026/](qc_history_integration_20261026) | 质控历史整合的固定ARM64检查点 |
| [result_format_20261027/](result_format_20261027) | 结果显示配置及退出修复前受影响文件副本 |
| [rk_board_20261027_01/](rk_board_20261027_01) | RK首次部署的匹配应用/源码和手动控制入口 |
| [single_slot_qc_20261026/](single_slot_qc_20261026) | 单个孔位的质控样本预设整合/修复前的有限文件副本 |
| [status_replay_20261024/](status_replay_20261024) | 离线状态回放检查点的应用包 |

## 常见名称

`sn100_shell_<发布ID>.tar.gz`通常是匹配应用包，`sn100_source_*.tar.gz`或`sources`包保存对应源码；`preimages*`、`pre_*`保存特定修改前的有限副本。`release.json`、`source_snapshot.json`和`SHA256SUMS`用于确认包、源码和依赖身份，不应混用不同批次。

这些归档不能自动恢复业务数据库。特别是项目、试剂、质控、校准和结果库之间存在引用与身份约束，一致业务备份/迁移仍按[当前计划](../../01_docs/plans/sn100_host_migration_plan.md)落实；不要直接拼接旧库或清除保护登记。原始日志和实际部署结论在[证据目录](../../04_references/sn100_host/README.md)与[同一记录](../../02_records/20261022_sn100_host_migration.md)。
