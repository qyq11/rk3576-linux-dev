# 第11批实际ARM64构建产物独立复核

2026-11-04，审查者：Root 独立审查 Agent `/root/review_amount`。工作目录为 `temp/sn100_host/workflow_next_20261103/integration_11_01/`，冻结工具目录为同级 `integration_11_root_frozen_01/`。

结论：本次实际构建产物、来源和对象绑定的只读核对通过，未发现确定不一致。构建真实退出0，完整收尾标志出现一次；688源、396个不同输入、293个对象及155份生成文件与冻结记录相符。此结论仅到ARM64构建/静态依赖/包文件身份，不含RK运行、界面、真实仪器、实体或资源验证。

仅使用自写只读文件核对：读取JSON/TSV、AST常量、正式构建数组、XML资源引用、ELF头和SHA；没有import或调用候选函数，没有执行预检/任何phase、编译、产品、测试、网络或板卡动作，没有修改WORK或冻结输入。Root并行进行的stage不属于本次复核，未读它的中间结果作成功结论。

## 真实退出与原件

`build_exit.txt` 原文为 `0\n`，`logs/build.log` 中 `ARM64_INTEGRATION_11_01_BUILD_AND_PACKAGE_PASSED=1` 精确出现一次。build_complete明确 `arm64_build_and_package_passed=true`，同时 `program_executed=false`、`board_runtime_verified=false`。

| WORK内原件 | 字节数 | SHA-256 |
| --- | --- | --- |
| `build_complete.json` | 1825 | `6d2c08b0aafb54bbe7be3b89a9657b644617f279e6de9cb3abcafa4c85af76df` |
| `input_verification.json` | 805 | `b5a59040c361ebeeb386724a0b665ce0a76473357087e5086a86edeec1a01c84` |
| `object_binding.json` | 771686 | `b67b30b5001f452eb5ff3c628e315e6bb6ac47b356556efb9abda4854d1f778b` |
| `dependencies/dependency_report.json` | 358796 | `87c1441887f582a933434bf9291c47791d84a979507e5f379b95d883075fa8f4` |
| `source_snapshot.json` | 73954 | `387348d0a0dfb77cb5c1640432044d3e283dfe8f3a427e946568ba8a6352a676` |
| `build_exit.txt` | 2 | `9a271f2a916b0b6ee6cecb2426f0b3206ef074578be55d9bc94f6f3fe3ab86aa` |
| `build_inputs.tsv` | 57301 | `c840991c036edccb4e4f0c8163b9778fd1be47bb5ee69671cbcb5fd4febd8fed` |
| `logs/build.log` | 24070 | `a3753f37abce8bc2d7c7c1aed3089e99ce05e530eb84ad913ae716364bf899f9` |
| `merge_inputs.json` | 209376 | `8470f2abccda6a475ee7f6e7e31caaa58a5c299f99990815534d42cea3252ed8` |

以上build_complete、object_binding、依赖报告等初读与实物核对时摘要一致；日志没有用旧批结果替代本批收尾。

## 来源、配置与688份实际源码

- build_complete绑定冻结cfg `051977b52473c907e3df8dba3245781432aa619b79cb0a9f4b22b2430b92186c`，实际文件相符。绑定真实v2聚合器 `307674360d5c24c3fe17eae04e5c7a869f7f35021fdaabecb9fd0afedbc165cd`、review `1c71afdec3568e547ace6193c533e68d499f132e890aa0817d54500729251abb` 与progress `8470f2abccda6a475ee7f6e7e31caaa58a5c299f99990815534d42cea3252ed8`，实物摘要均匹配；WORK的merge_inputs原文逐字等真实progress。
- 构建snapshot、binding内source_files、v2最终源表及cfg指定批准表的688项字典逐项相等。批准源表原文仍为 `b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767`；本次构建snapshot原文为 `387348d0a0dfb77cb5c1640432044d3e283dfe8f3a427e946568ba8a6352a676`。两种JSON序列化各自保留并正确绑定，未因字典相等混用原文摘要。
- binding的完整frozen_inputs集合恰为批准的688份正式源；逐项读取当前实际源文件，SHA及bytes均匹配。基线仍固定C7归档原表fd50及真实源包56fb，未把它当本次终点。
- input_verification除自身format外的全部字段等于build_complete对应值；来源片数2、新增纯值头1项、无删除，与真实Mg/LIS范围对应。未把Uh或急诊候选算入本次源码。

## 实际输入、对象与生成文件

实际TSV输入为：

| 类别 | 实际列项 |
| --- | ---: |
| 普通源文件 | 185 |
| 界面表单 | 49 |
| 头文件 | 149 |
| 资源清单 | 11 |
| 报告辅助来源 | 18 |

五类合计412列项，辅助来源中包含共享主源，因此去重为396个实际文件；全部路径和实际摘要都被688源表覆盖。新probe纯值头在真实头输入内，不凭空新增moc。

独立静态展开正式build.sh的输入数组，与实际TSV顺序逐项一致；去数组后的生成/编译/链接骨架等已审冻结规则，骨架SHA `35ad4b2c1c8197023cc805b750dd59aa132173d8c501a9a795bb7c4cd1d05eeb`。没有运行build.sh或对象绑定工具来复用其“通过”结果。

据实际数组、Q_OBJECT标记及QRC直接资源引用，独立重建后逐项核对：

- 主对象291 = 185普通源 + 95个moc元对象源 + 11个资源源；另有2个报告辅助对象，总293。
- 155份生成物 = 95个moc + 11个资源生成源 + 49个界面头；其实际文件集合精确相等，无缺失或额外生成源。
- 所有对象的实际编号/路径、直接来源、冻结输入顺序和摘要、链接角色均等binding。主程序链接291项，报告程序链接30项（含共享主对象及2个新增辅助对象），未把30当额外编译对象数。
- 实际arm64_build目录449个文件逐项等绑定目录清单；其中293对象、155生成物及build.lock。没有多出旧对象或少报实际对象。
- 本次去重读取并计算1150份原件的SHA，所有登记bytes及普通单链接文件检查匹配，0问题。没有整份大JSON输出到报告。

这里核实的是冻结构建规则和真实TSV导出的直接来源对应，以及产物原字节；binding明确 `direct_generation_inputs_only_not_compiler_trace_or_full_header_graph`。不把事后绑定说成编译器原始依赖跟踪，也不据此证明业务或算法已运行。

## ELF、实际程序及包文件

293份对象逐个读取实际ELF头，均为64位小端、`e_type=1`（可重定位对象）、`e_machine=183`（AArch64），SHA逐个等绑定记录。

| 实际程序 | 字节数 | SHA-256 |
| --- | ---: | --- |
| sn100_shell | 184780096 | `3248b4fe66aa0014ccf3890bf253a1ea5905352981f0e8c692ad628b0c372642` |
| sn100_report_renderer | 15775328 | `9a6091a9aee0e38e51a1963f323559091d6cb879412e6cb4f3a11c6550ab568b` |

两份实际程序均为64位小端AArch64 ELF、`e_type=3`，与build_complete、binding及依赖报告的main/report_renderer对应记录一致。只读文件头，没有执行程序。

程序包实际114732910字节，SHA `4f90ea77c1c9eef9e94b7af92761cb6efeecb5a77724daabaefc2828539ea6f5`，等build_complete固定包文件。文件名对应发布编号 `356fe5f88efe52226105f68ec3d6479f70d3f4ad12bcb967da790a180d22b6d5`。发布编号来自包内清单身份，与压缩包自身SHA分开，不互相替代。本报告未替代后续包内全成员回读、远端另存与归档收尾。

## 静态依赖的真实边界

依赖报告摘要等build_complete，errors为空；main和report_renderer记录的实际路径/SHA均等上述两份程序。报告有56项文件、242条依赖边、398项符号版本检查及6条明确限制说明，且 `board_runtime_verified=false`。

原字段名 `offline_link_and_sql_closure_passed`、状态 `offline_closure_passed_board_runtime_pending` 只表示静态文件、动态库及SQLite插件的依赖核查，不是离线业务测试、开业务库、产品运行或已安装板端库的实测。六条限制仍涉及板端显示/输入、GPU/DRM、权限、Qt插件/字体及实时动态链接环境，不应删掉或转为通过。

本阶段可以登记“完整ARM64构建0，实际产物及直接来源绑定独立复核一致”。Root随后仍须分别收取实际stage、归档/采集/收口结果；运行C7保持的现场、RK新功能、真实仪器、实体和资源结果不由本报告推定。

