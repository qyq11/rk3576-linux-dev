# 实际操作与验证记录

这里回答“当时做了什么、输入是什么、实际输出是什么、通过到哪一步”。同一事项持续追加到同一份记录，文件名日期不代表最后更新日期。

[返回上级说明](../README.md)

**板 A：Qt；板 B：后续其他用途。** [归属核查与待确认项](20261021_ai_workflow.md#historical-board-audit)。

## 文件用途

| 文件或目录 | 归属 | 用途 |
| --- | --- | --- |
| [20261102_can.md](20261102_can.md) | 板A、板B分节 | 两板CAN0配置、重启与发送确认；独立接收及真实硬件收发待验。 |
| [20261030_system_image.md](20261030_system_image.md) | 电脑编译；烧录板 B待确认 | 新板Ubuntu Server整机镜像及编译环境，编译和归档核对通过。 |
| [20261021_固定IP地址.md](20261021_固定IP地址.md) | 板 A | 固定地址、重启保留、ping及SSH登录核对。 |
| [20261021_TTL串口调试验证.md](20261021_TTL串口调试验证.md) | 第1—6、9节板A；第7—8节板B | 板A历史回环及本轮五组串口配置；两板五路打开检查通过，当前实际收发待验。 |
| [20261021_gpio_output.md](20261021_gpio_output.md) | 板 A；含电脑编译 | 物理13针输出与万用表电压核对。 |
| [20261021_Samba共享文件夹.md](20261021_Samba共享文件夹.md) | 板 A | 文件共享、目录权限与主目录迁移。 |
| [20261021_workspace_setup.md](20261021_workspace_setup.md) | 电脑侧；历史RK操作为板 A | WindTerm、WSL工作目录、root会话及文件整理。 |
| [20261021_ai_workflow.md](20261021_ai_workflow.md) | 共用；原板端执行为板 A | 编译部署流程、工作约定与板卡归属核查。 |
| [20261022_system_architecture.md](20261022_system_architecture.md) | 共用设计资料 | 设备分工与接口需求图。 |
| [20261022_preempt_rt.md](20261022_preempt_rt.md) | 板 A | 实时内核构建、烧录、启动、延迟测量与回退依据。 |
| [20261022_qt_environment.md](20261022_qt_environment.md) | 板 A相关环境；含电脑侧 | Qt版本、编译依赖、中文输入与恢复入口。 |
| [20261022_qt_environment.json](20261022_qt_environment.json) | 同上；原件保留 | Qt环境与工具版本资料。 |
| [20261022_sn100_host_migration.md](20261022_sn100_host_migration.md) | 已有RK部署与验证为板 A；电脑测试另计 | SN100功能实现、部署、失败与复测记录。 |

## 顺着记录找证据

记录中的结论要结合当批源码、环境、日志和校验清单理解。图片在[照片目录](../03_photos/README.md)，长期日志及清单在[资料目录](../04_references/README.md)，恢复用包在[备份目录](../07_backups/README.md)。新的README不另立一套测试结论。
