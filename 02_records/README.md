# 实际操作与验证记录

这里回答“当时做了什么、输入是什么、实际输出是什么、通过到哪一步”。同一事项持续追加到同一份记录，文件名日期不代表最后更新日期。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [20261021_固定IP地址.md](20261021_固定IP地址.md) | RK网口地址配置，以及Windows ping、SSH登录的实际核对。 |
| [20261021_TTL串口调试验证.md](20261021_TTL串口调试验证.md) | TTL调试登录与UART4本机回环；这两种串口用途分开说明。 |
| [20261021_gpio_output.md](20261021_gpio_output.md) | 指定40Pin引脚的GPIO输出、交叉编译和万用表电压核对。 |
| [20261021_Samba共享文件夹.md](20261021_Samba共享文件夹.md) | Windows与RK共享文件、目录权限以及共享目录迁移。 |
| [20261021_workspace_setup.md](20261021_workspace_setup.md) | 电脑工作目录、WindTerm会话、主目录迁移和历次临时文件整理。 |
| [20261021_ai_workflow.md](20261021_ai_workflow.md) | 自动编译/上传/执行工作流、失败路径验证及通俗中文沟通约定。 |
| [20261022_system_architecture.md](20261022_system_architecture.md) | 设备整体架构和RK与其他控制部分的职责边界。 |
| [20261022_preempt_rt.md](20261022_preempt_rt.md) | 实时内核的选择、构建、烧录、启动、延迟测量及回退依据。 |
| [20261022_qt_environment.md](20261022_qt_environment.md) | SN100所用Qt版本、编译依赖、中文输入和恢复入口。 |
| [20261022_qt_environment.json](20261022_qt_environment.json) | 保留的SN100 Qt环境与工具版本资料，不含已删除的显示示例记录。 |
| [20261022_sn100_host_migration.md](20261022_sn100_host_migration.md) | SN100各功能的实现、验证、失败/复测、资源测量、文件整理和板端记录。 |

## 顺着记录找证据

记录中的结论要结合当批源码、环境、日志和校验清单理解。图片在[照片目录](../03_photos/README.md)，长期日志及清单在[资料目录](../04_references/README.md)，恢复用包在[备份目录](../07_backups/README.md)。新的README不另立一套测试结论。
