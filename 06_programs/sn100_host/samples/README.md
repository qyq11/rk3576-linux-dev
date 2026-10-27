# 样本架与本地任务预设

这里准备“哪个样本要测哪些项目”。保存的是本地预设和草稿；目前不能据此认为任务已发给真实仪器或已产生检测结果。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [sample_page.cpp / sample_page.h](sample_page.cpp) | 普通架/急诊架、样本明细、批量与多项测试的页面操作。 |
| [sample_store.cpp / sample_store.h](sample_store.cpp) | 样本架、孔位、项目预设、多项组合的保存/读取与权限校验。 |
| [sample_types.h](sample_types.h) | 样本任务、孔位、架、组合模式和快照定义。 |
| [sample_widgets.cpp / sample_widgets.h](sample_widgets.cpp) | 样本架/孔位/任务等可视控件与选择事件。 |
| [sample_task_reset.cpp / sample_task_reset.h](sample_task_reset.cpp) | 本地复位/复测草稿与任务状态转换规则，不直接下发设备。 |
| [sample_qc_preset.cpp / sample_qc_preset.h](sample_qc_preset.cpp) | 从质控品、批次、项目快照准备并复核单个孔位的质控样本预设。 |
| [sample_qc_protocol.h](sample_qc_protocol.h) | 样本与质控功能之间的稳定身份、预约和完成回复数据。 |
| [sample_qc_storage.cpp](sample_qc_storage.cpp) | 质控样本身份、版本、引用及提交过程的存储实现。 |
| [cuscheckheader.cpp / cuscheckheader.h](cuscheckheader.cpp) | 带勾选操作的表头。 |
| [cuscombox_base.cpp / cuscombox_base.h](cuscombox_base.cpp) | 原版组合选择控件。 |
| [cusracklistwidget.cpp / cusracklistwidget.h](cusracklistwidget.cpp) | 样本架列表及滚动手势控件。 |
| [main.cpp](main.cpp) | 此功能的独立演示/命令行入口；统一程序中连接各功能的代码另在shell。 |
| [wsl.sh](wsl.sh) | 在WSL本机构建、运行或调用专项测试；可用动作以脚本为准。 |
| [combination_mode_origins.json](combination_mode_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [cussamplehole.ui](cussamplehole.ui) | 单个样本孔位控件布局。 |
| [cussamplerack.ui](cussamplerack.ui) | 样本架控件布局。 |
| [frmsamplematerial.ui](frmsamplematerial.ui) | 样本设置主页面的样本架与任务区域布局。 |
| [frmsamplesettingmaterial.ui](frmsamplesettingmaterial.ui) | 样本设置使用的原版资料/设置区域布局。 |
| [maskdlgcombasetting.ui](maskdlgcombasetting.ui) | 多项测试组合设置弹窗布局。 |
| [maskdlgracknosetting.ui](maskdlgracknosetting.ui) | 样本架号设置弹窗布局。 |
| [maskdlgrepeatsetting.ui](maskdlgrepeatsetting.ui) | 重复测试设置弹窗布局。 |
| [maskdlgsampledetail.ui](maskdlgsampledetail.ui) | 单孔样本明细与项目选择弹窗布局。 |
| [material/](material) | 沿用的界面控件及依赖，保持原交互和样式。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [resources/](resources) | 此功能的图标等界面资产；资源名称和加载入口见.qrc。 |
| [sample_qc_values.pri](sample_qc_values.pri) | 单孔质控样本值与引用数据支持的构建清单。 |
| [samples.pro](samples.pro) | 本功能的独立qmake工程定义。 |
| [samples_core.pri](samples_core.pri) | 样本预设的共用源码与界面构建入口。 |
| [samples_resources.qrc](samples_resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [samples_standalone.pri](samples_standalone.pri) | 样本独立演示程序需要的接入清单。 |
| [tests/](tests) | 此功能专项测试源码和固定输入；运行产物写入temp。 |

明细弹窗里的“设置”先更新架草稿，外层“更新设置”才持久保存。完整操作见[手动测试说明](../../../01_docs/knowledge/sn100_host_manual_test_guide.md)。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
