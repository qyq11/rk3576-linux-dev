# 已有告警历史的查询界面

用于查看已存在的告警历史。它不是完整的实时告警、蜂鸣或设备动作之间的相互限制及故障保护实现。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [alarm_history_host.cpp / alarm_history_host.h](alarm_history_host.cpp) | 告警历史窗口的打开、忙状态和退出协调。 |
| [alarm_history_page.cpp / alarm_history_page.h](alarm_history_page.cpp) | 最近当日记录、日期区间查询、分页/显示预算和取消交互。 |
| [alarm_history_store.cpp / alarm_history_store.h](alarm_history_store.cpp) | 打开指定已有库、分段读取历史、取消和关闭。 |
| [alarm_history_core.pri](alarm_history_core.pri) | 已有告警历史读取核心的构建清单。 |
| [alarm_history_page.pri](alarm_history_page.pri) | 告警历史窗口、日期控件与资源构建清单。 |
| [alarm_history_resources.qrc](alarm_history_resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [calendar/](calendar) | 告警历史使用的日期选择控件。 |
| [cusmaskcalendar.ui](cusmaskcalendar.ui) | 告警历史日期选择的遮罩弹窗布局。 |
| [maskdlgabnormal.ui](maskdlgabnormal.ui) | 告警历史列表与查询区域布局。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [page_origins.json](page_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [resources/](resources) | 此功能的图标等界面资产；资源名称和加载入口见.qrc。 |

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
