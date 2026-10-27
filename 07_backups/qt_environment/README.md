# Qt依赖与环境恢复输入

按Windows电脑、WSL电脑和RK ARM64区分保存包与配置；不同架构的库不能混用。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [sn100_base_arm64/](sn100_base_arm64/README.md) | SN100专用的Qt5.15.13 ARM64基础开发头文件和运行库；prepare_arm64.sh及shell/prepare_runtime.sh直接核验并解包。 |
| [sn100_arm64/](sn100_arm64) | SN100使用的ARM64 SQL/SQLite插件相关输入。 |
| [sn100_protocol_arm64/](sn100_protocol_arm64) | 通信模块需要的ARM64网络及关联依赖。 |
| [sn100_report_amd64/](sn100_report_amd64) | WSL本机报告验证所需的amd64依赖。 |
| [sn100_report_arm64/](sn100_report_arm64) | ARM64报告构建相关依赖。 |
| [sn100_report_inspection_arm64/](sn100_report_inspection_arm64) | ARM64报告离线检查所需输入，不能推定为板端安装方案。 |
| [sn100_report_runtime_arm64/](sn100_report_runtime_arm64) | 报告运行库和字体配置等相关依赖。 |
| [sn100_rk_runtime/](sn100_rk_runtime) | 私有运行/离线检查所需loader与SQLite输入；恢复范围查source.json和原记录。 |
| [sn100_samples_arm64/](sn100_samples_arm64/README.md) | 样本界面SVG相关ARM64依赖及签名/来源核对。 |
| [wsl_amd64/](wsl_amd64) | WSL本机Qt开发包、工具包及Kit配置和前后清单。 |
| [wsl_ime/](wsl_ime/README.md) | WSL中文输入安装包、前后清单和必要用户配置。 |

每组的`source.json`、版本文件和`SHA256SUMS`解释来源与身份。实际安装/恢复边界查[Qt环境记录](../../02_records/20261022_qt_environment.md)；输入法的使用与验证也在SN100移植记录中有链接。
