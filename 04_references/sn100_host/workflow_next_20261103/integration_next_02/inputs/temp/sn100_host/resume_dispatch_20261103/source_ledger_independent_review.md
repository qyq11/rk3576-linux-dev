# 本批六片源码来源聚合独立审查

2026-11-03，只读审查者：`/root/review_amount`。本报告用于判断“已经由总控实际接纳的源码，能否整理成九份构建工具可读取的来源证据”。未执行聚合候选正文、任何候选函数、Git apply、构建、产品、SQL、模拟、网络或板卡操作；未修改正式源码、作者候选或既有证据。仅另写本报告。

**结论：已读的修订版源码与六片派生输入，可进入总控冻结步骤；当前草案本身还不能作为冻结终态使用。** 修订版的根目录层级问题已经修正，未发现尚存的确定代码阻点。Root仍须亲自认可事后退出记录、补入真实Git配置来源、写明确的本批范围批准，再另存冻结spec。此结论只覆盖来源聚合，尚未产生聚合结果，更不表示编译、发布、RK运行、真实仪器或整个SN100业务已完成。

读时绑定如下，路径均相对仓库根目录：

| 文件 | 字节数 | SHA-256 |
|---|---:|---|
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_candidate.py` | 18416 | `738fa974c4fb74e5de82485d423bf91995571f9b0022570330d6d15286118ac7` |
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_contract.md` | 12156 | `3fca9728b946c53560c8cba9dcc5796333e33d00ff5c91b0f27c2997e6df9739` |
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_01/source_ledger_candidate_v2.py` | 18701 | `307674360d5c24c3fe17eae04e5c7a869f7f35021fdaabecb9fd0afedbc165cd` |
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_01/root_spec.draft.json` | 64486 | `5d0d1ac7bb7921429089324543f60c8281cd20369bfd8c758abc5d06451b83c3` |
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_01/evidence_inventory.json` | 37015 | `7b06cc1fe50d60272a796cd17629c1cb0792ebc54b7b07e18c57242aefcb2575` |
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_01/user_init_04_acceptance.canonical.json` | 2261 | `b60a0644c780f034bd057c47a3a511d6da74e2793538587281b211f80f9ed284` |
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_01/observer_04_acceptance.canonical.json` | 4487 | `f7e2938ff2039c8198e1517c4334e9e0ce12ba896c60829586bd19b06f0d7f36` |
| `temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_01/root_reported_user_correction_tool_result.pending_confirmation.json` | 1153 | `ad6b425c849dd73fb89480808a386b062569a6f533085414c59f80527975ab11` |

1. 写入边界与修订版差异。

   原版位于`resume_dispatch_20261103`，第16行`parents[3]`能定位仓库根。修订版放深一层，当前第16行已经改为`parents[4]`，定位正确。本人实际逐行比较，修订版相对原版只有这一行，以及第253–256行逐条`checked(supporting_records)`的四行新增；两个Python文件均只作AST解析，成功。新增检查使派生记录依赖的原acceptance、外层退出文件和事后归因记录也进入`actual_evidence`，并经第292–293行及第327行保留到后续cfg必要证据列表。

   修订版第218–223行先要求Root冻结spec、完整范围声明及真实批准record；第239–277行核逐片连续性和正式完整源码；第294–303行才允许新建`temp/sn100_host`下尚不存在的输出目录。输入不得位于未来输出之内，第299–301行还会在写入前复核。使用`xb`新建文件；写后源码或输入漂移则不写末尾progress，保留部分现场。它没有构建、Git、产品或板卡执行入口。

   初次送来的`source_ledger_v2_only_supporting_records.diff`只列四行新增，未列后补的根目录行。冻结时应另存真实两块差异说明或明确旧diff的历史含义，不能继续把它称为当前完整差异。本报告直接比较了两个实际源码，不依赖该旧diff。

2. 六片实际来源与当前完整源码。

   本人以另写的只读文件检查代码解析JSON、计算字节指纹，递归核了草案及派生记录引用的144条不同record，未见SHA、字节数或普通单链接文件属性不符。没有调用聚合器。六片顺序、每片before等于上一片after、after等于expected以及scope等于实际修改/新增/删除并集，均成立：

   | 本批片 | 完整源表前→后份数 | 本片范围份数 | 最终after原文件SHA-256 |
   |---|---:|---:|---|
   | ASCII收发 | 684→684 | 2 | `747ae955c12b4bc39e6b76ab80b282445c07500d92b08a364861333ddad5f913` |
   | CAL空批号 | 684→685 | 17 | `7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e` |
   | User初始化04 | 685→685 | 20 | `d0ff4d57170e0c77554f24a54bd80110734bdeb99735bad43e754645c205988b` |
   | LIS连接排队3文件 | 685→685 | 3 | `c8bf26a4247c03060feb778d250bf9b5c14a9b93f3bcd550081fb617c88c8c18` |
   | 被动观察04 | 685→687 | 4 | `b688f7a8c9c888482bf53b9520239ec9618c61a2da5a701d526387e644add5d3` |
   | 管理员手动ASCII B1 | 687→687 | 4 | `f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b` |

   本人只解析正式`06_programs/sn100_host/shell/package_arm64.py`的snapshot清单，再按它的实际目录、显式文件和protocol清单独立读取源码。读时取得687份，集合和每份字节指纹均等于最后B1实际接纳表，差异0。聚合器第170–209行也以AST核整个snapshot控制结构；规则改变会拒绝，未写死684、687、补丁数、对象数或旧08后验。

   本人另流式读取09源归档，不解包、不生成源码树。归档684个普通成员逐份等于起始表，未发现重复、不安全路径或链接成员。归档实际SHA为`6581e0e22406097ab5f2239df212d0569ef3a4ae65783c20a53a483057a5e7eb`；不能把归档文件名中的b31f关联值当作压缩包实际SHA。两份准备失败记录保留着，未被写成成功。

3. 原命令和失败后补正。

   第117–130行要求每片check/apply各有原result、stdout、stderr三份record，核原phase、真实整数退出0、原argv最后一项精确指向本片补丁，拒绝未经审查的自动空白修复、三方合并和忽略空白选项。当前六片实际原件都齐全；CAL apply.stderr原有277字节警告仍在，其余所读check/apply输出为空文件。草案保留了逐步真实argv：前三片没有后来才用的`-c core.autocrlf=false`；从LIS3开始才有，未改写成统一历史命令。

   CAL首外层1、原check/apply 0/0仍在。User首外层1、原check/apply 0/0也仍在。本人独立实读CAL两份、User十份补正前备份及批准后像：备份SHA等于失败表，批准SHA等于最终表；仅将CRLF归一到LF后字节相等，failed→final差集精确为这2/10份；最终表等于批准expected。修订版第133–167行同样实读这些字节，不以“只是行尾”文字豁免任意失败。

   User原first结果没有`mismatch_paths`、原acceptance没有`correction_actual_exit`；新canonical文件明确标`derived`并引用未改原件。新增兼容字段来自原失败表、归档备份和检查表，`current_equals_failed_snapshot`旁明确解释为“归档补正前字节与失败表匹配”，没有声称当前正式源码还处于失败后像。新acceptance保留`first_entry_actual_exit=1`，补正0明确只归因Root报告的真实Tool块`07c46d`，原始stdout并未伪造为文件，记录标明事后整理、待Root确认。本人未直接取得该历史Tool块，故此0的实际执行事实仍须Root亲自认可；本人确认的是归因方式诚实、所列两份后像SHA与实际原件一致。

   被动观察原acceptance的`source_count=687`到新`source_files=687`为同值映射；原`installer_actual_exit=null`原样保留在原件。新记录把0归因到既有`observer_install_04.exit`（实读为0），并列出原stdout/stderr。没有把缺字段直接说成原件当时已记0。相应原件均在每片supporting_records内，不能在最终spec或cfg里删掉只留canonical结论。

4. 终态含义及冻结前仍需做的事。

   第281–291行review只引用已存在输入，写完review后第312行才计算其SHA；第316–328行progress再引用该SHA，最后才写progress。cfg在外部最后冻结，未反向写进review/progress，没有SHA循环。`source_snapshot_sha256`按九工具现行接口指向基线表；当前最终表另由final_source_files、final_source_manifest和review绑定。九工具`integration_next_tool_support.py`的review/progress字段校验与该结构相容，未发现为适配它而降低门槛。

   `complete=true/phase=complete`只表示这次来源聚合结束；`aggregation_only=true`、`executed_git_apply=false`及原Root执行拥有者都写明。没有预填程序SHA、对象数、发布号、构建成功或RK通过。当前草案明确`frozen=false`、`all_intended_source_slices_present=false`、`root_approval=null`，也没有实际Git配置捕获record；不要执行它后补真假字段。

   Root冻结前应另存新spec和批准原件，保留本草案：

   - 明确认可User补正Tool块`07c46d`的事后真实记录；若增加Root确认文件，把其record和原pending记录一并保留，不把子Agent记录伪装成原stdout。
   - 加入实际只读Git配置来源，保留各步原argv与根`.gitattributes`实际SHA`c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a`。聚合器接受含record的字典，实际来源的诚实性仍由Root批准承担。
   - 批准本次仅ASCII→CAL→User04→LIS3→Observer04→B1六片，用真实最终f741表；排除未采用的5份纯字段候选和Mg新候选。`all_intended_source_slices_present`或以后使用的`source_scope_complete`只可解释为本次构建六片来源齐备，不是整个SN100业务已完成。
   - 给旧合同补当前范围裁定。旧合同第11、104–108行仍写User待补正、LIS未进入、还需五份纯字段接纳和被动观察接线，已不是当前状态；应保留其历史范围，在新的Root批准/维护补充中明确被后续真实六片事实取代，不能照抄为本批未完成依赖。其余历史失败保留要求继续成立。
   - 最终cfg保留`required_cfg_evidence.json`所列全部来源，尤其原失败表、首失败记录、原日志、备份/批准字节、canonical的原来源和Root事后确认。Root批准不能反向引用尚未形成的review/progress/cfg SHA，冻结spec也不要制造“批准文件↔spec互相包含SHA”的循环。

最终冻结spec、实际聚合输出及后续九工具cfg不在本次已读范围内；它们生成后仍需按实际record核对。这里没有将来源审查替代为后续完整编译、板端软件、真实仪器、实体操作或资源验证。
