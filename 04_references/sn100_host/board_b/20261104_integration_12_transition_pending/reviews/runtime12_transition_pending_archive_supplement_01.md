# 第12批实际换版待处理：部分结果归档补充

2026-11-04。本补充替代旧草案中“等待新版七阶段完成后归档”的本次执行目标；旧`runtime12_evidence_archive_draft_01.md`及所有审查原件保留不改。建议Root只归档已发生的部分结果到全新`04_references/sn100_host/board_b/20261104_integration_12_transition_pending/`。本Agent仅写本temp方案，不创建公共归档、不收证/联板、不重试关闭。

## 本轮实际收口口径

- 新02链metadata、prepare、preflight真实外层均0；stop唯一正常helper调用返回4/CLOSE_PENDING，整阶段外层4，没有stopreceipt，旧C7的result.txt/exit_code仍不存在。它们不是APP_EXIT0、clean1或成功释放显示。
- 已读Host本次只读诊断原件：七INI/CFG匹配ad11，三指针不变；current仍C7，run20261103_221819_662833，PID669382/ticks2102645、原exeSHA仍在。全文日志过滤原件明确有`SHUTDOWN_REQUEST signal= SIGTERM`和`SHUTDOWN_REJECTED window_guard_or_unavailable`。保留原英文及真实退出，不将其解释成已经正常关闭。
- 新发布6922只完成既有构建/独立存包及平铺准备/普通预检；尚未activate/launch/observe，没有新LIS collection或新版RK软件结果。仍显示的是旧C7，不能以“留屏新版通过”标题归档。
- 不再次close/TERM/stop，不强杀或自动重试，不继续activate/launch/observe；原主控/相机不启停。当前缺键鼠/人工处理条件列待验，不追问密码、不造登录或跳过关闭保护。

## 这次只收实际存在的原件

| 分组 | 具体范围 |
| --- | --- |
| `new_chain02/inputs_code/` | core652、Root批准、stable意图、fresh_baseline及provenance、新03基线四原件；Root快照cadf与10代码、实际Host工具及固定依赖，源表/包等既有归档用准确SHA链接。 |
| `new_chain02/metadata_prepare_preflight/` | 仅三阶段实际authorization、generated control/generation_manifest、生成/语法检查日志与exit；board08派发sh/log/exit/started，CAP receipt/log/exit/provenance及stderr。明示只有这三阶段真实0；prepare复用flat标志保实际值。 |
| `new_chain02/stop_pending/` | stop授权9b892、control0f499、生成/语法检查实际日志、board08 stop四原件；实际远端stdout/stderr/actual_exit已由诊断捕获的原文与SHA；`runtime12r2_stop_pending_readonly_01.{sh,log,exit,started}`和`runtime12r2_close_log_readonly_01.{sh,log,exit,started}`。有真实独立原件才复制；没有的stopreceipt/result/exit不建占位。未执行的stop收集脚本只列工具，不称已取齐stop9。 |
| `previous_chain01/` | 旧3446链实际metadata/prepare/preflight0的核心、授权/生成物、board08/CAP原件；唯一stop79及旧相机变化诊断、换基线原因和新03来源映射。旧79在任何关闭调用前拒绝，新4发生正常关闭请求后被窗口守卫拒绝，两种失败分开。 |
| `reviews/` | 实际相关核心/生成器/Shell/Host/stop实物和本补充报告；proof01/02代码与静态报告可作“未执行候选工具”保留。02静态审通过只证明工具源码范围，绝不成为远端采集/封口或新版软件结果。 |

不收未来activate/launch/observe“成功”材料，不编造七阶段汇总，不运行proof02或LIS collector来补不存在的生产链。旧C7已持续运行的历史软件1项结果不移成6922结果；资源/人工实屏/真实LIS与仪器均另列待验。

## 归档方法沿原草案，成功门改为部分事实门

新README明确标题“换版待处理”，分别列已完成编译/存包/三阶段准备、stop4及旧链79、当前仍旧C7、缺失新版启动/观察和业务结果，以及禁止重试/强杀的现边界。采用`source_records.json`逐原件记录源路径/归档路径/SHA/bytes/所属链阶段，复制后逐字回读并核源未变；原exit换行与CAP派生来源均保留。叶级`.gitattributes`用`* -text`，最终`SHA256SUMS`列除自身外全部文件。外部固定依据单列准确路径/SHA，不复制INI正文、DB、患者正文、known_hosts或凭据。不清理/覆盖任何旧文件。

部分归档可核“已存在原件复制和清单完整”，不能核“七阶段运行通过”。没有新程序，所以新程序退出/观察结果写“未启动、未执行”，不造null运行结果；旧C7本次未退出，退出结果未评估。后续解决真实窗口保护前，仍停在此处。

本次Host实读关键指纹：stop外层exit4为`7c468f5e59f2871b946e051445493bbcace531d597edbbcc9935e7d02d025114`（3字节）；stop.log为`d286b53f659ae391a85c719f07d2693b6a0b3d1c52e57bf6deab028ec68a77eb`（23字节）；pending只读诊断log为`0dede766af16756dc0d4883085c5488b4c6f534f213d5965c9ee5ebe72221d52`（3147字节）。这只是读已收原件，不是本Agent新增板端观测。
