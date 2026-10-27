# 实时内核证据怎么找

本目录保留普通内核与PREEMPT_RT实时内核的测量和构建依据。完整条件与可恢复范围先看[实时内核记录](../../02_records/20261022_preempt_rt.md)。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [kernel.config](kernel.config) | 该轮内核构建配置。 |
| [tspi_6.1.99_rt36_candidate.patch](tspi_6.1.99_rt36_candidate.patch) | 实时内核候选补丁，用于说明本地改动。 |
| [normal_baseline.zip](normal_baseline.zip) | 普通内核测量的原始记录包。 |
| [rt_validation.zip](rt_validation.zip) | 实时内核测量的原始记录包。 |
| [normal_summary.json](normal_summary.json) | 普通内核测量汇总。 |
| [rt_summary.json](rt_summary.json) | 实时内核测量汇总。 |
| [latency_comparison.csv](latency_comparison.csv) | 普通/实时内核的延迟对比数据表。 |
| [plot_manifest.json](plot_manifest.json) | 绘图输入、输出和校验信息。 |
| [rt_candidate_selection.json](rt_candidate_selection.json) | 实时内核候选选择依据。 |
| [stage_status.json](stage_status.json) | 各阶段当时的执行状态。 |
| [test_method_manifest.json](test_method_manifest.json) | 测试方法及相关输入清单。 |
| [vendor_rt_source_manifest_20261022.json](vendor_rt_source_manifest_20261022.json) | 厂商实时内核来源清单。 |
| [verification.json](verification.json) | 本批归档核查结果。 |
| [SHA256SUMS](SHA256SUMS) | 固定证据的SHA-256校验值。 |
| [console/](console) | 串口/终端过程证据。 |
| [methods/](methods) | 测量方法相关材料。 |
| [vendor_sources/](vendor_sources) | 厂商来源核对材料。 |
| [work_logs/](work_logs) | 构建、烧录和核对过程日志。 |

测量脚本见[测试工具说明](../../05_tools/tests/preempt_rt/README.md)，内核镜像与匹配模块见[恢复输入](../../07_backups/preempt_rt/README.md)。短时延迟测试不代表业务长时间运行稳定性或同板控制已验收。
