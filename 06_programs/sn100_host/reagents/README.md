# 试剂资料、批组和本地孔位

用于维护试剂名称、瓶型、批号、条码、批组和本地孔位余量。手工登记的余量与仪器探测/真实消耗分开。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [reagents_page.cpp / reagents_page.h](reagents_page.cpp) | 试剂设置用户入口、各编辑弹窗、保存反馈和未保存草稿保护。 |
| [reagent_store.cpp / reagent_store.h](reagent_store.cpp) | 试剂/瓶型/批号/条码/孔位的数据库操作，以及批组被校准等功能引用时的保护。 |
| [reagent_types.h](reagent_types.h) | 试剂、瓶型、批次、条码、孔位和快照的数据定义。 |
| [wgtreagentpanel.cpp / wgtreagentpanel.h](wgtreagentpanel.cpp) | 试剂盘/孔位区域的显示、选择和编辑请求。 |
| [cusreagenthole.cpp / cusreagenthole.h](cusreagenthole.cpp) | 单个试剂孔位的绘制、点击和双击入口。 |
| [reagent_batch_verify.sh](reagent_batch_verify.sh) | 试剂批组专项验证的脚本入口。 |
| [main.cpp](main.cpp) | 此功能的独立演示/命令行入口；统一程序中连接各功能的代码另在shell。 |
| [build.sh](build.sh) | 为此独立目标交叉编译ARM64程序，参数是绝对输出文件路径。 |
| [wsl.sh](wsl.sh) | 在WSL本机构建、运行或调用专项测试；可用动作以脚本为准。 |
| [addbatchgroupdialog.ui](addbatchgroupdialog.ui) | 新增试剂批组弹窗布局。 |
| [frmreagentmaterial.ui](frmreagentmaterial.ui) | 试剂资料、试剂盘和本地孔位设置主页面布局。 |
| [maskdlggroupsetting.ui](maskdlggroupsetting.ui) | 试剂组设置弹窗布局。 |
| [maskdlgreagentcode.ui](maskdlgreagentcode.ui) | 试剂条码设置弹窗布局。 |
| [maskdlgreagentlistsetting.ui](maskdlgreagentlistsetting.ui) | 试剂资料列表设置弹窗布局。 |
| [maskdlgreagentsetting.ui](maskdlgreagentsetting.ui) | 单个试剂/孔位设置弹窗布局。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [reagents.pri](reagents.pri) | 试剂页面、存储与控件的共享构建清单。 |
| [reagents.pro](reagents.pro) | 本功能的独立qmake工程定义。 |
| [reagents_resources.qrc](reagents_resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [resources/](resources) | 此功能的图标等界面资产；资源名称和加载入口见.qrc。 |
| [tests/](tests) | 此功能专项测试源码和固定输入；运行产物写入temp。 |
| [wgtreagentpanel.ui](wgtreagentpanel.ui) | 试剂盘孔位显示区域布局。 |

用户操作中，双击容器值可管理瓶型，双击余量值可填写本地装载/余量；详细步骤见[手动测试说明](../../../01_docs/knowledge/sn100_host_manual_test_guide.md)。探测、扫码和消耗链的状态以计划为准。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
