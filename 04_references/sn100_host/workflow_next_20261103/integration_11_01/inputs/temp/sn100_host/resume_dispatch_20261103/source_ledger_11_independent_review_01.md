# 第11批源码来源账独立静态审查

审查日期：2026-11-04。审查者：Root 独立审查 Agent `/root/review_amount`。

结论：本次读取的未冻结草案及其新派生记录，未发现需要改来源工具或重做源码接纳的确定阻点。可以进入 Root 补真实事实记录、批准及冻结的步骤；本报告不是已执行聚合器的结果。草案仍为 `frozen=false`、`all_intended_source_slices_present=false`、`root_approval=null`，这些值在当前阶段正确，不能提前当作可执行的最终输入。

审查只使用文件读取、哈希、JSON/AST 解析和归档逐成员读取；没有执行 ledger、接纳 helper、冻结工具、编译、SQL、产品、网络或板卡动作。只写本报告，未改正式源、作者候选、旧来源账或草案。

## 本次绑定的原件

以下路径均以仓库根为起点，摘要均为 SHA-256。

| 原件 | 读时摘要 |
| --- | --- |
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_11_01/root_spec_11.draft_final_unfrozen.json` | `ad28dd9444b9204672311b420ca55cfebacb1f8caab8f905630f67d04959d4e5` |
| 同目录 `source_ledger_inputs_11_contract.md` | `62435022d504661b0c18687bf13a1f80e5ec600863e15688abcca61f76914cb5` |
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_01/source_ledger_candidate_v2.py` | `307674360d5c24c3fe17eae04e5c7a869f7f35021fdaabecb9fd0afedbc165cd` |
| `07_backups/sn100_host/board_b/workflow_integration_20261103/integration_next_02/sn100_sources_c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e.tar.gz` | `56fbff3871eec9776205487ef59d1c9826b6605ec4f8f884bc90b6ceede6b4bf` |
| 上述归档目录 `source_snapshot.json` | `fd50fedc204888ba6277029715caa80054bd3c760d2c80e1e9c9b93762abb4fc` |
| `temp/sn100_host/workflow_next_20261103/integration_11_source_acceptance_02/lis_profile_source_retry_01/source_after.json` | `b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767` |

本次草案直接引用的 record（带路径及摘要的文件记录）去重后为117项；逐项核对摘要、已提供的大小、仓库内路径和普通文件属性均成立。这个117是当前草案的输入数，不是未来加上批准和配置实录后的固定总数。

## 基线、两片连续关系及正式源码

独立逐成员读取 c7 实际源码归档，得到687个普通源码成员，路径、成员摘要均等于归档自己的 `fd50...` 表；该字典也等于 f741 表。两份 JSON 原始字节不同，不能据字典相等把 `fd50...` 原文 SHA 换成 f741 的 SHA。草案已经分别固定实际归档及其原表。

当前构建批次只有两片，顺序为 Mg 单孔探测15项、LIS 配置来源2项。不是重放上批六片，也不包含未采用的 User 急诊16源、Uh 或其他新候选。

- Mg 实际 patch 为 `integration_11_source_acceptance_02/mg04_on_f741_root_revision02.patch`，SHA `440f22fe22ac42503e7df5d2b3099e4732bc5a19f0b96c1728fd13672b08ebb6`。before 为 f741 字典；实际 after 为688项，原文 SHA `66ece18e56690e31d6b911775ccd8ae6fc70b982c04c430bf02d322b4029dbd5`。
- LIS 实际 patch 为 `temp/sn100_host/lis_profile_source_candidate_20261103_01/lis_profile_source.patch`，SHA `7fd8567ece6c4f938f2acc210c2e606abecaae4fdf7b91f9870e3fdd9ad23bdf`。重试 before 逐项等于 Mg after，实际 after 为最终 b078 表；其两项实际变更范围、期望字节均相符。
- 两片的 after 均逐项等于各自 expected，实际差异集合等于声明的15项和2项。LIS expected 引自首次错误工作目录时保存的期望表；没有把该表说成首次已成功。
- 独立按正式 `shell/package_arm64.py` 的快照常量、显式文件及协议文件规则枚举并计算当前正式源，实际688项，集合及每项摘要全等最终 b078。该正式快照工具读时 SHA 为 `9fe7dab75b7566044d0ec485b0ffd47f4232be0142b664f35857dd6a18c524e3`。没有复制或重放整树。

因此 cfg/ledger 的基线原文身份应为归档 `fd50...`，最终实际源码身份应为 `b078...`；f741 只作为相同内容的首片前像原表保留。v2 第322行的 `source_snapshot_sha256` 取的是基线表摘要，不应改成最终表、程序版本或归档文件摘要。

## 原命令、工作目录及失败记录

四份 canonical 命令记录逐项对照原 `.command.json`、整数 `.actual_exit` 和实际接纳 helper：原 argv、退出值均保留，新增的只是 v2 接口所需 phase 与真实工作目录。Mg 的原记录没有 cwd，实际 helper 明确 `subprocess.run(..., cwd=ROOT)`；LIS 成功重试实际 cwd 为 `06_programs/sn100_host`。两个 cwd 与 patch 自身路径形式相符，没有扁平写成同一环境。四次实际命令均有 `-c core.autocrlf=false`，check/apply 分别0/0。

两份 canonical acceptance 保留原记录所有既有字段的值，把原 `all_after_bytes_equal` 映射为 `actual_source_equals_expected`，并显式保留原 acceptance、expected 和接纳脚本来源。未重建或虚构原 stdout。

三次最外层调用必须继续分别记录：

| 入口 | 实际事实及账内处理 |
| --- | --- |
| `accept_sources_11_01.py`，Tool `229212` | Root 本次明确实际外层退出1。原 snapshot 子步骤为0，但反斜杠表键比较失败，发生在 split/check/apply 前；不能用子步骤0代替外层1。准备者当时未获外层数值，所以旧派生记录留 null 是如实保留当时已知范围。 |
| `accept_sources_11_02.py`，Tool `3ed224` | 外层实际1。Mg check/apply0/0、实际688后像成立；随后 LIS 在 ROOT cwd 首次 check1，stderr 为两源路径不存在，没有 apply 原件。不得为了 Mg 成功把整个入口写0。 |
| `resume_lis_profile_source_11_03.py`，Tool `3ad361` | 外层实际0。确认前次失败及 Mg 实际接纳后，在 PROGRAM cwd 仅续 LIS，两次命令0，最终 b078。成功入口保留在支持证据中，没有归入失败入口子集。 |

Root 新提供的三条 Tool 事实属于本次协调事实，尚不是本报告新造的原 stdout 文件。应另存真实 Root 事实记录，固定 Tool 标识、外层退出及解释，并在批准中确认；旧 `root_reported_entry_results.pending_confirmation.json` 和失败子集原件应保持不动。首01旧 null 与新补充1可以同时保存，并注明各自记录时点。

Mg apply 原 stderr 的 whitespace warning 保留，实际退出0且后像成立；没有使用自动修空白参数、重写警告或把有 stderr 说成无 stderr。准备助手自己的可选 bytes 比较错误及修订记录也单独保存，不冒充业务源码 apply 失败。

## 原始路径表及审查来源覆盖

独立核对02 before、Mg after、LIS retry before/after四组 native_raw JSON、正斜杠 JSON 及 path_normalization 记录：只对键作反斜杠到正斜杠的一对一映射，没有键碰撞，源码摘要值不变；raw/canonical 摘要和项数均对应。首01失败 raw 表同样可映射到 c7/f741 字典。两片 `correction=null` 正确；这批不是源码行尾补正，不能套用上批 CAL/User 的失败与补正说明。

四命令 canonical、两 acceptance canonical、失败入口派生和路径验证的新派生所需原来源，均已作为草案显式 record 覆盖。v2 仅校验登记的 record，不自动递归分析所有 JSON 正文；当前新派生链没有依赖这种不存在的自动追溯。c7 作为既有真实归档基线固定，并不要求重新展开、晋级旧批全部历史证据。

本批来源保留组合业务/生命周期01不放行与02修点报告、Mg03问题与04定向关闭、LIS两源独审等原记录；没有把早期不放行改写成一次通过。相关接纳、包装和交接来源与实际两个源码片分开，未把支持记录新增成虚假的源码片。

## 工具写入边界及冻结前条件

v2 原位置的 `parents[4]` 正确解析仓库根，不能因新草案在 inputs_11 目录就复制脚本并误改 Root。静态核对 v2 第218–223行，先要求 Root、frozen、完整本批范围和批准；第237–277行核历史、连续前后像、实际命令及当前全部正式源；第294–303行限定新的 `temp/sn100_host` 子目录、禁止输入落入未来输出，再次核源及证据后才创建目录。读时输出 `root_source_ledger_final_11_01` 尚不存在。

第310–328行先写实际 review，再以该文件真实摘要生成 progress 和证据清单；没有预填编译、部署或 RK 成功。读时 `source_complete.json` 也明确 `scope=source_acceptance_only`、`compiled=false`、`board_operated=false`，当前 c7 留驻事实没有被提升为本批程序运行通过。

Root 冻结前仍需完成：

1. 另存并显式登记上述三次 Tool 真实外层事实；确认 canonical、失败上下文及独审结论。旧 null、失败命令和原 stdout/stderr不改。
2. 确认历史 Git 配置实录适用于本批，或另附新的真实只读实录；根 `.gitattributes` 读时仍为 `c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a`。不倒写原命令环境。
3. 写实际 Root 批准，再另存冻结 spec；仅确认当前两片源码范围齐备后置范围标记为真。批准可固定已存在草案、contract、脚本及原件，不能反绑未来含该批准摘要的冻结 spec，避免 SHA 循环。
4. 冻结时重核原件和正式688后像，将新增事实、批准及配置实录作为显式证据；不能把117硬编码为最终数。随后只有真实 ledger 成功产物才能交 cfg，cfg 最后绑定 review/progress 及必要原件，不反写来源账。

本次所审成功范围是两片已接纳源码的来源链与可冻结条件。它不表示整个 SN100 业务完成，也不表示本批 ARM64 编译、部署、RK 软件、真实仪器或资源验证通过。
