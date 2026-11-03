# 第09轮检查工具改绑正式revision04的最小要求

2026-11-03，只读核查`20261103_status_login_09_preparation_02`。本次不执行构建工具、产品、测试或板卡命令，不修改作者候选。总控已说明revision03编译被中断、没有最终exit/complete；部分对象不能作为成功构建或检查器输入。下面区分真正执行路径与旧源码依据，避免全局替换误写历史。

## 最小可执行改动

建议新建一个独立工具候选目录，原preparation_02及其校验清单保持原件。**在同一684份源码、继续采用已批准03源码依据的情况下，真正需要改的运行代码只有两处工作目录守卫：**

| 文件/位置 | 现在绑定什么 | 改绑04需要什么 |
| --- | --- | --- |
| `bind_build_09.py:89` | `resolved.name == "integration_09_revision_03"`及同句提示 | 精确改为`integration_09_revision_04`，继续拒绝其他目录与路径重定向 |
| `execute_final_09_build.py:21` | `EXPECTED_WORK = ROOT / 'temp/sn100_host/workflow_night_20261102/integration_09_revision_03'` | 精确改为`integration_09_revision_04`，保持实际传入路径必须等于EXPECTED_WORK |

`build_review_09.sh`从`prepared_inputs.json`取实际integration再经过binder核查，没有固定03路径，无需改。`freeze_release_09.py`和后续binder/独立核对中的Task批准SHA也从实际批准文件读取，没有另一处固定03工作目录。

同步维护新候选的`usage_09.md:11`调用路径和`handoff.md`最后的当前构建目录说明，让调用者知道新尝试是revision04；不要把未完成的revision03改写成成功。

## 批准审核SHA的处理

`freeze_release_09.py:116`目前精确要求批准文件SHA：

`0e5c8a4a1f29b059c3ddd19ff1112c2f9881692c192a21b5134f708e9ce393c3`

它对应的是已经完成的**源码合入批准审查**，不是revision03编译成功证明。正式684份源码表SHA为：

`b0667063407c2534ece08e4063cf1e63c3a25cb8c6212066ad966a9c5d492815`

若revision04只是在全新目录重编这同一684份源码，最小方式可以继续使用这份真实批准文件及源码表，freeze的精确SHA保持不变；新的构建路径仍由上面两道守卫改成04，实际构建成功另外由04的exit/complete和官方包证明。无需为换构建尝试虚构一份新源码批准。

若Root确实另建revision04的审查/晋级输入说明，则先生成实际文件、核其来源与源码表，再把新候选中的复制品和对应路径/SHA/字节数一起更新，最后修改freeze的精确批准SHA。不得先填预测SHA，不删精确匹配断言，也不把旧03失败对象改归04。

## 所有03引用的分类

| 文件或字段 | 实际用途及规则 |
| --- | --- |
| `prepare_tools_09.py:12`的PROMOTED | 指向`integration_09_revision_03_review`的源码晋级依据，不是构建对象目录；沿用原批准时可保留。若Root真建04来源元数据才改到真实04路径。 |
| `prepare_tools_09.py:62`的字符串替换 | 一次性生成器把旧模板的`integration_09_revision_02`换成03。若保留这个生成器作为新04候选的复现输入，应改目标为04。不要对已存在文件再次运行这个只许新建的生成器；它不能代替从最终已审候选做窄重基。 |
| `root_promoted_inputs_09.json/final_review/path`、`final_source_manifest/path`、`promotion_progress/path` | 三处指向03_review的真实源码原件；可保留历史来源。另有10条`evidence`路径指向03_review，均是已发生的源码晋级/检查依据，不应批量改名。 |
| `required_inputs_09.json/promoted_source_reference/root_inputs` | 03_review的已批准来源入口；沿用原批准可保留。若新建04来源入口，则连同review/source_manifest/promotion_progress实际SHA一起更新。 |
| `approved_final_merge_review.json/patches/10`的patch及前后checkpoint路径 | 第11片补全类型include的真实来源在03_review，必须保留原来路径和SHA；不能为了新编译把旧补丁说成在04执行。 |
| `approved_final_merge_review.json/patches/0/patch`里的`qb_entry_revision_03_20261103` | 这是QB作者第三版候选名称，与正式build03无关，必须保留。 |
| `usage_09.md`和handoff当前目录文字 | 是下一次实际调用说明，应同步04；历史来源描述仍保留原时点。 |

本次JSON窄搜索结果：`root_promoted_inputs_09.json`共有13条03引用，其中上述3条来源入口和10条evidence；批准审查有4条（QB版本名1条、第11片来源3条）；`required_inputs_09.json`1条；`rebase_result_02.json`没有03路径文字。不要把这些历史引用当成额外固定对象目录。

## 必须保持的内容与事实

- 保持已审13份观察/运行逻辑原字节：AD观察头、检查器构建脚本、应用退出捕获、collector、当前告警/LIS观察头、静态观察scope、原RX独审、runner、关闭合同工具、检查器CPP、summary及Task合同适配工具。不借重基调整24基础、可选评估、Qj后一次Qk、QK非确认、10秒本地提示、LIS只读观察、原配置守卫或640权限。
- 保留真实main、全部同批正式对象、moc/qrc/ui、报告辅助对象和依赖检查的现有绑定规则；04必须从自己的全新完整目录和实际完成记录取对象，不复制03部分对象，也不补写03退出0。
- 如源码确实未变，684源码表必须逐项仍等于上述b066表及正式文件。不能仅以文件数684相等替代逐项SHA一致。
- 正式官方归档位置仍为`07_backups/sn100_host/board_b/workflow_integration_20261102/integration_09`，runner继续只接受`releases/<实际id>`的flat路径。不要把所有`integration_09`字样都改成revision04。
- 新候选必须无旧`frozen_09`、prepared/link/build/最终handoff/上传产物。release、应用/报告/包/源码包SHA、对象数量、实际检查数、关闭数量继续未知；只有04真实构建和官方归档完成后才产生它们。静态scope中的null和所有业务未验false保持。
- 原preparation_02的`candidate_sources_02.sha256`及`rebase_result_02.json`不改写。新候选另做逐份校验和重基结果，明确只变了工作目录/必要来源元数据，旧03中断事实及失败材料保留。

## Root后续执行链

1. 先冻结同一684份源码及输入到新`integration_09_revision_04`，全量交叉编译；真实exit/complete未齐之前不调用检查器绑定，不上传旧版本。
2. 正式04构建真实0并完成匹配官方源码/程序包归档后，从**04** `build_complete.json`和官方`provenance.json`读取实际release及源码包SHA。
3. 普通WSL用户在新的工具候选目录调用：

```text
python3 <新候选绝对路径>/execute_final_09_build.py --execute \
  --integration-directory <项目绝对路径>/temp/sn100_host/workflow_night_20261102/integration_09_revision_04 \
  --release-id <04实际完整发布SHA> \
  --source-archive <项目绝对路径>/07_backups/sn100_host/board_b/workflow_integration_20261102/integration_09/sn100_sources_<实际发布SHA>.tar.gz \
  --source-archive-sha256 <同包实际SHA>
```

4. 驱动真实0且新final_handoff绑定正确后才由Root安排板端。仍先核板B身份、current、固定摘要清单及3INI+4主控配置、唯一原主控，不造LIS服务器、不重复启主控。收集后另实跑原RX独审，分别记录真实退出和待验范围。

当前只能给出重基要求；没有revision04构建、检查器或板端通过结论。
