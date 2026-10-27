# 原配置数据库与INI的RK验证证据

用途、正式部署位置、操作方法、已验与待验范围统一见[移植记录](../../../02_records/20261022_sn100_host_migration.md#original-db-rk-20261027)，后续任务见[原库实施项](../../../01_docs/plans/sn100_host_migration_plan.md#original-database-reuse-20261027)。本目录只保存固定结果，不维护另一份计划。

- `original_manifest.json`和`original.SHA256SUMS`列出原样复制的141文件及指纹，没有数据库记录内容。
- `ini_fix_rk/`核对四份INI共134个有效配置项和29份DB的完整性；原值和凭据未输出。
- `store_03/`为后端90项通过，`page_02/`为实际页面114项通过；`verification.json`和`page_result.json`逐项记录结果。
- `store_01/`、`store_02/`、`page_01/`保留工具缺失、测试请求号和点击位置错误造成的失败，不与后续通过混写。
- 构建日志、运行退出码和资源原始采样保留原范围。所有产品功能测试都在RK执行，电脑只交叉编译。

页面图片另见[按内容分类的截图](../../../03_photos/07_SN100上位机移植/12_原配置数据库验证/)。原DB、原INI及患者/账户资料未纳入本目录。程序能保存配置不等于已完成检测、算法或真实仪器验收。
