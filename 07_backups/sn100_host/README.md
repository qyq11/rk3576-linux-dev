# SN100应用与源码归档索引

有些子目录是可恢复的应用包，有些仅是少数文件的修改前副本。优先读子目录已有README或清单，再确定能恢复什么。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [板B第八轮有限恢复输入](board_b/status_login_08_20261103/checker_restore_scope.md) | 原07指针和同版2968检查器恢复包；8文件、新archive_SHA256SUMS 7项，原三指针原件不变；只配套正式08，不含工作库/配置/凭据。24基础项/0失败、可选0未评估，真实业务另验。 |
| [板B第八轮流程版本](board_b/workflow_integration_20261102/integration_08/README.md) | 08bc43a7程序/报告程序及674份匹配源码，381输入、284+2对象完整构建保存与归档0；包内README的未运行是当时时点，后续切08及软件专项见[实际记录](../../02_records/20261022_sn100_host_migration.md#integration-08-live-20261103)。不含业务库或整环境。 |
| [板B第七轮有限恢复输入](board_b/status_login_07_20261103/checker_restore_scope.md) | 原06程序指针和同版9819检查器恢复包；原指针三件保持原字节，不含工作库或凭据，仅配套正式07使用。 |
| [板B第七轮流程版本](board_b/workflow_integration_20261102/integration_07/README.md) | 94d1b979主程序/报表程序及663份匹配源码；构建保存阶段未启用，后续11:03软件专项24/0另有记录；不含业务库，真实业务另验。 |
| [板B第六轮有限恢复输入](board_b/status_login_06_20261103/checker_restore_scope.md) | 实测旧e314程序指针/属主、同版c920检查器和匹配脚本；正式649源码沿integration_06，原指针三件保持原字节，新包42成员读回一致，不含业务库或凭据。 |
| [板B第六轮流程版本](board_b/workflow_integration_20261102/integration_06/README.md) | 14ce9714程序及649份匹配源码；完整构建和板B独立目录保存通过，合入与网络恢复证据另存，不含业务库，实际功能待验。 |
| [板B第五轮状态专项的有限恢复输入](board_b/status_login_05_20261102/README.md) | 实际旧current目标/属主、同版8b48检查器及匹配源码脚本；正式程序与639源码沿integration_05，不含工作库或凭据。 |
| [板B第四批流程修补](board_b/workflow_integration_20261102/integration_05/README.md) | e31491a1程序与639项源码；均值/主题、重连v2和批组本地开关完整构建及板B保存通过，实际功能待验。 |
| [板B数据库命令行工具](board_b/sqlite3_20261102/README.md) | 匹配现有运行库的sqlite3安装包及来源校验；仅恢复工具，不含业务库或整环境。 |
| [板B第三批流程修补](board_b/workflow_integration_20261102/integration_04/README.md) | 4e6c321f程序及639项匹配源码；八份窄补丁完整构建和板B独立目录保存通过，未切换或运行，实际功能待联机。 |
| [板B第二批流程版本](board_b/workflow_integration_20261102/integration_03/README.md) | d71e4cf4主程序/报告程序及639项匹配源码；完整构建和板B独立目录保存通过，未切换或运行，联机功能待验。 |
| [板B第一批流程版本](board_b/workflow_integration_20261102/integration_02/README.md) | 3cfcea34主程序/报告程序及622项匹配源码；完整构建和板B独立目录暂存通过，未切换启用，联机功能待验。 |
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
