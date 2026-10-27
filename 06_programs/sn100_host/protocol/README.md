# 通信编码、只读查询与模拟验证

这里拆出报文边界、校验、只读状态、超时和断线处理。当前TCP入口限定本机模拟连接，不能直接当作已支持现场仪器。

[返回上级说明](../README.md)

## 文件用途

| 文件或目录 | 用途 |
| --- | --- |
| [frame_codec.cpp / frame_codec.h](frame_codec.cpp) | 报文编码、分包/组包、校验和超时清理。 |
| [status_session.cpp / status_session.h](status_session.cpp) | 状态请求/回复会话、超时和旧状态失效规则。 |
| [read_only_messages.cpp / read_only_messages.h](read_only_messages.cpp) | 只读查询的报文内容、回复字段和解析。 |
| [read_only_client.cpp / read_only_client.h](read_only_client.cpp) | 将只读查询、传输状态、请求结束和错误反馈组合成客户端。 |
| [tcp_transport.cpp / tcp_transport.h](tcp_transport.cpp) | Qt TCP连接和收发队列；connectLoopback限定本机地址。 |
| [status_replay.cpp / status_replay.h](status_replay.cpp) | 从显式JSON文件回放匿名状态事件，不创建仪器连接。 |
| [status_loopback.cpp / status_loopback.h](status_loopback.cpp) | 创建本轮自己的本机模拟端，后台查询并返回状态及具体TX/RX。 |
| [main.cpp](main.cpp) | 协议文件回放命令行入口。 |
| [client_demo_main.cpp](client_demo_main.cpp) | 只读客户端示例入口。 |
| [loopback_main.cpp](loopback_main.cpp) | 本机回环传输验证入口。 |
| [soak_main.cpp](soak_main.cpp) | 连续通信和恢复诊断入口。 |
| [client_build.sh](client_build.sh) | 构建只读客户端示例。 |
| [transport_build.sh](transport_build.sh) | 构建传输回环验证目标。 |
| [soak_build.sh](soak_build.sh) | 构建连续通信验证目标。 |
| [prepare_transport_arm64.sh](prepare_transport_arm64.sh) | 准备通信目标的ARM64交叉依赖。 |
| [wsl_transport.sh](wsl_transport.sh) | WSL传输专项构建/运行。 |
| [wsl_recovery.sh](wsl_recovery.sh) | WSL故障恢复诊断入口。 |
| [build.sh](build.sh) | 为此独立目标交叉编译ARM64程序，参数是绝对输出文件路径。 |
| [wsl.sh](wsl.sh) | 在WSL本机构建、运行或调用专项测试；可用动作以脚本为准。 |
| [client.pro](client.pro) | 独立只读客户端示例的qmake工程定义。 |
| [diagnostic_origins.json](diagnostic_origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [loopback.pro](loopback.pro) | 本机TCP回环验证程序的qmake工程定义。 |
| [origins.json](origins.json) | 原版来源、字段依据或本地适配差异清单。 |
| [protocol.pro](protocol.pro) | 协议文件回放程序的qmake工程定义。 |
| [soak.pro](soak.pro) | 连续通信/故障恢复验证程序的qmake工程定义。 |
| [status_loopback.pri](status_loopback.pri) | 本机模拟只读状态和相关传输依赖构建清单。 |
| [status_loopback_origins.json](status_loopback_origins.json) | 原版来源、字段依据或本地适配差异清单。 |

文件回放的时间是记录里的虚拟时间，不能当作真实响应时延。真实仪器地址、协议/字符编码和任务/结果产生、保存和显示流程按当前计划补齐；本次没有执行任何收发。

## 怎样顺着代码看

先看页面入口如何接收用户操作，再跟到存储或计算实现，最后看失败反馈、草稿保护和关闭流程。`.h`是接口与数据定义，`.cpp`是实现，`.ui`是Qt Designer界面，`.pro`是独立工程，`.pri`是共享构建清单，`.qrc`是资源索引；`origins.json`及同类文件记录原版来源和有意差异。

这里只解释文件职责，最新功能缺口和验收状态查[移植计划](../../../01_docs/plans/sn100_host_migration_plan.md)，实际执行查[同一记录](../../../02_records/20261022_sn100_host_migration.md)。本目录里的源码/配置定义不等于运行数据库。
