# QtRPT报表引擎

当前集成QtRPT 3.0.0，从用户提供的原参考工程选取必要源码。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [NOTICE](NOTICE) | 版本、版权、Apache 2.0许可和本地补丁说明。 |
| [origins.json](origins.json) | 原始与当前SHA-256及每处本地补丁依据。 |
| [qtrpt.pri](qtrpt.pri) | 本工程的Qt5构建接入；保留NO_BARCODE，原可选XLSX实现未启用。 |
| [QtRPT/](QtRPT) | 报表引擎源码、资源和完整license.txt。 |
| [CommonFiles/](CommonFiles) | 报表使用的共用工具类及其构建清单。 |

本次只在组件上层新增说明，不改QtRPT/、CommonFiles/原有文件或其来源校验。业务报告入口查[history](../../history/README.md)。
