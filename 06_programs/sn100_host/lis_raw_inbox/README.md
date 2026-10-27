# LIS原报文离线收件箱

LIS是实验室信息系统。这个目录只把人工选择的匿名原报文文件解码后存入独立本地收件箱，供查看与追踪；不建立网络、不发送应答、不生成检测任务。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [lis_dsr_raw.cpp / lis_dsr_raw.h](lis_dsr_raw.cpp) | 严格解析指定UTF-8 DSR报文、片段及控制号，并比较报文内容。 |
| [lis_inbox_store.cpp / lis_inbox_store.h](lis_inbox_store.cpp) | 独立收件箱的导入、去重、列表、明细和权限校验。 |
| [lis_inbox_page.cpp / lis_inbox_page.h](lis_inbox_page.cpp) | 文件草稿、导入确认、列表和原文显示窗口。 |
| [lis_inbox_page_后台工作线程.cpp / lis_inbox_page_后台工作线程.h](lis_inbox_page_worker.cpp) | 在后台线程处理文件/数据库工作并返回真实结果。 |
| [lis_inbox_page_host.cpp / lis_inbox_page_host.h](lis_inbox_page_host.cpp) | 界面与后台线程之间的请求接线、会话变化和关闭确认。 |
| [lis_raw_inbox.pri](lis_raw_inbox.pri) | 离线原始通信消息解码、收件箱与辅助窗口构建清单。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |

统一入口在`shell/lis_raw_inbox_shell.cpp`，本地连接参数页面在`ui/frmlissetting.cpp`；两者都不能代替完整LIS任务/结果收发。当前平台限制和验证边界见上级工程README。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
