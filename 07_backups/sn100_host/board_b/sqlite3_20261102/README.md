# 板B的sqlite3命令行工具安装包

这里保存2026-11-02在板B新增的sqlite3命令行工具，用于只读查看数据库结构和数量。安装刷新了APT软件索引，随后**只新增sqlite3一个包，0升级、0移除**；已有`libsqlite3-0`保持`3.45.1-1ubuntu2.8`。没有覆盖现有业务配置，所以本次没有另做配置备份。

板卡为`board_b`，硬件序列号`8314b1e8ffd0321f`。当次脚本使用板B专用SSH主机别名及严格主机校验，并在板端核序列号；这些结果不适用于板A。这里没有连接板卡或再次安装，只归档已产生的原件。

| 项目 | 实际值 |
| --- | --- |
| 包 | `sqlite3_3.45.1-1ubuntu2.8_arm64.deb` |
| 系统/架构 | Ubuntu 24.04，ARM64 |
| 包版本 | `3.45.1-1ubuntu2.8` |
| 大小 | 139580字节 |
| 包SHA-256 | `ced8d1e467584a3d96b89b79d1b8342b173966db21f9acedae8f568ca7f85147` |
| APT镜像 | `http://mirrors.ustc.edu.cn/ubuntu-ports` |
| 包相对路径 | `pool/main/s/sqlite3/sqlite3_3.45.1-1ubuntu2.8_arm64.deb` |

镜像地址来自原安装日志，包路径、大小和SHA来自原APT包元数据；两者合成的来源URL为`http://mirrors.ustc.edu.cn/ubuntu-ports/pool/main/s/sqlite3/sqlite3_3.45.1-1ubuntu2.8_arm64.deb`。本轮没有联网查找或重新下载。归档包与板端APT缓存回传原件及给定SHA一致。

这个包只用于恢复CLI（命令行工具），**不是SQLite运行库、业务数据库或整套环境的备份**。恢复时先核板B身份、ARM64系统及已有依赖；包要求`libsqlite3-0 (= 3.45.1-1ubuntu2.8)`，另依赖libc6、libreadline8t64和zlib1g。具体版本条件见`provenance.json`。本目录不提供自动恢复或改库动作。

`provenance.json`记录来源、版本、依赖和逐文件校验；`SHA256SUMS`列出本目录其余每个文件的实际字节SHA，不包含清单自身。安装包按现有Git属性由LFS管理，其余说明按原字节保留。本轮没有提交Git。

原脚本、安装输出、失败/成功退出码及只读数据库结构/数量证据保存在[对应证据目录](../../../../04_references/sn100_host/workflow_night_20261102/sqlite3_20261102/README.md)。统计确认6条正编号校准绑定各有唯一base、0号点为0、默认试剂20组中15组无对应校准绑定；原复制不会复制这些未绑定组。文件头1/1只说明当时为rollback格式，不能证明连接同步为FULL、实际复制或崩溃恢复通过。
