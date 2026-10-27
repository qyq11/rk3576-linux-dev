# 校准资料与原始点

校准用于建立测量值与已知浓度之间的对应关系。这里管理本地记录、项目/试剂引用和原始点图，完整拟合、逆算、审核应用仍有缺口。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [calibration_page.cpp / calibration_page.h](calibration_page.cpp) | 校准记录选择、新建/保存/复制/删除及草稿交互。 |
| [calibration_store.cpp / calibration_store.h](calibration_store.cpp) | 校准记录和原始点存取、会话校验，以及项目/试剂跨库引用保护。 |
| [calibration_types.h](calibration_types.h) | 校准点与记录的数据结构。 |
| [calibrationchart.cpp / calibrationchart.h](calibrationchart.cpp) | 显示已保存或编辑的原始点，包含坐标和单位。 |
| [main.cpp](main.cpp) | 此功能的独立演示/命令行入口；统一程序中连接各功能的代码另在shell。 |
| [build.sh](build.sh) | 为此独立目标交叉编译ARM64程序，参数是绝对输出文件路径。 |
| [wsl.sh](wsl.sh) | 在WSL本机构建、运行或调用专项测试；可用动作以脚本为准。 |
| [calibration.pri](calibration.pri) | 校准页面、原始点与存储共用构建清单。 |
| [calibration.pro](calibration.pro) | 本功能的独立qmake工程定义。 |
| [frmcalibrationmaterial.ui](frmcalibrationmaterial.ui) | 校准记录、原始点和曲线主页面布局。 |
| [tests/](tests) | 此功能专项测试源码和固定输入；运行产物写入temp。 |

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
