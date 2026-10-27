# SN100的Qt基础编译依赖

这里保存SN100需要的Qt5.15.13 ARM64头文件和匹配的基础运行库。2026-10-27迁入SN100专用归属，6个原文件逐一核对SHA-256，内容没有改变。不是RK整机恢复镜像，不向RK系统目录覆盖解包。

当前恢复入口为正式工程的`06_programs/sn100_host/prepare_arm64.sh`；完整主程序运行组件使用`06_programs/sn100_host/shell/prepare_runtime.sh <绝对依赖目录>`，由现有`shell/rk.sh prepare`调用。输出位于`temp/sn100_host/`的独立依赖目录，不读取已移除的示例目录。

`source.json`、`target_info.txt`、`runtime_files.txt`和`SHA256SUMS`保留采集时的来源及文件身份；其中旧采集路径仅解释历史来源，不再作为操作命令。正式使用以本说明和当前SN100脚本为准，已有固定版本清单也仍保持原件。

[返回Qt依赖索引](../README.md) · [SN100环境说明](../../../02_records/20261022_qt_environment.md)
