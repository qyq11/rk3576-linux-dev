# 实时内核测量脚本

这里保留固定实验脚本；部分脚本校验历史内核版本、文件校验值和日期目录，不是换个目录就能直接重跑的通用入口。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [prepare_rt_tests.sh](prepare_rt_tests.sh) | 在板端准备指定版本rt-tests工具及其校验信息。 |
| [check_rt_tests.sh](check_rt_tests.sh) | 检查cyclictest依赖、帮助输出和工具校验。 |
| [measure_normal_baseline.sh](measure_normal_baseline.sh) | 在普通内核下做空载与CPU压力延迟测量。 |
| [validate_rt_startup.sh](validate_rt_startup.sh) | 测量前核对实时内核、配置、启动分区和匹配模块。 |
| [measure_rt_latency.sh](measure_rt_latency.sh) | 在实时内核及预检通过条件下执行相同条件延迟测量。 |
| [plot_latency.py](plot_latency.py) | 在电脑上重算已归档测量并绘图；不会连接RK。 |
| [SHA256SUMS](SHA256SUMS) | 该批脚本的校验清单。 |

执行条件与实际结果见[实时内核记录](../../../02_records/20261022_preempt_rt.md)；原始包和汇总见[证据目录](../../../04_references/preempt_rt/README.md)。
