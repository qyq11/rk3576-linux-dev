# SN100统一主窗口与运行入口

完整主窗口在这里组装各业务页、后台数据存取和正常退出流程。根main.cpp对应早期独立设置页，不能当作统一程序入口。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [main.cpp](main.cpp) | 统一程序入口：解析显式运行模式、建立数据根和后台线程，接入各页并管理退出。 |
| [frmmain.cpp / frmmain.h](frmmain.cpp) | 主窗口、导航、页面切换、业务操作入口和状态提示。 |
| [frmuserset.cpp / frmuserset.h](frmuserset.cpp) | 用户设置容器，把医院、账户、打印等页面放进原设置区域。 |
| [catalog_shell.cpp](catalog_shell.cpp) | 项目与试剂页面和数据存取的共用代码衔接。 |
| [history_shell.cpp](history_shell.cpp) | 历史查询、结果显示与报告页面操作与处理代码的连接。 |
| [account_resources.qrc](account_resources.qrc) | 账户页使用的资源索引。 |
| [instrument_shell.cpp](instrument_shell.cpp) | 仪器操作页及人工维护登记操作与处理代码的连接。 |
| [device_status_shell.cpp](device_status_shell.cpp) | 离线文件回放、本机模拟状态到界面的代码连接和失效处理。 |
| [login_shell.cpp](login_shell.cpp) | 原登录卡、本地账户验证、会话切换和忙状态保护。 |
| [printer_shell.cpp](printer_shell.cpp) | 本地打印机设置页与处理代码的连接。 |
| [printer_report_shell.cpp / printer_report_shell.h](printer_report_shell.cpp) | 已保存打印机选择与报告打印动作之间的桥接。 |
| [lis_shell.cpp](lis_shell.cpp) | LIS（实验室信息系统）本地连接设置页与处理代码的连接。 |
| [lis_raw_inbox_shell.cpp](lis_raw_inbox_shell.cpp) | 显式离线原始通信消息收件箱入口、权限和从启动、运行到关闭的过程接线。 |
| [language_shell.cpp](language_shell.cpp) | 原语言页接入；不能据文件名推定英文翻译已完成。 |
| [alarm_shell.cpp](alarm_shell.cpp) | 告警历史查看窗口操作与处理代码的连接。 |
| [quality_shell.cpp](quality_shell.cpp) | 质控页面和后台数据存取代码的连接。 |
| [calibration_shell.cpp](calibration_shell.cpp) | 校准页面、目录引用和后台数据存取代码的连接。 |
| [sample_qc_shell.cpp / sample_qc_shell.h](sample_qc_shell.cpp) | 样本预设选择质控品/批次时的跨功能引用保护和预约完成接线。 |
| [shutdown_request.cpp / shutdown_request.h](shutdown_request.cpp) | 关闭前检查未保存内容、后台任务和退出状态；不等于界面红色关闭按钮已经接好。 |
| [build.sh](build.sh) | 统一主程序ARM64构建入口，与根独立页build.sh区别使用。 |
| [wsl.sh](wsl.sh) | WSL统一主程序及专项测试入口；integrated-run使用完整本地页面组合。 |
| [rk.sh](rk.sh) | WSL侧统一程序准备、部署、核对、启动/停止和取证流程；只有明确远程动作访问RK。 |
| [rk_board.sh](rk_board.sh) | RK侧发布校验、独立运行目录、启动/停止、退出与取证的从启动、运行到关闭的过程实现。 |
| [prepare_runtime.sh](prepare_runtime.sh) | 为ARM64离线检查准备私有依赖；其中loader只用于检查，不能当系统库安装。 |
| [package_arm64.py](package_arm64.py) | 电脑侧生成含程序、依赖、资源和清单的ARM64发布包。 |
| [check_arm64.py](check_arm64.py) | 电脑侧检查ARM64 ELF依赖、运行路径和包内容。 |
| [arm64_object_reuse.py](arm64_object_reuse.py) | 按构建输入依据管理对象复用；不是板端业务程序。 |
| [frmmain.ui](frmmain.ui) | 统一主窗口的导航、功能画布、状态和底部操作按钮布局。 |
| [frmuserset.ui](frmuserset.ui) | 用户设置容器的子页面与入口布局。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [resources/](resources) | 此功能的图标等界面资产；资源名称和加载入口见.qrc。 |
| [resources.qrc](resources.qrc) | 本功能资源文件与程序内资源路径的映射。 |
| [shell.pri](shell.pri) | 统一主程序共享构建清单：汇入各功能源码、界面、资源和公共控件。 |
| [shell.pro](shell.pro) | 本功能的独立qmake工程定义。 |
| [widgets/](widgets) | 主窗口共享控件和按钮/动画依赖。 |

## 用户怎么打开

RK日常手动操作使用上级`rk_control.sh`安装成的`sn100.sh`，详见[操作说明](../../../01_docs/knowledge/sn100_host_manual_test_guide.md)。开发者构建/打包流程见[工程README](../README.md)。仅需看现有程序时不要误运行部署命令。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
