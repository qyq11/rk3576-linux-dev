# 凝血点研究原型

这里研究从曲线中定位凝血点的两种计算方法，不是原Windows全部算法的ARM64替代。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [clot_endpoint.cpp / clot_endpoint.h](clot_endpoint.cpp) | 曲线方向、参数校验、百分比法和最大导数法的凝血点计算。 |
| [main.cpp](main.cpp) | 此功能的独立演示/命令行入口；统一程序中连接各功能的代码另在shell。 |
| [build.sh](build.sh) | 为此独立目标交叉编译ARM64程序，参数是绝对输出文件路径。 |
| [wsl.sh](wsl.sh) | 在WSL本机构建、运行或调用专项测试；可用动作以脚本为准。 |
| [coagulation.pro](coagulation.pro) | 本功能的独立qmake工程定义。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [tests/](tests) | 此功能专项测试源码和固定输入；运行产物写入temp。 |

合成曲线在`tests/fixtures/synthetic_curve.json`。完整算法需要可移植实现或完整数学规则、真实匿名曲线与认可对照；合成数据通过不等于仪器结果通过。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
