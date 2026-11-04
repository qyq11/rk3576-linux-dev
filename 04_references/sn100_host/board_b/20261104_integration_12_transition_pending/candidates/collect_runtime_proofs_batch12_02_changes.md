# proof02三处窄修：待独立复审，未执行

新工具 `collect_runtime_proofs_batch12_02.py` 为27533字节，SHA `5d2aa85c7762e1070ba9974af791e40c0dcf0d1126964eac50d935e03f19c47e`。保留01/d08原件；依赖仍固定21805新LIS helper，核心仍652e。累计差异见同目录 `collect_runtime_proofs_batch12_02.delta.patch`：10897字节，SHA `f3a7ddb024b9d4b2d431c90f28684d31e84a9500485d59a123fc62fff71b685a`。

1. **新程序仍活的证据。**远端check_context在入口、出口都核new_run为规范非链接目录，拒绝exit_code/result.txt的普通存在或链接存在；同一新PID/start/current/exe/SHA继续前后核对。tar前后仍各调用check_context，之后才输出null及同活体快照。没有发关闭、重启、探测业务包或额外信号。
2. **真实停止九原件。**新增read_old_stop（102–138行），仅在执行时读取activate授权的九条实际本地原件记录；不在准备阶段生成它们。核九SHA与真实stop回执完全相等，旧run/PID/start/exe/boot/helper/清单来自冻结核心；result必须原字节APP_EXIT0/CLEAN_SHUTDOWN1，应用退出和stop工具退出均LF0。activate/launch/observe的旧停receipt/log/exit/intent四记录及九原件须始终同一rows.stop。白名单增加旧run六件并以这些实际原件字节为expected，已有meta stop stdout/stderr/actual_exit只补expected，不重复add。远端路径只由核心旧run与本链meta组成，绝不把本地记录路径用于tar。
3. **实际快照严格解析。**新增read_snapshot（278–327行）：只接受明确单值字段全集（固定核心/新版身份/current/null/noaction/原主控/服务）、一个规范且有效的UTC、恰两条准确camera/3A身份，以及七配置加三指针共十条准确SHA原文。拒重复、未知、多余、遗漏或不一致；服务字段顺序自由。期望SHA取已pin配置清单原文及冻结核心指针，不预填现场快照；成功后仍保存收到的原字节。未增加时钟同步假设、采样预算或业务判定。

仅做AST语法树解析和源码差异检查，源码为LF，没有导入或调用任何候选函数、生成样本、运行工具/解析器或SSH。旧proof01仍19348字节/SHA d08fea2553d74b51aca948d7310daa7a50712b49e6473d75a9a9008fd016453d。没有新建采集输出目录，也没有未来启动、观察、停止九原件。

这是作者修订说明，**不替代leave_running/dispatch的独立复审**。原有白名单、有界tar、固定helper、独占保存和活动app.log增长可能导致tar非0等边界继续保留；失败保留真实输出，不为取证停程序。只修三处已指出的收证缺口，没有追加新业务流程。
