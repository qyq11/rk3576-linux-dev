# 板B：状态、登录和仪器数值候选发布

2026-11-02。用于恢复本轮已完成ARM64编译并安装到板B的程序，**不代表板端功能验收通过**。两轮检查失败及主控停止情况见[本轮证据](../../../../04_references/sn100_host/board_b/20261102_status_login/README.md)。

发布`7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9`，主程序SHA-256为`937c77eb111cae02b25ca6d0ea20b14c24122e9ed7e58f832ccb4e6aafb1fb94`。仅限板B恢复；板B序列号`8314b1e8ffd0321f`，不能拿本轮结果替代板A验证。

| 文件 | 用途 |
| --- | --- |
| `sn100_shell_7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9.tar.gz` | 同批主程序、报告程序和私有运行组件；包SHA-256为8602e5ad9a5fa042a0ed87773bc0553af3cce9753a7ed7f7a0d4c38e3949d0cd。 |
| `matching_sources.tar.gz` | 620项固定编译输入及当时检查入口/说明；不含板端运行数据库。 |
| `source_snapshot.json` | 620项固定源码/资源的逐文件SHA-256。 |
| `dependency_report.json` | 两个ARM64可执行文件及依赖组件的检查结果；不是RK运行结论。 |
| `connected_review_source.cpp`、`connected_review_build.json` | 后续修正启动检查时机的工具源码、对应编译指纹；已交叉编译，尚未在主控恢复后运行。 |
| `SHA256SUMS` | 上述文件的校验清单。 |

恢复前先核板卡身份及当前运行状态，正常关闭已有上位机并核对`SHA256SUMS`。程序包只安装到`/home/rkshare/programs/sn100_shell/releases/<发布号>`，验证包内清单后再切换`current`；不覆盖`runtime/data`、INI、账户、原启动器或主控。

本轮之前的板B发布为`eb9d9ae6d593fa20155f83f4e1c8455cc694da083c919f7142343187d1515e9b`，仍保留在板端`releases/`，旧发布指针保存在本轮部署证据。回退程序前仍需确认当前版本及整合聊天的部署安排，不能覆盖后来新装的版本。本归档不恢复业务数据、配置、系统或主控进程，不含密码、私钥或认证配置。
