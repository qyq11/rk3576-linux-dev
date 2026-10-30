# 原配置DB接入的源码与应用恢复输入

本目录保存本轮固定的正式源码及匹配应用包。最终发布为`f8c6e73f59f458981e243af5dd00999c12a671f449f0daaf3f4ff70e82d8a11f`，ARM64编译、上传指纹与动态组件核对通过；正式RK应用运行40秒和25秒均正常退出0，随后重开就绪供用户查看。详细已验与待验范围以[唯一移植记录](../../../02_records/20261022_sn100_host_migration.md#original-db-rk-20261027)及[原始证据](../../../04_references/sn100_host/original_db_20261027/README.md)为准，不以归档存在推定完整系统或仪器通过。

`sn100_original_db_sources.tar.gz`与`source_snapshot.json`配套，覆盖本轮原项目库/单位库读写、INI兼容、控制按钮和共享的正式构建输入。`sn100_shell_f8c6e73f59f458981e243af5dd00999c12a671f449f0daaf3f4ff70e82d8a11f.tar.gz`是已实际部署的不可变应用包。`source.json`说明版本、文件指纹、两次正常退出与恢复范围；[SHA256SUMS](SHA256SUMS)核对本目录文件。仅本轮未部署的`afca64…`中间应用包在最终包及源码校验后移出本归档，该中间包后来已在2026-10-30按用户要求清理，原替代过程与文件指纹保留；[本次清理范围](../../../04_references/sn100_host/cleanup_20261030/README.md)不影响本目录正式恢复包。原替代记录见[替代记录](intermediate_package_replaced.json)。

这里不保存原DB、原INI、患者、账户口令或仪器凭据。恢复应用时仍使用既有`/home/rkshare/programs/sn100_shell`部署位置，程序包与运行数据分开；原数据位于其`runtime/data`，按原`db/systemInfo`、`db/userSet`、`project`、`result`分类使用。恢复源码或应用包不能代替恢复现场数据、确认仪器参数或重新验收；不要把本目录直接覆盖到正在运行的数据根。当前只有项目与单位维护直接接原库，其他业务仍有原库接入、完整算法或真实仪器验收缺口。
