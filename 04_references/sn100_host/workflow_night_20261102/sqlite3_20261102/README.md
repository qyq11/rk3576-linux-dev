# 板B安装sqlite3及只读结构检查证据

这些是2026-11-02当次脚本、原输出和真实退出码。用途是说明为什么补装sqlite3命令行工具、实际安装范围，以及当前项目复制所需表结构和关联数量。归档时仅做本地复制和字节校验，没有连板、运行产品或打开数据库。

板卡为`board_b`，硬件序列号`8314b1e8ffd0321f`；脚本同时使用板B专用SSH主机校验和板端序列号检查，不能把结果套给板A。脚本中的地址、目录和电脑侧known_hosts路径是当次取证上下文；未归档known_hosts实际文件、私钥、密码、患者或业务账户表。

安装在刷新APT软件索引后只新增`sqlite3=3.45.1-1ubuntu2.8`，0升级、0移除，已有`libsqlite3-0`仍为同版本。没有覆盖业务配置，故未另做配置备份。可恢复输入见[安装包目录](../../../../07_backups/sn100_host/board_b/sqlite3_20261102/README.md)；它只恢复命令行工具，不是整环境备份。

| 原件前缀 | 真实退出码 | 应如何理解 |
| --- | --- | --- |
| `board_b_sqlite_install_preflight` | 100 | 当时APT索引找不到sqlite3，预检未通过，原失败保留。 |
| `board_b_sqlite_install` | 0 | 刷新索引并按已有运行库版本新增CLI；不是产品运行。 |
| `board_b_sqlite_install_verify` | 0 | 核包版本、CLI字节SHA、动态库和APT元数据；未启动上位机或主控。 |
| `board_b_copy_schema_readonly` | 1 | 23:06:56因`command -v sqlite3`未找到工具而停止，尚未打开库。 |
| `board_b_copy_schema_readonly_02` | 0 | 23:11:21用只读CLI检查字段及匿名数量，未复制或修改业务库。 |
| `board_b_copy_journal_modes_readonly` | 0 | 首轮只读文件头，默认试剂路径写成`db/reagent`，出现REGULAR_FILE_NOT_FOUND；不能当作默认试剂库已核。 |
| `board_b_copy_journal_modes_readonly_02` | 0 | 22:58:57更正为实际`db/userSet`路径，核校准、默认试剂和7份项目文件头。 |

每组都有原`.sh`、`.log`和`.exit`。另保留4份对应电脑侧`run_*.py`发送/回收脚本；它们未在归档期间执行，板端原操作使用bash/sqlite3。首次缺CLI失败、首次错误路径输出均原样保留，没有改写成成功日志。

第二次结构/数量检查得到：

- 五张表的列与复制候选一致，隐藏列均为0；计数器只有1行且为非负整数，下一编号在base/data/binding中均无冲突。
- 6条校准绑定全部为正编号，各按校准号和项目名匹配唯一base；0号data共0行。
- 默认试剂库共有20组，其中15组没有同名同组校准绑定。Windows原复制只循环校准绑定，这15组本来就不复制，不能额外补入。

第二次文件头检查显示校准库、默认试剂库和7份项目库write/read version均为1/1。这只说明当时文件格式符合rollback日志方式；它没有证明连接`synchronous=FULL`，也没有执行项目复制、多库提交、故障恢复或共享同步。当前统计没有0绑定无base的情形；未来缺base仍需单独核实，不能造默认空记录并称其为有效曲线。

这里共25份诊断原件，逐份与`temp/sn100_host/workflow_night_20261102/`来源SHA一致。没有归档临时C助手、它的ARM64产物或真实数据库。`provenance.json`给出每份来源、大小和SHA；`SHA256SUMS`覆盖本目录除清单自身以外的所有文件。Git的`-text`属性保留原字节；本轮没有提交Git或改公共README/计划/记录。
