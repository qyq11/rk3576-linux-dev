# 仪器操作页与人工维护登记

这里承载原仪器操作界面及人工维护记录。按钮存在与真实泵阀、针、抓手等设备动作可用是不同状态。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [instrument_page.cpp / instrument_page.h](instrument_page.cpp) | 仪器操作页、设备状态显示和人工维护确认入口；未实现的真实动作仍受限制。 |
| [maintenance_store.cpp / maintenance_store.h](maintenance_store.cpp) | 日/周/月维护登记、月份查询、身份/动作/日期校验与数据库提交。 |
| [frmdriverset.ui](frmdriverset.ui) | 原仪器操作各子页和设备状态区域布局。 |
| [instrument_resources.qrc](instrument_resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [instrument_widgets.pri](instrument_widgets.pri) | 仪器页及其控件/资源构建清单。 |
| [maintenance_core.pri](maintenance_core.pri) | 人工维护记录存储的构建清单。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [resources/](resources) | 此功能的图标等界面资产；资源名称和加载入口见.qrc。 |

维护数据库在指定运行根下的`instrument/`，不是本源码目录。真实动作需要现场机型、协议、状态和停止规则后逐项验证；不要用参考工程的旧连接配置启动。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
