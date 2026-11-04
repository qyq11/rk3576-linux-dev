# 真实源码来源终态：只读合同与未执行候选

本合同配套 `source_ledger_candidate.py`。它把总控已经做过的每片源码接纳、检查日志、前后指纹表和必要补正连成一条可核对的链，让九份构建归档工具能认出“实际源码已经全部到齐”。它本身不执行Git apply、不合源码、不构建、不连板，也不生成程序发布号。

**当前只交代码候选，未运行候选正文，未创建final_review/promotion/complete。** 默认没有完整总控确认清单，命令缺参数或清单未冻结即拒绝，连输出目录也不创建。只做AST语法检查不等于聚合已成功。正式源码、公共工具门槛及既有原件保持。

## 已读的实际事实，不改写历史

- ASCII：`integration_10_source_acceptance_01/ascii/` 的check/apply各有真实argv、实际退出0、stdout/stderr原件，source_before/source_after和acceptance已存在。它是实际接纳过的第一片，不是此聚合器执行过。
- CAL：普通git check/apply实际均0；首次外层入口实际1，因为两份工作树后像的CRLF/LF字节与批准候选不同。`cal/source_after.json`、检查日志和postimage_mismatch_inspection.json必须保留。总控随后逐字节备份、核规范化后相等、只恢复批准两文件原字节，补正实际0，最终after必须指向 `cal/line_ending_correction_01/source_after.json`（7cad…），不能把首失败表覆盖或换名字冒作首次成功。
- 最新总控告知：User20首片check/apply实际0/0，外层1因10文件行尾后像不等，LIS3尚未进入。总控正在按同样方式保存失败表/日志、比对和备份10份原字节后补正。**本合同不预填补正结果，也不把LIS3登记为已应用。** User最终after须取实际产生的 `user_init_04/line_ending_correction_01/source_after.json`；完整路径/指纹由总控晚定。
- Git系统core.autocrlf=true是实际环境事实；后续Root命令使用 `-c core.autocrlf=false`，不修改全局配置和根属性。元数据必须逐步保留真实argv，不能说前面阶段都用了后来选项。

候选已经把同片“普通apply成功、外层比较失败、精确字节补正成功”分开保存，而不是把补正伪装成重新apply。CAL两文件和User实际10文件使用同一 `exact-crlf-postimage-v1` 合同；Root必须提供各自真实范围和原件。范围不限于写死两个CAL文件，但必须与失败/批准表差集、备份及归一化字节比较完全相同。其他失败种类不走此例外，须另审。

## 总控以后需要提供的清单

候选接收一个现有、普通文件的Root spec，格式 `sn100-root-source-ledger-spec-v1`。下列是**未冻结形状示意**，不是可运行配置；空值/false均会拒绝：

```json
{
  "format": "sn100-root-source-ledger-spec-v1",
  "frozen": false,
  "owner": "root",
  "all_intended_source_slices_present": false,
  "ledger_script": null,
  "root_approval": null,
  "baseline_snapshot": null,
  "baseline_archive": null,
  "final_source_manifest": null,
  "snapshot_rules": null,
  "root_attributes": null,
  "git_apply_config": {},
  "required_patch_names": [],
  "patches": [],
  "historical_failed_checks": [],
  "output_directory": ""
}
```

record统一为 `{"path":"项目内相对路径","sha256":"实际64位SHA","bytes":实际字节数}`。bytes可省略，其余不可空；候选会重新读实际文件核SHA，拒绝链接/越界/覆盖。baseline_archive可以现在保持待总控确认，未读旧09大包；将来运行必须提供实际已确认record，不用文件名或旧发布号代替来源绑定。

- ledger_script必须是总控审定的聚合器自身record；root_approval是总控对完整片序、范围及来源合同的实际批准原件。批准原件不能反向包含尚不存在的review/progress SHA。
- baseline_snapshot是本轮起始完整源码表，基线源包应与之匹配；总控确认旧09/684来源关系。它的原文件SHA会用于九工具遗留字段source_snapshot_sha256，虽然名称像最终快照，此处仍按原门槛填写**起始表**的SHA，不自行改义。
- final_source_manifest必须是最后一片实际后像的完整表，不能是作者候选整树的表。当前源码范围及每份字节都要再次相同。
- snapshot_rules是最终正式 `shell/package_arm64.py` 的实际record。候选只通过AST识别其snapshot函数当前“目录列表+显式文件+protocol文件”的控制结构，再按该范围只读核全源；不import/运行该脚本。增加LIS目录等清单内容可以按同结构识别，改变控制规则则拒绝，须另审。源范围应先由Root与实际包规则核准，不以手写目录替代正式规则。
- git_apply_config须含真实Git设置/选项来源record，至少一份；代码另逐片收录原check/apply argv。可明确“逐步argv策略+系统core.autocrlf实际值”，不要求虚构统一历史配置。
- required_patch_names必须是最终计划全部片名、顺序；patches必须一一相同且index从1连续。all_intended_source_slices_present只有在最后所需源片都实际接纳后才可true，不能为只收了ASCII/CAL的局部链生成全任务完成。

每个patches条目要求：

| 字段 | 必须来自的实际材料 |
| --- | --- |
| index/name | 总控连续顺序及可读功能名 |
| patch | 原实际git apply使用的补丁record，不换成“等效重建片” |
| before_checkpoint/after_checkpoint | 该片实际全源前后表record；后片before字典必须等于前片最终after字典 |
| expected_after | 批准的组合后像完整表record，不只取作者旧基线后像 |
| scope | 本片实际修改/新增/删除路径并集，排序且等于批准范围 |
| reviews/adoption_scripts | 非空record列表：独立源审与原真实接纳脚本；聚合器不能替代它们 |
| check/apply | 各含result/stdout/stderr三个record；result保存原phase、argv、actual_exit。缺结果或非实际0则不进applied |
| acceptance | 总控实际接纳记录，至少包括actual_source_equals_expected=true及实际source_files；无补正时还须绑定patch_sha256 |
| correction | 无补正为null；有补正按下表，作为同一片的后续entry，不增加假补丁编号 |

correction字段为：

| 字段 | 内容 |
| --- | --- |
| kind | `exact-crlf-postimage-v1` |
| paths | 本次Root实际核准补正路径，排序、无重复 |
| failed_checkpoint | 原普通apply后、字节比较失败的完整source_after表record，永不覆盖 |
| first_entry_result | 原外层实际退出1、git_check_actual_exit/git_apply_actual_exit实际0、mismatch_paths及原件保留事实的record |
| inspection | 实际路径/actual/expected SHA、equal_after_crlf_to_lf及current_equals_failed_snapshot的检查数组record |
| script | 原实际补正脚本record |
| backups | `[{"relative_file":源码相对路径,"record":补正前原件record}]` |
| approved_postimages | 相同形状，指实际批准的完整后像字节record |

补正acceptance还须包含真实first_entry_actual_exit=1、correction_actual_exit=0、line_ending_correction路径及source_map_sha256，后者等于最终after_checkpoint原文件SHA。候选独立读备份/批准字节核CRLF→LF后相等，核failed→final只差这些路径，final精确等于expected；不是只相信“行尾问题”文字。若实际新入口记录格式不同，先加一份有原来源record的诚实元数据/重新审接口，不能改旧日志或随意把缺字段补成0。

## 能输出什么，不能声称什么

只有全部输入合格、所有片最终后像连续、当前完整源码按正式snapshot规则逐份等最终表，并在写前再次核输入/源码，候选才新建Root指定temp目录：

1. `verified_final_source.json`：本次实际只读核得的完整表。
2. `final_review.json`：格式 `sn100-root-real-source-final-review-v1`，含base_source_files/final_source_files、动态patches、准确before/after record、实际命令及补正原件、root_attributes、git_apply_config和historical_failed_checks。
3. `required_cfg_evidence.json`：Root以后冻结cfg至少需保留的全部输入record列表。还要另加九工具要求的Root留屏裁定等记录。
4. `promotion_progress.json`：最后才写，格式 `sn100-root-real-source-promotion-v1`。只对原实际check/apply成功且补正后精确等批准后像的片填applied/preflight_applied；phase=complete/current_patch=null只说明来源链聚合完毕，不表示构建、上板或整个业务完成。

review/progress都明确 `aggregation_only=true`、`executed_git_apply=false`；原apply执行拥有者为root_original_adoption_scripts。preflight_applied沿九工具字段名，但证据来源明确是原Root检查记录，不是本聚合器重跑过preflight。程序没有Git subprocess或补丁执行入口。

源码或输入复核失败时，不写progress complete。输出路径已存在时拒绝，不覆盖失败现场；若IO或写后源码复核失败，可能只留下新目录中的部分review/表，不能选它们作已完整终态，也不自动删除或重跑。无完整spec时不会创建任何输出。

## 与九工具的字段和单向绑定

没有降低/修改build_archive_candidate_02的cfg/promotion检查。Root冻结cfg时：

- metadata_schema.review_format / promotion_format使用上面的两个确切格式名。
- final_review、promotion_progress、promotion_script分别绑定聚合器实际生成的review/progress和本候选经审定后的脚本；baseline_snapshot/final_source_manifest及根属性按同一实际来源绑定。
- review内的patches保持原index/patch/sha256及before_checkpoint/after_checkpoint，progress完整复制同一列表；applied仅取原实际已应用三元组，preflight_applied取同一实际已核序号，verified_source_paths从最终实际表推导。
- final_merge_review_sha256与merge_review_sha256只由**写完review后**实际读取SHA填入progress；promotion_script_sha256是本聚合器脚本SHA。最终cfg的approved_final_review_sha256最后由Root明确固定。
- 所有historical_failed_checks均是扁平record，必须同时进cfg.evidence；CAL/User外层1、失败表、行尾检查会自动纳入，不强行转换成通过。其他历史失败由Root显式列出，可为空但不可漏掉本轮事实。

绑定方向为：实际来源/脚本/spec → review → progress → 工具cfg。review可含finalizer脚本SHA，但不含progress/cfg SHA；spec只写未来输出**路径**，不写输出SHA；cfg不被加回自身tools/evidence。没有未来发布号、对象数或程序SHA，构建后运行意图另走原合同。

## Root后续仍缺的实际输入

当前只确认ASCII和CAL原接纳事实、User失败事实及后续补正计划。Root尚需按实际状态补齐User最终补正/接纳、LIS3真实check/apply及接纳、五份纯字段片的真实接纳、被动观察正式接线等本轮最终源片，并固定最终完整片序/源表；这里不假设它们已经执行，实际范围由Root最终裁定。

还缺：Root确认的09基线源码包record及匹配起始表、各片完整审查/实际接纳脚本/原日志record、Git实际配置来源和根属性record、最终snapshot规则、完整Root批准清单、聚合器自身独审，以及九工具最终冻结前现场current/原主控路径、工具SHA和留屏裁定证据。不能因本候选存在就安排构建；本交付没有创建这些终态，也没有新构建、RK、真实仪器、资源或留屏通过结果。

## 本次实际静态检查

实际仅用Python AST解析候选文本，命令退出0；未import候选、未调用main或任何候选函数。另以AST比较候选中声明的snapshot控制形状与当时正式package_arm64.py，结果相同，没有调用snapshot或读取全源。该次候选SHA-256为 `738fa974c4fb74e5de82485d423bf91995571f9b0022570330d6d15286118ac7`。这只证明语法和已知清单结构可识别，未生成终态、未验证一份真实Root spec，也不代替独立源码审查。
