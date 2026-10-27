# 原上位机文件与数据库的静态核查

核查对象是根目录`无标题.png`所示的原工程`bin/`，不是正式RK运行数据。用途、更正和整体沿用原库的要求见[说明正文](../../../01_docs/knowledge/sn100_original_bin_files.md)，后续实施见[原计划](../../../01_docs/plans/sn100_host_migration_plan.md#original-database-reuse-20261027)。

| 文件 | 用途 |
| --- | --- |
| [inventory.json](inventory.json) | 305个文件的相对路径、大小、SHA-256文件指纹，以及数据库表/字段、项目参数名、配置键或JSON结构、图片尺寸等静态信息。仅记录结构，不保存患者记录、口令或连接凭据的值。 |
| [exports.json](exports.json) | 只读解析`GL4Ad.dll`和`lib/AL4ADLL.dll`得到的对外函数名称，供区分日志与算法。没有加载执行DLL，也不代表内部算法已获得源码。 |

本批盘点48个顶层项目、29个SQLite数据库、73个DLL、3个EXE；两个同尺寸的试剂容量校准JSON经字节比较内容相同。73个DLL均为Windows32格式，3个EXE中2个Windows32、1个Windows64。

所有原文件只读，原库内容未改；没有复制原数据库、患者资料或认证文件到本证据目录。临时检查脚本和文档核查输出在`temp/sn100_host/bin_review_20261027/`。这是文件、格式与源码依据核查，不是Windows/WSL产品功能测试，也不是RK运行或仪器验收。
