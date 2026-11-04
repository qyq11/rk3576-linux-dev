# 第12批九工具：独立代码窄审

2026-11-04。独立读取 `workflow_next_20261103/build_archive_candidate_12_01` 九份实际代码，对照 `integration_11_root_frozen_01` 同功能冻结九工具。审前、审后及机器清单写出后重算九份指纹，均稳定；旧九工具也仍与其冻结cfg一致。只新增本报告和同名.tools.json，没有改候选、正式源码、旧工具或公共文档，没有import候选、执行preflight/phase、产品、SQL、模拟、编译、网络或板端流程。

结论：这九份实际代码未发现确定的新增阻点，可按机器清单中的固定字节交Root复制冻结。代码相对已审第11批工具只有必要批次映射；这不是第12批真实冻结cfg、正式来源台账或执行结果的接纳。作者后续若改九代码，应以新实物重新比对，不能按文件名或本报告标题继续认定相同。

机器清单为 `build_archive_12_independent_review_01.tools.json`，顶层是九项数组，每项只有仓库相对path、sha256、bytes，可直接由JSON读取后构造cfg.tools，不需要从Markdown提取表格。该JSON实读SHA为 `1515aa15d996633bdbf21dadbe6f206411647864adc4043c1aec9ea094e11e06`，2043字节；写出后九项再次核实际文件，0不匹配。

## 九代码增量

按明确映射重建预期字节后，九份全部与实际候选逐字节相同，行尾分布保持；不是只比较作者清单。映射包含integration_11_01→integration_12_01、格式中的integration-11-01→integration-12-01、对应文件/import及大写构建标记，以及 `.workflow_upload_11_01_` / `.workflow_stage_11_01.` 的本批前缀。工具路径和批次相关文字同步映射，没有其他控制流或业务表达式变化。

九份为archive、build、finish、object_binding、tool_support、stage.py、stage.sh、verify_and_save、verify_inputs。七个Python文件均由本审直接ast.parse成功；两个shell文件分别用本地Git Bash仅执行 `bash -n <文件>`，实际退出0/0、输出为空。没有执行任何shell正文，也没有运行作者私有回演；作者所称回演结果不作为本审执行证据。

build通过脚本自身目录确定tools，WORK为本批新目录；support中的WORK/ARCHIVE/EVIDENCE/CONFIG、各import、各格式/标记、远端 `releases/integration_12_01`、上传唯一文件及临存拒覆盖规则配套。旧11目录没有被新代码作为输出误覆盖，也没有因切批次增加启动、切current或关闭旧程序的分支。

## 留驻与仅存包保护

23字段v2的集合、身份读取和前后比较算法保持；七配置的准确路径顺序、普通文件/非链接核对、原文SHA及日志BASE64封口保持；原C7期待路径、应用/主控SHA和硬件serial/SSH pin保持。stage仍在同一immutable_store.lock中解包/提交前后取guard，独立新目录存包，current只核原值，真实ACTIVATED/PROGRAM_STARTED标记均要求0。ldd仍只作动态库依赖解析，不形成新产品运行结论。

运行保护仍为C7：`releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e`，原应用SHA `50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258`；原主控 `/userdata/shared/ACA_V1.0OffLineD-500`、SHA `ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff`。这些是保护条件，本审没有重新采板端快照，既有11观测也不算12实测。

stage/upload/collector/finish仍要求本批started/complete、真实命令退出、日志标记及所有输入/结果的精确记录；新版本只另存，不调用启停入口、运行器、主控重启或真实业务观察。observation_contract保留包后另由Root决定的运行身份交接，没有在文件阶段生成/使用新的运行ID或启动产品。

## 完整构建、真实对象和一片增量兼容

build在preflight通过前不创建WORK；已有WORK拒绝覆盖。正式构建前仍unset `SN100_ARM64_OBJECT_REUSE_MANIFEST` 和 `SN100_ARM64_FROZEN_SNAPSHOT`，使用本批新arm64_build与真实编译输入，未把第11批对象/二进制拷贝为本批结果。object_binding与verify_inputs逻辑除命名外未变：按实际source/form/header/resource/helper_source输入和生成moc/rcc重新计算对象集合，核ARM64对象、源码/生成物SHA及真实构建终态，再与实际包绑定。没有新对象复用或提前成功逻辑。

单片兼容是本次静态检查：support第249行只要求patches非空，第258—264行按实际列表遍历并要求索引等1到len，因此只有索引1的一片合法；promotion预期applied/preflight_applied同样从review动态导出。verify_inputs第104—110行按真实清单/patches长度统计source_count、patch_count、增删文件；对象数量也从真实输入动态计算。没有固定两片、固定687/688或只接受本批六个文件的硬编码。因此Root将一份实际Uh组合片登记为index=1可与现校验结构配套；本审没有构造假元数据或运行preflight来冒称这份实际台账已经通过。

## False配置与正确源码前像

候选cfg原件实读SHA为 `8152b1154806ef6c9019020bee534cf7dbf8c256539620f4b7f00672a2fb4b46`，审前后相同。frozen=false，allow_full_build=false、allow_stage=false，final_review/final_source_manifest/promotion等真实值仍未填。cfg()第208行拒绝未冻结模板；build的preflight发生在mkdir WORK之前。没有为静态审查把False模板改成可执行配置。

本次实际重算三份已存在的输入，路径、SHA、bytes均与False配置一致：

| 输入 | 实际SHA-256 / bytes |
| --- | --- |
| 第11批源码归档 `07_backups/.../integration_11_01/sn100_sources_356fe5f88efe52226105f68ec3d6479f70d3f4ad12bcb967da790a180d22b6d5.tar.gz` | `d709196b4fa1f33af57c7f038abdf2134341bd5619617f3c9d94b8144055dd96` / 2285866 |
| 第11批 `source_snapshot.json` | `387348d0a0dfb77cb5c1640432044d3e283dfe8f3a427e946568ba8a6352a676` / 73954 |
| 七配置冻结原件 | `ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c` / 775 |

387快照实际为688项，其字典精确等b078源码表。baseline资料路径中的integration_11_01是有意引用上轮真实输入，保留正确，没有误映成12输出目录；运行expected_current仍是C7，未与源码前像混淆。配置中的九代码记录也逐项与本次实际SHA/bytes相同。

Root后续仍须另存真实冻结cfg，绑定实际来源独审、正式源码、promotion及完整补丁链，然后才能执行本批。当前结论固定九代码及本次读到的False模板，不包含仍可能更新的README/最终作者交接，也不替Root最终来源台账独审。没有新增第12批编译、存包、归档、RK/实体、功能或资源通过结论；急诊生产来源和UI启用仍在本批之外。
