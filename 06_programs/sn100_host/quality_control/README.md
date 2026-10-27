# 质控资料与质控历史

质控是用已知控制品观察检测系统是否稳定。这里有控制品/批次/项目关联、本地参数和历史图表；完整真实测量与临床失控判断另按计划补齐。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [quality_control_page.cpp / quality_control_page.h](quality_control_page.cpp) | 质控主页面、资料管理、草稿和离页保护。 |
| [qc_store.cpp / qc_store.h](qc_store.cpp) | 控制品、批次、项目关联与历史读取的存储入口。 |
| [qc_sample_reference_guard.cpp](qc_sample_reference_guard.cpp) | 样本预设引用控制品/批次等资料时的删除与变更保护。 |
| [qc_history_core.cpp / qc_history_core.h](qc_history_core.cpp) | 历史原值解析、单位证据、点选择和统计规则。 |
| [qc_history_reply.h](qc_history_reply.h) | 后台历史查询返回的批次、结果序列及状态定义。 |
| [qc_history_presenter.cpp / qc_history_presenter.h](qc_history_presenter.cpp) | 把已返回历史组织成页面图卡并处理过期回复。 |
| [cus_quality_chart.cpp / cus_quality_chart.h](cus_quality_chart.cpp) | 质控历史图卡，负责勾选、翻页和追踪入口。 |
| [qualitychart.cpp / qualitychart.h](qualitychart.cpp) | 质控点绘图、坐标范围、点击与逐点追踪。 |
| [main.cpp](main.cpp) | 此功能的独立演示/命令行入口；统一程序中连接各功能的代码另在shell。 |
| [build.sh](build.sh) | 为此独立目标交叉编译ARM64程序，参数是绝对输出文件路径。 |
| [wsl.sh](wsl.sh) | 在WSL本机构建、运行或调用专项测试；可用动作以脚本为准。 |
| [cus_quality_chart.ui](cus_quality_chart.ui) | 质控历史单张图卡及翻页/追踪区域布局。 |
| [frm_quality.ui](frm_quality.ui) | 质控主页面布局。 |
| [mask_dlg_add_control.ui](mask_dlg_add_control.ui) | 新增控制品弹窗布局。 |
| [mask_dlg_control_batch_setting.ui](mask_dlg_control_batch_setting.ui) | 质控批次设置弹窗布局。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [qc_core.pri](qc_core.pri) | 质控存取与历史核心逻辑的构建清单。 |
| [qc_history_resources.qrc](qc_history_resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [qc_standalone.pri](qc_standalone.pri) | 质控独立程序接入所需构建清单。 |
| [qc_widgets.pri](qc_widgets.pri) | 质控页面及图卡控件构建清单。 |
| [quality_control.pro](quality_control.pro) | 本功能的独立qmake工程定义。 |
| [quality_control_add_wgt.ui](quality_control_add_wgt.ui) | 质控资料新增区域布局。 |
| [quality_control_item_add_project_wgt.ui](quality_control_item_add_project_wgt.ui) | 控制品关联项目的编辑区域布局。 |
| [resources/](resources) | 此功能的图标等界面资产；资源名称和加载入口见.qrc。 |

`quality/`是较小的统计计算原型，`quality_control/`才是质控业务页；两者不能仅按英文名混用。缺少可信历史单位时不能套当前单位计算统计。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
