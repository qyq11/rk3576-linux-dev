# 第八轮晋级工具第三版窄审

2026-11-03 12:43（北京时间）。**相对已审第二版，本次窄审未发现剩余确定阻断。**第三版将与实际执行不符的回放字段改为“单次隔离应用”，明确第二次独立回放未执行；正式写入前原有的运行时独立五片预演和完整逐字节比较没有删除。

本次只读比较工具文本、实际Stage2元数据/输出及文件指纹，仅新增本说明。没有执行生成器、晋级、补丁检查/应用、AST、编译、产品、SQL或网络；没有修改工具、正式源码、旧候选或旧报告。Stage2实际退出码来自已有原件，不冒称本审重新执行。

## 本次精确读取的原件

T表示`temp/sn100_host/workflow_night_20261102/`。

| 原件 | SHA-256 |
| --- | --- |
| T/prepare_integration_08_promotion_v3.py | 9466015b014866ae2351525803219718e2fb3ad41e164d909e922b446d67ab5a |
| T/promote_integration_08_template_v3.py.in | 4d9ec71bf5b0aac30f9db2e3c5e543ec05c38aac9e3be121bc10bd92021476ee |
| 对照的第二版生成器 | bc2407b298c06b447bd4e20f364e9abde4d4dc8b4163724f91c2c65062315aec |
| 对照的原08模板 | 9a4624021e9d246888598600c9d75a8bd6dfc7719b1b7cd36aba3f95a947a11d |
| T/integration_08_review/stage_02/integration_metadata_candidate.json | decaee0e5c346131fe0d2b8fba6ff6313ea4c20b65dc4b9fdb98d6f7b69a28c6 |
| 同目录single_stage_application_result.json | de01b312c19e8081b72551998d79b3f568e811346d4ef08c0d1d10c1b78730d2 |
| 同目录source_manifest.json | 30cf6f075afaea1f5c6d5842759e63747abd8d7d0403f0e85f05ed019ae0346e |

第三版两份工具读取前后SHA相同。旧工具保留原样，前次[工具审阅](promotion_tools_review.md)按原SHA和原字段继续保留历史范围；它不能替代第三版当前字段核对。

## 实际差异

只读文本差异显示，生成器仅改三类位置：固定模板SHA更新为4d9ec71b、模板文件名改为v3、原`full_source_replay`字段改为`single_stage_application`及其真实完成条件。模板仅在固定引用和完成条件中作对应改动。基线07快照/源码包、当前ac7根属性、五片顺序、六检查点、路径/链接拒绝、输入冻结、正式写入流程和失败留档代码均未因此放宽。

生成器和模板现在同时要求single_stage_application原件满足：

- format为`sn100-integration-08-single-isolated-application-v1`。
- complete为true。
- five_standard_checks_and_applies_passed为true。
- root674_map_and_default_seven_after_bytes_equal为true。
- independent_second_replay_executed为false。

该条件不将“第二棵独立回放已经执行”作为虚构前提，也不以缺字段或真值猜测替代明确的false。最终Review仍须根给出正确最终格式、显式SHA及所有固定引用，不能直接拿Stage2候选元数据当已绑定的最终Review执行。

## 与真实Stage2结果的对应

实际Stage2元数据为source_count=674、patch_count=5，stage_directory指向`integration_08_review/stage_02/source`。它引用的single_stage_application路径和SHA与实际de01b312原件完全相同，未含`full_source_replay`字段。

实际单次应用结果的上述五个字段均与第三版要求相符。其actual_git_steps含12项：Git初始化、属性核对各1项，五片普通check/apply共10项。本审逐项读取已有`.exit`及stdout/stderr原件，退出均0，输出SHA均等于结果记录；没有重跑这些命令。WSL代理提示原文保留，不将提示或Git源操作当产品功能验证。

结果中的final_source_map_sha256等于实际source_manifest.json的30cf6f07。按该完整清单重新只读核对隔离源674份，差异0。这里确认的是这一棵Stage2隔离源及原始执行记录，**没有据此声称已执行另一棵独立回放或正式晋级。**

## 写入前的独立预演仍在

第三版模板仍在正式写入前创建UUID命名的新预演目录，从正式起点逐文件复制并镜像实际根属性；核正式、预演和Stage2的实际Git属性结果相同。随后对五片依次执行普通check/apply，每片前后核完整检查点，最后`compare_review(preflight, final)`与已审Stage2逐字节比较全部最终源路径。

预演成功后仍再次核正式起点和全部固定输入，才进入applying。每片正式写入前仍核此前完整表、所有输入和实际执行脚本SHA，先持久保存current_patch/可能已写入标记，再应用并核写后全表。全部五片之后仍完整比较并重新核输入/脚本，才登记complete；失败仍保留failed_requires_review与当前片，不自动覆盖、删除、回滚或续跑。

生成器仍要求根显式提供最终Review SHA、模板唯一占位及精确模板SHA，输出固定到新`promote_integration_08_review.py`并拒绝已有目标。生成和运行阶段的独立守卫与前次审阅相同，本次字段更正没有跳过它们。

## 后续边界

根仍须绑定最终五片Review及真实证据后才生成实际脚本；生成后另读实际脚本SHA再决定执行。若工具、根属性或最终元数据变化，不能沿用本说明旧SHA结论。

本审只确认工具适配和已有文件事实。正式源写入、完整ARM64构建、RK运行、真实任务/液面业务、资源及仪器验证仍分别登记；单次隔离应用和只读文件一致性不代替这些结果。
