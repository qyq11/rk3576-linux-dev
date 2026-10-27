# 基础设置的数据存取源码

这里的data是代码目录，不是可以删除的测试数据。医院、账户及本地连接设置由这些类校验和保存。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [hospital_store.cpp / hospital_store.h](hospital_store.cpp) | 医院名称和五类字典的读取、保存、删除、提交后字典查询。 |
| [hospital_dictionary_query.h](hospital_dictionary_query.h) | 报告等页面查询已提交医院字典时使用的请求/回复数据。 |
| [account_store.cpp / account_store.h](account_store.cpp) | 账户增删改、会话/角色、密码验证及默认用户名保存。 |
| [printer_settings_store.cpp / printer_settings_store.h](printer_settings_store.cpp) | 枚举打印设备、读取和保存本地打印机选择；保存配置不等于已出纸。 |
| [lis_settings_store.cpp / lis_settings_store.h](lis_settings_store.cpp) | 保存LIS本地参数并提供串口候选；不负责完整LIS网络收发。 |
| [serial_port_catalog.cpp / serial_port_catalog.h](serial_port_catalog.cpp) | 按本机环境列出可用串口名称，供设置页选择。 |

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
