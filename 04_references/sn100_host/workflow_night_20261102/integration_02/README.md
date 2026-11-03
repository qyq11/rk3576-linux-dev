# 板B：第一批流程代码的构建和暂存核对

2026-11-02版本`3cfcea34`：251个ARM64对象、主程序及报告辅助程序完整编译/链接、运行库依赖扫描、打包通过，实际构建退出0；622项源码恢复包解压读取并逐文件指纹核对通过。程序与匹配源码见[正式恢复输入](../../../../07_backups/sn100_host/board_b/workflow_integration_20261102/integration_02/README.md)。

板B序列号`8314b1e8ffd0321f`及专用SSH主机指纹已核。新程序已放入`/home/rkshare/programs/sn100_shell/releases/3cfcea34b6bac23a0b5e244c8bcc4fbce61c83cfbf0a67181c8cd7adadc19457`，上传SHA-256、完整包内清单及主程序/报告程序/SQL插件/显示辅助插件的动态库核对通过，实际退出0。

**本次没有切换current、运行新程序或恢复主控。**current仍为`7c4abe30`；检查时上位机未运行、5000没有监听，联机功能、真实仪器、界面/资源和长时间运行均待验。暂存文件和缺库检查通过不能算RK业务通过。

## 具体证据

- [verification.json](verification.json)：版本、实际退出码、已验与未验。
- [build.log](build.log)、[build_exit.txt](build_exit.txt)、[dependency_report.json](dependency_report.json)：完整编译/链接和依赖；日志中旧工具输出`offline_closure_passed`只是静态依赖扫描名称，没有在电脑执行产品功能。
- [board_b_stage.log](board_b_stage.log)、[board_b_stage.exit](board_b_stage.exit)：板B独立目录暂存、主程序及报告指纹、实际动态库路径。
- [board_b_preflight.log](board_b_preflight.log)：板B身份、实际安装入口、未运行状态、配置指针和INI指纹；没有认证配置正文。
- [source_snapshot.json](source_snapshot.json)、[provenance.json](provenance.json)：622项实际输入及程序/恢复包指纹；不是完成百分比。
- [first_build_interrupted.json](first_build_interrupted.json)：首轮22对象时为修三项源码问题主动停止，未链接/打包，中断EXIT文件的0没有被当作成功。
- 三份独立源码复核及告警初稿复核：分别说明跨页边界、任务错误/重复恢复、保存前备份和告警失败路径；审查结论不代替运行。
- `scripts/`保存当次执行原文及路径计算，供核原记录，不作为本目录可直接运行的工具；正式构建入口仍是`06_programs/sn100_host/shell/build.sh`。

本批代码范围为报告翻条、项目同步/任务错误恢复、实际运行态进入资料权限、新告警保存/失败恢复/关闭排空，以及项目保存前保留旧版本。校准/QC任务、试剂操作和通道AD后续候选未包含在本包中。全部后续任务仍维护在[当前执行卡](../../../../01_docs/plans/sn100_host_migration_plan.md#workflow-night-20261102)，实际过程仍追加[同一移植记录](../../../../02_records/20261022_sn100_host_migration.md#workflow-night-coordination-20261102)。
