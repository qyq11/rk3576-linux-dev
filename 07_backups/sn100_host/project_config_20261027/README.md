# 2026-10-27 项目配置的应用恢复输入

本包用于恢复本批应用和匹配源码。发布`c89e4a65ef8421ae240f025a99093a70e876c2aef5430af2db4c10ea9815944e`已经上传RK并核对SHA、运行组件、匿名旧结构打开和重开；三次短生命周期实际退出0。功能范围及剩余待验见[同一移植记录](../../../02_records/20261022_sn100_host_migration.md#project-config-dispatch-20261027)。

| 文件 | 用途 |
| --- | --- |
| `sn100_shell_<完整发布ID>.tar.gz` | 实际上传的应用、报告组件、私有运行库/插件、中文字体、许可和原生命周期脚本。 |
| `sn100_shell_sources.tar.gz` | 编译前521项输入，逐项从归档读回核SHA；恢复与该应用匹配的源码。 |
| `source_snapshot.json` | 上述521项输入的相对路径和SHA。 |
| `release.json`、`release.SHA256SUMS`、`release_id` | 包内功能范围、文件清单和发布身份。 |
| `dependency_report.json` | 该ARM64产物的静态运行组件检查结果；实际板端检查另在证据目录。 |
| `SHA256SUMS` | 本目录恢复输入的外层校验清单。压缩包沿用Git LFS。 |

主程序SHA-256：`6c71c9845983ef5d049058285e6f0a5bc3709ceaa6d5fbacfafda807b024d9fe`；报告组件：`a1c85da8dbb34183e17885b4ee9179527112b8b5c53805182b1335faaa65e239`。最终RK验证驱动在应用打包后完善并同步正式`projects/tests`，不参与主程序链接；它们的最终源码和指纹另在[验证证据](../../../04_references/sn100_host/project_config_20261027/README.md)，应用源码包保留编译时原输入。

恢复范围是应用、配套运行组件与源码，不含现场运行数据、账户库或认证材料，不是整机镜像。内核、系统Qt和显示环境沿用已有归档；本轮没有改系统或现场`runtime/data`，不额外备份这些未改内容。

板端`/home/rkshare/programs/sn100_shell/current`现指向本发布，`previous_release`记录旧`e230d277`，旧release与[旧正式恢复包](../rk_board_20261027_01/README.md)均保留。切换应用前正常stop，确认无待处理草稿，再按原`shell/rk.sh`的清单/SHA/运行组件核查流程安装恢复包。应用回退不等于资料回退；不要用这个包覆盖业务数据库。

本轮只验证匿名目录的升级和重开；原现场资料保留但未打开。程序最终保持关闭，按[使用说明T15](../../../01_docs/knowledge/sn100_host_manual_test_guide.md)手动start，不设开机自启动。没有自动提交或推送Git。
