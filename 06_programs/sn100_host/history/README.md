# 历史查询、结果保存与报告输出

本目录负责已保存结果的查询/编辑/显示，以及报告预览、文件输出和打印桥接。匿名历史与本地原值结果是不同模式，不能混成真实检测来源。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [history_page.cpp / history_page.h](history_page.cpp) | 历史列表、条件查询、患者编辑、勾选导出和报告按钮入口。 |
| [history_store.cpp / history_store.h](history_store.cpp) | 历史库初始化、查询、患者保存、记录隐藏及业务存储接口。 |
| [result_ingest_store.cpp](result_ingest_store.cpp) | 正式任务预约、四类原值写入、覆盖/复测等结果存储后端。 |
| [result_project_references.cpp / result_project_references.h](result_project_references.cpp) | 结果引用真实项目时的目录读取和引用保护。 |
| [history_current_display_state.cpp / history_current_display_state.h](history_current_display_state.cpp) | 按请求身份接收结果显示回复，并在配置失效时清掉旧显示。 |
| [history_current_format.cpp](history_current_format.cpp) | 将当前已提交的项目显示配置应用到历史结果显示。 |
| [history_value_text.cpp](history_value_text.cpp) | 将结果值转换为显示文字的共用逻辑。 |
| [result_statistics.cpp / result_statistics.h](result_statistics.cpp) | 结果统计和平均值计算的适用规则。 |
| [report_controller.cpp / report_controller.h](report_controller.cpp) | 报告预览/导出/打印动作及辅助进程的协调和失败反馈。 |
| [report_document.cpp / report_document.h](report_document.cpp) | 从结果快照组成报告文档和打印列。 |
| [report_render.cpp / report_render.h](report_render.cpp) | 把报告文档交给QtRPT报表引擎。 |
| [report_renderer.cpp](report_renderer.cpp) | 独立报告辅助程序入口，用于渲染或输出并返回实际退出状态。 |
| [report_pdf_mapping.cpp / report_pdf_mapping.h](report_pdf_mapping.cpp) | 处理中文PDF文本映射相关问题。 |
| [report_dictionary_dialog.cpp / report_dictionary_dialog.h](report_dictionary_dialog.cpp) | 报告患者字段选择医院字典时的查询和选择窗。 |
| [report_remaining_store.cpp](report_remaining_store.cpp) | 显示列、报告记录隐藏等剩余本地业务的存储实现。 |
| [report_remaining_display_dialog.cpp / report_remaining_display_dialog.h](report_remaining_display_dialog.cpp) | 报告显示列及顺序设置弹窗。 |
| [report_remaining_view.h](report_remaining_view.h) | 报告列和视图的数据定义。 |
| [report_remaining_html.h](report_remaining_html.h) | HTML输出相关逻辑。 |
| [prepare_report_arm64.sh](prepare_report_arm64.sh) | 准备报告目标所需的ARM64交叉依赖。 |
| [report_remaining_wsl.sh](report_remaining_wsl.sh) | 报告剩余本地功能的WSL验证入口。 |
| [main.cpp](main.cpp) | 此功能的独立演示/命令行入口；统一程序中连接各功能的代码另在shell。 |
| [build.sh](build.sh) | 为此独立目标交叉编译ARM64程序，参数是绝对输出文件路径。 |
| [wsl.sh](wsl.sh) | 在WSL本机构建、运行或调用专项测试；可用动作以脚本为准。 |
| [frm_report_material.ui](frm_report_material.ui) | 历史报告主页面布局。 |
| [history.pro](history.pro) | 本功能的独立qmake工程定义。 |
| [history_core.pri](history_core.pri) | 历史查询和结果存储的共用构建清单。 |
| [history_report.pri](history_report.pri) | 历史页接入报告预览/输出能力的构建清单。 |
| [mask_dlg_display_setting.ui](mask_dlg_display_setting.ui) | 报告显示列设置弹窗布局。 |
| [mask_dlg_export.ui](mask_dlg_export.ui) | 报告文件导出选项弹窗布局。 |
| [mask_dlg_query_report.ui](mask_dlg_query_report.ui) | 历史报告查询条件弹窗布局。 |
| [mask_dlg_report_detail.ui](mask_dlg_report_detail.ui) | 报告详细结果弹窗布局。 |
| [mask_dlg_select.ui](mask_dlg_select.ui) | 报告等场景使用的字典候选选择弹窗布局。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [printer_application_origins.json](printer_application_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [renderer.pro](renderer.pro) | 独立报告辅助程序的qmake工程定义。 |
| [report_dictionary.pri](report_dictionary.pri) | 报告字段读取医院字典的对话框及依赖构建清单。 |
| [report_dictionary_origins.json](report_dictionary_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [report_dictionary_resources.qrc](report_dictionary_resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [report_engine.pri](report_engine.pri) | 报告渲染引擎相关的构建清单。 |
| [report_resources.qrc](report_resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [resources/](resources) | 此功能的图标等界面资产；资源名称和加载入口见.qrc。 |
| [resources.qrc](resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [result_ingest_origins.json](result_ingest_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [templates/](templates) | 原报告布局模板，供报告引擎加载。 |

## 输出文件在哪里

输出位于显式运行数据根下的`history/exports/`。保存业务资料与导出PDF/CSV/HTML是不同操作；文件在RK生成时不会自动出现在Windows。适用格式、未知单位限制和实体打印待验范围查[操作说明](../../../01_docs/knowledge/sn100_host_manual_test_guide.md)。`templates/report.xml`是原报告布局，`resources.qrc`等负责加载资源。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
