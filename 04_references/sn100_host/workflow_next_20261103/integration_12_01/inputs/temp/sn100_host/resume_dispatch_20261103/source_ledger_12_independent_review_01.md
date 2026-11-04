# 第12批Uh单片来源草稿02独立审查

结论：锁定的draft02及本次实际来源原件未见阻点，可以由Root按原v2门槛另存冻结清单并实际汇总。本报告没有执行v2、补丁、阶段工具、产品、编译、SQL或板卡动作，也没有修改spec、来源原件、候选、正式源码或公共文档。只读核对及新增本报告；未使用的draft03没有展开或参与本结论。

锁定对象是 `temp/sn100_host/resume_dispatch_20261103/source_ledger_inputs_12_01/root_spec_12.draft_02.json`，39699字节，读取开始和结束SHA-256均为 `0e775cbb6d30a38713fb9f86b9cebc5d8f0f9023a6924fa668aebf05f0a1a989`。本次亲读的当前Root批准为同目录 `root_source_approval_688_12_01.md`，开始和结束SHA-256均为 `408b8f27f70e2139686aeec7b0356e23c33038981f9e93aedf24c4072cdcf648`，已经包含采用后Git采集的时点和缺键退出1范围，没有反绑未来冻结清单或汇总结果。

## 输入覆盖与原v2接口

- 119项 `supporting_records` 的实际文件、SHA-256和字节数在起止两次核对均119/119吻合。源包、源表、v2脚本、快照规则和根属性记录也逐项吻合。三份canonical及外层/路径/Git/源包说明中显式引用的来源record，均已经进入spec的可检查record集合，没有只藏在派生JSON中而漏掉119项的来源，也没有同路径不同SHA。
- 未修改的v2实际仍为 `307674360d5c24c3fe17eae04e5c7a869f7f35021fdaabecb9fd0afedbc165cd`，快照规则为 `9fe7dab75b7566044d0ec485b0ffd47f4232be0142b664f35857dd6a18c524e3`，根 `.gitattributes` 为 `c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a`。只读核对原v2的schema和checked/source_map/command_evidence/来源链门槛，未导入或调用它。
- 本批确为一片 `ascii_uh`、index=1，required_patch_names只有它；scope等实际六项变化。adoption_scripts指向真实 `accept_uh12_source_01.py`，不是拿JSON伪装脚本。两份真实Root组合独审仍为f424f1…和42c3fb…。采用前即时核对 `uh12_adoption_preflight_review_01.md` 尚不在02的119项内，Root已明确在新冻结版补入，不能声称02原来就包含它。
- draft02保持 `frozen=false`、`all_intended_source_slices_present=false`、`root_approval=null`，输出目录尚未创建；pending和draft_notes是准备时点的待办，没有预填审批或未来成功。这是送审草稿的正确边界。当前Root批准虽已实际存在，也没有倒写到旧draft02。

## 基线包、实际前后表与当前正式源

基线使用已封口第11批356发布的真实源码包，2285866字节，实际SHA-256为 `d709196b4fa1f33af57c7f038abdf2134341bd5619617f3c9d94b8144055dd96`。本次独立流式读取tar成员并计算内容摘要，没有解包：恰688个普通文件，路径均只有固定 `06_programs/sn100_host/` 前缀，未发现额外类型、重复或不安全路径；去掉此前缀后的完整成员摘要字典等归档snapshot。

归档 `source_snapshot.json` 原文实际SHA-256为 `387348d0a0dfb77cb5c1640432044d3e283dfe8f3a427e946568ba8a6352a676`，73954字节。它的内容字典等本片实际before `b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767`，原文SHA不同的事实正确分开保留。没有把运行C7、旧f741、发布号或b078表文件的摘要当作这次源码包摘要。

本片actual after与expected after均为 `0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d`，完整688项。独立比较实际前后表得六改、零新增、零删除，正好等spec scope：仪器页、通道接收器h/cpp、主窗h/设备控制cpp、main创建点。按官方snapshot同一目录及显式文件范围原生枚举当前正式源码并逐项算SHA，实际688项，较actual after新增0、缺失0、变动0；没有用作者合成预期替代实际after或当前实物。

原生Windows的before/after各自保留native_raw、canonical、path_normalization、实际snapshot命令、输出和退出。独立读取两组688项，反斜杠转正斜杠均一对一，全部SHA值不变，canonical完整字典吻合。这里只改源表路径表示，没有源码行尾补正，所以 `correction=null` 正确。

## 真实命令与派生诚实性

原Root补丁仍为d89e920d…，check/apply原命令有完整argv、仓库根cwd、整数actual_exit=0，两个原 `.actual_exit` 均0，四份stdout/stderr均为空且实际存在。逐键比较两份canonical与原command：所有原字段和值均保留，phase分别只补为check/apply；原命令确实使用 `-c core.autocrlf=false`，没有忽略空白、自动修复或三向apply选项。cwd与patch路径仍指向这一次正式采用。

acceptance canonical的每个原键值逐项等原acceptance，只新增v2所需的 `actual_source_equals_expected` 和清楚的来源说明；它精确映射真实bool `all_after_bytes_equal=true`，source_files仍688，patch_sha256仍绑定d89e920d…，没有拿来源汇总重新生成一次采用结果。

外层 `accept_uh12_source_01.exit` 原件为0，原stdout/stderr实际存在，stderr为空；stdout解析后完整等原 `source_complete.json`。该原件scope是 `source_acceptance_only`，complete=true、compiled=false、board_operated=false，仍注明运行C7及急诊恢复资料列待办。Tool526239只是Root消息所给原执行编号；退出和stdout直接取现存原件，没有用事后Tool说明伪造stdout。本批没有新的源码采用失败，`historical_failed_checks=[]` 正确，没有搬入第11批失败。

新Git窄采集发生在采用后的08:52:32，记录及当前Root批准都明确只证明采集时点。五项原退出逐份实读为0、1、1、1、0：core.autocrlf与六路径属性查询0；core.eol/core.safecrlf/apply.whitespace缺键分别1，stdout/stderr为空，原值保留。core.autocrlf原输出仍为系统配置true；这没有推翻原check/apply argv的命令级false，也没有改全局配置。三个缺键1没有冒充成功或被归类为补丁失败。

## 冻结接续与验证边界

spec现有record没有任何一项反向指向未来 `root_source_ledger_final_12_01/` 输出；当前批准不含未来spec/final_review/promotion/cfg摘要，本报告也不含这些未来摘要，未见SHA循环。原v2将核已冻结spec→产生final_review→promotion引用真实review SHA，cfg最后绑定；这些原有门槛应继续保留。

Root下一步另存frozen版本时，按已经作出的裁定绑定当前批准、采用前即时核对和本独审原件，设置真实本批scope齐备/frozen标记并清理该新版本的pending即可；不改draft02，不需要展开未使用03，也不需要再索用户批准。原draft里“独审/批准待补”仅保留其准备时点，不把旧空值作为后来审批结果。冻结后的实际v2退出、最终审查与终态仍由Root运行并登记，本审不预填0或complete。

通过的是当前单片源码来源审查。新Uh完整ARM64构建、板端软件、真实Uh收发与原库保存/页面采用、实体操作和资源测量仍各有后续范围；急诊恢复来源、LIS任务等待/保存/确认和算法不纳本批。C7运行保护与356源码基线分别记录，源码采用没有变成运行版本切换。
