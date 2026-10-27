# 分析项目与结果显示设置

分析项目定义“测什么、如何配置、怎样显示结果”。这里保存项目/单位/动作方案等本地配置，不执行真实检测。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [project_page.cpp / project_page.h](project_page.cpp) | 项目列表、草稿、保存/复制/删除和结果显示配置的用户入口。 |
| [projects_store.cpp / projects_store.h](projects_store.cpp) | 项目及单位读取、配置提交、版本校验和被其他业务引用时的保护。 |
| [original_project_repository.cpp / original_project_repository.h](original_project_repository.cpp) | 保持原目录和原表，直接读取、保存原项目DB与单位DB；保留未知参数并核对原引用。 |
| [project_advanced.cpp / project_advanced.h](project_advanced.cpp) | 高级方案编辑，包括样本/试剂/清洗液等动作字段与候选。 |
| [project_format_dialog.cpp / project_format_dialog.h](project_format_dialog.cpp) | 四类原值结果显示的独立配置弹窗及读写状态。 |
| [project_current_format.h](project_current_format.h) | 已提交显示配置所需的数据结构及当时副本定义。 |
| [projects_current_format.cpp](projects_current_format.cpp) | 当前显示配置的数据库读取、校验及替换实现。 |
| [cuscheckbox.cpp / cuscheckbox.h](cuscheckbox.cpp) | 沿用原版外观和交互的复选控件。 |
| [main.cpp](main.cpp) | 此功能的独立演示/命令行入口；统一程序中连接各功能的代码另在shell。 |
| [build.sh](build.sh) | 为此独立目标交叉编译ARM64程序，参数是绝对输出文件路径。 |
| [wsl.sh](wsl.sh) | 在WSL本机构建、运行或调用专项测试；可用动作以脚本为准。 |
| [frmsystemmaterial.ui](frmsystemmaterial.ui) | 分析项目的基础字段、高级设置和结果显示区域布局。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [projects.pro](projects.pro) | 本功能的独立qmake工程定义。 |
| [projects_core.pri](projects_core.pri) | 项目功能共用构建入口；按实际引用带入存储与界面支持。 |
| [projects_resources.qrc](projects_resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [projects_standalone_resources.qrc](projects_standalone_resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [projects_store_support.pri](projects_store_support.pri) | 项目存储和配置支持代码的构建清单。 |
| [projects_widgets.pri](projects_widgets.pri) | 分析项目页面与控件的构建清单。 |
| [resources/](resources) | 此功能的图标等界面资产；资源名称和加载入口见.qrc。 |
| [tests/](tests) | 此功能专项测试源码和固定输入；运行产物写入temp。 |

空库从零配置完整项目/单位仍是已登记缺口，不能把自动添加的演示项目当现场有效配置。此目录的`tests/`验证本地存取和高级方案；真实算法参数、设备动作及单位规则依据按当前计划逐项补齐。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
