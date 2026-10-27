# SN100 O10-2 LIS本地设置恢复输入

本目录配对应用包与371项正式编译/发布源码，release_id=`3902179cf0454c730bf27b8016c50e71b9d67ebf11c91c67e6891b66760eb477`。应用已交叉构建/离线闭包与全部包成员SHA核验；未部署/执行RK、连接LIS或仪器。

- 应用：`sn100_shell_3902179cf0454c730bf27b8016c50e71b9d67ebf11c91c67e6891b66760eb477.tar.gz`，SHA-256 `78b2ee85c8592c977a3bd23ab7c1df5016ebca7d7d663dc76a19aab44022de57`。
- 源码：`sn100_source_3902179cf0454c730bf27b8016c50e71b9d67ebf11c91c67e6891b66760eb477.tar.gz`，SHA-256 `2c7943f4c0f6ff1ccbcbadc8c01150cd9423260956828cc85e7b8c27e65050c4`，严格371成员，与source_snapshot.json一致；不是包含全部测试/记录的仓库备份。
- `arm64_verification.json`保留初轮Hangup/原总退出缺失及126本批对象验证接续、最终27编译/2链接0、153对象/DWARF、56离线ELF闭包和包冻结依据。

恢复范围仅本应用release目录/对应源码：先在新隔离目录按SHA256SUMS核本文件及两压缩包，核tar路径/成员，再按shell部署工作流在确认设备/范围后发布；源码应解到新项目副本作对照，不覆盖当前dirty工作区。运行数据、INI/账户/密码/私钥、系统Qt/驱动/板配置不在本包内，不以应用包充当多store数据备份或生产迁移方案。

本地配置通过`--lis-settings`显式启用，保存自动选项只是意图，不建连接/打开串口；Linux候选为sysfs/dev元数据，Windows仅固定注册表查询。真实通信、打印、RK画面/输入/实际依赖、预算/同板RT/长稳及生产安全另验。新371包与旧361/353/348检查点分开保留，不把旧包称为新功能版本。

压缩包按仓库Git LFS规则保存。完整证据见../../../../04_references/sn100_host/lis_settings_20261024/verification.json，同一记录见../../../../02_records/20261022_sn100_host_migration.md#o10-lis-settings-20261024。
