# 第12批新02链generator的prepare事实增量独审

结论：本次generator新增的prepare收据二择一检查未见阻点。它只明确区分“完整flat已经存在并核对通过”与“本阶段首次复制并核对通过”，没有放宽准备完成、旧C7保护、核心/授权/阶段链、真实退出或来源绑定。旧stop、chain和许可门槛逐字保留。新Host命名空间前审结论保持；本报告不代表新核心、实际生成脚本或板端执行已通过。

本次只读Root复制实物、作者锁定清单及必要旧码，计算字节差异和摘要、做AST解析，只新增本报告。未执行/导入生成器、候选、phase或产品，未生成控制脚本、编译、运行测试/SQL或联板，也未修改源码、模板、核心或授权。

## 固定实物与范围

Root目录 `temp/sn100_host/workflow_next_20261103/runtime_transition_root_review_12_02/`：

| 对象 | 起止实际SHA-256 / 字节数 |
| --- | --- |
| `root_code_snapshot.json` | `cadf9eb89191f2a1327e30dc7e6cbcacefde24a01c6a59c844747fd87872a56f` / 3060 |
| `generate_runtime_controls.py` | `0322264fbf1c7a7439f86fa04381ef3695c70d4e5c128aa5f78ff35334d66a4e` / 35636 |
| `phase_prepare.sh.in` | `7ec12ed530465f193843aff83b47d906ddd4faeda6d17b4bff7ad31817b13dfe` / 892 |

作者 `runtime_transition_candidate_12_02/candidate_manifest.json` 实际为 `41ef29fe553751f57d6a21b301931443c4f56c77da75f76be5d72acfd407a52a`，code_locked=true。其10个code record实际SHA/字节全部吻合；Root10份副本逐字等作者对应实物，Root snapshot记录也10/10吻合。与旧f616快照比较，实际只改变generator和prepare模板，其余8份逐字相同，没有以“旧八份不变”代替实际点核。

## generator实际只新增三行判断

对原始LF字节按行比较，唯一非相同操作是check_receipt中原第318行后插入三行；无删除、替换、行尾转换或其它修改：当phase==prepare时，要求 `(FLAT_ALREADY_PRESENT, FLAT_CREATED_THIS_PHASE)` 恰为字符串 `(1,0)` 或 `(0,1)`。

因此缺字段、空值、同为1、同为0或其它文字均不能通过；允许的是一项1且另一项0，不是允许两个都真。原receipt_fields仍拒重复/不规范字段，所以不能通过重复同名键覆写这两个事实。

此前的prepare共同事实仍同时要求：FLAT_PREPARED=1、NESTED_PRESERVED=1、CURRENT_CHANGED=0、PROGRAM_STARTED=0、CLOSE_REQUESTED=0。FLAT_PREPARED现在表达“本次已核准备好”；究竟是否本阶段创建由两个互补标志明确表达，没有把复用冒称本次复制。

新增判断之后，原真实exit必须精确LF0、唯一收据SHA、完整日志末尾原文和PHASE/TRANSITION/core/auth单次字段检查仍全部保留。不能仅凭两个新flag跳过真实包检查或原log/exit；旧prepare回执若没有新标志，也不能替新链prepare提供二择一事实。

## 新事实的真实来源及原保护门

已读prepare全文确认其标志来源：先require_phase prepare、持同一store锁、核准确旧C7/旧包/nested新包及阶段材料；已有flat或链接进入“已有”分支，必须完整check_new_release both通过后才置already=1。坏链接、缺件或摘要不符由原检查拒绝，不修复、不覆盖。不存在时再核材料和旧C7，cp成功后才置created=1。两路最终都再次核nested/flat完整包、同一旧C7及旧包，再生成真实收据。它不是只看路径存在就算准备完成，也没有为了复用增加停旧/切版或启动动作。

本报告重点是generator如何接受该收据，不替代prepare对应Shell审查或实际生成脚本审查。完整字节核对证明以下generator函数未改：

- check_core：frozen=true、目标/来源、准确旧C7/主控/相机/配置、24字段稳定目标及文件阶段输入门均保留。
- check_stop_files：真实旧停九文件、stop receipt及原内容/SHA一致性门均保留。
- check_authorization：精确字段集合、frozen/allowed必须真实bool true、同一core记录、Root批准、逐阶段前一授权/回执/日志/退出链和activate必须直接承接真实stop均保留。
- shell_globals：不改当前核心事实、阶段许可、来源payload或旧/新进程身份，不自动给稳定目标四动作置true。
- main：未冻结核心/批准先拒绝，严格check_core/授权链，来源脚本和模板读取/占位边界、输入不变复核、全新输出独占写入均保留。

check_receipt其它身份字段、boot/current/控制器/相机及各阶段事实判断也未改。新的prepare判断通过check_authorization递归应用到后续preflight/stop等生成，不能通过从后面phase进入来避开准备回执要求。固定来源脚本SHA、真实file-phase元数据、配置和包记录均没有换成仅存在或默认成功。

## 结论边界

Root snapshot仍注明generation_approved=false、board_executed=false；本审没有给新运行链补执行事实。先前相机自动重启导致旧3446链stop79的原件继续保留，本片只是允许新链在完整核对后诚实复用已经存在的同包flat，不把旧阶段回执变成新阶段回执，也不让新相机自动冒充旧身份。

Root后续仍按新现场另存真实核心/授权，逐阶段生成并复核实际脚本、执行和收回真实回执。此前 `runtime12r2_host_namespace_review_01.md` 所核的新CAP/CODE/caller隔离逻辑无需因这三行generator增量再改；本报告没有执行该Host工具或宣称新版已启动。
