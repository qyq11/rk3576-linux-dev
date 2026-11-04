# 第14批两片源码来源输入独立复核

结论：未发现阻止Root按已选草案另存冻结来源spec的确定问题。第12批688项底稿，经过已实际采用的LIS九源，再经过样本加号顺序两源修复，连续得到当前正式697项最终源码；合计9新增、5修改、0删除。Root后来补充的来源批准明确历史范围为空数组且不补造Git环境，符合实际记录。此结论只针对来源输入，不代表来源账已运行、工具配置已冻结、编译或RK业务通过。

本次独立只读解析JSON、原命令/输出/退出、补丁文本、源码和源码压缩包；只在内存核补丁文本前后像，没有调用聚合器、git apply、产品、编译、SSH或板端工具，没有解包写工程。仅新增本报告，未修改四草案、正式源码、九工具、第13批或固定归档。

## 锁定输入与稳定性

审核开始和完成时，下列四份作者锁定原稿的SHA与字节数均一致，仍保持原false/null状态：

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `root_spec_14.draft_01.json` | 38331 | `af823fc38bd61d3a5c9984067f5440f76479635b47f8e4b9a1ae3e39a47570d0` |
| `basis_inventory.json` | 17660 | `06f1138752b92af517b62873493cfb3ff236006f7d8de4caa1d472ddfa20a1a1` |
| `sample_add_order_acceptance.canonical.json` | 6907 | `c67d56bdf0144707dcd2dae8984d1c2eb0d643a09a4c6a11171b3c08e3b7e678` |
| `tools_config_14.draft_01.json` | 8123 | `d31dfdfcab631c25f85763b3363c7bc27f6fa42c6a849d4737a3a30788bc1bc3` |

四输入共246次record出现，实际非空原件去重为100份；逐份核项目内规范路径、无软链接/目录联接、单链接普通文件、SHA及声明字节数，全部一致。false配置中的`final_review`、`promotion_progress`两份空record是明确的未来槽位，未当作实物或通过结果；批准摘要也仍为空。其他Root待填项继续是null/false，未由本审擅自填入。

## 完整源码链

真实基线包为第12批6922版本源码归档，2287331字节、SHA `e4586b75facfa4fae2a3ee23defa8398885395f95c42f7badfc83a115aae6fcc`。本审重新读取压缩流，688个成员均是预期源码前缀下的唯一普通文件，无链接或路径越界；逐成员字节指纹完整等于raw `source_snapshot.json`的688项字典。raw清单SHA `a9acea007b948d21dabc0d109b17d76ecf207f8d1eed838212d3e6462f79791d`与首片canonical前表`0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d`的键值相同，原始字节摘要分开保留，没有混配路径。

第一片`index=1/name=lis_a`的前表等于上述基线。补丁`f0050002…007d5a`实际九新增、三处构建登记修改，12项scope完整。第二片`index=2/name=sample_add_order`的前表与第一片真实后表逐项相同，都是697项、SHA `ed4a3eb3025ef0602cdb49d41a6094f35011eb68ac19cf9f36e4f75fb3f6e254`。第二片补丁`85d9aadc…43928`只改`samples/sample_page.cpp/.h`两文件，无新增或删除，其实际后表、expected和本批final_source_manifest逐项相同，都是697项、74988字节、SHA `169f571193b2893e8cbb3df8359c723e414cff89232ccc6e5d3a9274c44022bd`。

本审在内存逐hunk核每个补丁的上下文、原行、增删行数及前后范围，保留CRLF/LF原字节；从688成员原字节推导出的两个完整后像分别精确等于ed4a和169f，没有调用git或写临时源码副本。两片scope与实际变化集合相同，没有以行尾修补或模糊匹配补足差异。

另按已固定`package_arm64.py`的源码范围规则只读解析AST并重新枚举正式工程，实际完整697项与169f最终表全部一致。相对基线的五修改恰为两份样本页和`shell/build.sh`、`shell/package_arm64.py`、`shell/shell.pri`，九新增全在`lis_business/`，无删除；相对13中间表只有两份样本页变化、其余695项保持。患者B1、全面审计其他发现、checker及其修订没有混入。

## 原命令、实际退出与兼容记录

LIS第一片的check/apply/acceptance三份canonical记录直接复用第13批原路径和原字节，与原13冻结spec对应项相同。命令记录逐字段保留原argv、cwd及actual_exit，新增phase和来源关联没有改命令内容。两份原`actual_exit`文件均0，原stdout/stderr及原采用脚本按SHA绑定，未重做采用。

样本第二片check/apply原command已带phase，本草案直接引用原件，未再生成命令兼容副本。两次实际退出均0，argv明确为命令级`git -c core.autocrlf=false apply --whitespace=error-all`，只有check带`--check`；准确指向本片补丁，cwd为本项目。未发现`--3way`、忽略空白或自动修补选项。原命令、独立退出文件与acceptance中的commands互相一致。

样本canonical接受记录完整保留原acceptance的全部11个键值，新增14个键只提供`actual_source_equals_expected=true`、派生说明和原件关联。该别名来自原`all_after_bytes_equal=true`及真实after=expected，不是新执行、新业务验证或新审查；原时间、三份review、补丁、两路径、697数量和169f摘要保持。两片共四份native/canonical路径规范化记录均重新核原字节SHA、文件数和字典：只将Windows反斜线键转换成正斜线，没有改源码指纹或产生重复键。

样本Root外层捕获的command准确调用`accept_sample14_source_01.py`，脚本SHA `8f1088572089cab5440f13e1d04cd8b7aee28f3731c42a8314dade38c8f4d9be`；外层exit真实0、stderr为空，stdout解析后完整等于其`source_complete.json`。后者明确`scope=source_acceptance_only`、compiled=false、board_operated=false，与实际范围一致，不借源码封口声称已编译或联板。

## Root决策、资源背景与未来输出边界

本审补核Root新写且停写的`root_source_approval_14_01.md`，SHA `231b1ef13370f14b3ac6cdf10d4e946333a8f19c8a181ed0b9c28a227cde7d42`。它准确选定上述draft01，明确本次两片源码采用没有失败、冻结时`historical_failed_checks=[]`；第12批运行过渡79/4另保留，不当作源码失败、不删除或改为成功。原草案history=null保持原样，Root决定写入未来冻结spec即可。

`git_apply_config.post_adoption_readonly_environment`仍为null；原argv中的命令级配置和cwd已有原件，没有补造当时未采的完整Git环境。根`.gitattributes`实际SHA仍为`c46d8032dc7311b5911dfb7e578ad70ba1258ac924b72675f8b2192c5b9d445a`。来源聚合脚本实际18701字节、SHA `307674360d5c24c3fe17eae04e5c7a869f7f35021fdaabecb9fd0afedbc165cd`，仍为已审原件；本审没有运行它。

相机停止后的support固定为`20261104_camera_stopped_baseline/`全部九文件，含隐藏属性文件，均已进入false配置证据。其SHA256SUMS为`c14f02edd25e5cc0a2142aae14baa90c10dfd6c1ac12057238bb0dacac5a0b19`；本审核八条成员校验及目录完整九文件。它只说明用户停机和Root11:28:57只读核板的历史时点，不是本审重新联板、资源达标或新版运行结果；不恢复相机，也不把相机身份说成原23字段文件保护已经覆盖。

另核Root第14批九代码副本快照SHA `d1c169d322ce4188093d405e2b756958cce1451373c8e7179be815f8c46649da`及实际九份SHA/字节一致。falsecfg仍引用作者候选路径且只保留禁止执行的输入状态；未来实际冻结配置须重新绑定Root九代码路径，并合入真实第14批required_cfg_evidence后另审，本报告不代替配置批准。

唯一新来源账输出为`temp/sn100_host/workflow_next_20261103/root_source_ledger_final_14_01`，本审结束时尚不存在。100份实际输入中没有指向该未来目录的record，Root批准也没有绑定包含自身摘要的未来spec/review/progress；旧13输出仅作实际support，没有冒称14结果。

Root可保留四原稿，另存冻结spec，补入本批准、本报告、已明确的空历史范围及齐全标记，保持同一两片顺序、实际记录和唯一输出。来源账真正成功后，才准备并独审14文件配置、安排完整ARM64编译和必要后续操作。C7及原主控留驻、关闭保护待处理、急诊定义待办、生产LIS不开，以及RK软件/真实仪器/实体操作/资源分别待验的边界均不因本来源输入审查改变。
