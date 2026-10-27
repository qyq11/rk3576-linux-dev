# 基础设置页与通用弹窗

这里保留医院、账户、打印、LIS、语言和登录相关界面，以及共用消息框。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [frmhospinfosetting.cpp / frmhospinfosetting.h](frmhospinfosetting.cpp) | 医院信息页面：表格编辑、医院名称、字典操作与存储反馈。 |
| [frmaccountsetting.cpp / frmaccountsetting.h](frmaccountsetting.cpp) | 账户设置页面和身份/权限相关操作。 |
| [frmprintersetting.cpp / frmprintersetting.h](frmprintersetting.cpp) | 打印机选择与本地设置页面。 |
| [frmlissetting.cpp / frmlissetting.h](frmlissetting.cpp) | LIS连接参数页面；真实收发另有实现缺口。 |
| [language_setting_page.cpp / language_setting_page.h](language_setting_page.cpp) | 承载原语言设置界面及当前本地行为。 |
| [maskdlglogin.cpp / maskdlglogin.h](maskdlglogin.cpp) | 用户头像对应的遮罩登录卡和登录交互。 |
| [cusmessagebox.cpp / cusmessagebox.h](cusmessagebox.cpp) | 通用确认、警告和错误消息框。 |
| [cusdialogmask.cpp / cusdialogmask.h](cusdialogmask.cpp) | 弹窗背后的遮罩层。 |
| [mnemonic.cpp / mnemonic.h](mnemonic.cpp) | 医院字典等输入使用的助记符生成。 |
| [cusdialogmask.ui](cusdialogmask.ui) | 共用弹窗遮罩布局。 |
| [cusmessagebox.ui](cusmessagebox.ui) | 共用消息框的提示区域和确认/取消按钮布局。 |
| [frmaccountsetting.ui](frmaccountsetting.ui) | 账户设置的列表、用户名/密码输入和增删改布局。 |
| [frmhospinfosetting.ui](frmhospinfosetting.ui) | 医院名称、科室等字典表格与保存/增删按钮布局。 |
| [frmlanguagesetting.ui](frmlanguagesetting.ui) | 原语言选择页布局；界面文件存在不代表英文翻译已实现。 |
| [frmlissetting.ui](frmlissetting.ui) | LIS连接方式、网络/串口参数和保存按钮布局。 |
| [frmprintersetting.ui](frmprintersetting.ui) | 打印机选择和保存按钮布局。 |
| [language_setting_origins.json](language_setting_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [lis_origins.json](lis_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [lis_settings.pri](lis_settings.pri) | LIS设置页及本地存储依赖的构建清单。 |
| [login_origins.json](login_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [maskdlglogin.ui](maskdlglogin.ui) | 遮罩登录卡的用户名、密码和默认用户名选项布局。 |
| [printer_origins.json](printer_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [printer_settings.pri](printer_settings.pri) | 本地打印机设置页及存储依赖的构建清单。 |

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
