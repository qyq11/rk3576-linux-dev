# Windows Qt 5.14 显式头文件修复前备份

来源：本仓库当前正式源码，复制于2026-10-26。只保存本次会修改的报告存储源码，不包含运行数据、参考工程或认证信息。

- 原位置：`06_programs/sn100_host/history/report_remaining_store.cpp`
- 备份文件：`report_remaining_store.cpp`
- 修前SHA-256：`2d325e1b82ac703b2ceba147a92b89b43a8c79a4f4045be7138277463aa72618`
- 对应426输入冻结清单SHA-256：`57f3b03290cc3635a5772a7169d22c1713f164186ab3b368028a0f35f8b710ed`
- 修复范围：仅增加`#include <QVariant>`，供Qt 5.14编译时取得完整类型声明。

恢复时先停止源码写入、构建和程序运行，确认目标文件没有本次修复以后的其他修改，再将本目录的源码复制回原位置并核对修前SHA。此操作仅撤回该文件的头文件修复，不恢复整个工程或环境；撤回后Windows原编译错误会重新出现。

`preimage.json`记录来源、冻结条目及首轮失败证据的SHA；本目录不代表已完成最终Windows构建、界面或仪器验证。最终诊断交接见仓库`04_references/sn100_host/report_remaining_20261025/windows_include_handoff.json`。
