# 知识与使用说明

这里帮助读者先理解项目，再找到代码或操作入口。想直接试用SN100，先看手动测试说明；想学习实现，先看功能分析。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [linux_basics.md](linux_basics.md) | 面向初学者解释Linux路径、文件、设备节点、GPIO、串口和相关命令。 |
| [sn100_host_project_guide.md](sn100_host_project_guide.md) | 解释正式工程、只读原版、规则文件、技能、计划和记录之间的关系。 |
| [sn100_host_function_and_porting_review.md](sn100_host_function_and_porting_review.md) | 介绍上位机业务、原Windows与Linux版逐项功能差异、代码阅读路线和待补实现。 |
| [sn100_host_manual_test_guide.md](sn100_host_manual_test_guide.md) | 给使用者看的RK操作说明：启动关闭、保存与导出位置、20组“点什么、应看到什么”的人工用例。 |
| [sn100_original_bin_files.md](sn100_original_bin_files.md) | 原文件用途及直接复制清单；原DB读取、输入、保存、导入导出、防卡顿和防重复点击要求，供后续窗口读取派发，按Windows原上位机维护原库。 |

## 如何理解文中的状态

功能“有代码”、电脑离线通过、RK运行通过、真实仪器通过分别看。人工用例中的预期现象不是已实测结论；实时待办以[计划](../plans/README.md)为准。
