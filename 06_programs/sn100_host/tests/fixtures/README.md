# 公共匿名测试输入

这些小文件提供可重复的固定输入，不能当生产配置或真实患者数据使用。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [accounts.json](accounts.json) | 匿名账户测试输入；测试身份不代表现场生产账号。 |
| [history_reports.json](history_reports.json) | 匿名历史报告样例，供本地查询/导出验证。 |
| [history_report_options.json](history_report_options.json) | 匿名报告输出选项。 |
| [protocol_diagnostics.json](protocol_diagnostics.json) | 通信异常和诊断场景输入。 |
| [protocol_replay.json](protocol_replay.json) | 协议回放输入。 |
| [status_replay.json](status_replay.json) | 状态回放事件；at_ms是虚拟记录时间，不是实测延迟。 |
| [rk_lifecycle_固定测试数据.c](rk_lifecycle_fixture.c) | 部署/关闭从启动、运行到关闭的过程测试使用的最小程序。 |

格式和期望以消费这些输入的专项测试为准；运行后生成的数据库、日志和输出不要写回本目录。
