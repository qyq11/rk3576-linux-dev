# SN100 本地密码登录 ARM64 离线恢复包

ARM64发布包来自当前353项冻结源码的全量主辅构建，release ID为`f558ff278eb332b1c304ac8f69843bd6951830136ee43ce2c2a5c91702cc48a2`。平台证据见[统一平台索引](../../../04_references/sn100_host/password_login_20261024/platforms/verification.json)。先执行`sha256sum -c SHA256SUMS`，解包到新目录后再核包内SHA256SUMS和release_id。

恢复范围为用户级统一主程序、报告辅助程序、必要私有Qt SQL/SQLite/报表/SVG库及插件、字体/许可和嵌入模板资源。不含系统镜像、内核、全机配置、运行数据库、密码或其他凭据；不迁移或重置已有数据。默认Guest，本地密码登录仅限显式隔离测试库；本机回环须另显式启用，登录不改变设备动作条件。

本轮未上传、安装、启动RK或连接仪器。恢复部署前仍须核目标动态库、显示/输入、权限和原生命周期合同；离线依赖闭包与编译通过不代替RK实际运行、原Windows行为、真实仪器或长期稳定性验收。旧发布包保持原位置。

另附`sn100_source_f558ff278eb332b1c304ac8f69843bd6951830136ee43ce2c2a5c91702cc48a2.tar.gz`和`source_snapshot.json`，仅保存本版本明确的353项构建输入，成员路径相对`06_programs/sn100_host/`。源码包已逐成员读回核SHA，按清单恢复到新的program目录后再构建；它不含完整Git历史、根级文档、设备配置或运行数据。Qt/工具链等外部输入继续使用既有07_backups/qt_environment归档，不将本源码包当系统备份。两个tar.gz均沿既有Git LFS规则保存。
