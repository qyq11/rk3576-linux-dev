# 两项原资料页收尾的应用恢复输入

本目录保存2026-10-30原质控/校准资料页和试剂批组选项通过RK软件检查后的匹配应用与源码。候选版本为`a247d2f89c4914655730f61217eb98bbbcfedeb983a14e4c86b0daaec8d90f7e`，主程序SHA-256为`25559f744dddc85436a1e314883afc33f29e4be49e2721605ea857cd03fad724`；正式位置沿用`/home/rkshare/programs/sn100_shell`。发布和安装后启动结果见[本轮证据](../../../04_references/sn100_host/resume_20261030/README.md)及[同一移植记录](../../../02_records/20261022_sn100_host_migration.md#quality-reagents-resume-20261030)，不以恢复包存在代替实际验收。

- `sn100_shell_a247d2f89c4914655730f61217eb98bbbcfedeb983a14e4c86b0daaec8d90f7e.tar.gz`：已核对的ARM64应用、报告辅助程序、私有运行库、插件和字体；只还原应用文件。
- `sn100_resume_sources.tar.gz`：匹配的832份工程源码与资源，质控测试采用本轮最终修订。正式编译输入见`source_snapshot.json`，归档文件逐份指纹见`source_bundle_identity.json`，测试修订及编译失败原因见`test_input_revision.json`。构建环境仍按已有`07_backups/qt_environment`恢复。
- `application_before.txt`：发布前的应用版本指向、原上一个版本指向和启动封装指纹；不含业务库或凭据。旧正式f8c恢复包继续使用[原库接入归档](../original_db_20261027/README.md)，本轮已经重新校验，不重复备份。
- `SHA256SUMS`：核对本目录归档文件的内容；压缩包沿既有Git LFS规则保存。

本次只增加应用版本目录并调整`current/previous_release`，没有覆盖系统组件、现场配置或`runtime/data`。因此不复制患者、账户、现场原数据库或仪器凭据。回退时先正常关闭程序，再将应用版本指向恢复为旧f8c；需要重建应用目录时才使用对应包。恢复应用不能恢复现场业务数据，也不能代替重新核对仪器或物理输入。

本版本已正式发布；安装后使用自产匿名资料两次EGLFS实屏启动、正常退出和重开通过。2026-10-30 12:12:15北京时间最终复核版本、指纹、主程序/驱动均无残留，显示及生命周期锁空闲；程序保持关闭。物理输入、完整原Windows视觉、真实仪器、资源预算和长稳仍未通过，不因归档关闭这些事项。
