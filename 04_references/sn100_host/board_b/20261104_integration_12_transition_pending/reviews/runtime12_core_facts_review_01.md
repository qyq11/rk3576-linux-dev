# 第12批运行核心草案与9:47捕获事实独审

结论：本次已读草案、派生捕获与第12批文件阶段原件未见事实错绑。准确旧C7、原主控、相机/3A、七配置及三资料入口均来自同次9:47:55原捕获，派生值逐项吻合；新6922目标对应已完成编译、独立存包和归档的实际包。草案仍frozen=false，稳定目标四项动作许可仍false，不表示已经停旧、切版或启动新程序，也不是可直接执行的运行批准。

本次只读原件、JSON元数据和稳定代码schema，计算摘要/字典与原文比较，只新增本报告；未改任何输入、源码、候选、冻结字节或公共文档，未导入或执行运行代码、check_core、file-phase校验器、产品、SQL、编译、网络或板端动作。没有把自行构造的派生值冒充板端原生JSON。

## 固定对象与schema边界

目录为 `temp/sn100_host/resume_dispatch_20261103/runtime_batch12_01/`。下列读取起止摘要均未变化：

| 文件 | 实际SHA-256 |
| --- | --- |
| `core_inputs.draft_01.json` | `e66b5ddf89e288b9eceac223aec948260e320fc76c949ab26d9a28a867bf0e9c` |
| `fresh_baseline_01.json` | `f4de87bb3055a41ab838812edd45c990ec8495b60ea7a50a850af352cfd78fff` |
| `fresh_baseline_provenance_01.json` | `47faadec2cb19aea487e770fd56665ac962e12d819e83b7f10efada63361873c` |
| `stable_target_intent_01.json` | `8268b34817c269de06949ce8c3468c21fd4cbb0f94159c9048e89545a1ebb87b` |

Core的26个本地record实际SHA和字节数全部匹配，包括file-phase原件、真实程序包、fresh捕获、Root裁定、稳定意图及七个来源脚本。三条 `/home/...` pointer是远端文件路径/指纹事实，已经逐项比对原捕获；它们不是要求电脑上同名文件存在的本地record。

只读对照 `workflow_next_20261103/runtime_transition_root_review_12_01/` 稳定快照：生成器实际 `6e7864038f34f8d600899cba48823a04a271af1c74151b304a5e54958b343797`，file-phase模块实际 `b5683da170a338ab9d7448f0d8d5760ab364b11c259d47f497a63a7bc9783760`，都等root_code_snapshot。check_core的核心字段集合、进程身份、独立metadata/audit目录、观察目录、来源脚本和24字段意图的已读值一致。它明确要求核心frozen=true，所以当前false草案按设计不可执行；本审没有运行该函数或把字段静态核对填成一次执行通过。

## 9:47:55原捕获与派生值

Root原捕获为 `temp/sn100_host/root_manager_20261103/runtime_context_integration12_20261104_02.{sh,log,exit,started}`，对应Root提供的Tool951970原调用。四原件和派生fresh记录均等provenance所列SHA/字节；原exit直接读取为0。原log SHA为 `b0532136b01e8142b1e432a33f6f8e9feb6c1e64318e7950ae4a3a7af05e52c8`，UTC为 `2026-11-04T01:47:55Z`，即北京时间9:47:55，46个键无重复。

fresh JSON的全部对应字段逐项等raw log，且完整等Core鲜活基线去掉capture_record后加原format/serial的结构，未让capture引用自己形成SHA循环。原串号 `8314b1e8ffd0321f`、boot `0eb94389-f120-42ad-9042-9c21d96a1481` 与Core一致；网络身份核验归Root该次原调用，本审没有再次联板取得新的SSH事实。

旧current仍是C7 `releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e`；准确旧run为 `/home/rkshare/programs/sn100_shell/runs/20261103_221819_662833`。原latest_run内容对应的SHA `6a3bd2621c4f31632cb21004d701e4460caa870b06d42cb8352e417e3e682bc8` 也与该run名加原换行的摘要吻合，不是只凭PID给新run改贴旧身份。

| 原捕获对象 | PID / 启动计数 / UID | 原映像SHA-256 |
| --- | --- | --- |
| C7主程序 | `669382 / 2102645 / 1000` | `50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258` |
| 原主控 `/userdata/shared/ACA_V1.0OffLineD-500` | `5008 / 118180 / 0` | `ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff` |
| `/opt/opto-camera/bin/opto-camera` | `2046481 / 5701959 / 0` | `7a128358683d146319d6c79fccbfc10b144937083ca2647a2cd03355fe2ed4c4` |
| `/usr/bin/rkaiq_3A_server` | `139148 / 847289 / 0` | `bd7b443fd92bf853c71a1816f2ad8ea5ebeef88601b1fe1a8039f7ec321178c0` |

旧helper实际仍为 `e3587bad320003bf1a2ea7b3176bf14d23527ebaae65b60ffb4bd5388d3866d2`。相机服务记录active/running，MainPID=2046481，与camera进程一致；camera/3A没有与C7或主控重用PID。原捕获记录唯一5000监听端点 `0.0.0.0:5000`，按原输出保留，没有增加原log未单列的socket所属PID字段。

原脚本在输出配置摘要前实际执行七配置 `sha256sum --quiet -c`，并在set -Eeuo pipefail下成功退出，不能只解释为manifest文件自身未变。对应775字节、7行规范LF清单摘要仍为 `ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c`；本地原件、Core和原捕获一致。没有读取INI正文。

三入口按原顺序逐项等捕获：original_project_root 为 `423baacf81fc1073b653eedd14651d701c3648b7e3324491d2a53f9d7af0f7a9`；instrument_configuration_file 为 `acf0371f227fc6fbcad1cff6a355c956d34105cec33546d980abab9317416422`；theme_configuration_file 为 `b89f9a8af4a052739555b0df65127859c7060bffc3ca204de1238556f7d3bf5c`。原脚本核它们是非链接普通文件后取指纹，未把三入口SHA冒充其指向全部资料内容的SHA。

原log末尾READ_ONLY_CONTEXT=1、PROGRAM_SIGNALLED=0、CURRENT_CHANGED=0、ACTIVE_VT=tty7；所读脚本只有读取、身份和哈希核对，没有产品启停命令。以上是9:47这次捕获的时间点事实，不是以后阶段现场永远不变的保证。

## 第12批实际文件阶段与新目标来源

Core固定的build/stage/archive/collect/finish五个原退出文件直接读出均0。build_complete实际arm64_build_and_package_passed=true，program_executed=false、board_runtime_verified=false；对象来源、依赖报告、实际包、stage/collection/archive/finish及其互相引用的record全部与Core固定值吻合。依赖报告errors为空且静态闭包通过，没有把静态链接结果说成产品已运行。

新release_id为 `69220b2eb393408a0c612b43a0fc954e479e8c450a09a48755b3fd2d43b84f72`；主程序SHA为 `fb1d42f5a67b0f3663cb3098947ac69d480d4a6784d790bd055ee3dcd5d8ef91`，报告辅助程序为 `74b8636086d13cefb63d69117fcafdbf9c3fc01c440885d800b5bafd7189c78f`，均等build_complete的binaries；helper仍e3587b…。实际程序包SHA为 `2834aaabfb492f0667e55c2f0a97a01be8fbbd3d100dd273ba46eb2f1ecdc687`，Core引用的114764370字节包实物哈希匹配，且与build/package/stage记录一致。发布号与程序包SHA没有混用。

构建实际绑定cfg `d09f1ab9555a5cda1e6a66fa39ec8e6b2afdf4262778028c3cf6f02ba45d44be`、正式完整源表 `0e5c70ef9d5ac5fa3db34888103b4158783141aac5f90845e225c720d532ac8d`、review79b3d9…与progressbd6e2d…。构建source_snapshot原文为 `a9acea007b948d21dabc0d109b17d76ecf207f8d1eed838212d3e6462f79791d`，实际字典与0e5c表同为688且完全相等；各自序列化原文SHA分开保留，没有把不同原文摘要误报为源码变化。

stage实际远端目标是独立嵌套 `releases/integration_12_01/<6922完整发布号>`，current_before/current_after均C7，activation_performed=false、program_started=false。四份完成记录均complete=true并指同一6922；收证、归档、封口的config/build/stage/object/配置guard相互引用一致，配置前后原文等冻结ad11。finish明确current_changed/program_started/rk_function_verified/instrument_verified均false。至此证明编译、独立存包和归档完成，不证明新GUI或Uh真实功能已经运行。

## 稳定目标固定不等于动作许可

stable_target_intent恰24字段，顺序等稳定check_core的INTENT_KEYS，实际字节等ensure_ascii规范缩进JSON加单LF。目标release、应用SHA、cfg、build_complete、package和观察值都与Core对应；flat `/releases/<6922>/sn100_shell` 是未来prepare/切版路线的稳定目标，不能冒称file-stage已准备或已激活该flat目录。

该目标意图frozen=true只固定目标内容；allow_prepare_flat、allow_activation、allow_old_app_stop、allow_unique_start四项均为bool false，leave_running_required=true、supervise_seconds=0。观察90秒只属于等待器设置，不是主程序寿命或90秒后关闭许可。Core仍frozen=false，后续Root须另存真实最终frozen输入，并按原逐阶段授权/收据链操作；本报告没有补动作许可、旧停回执、新启动结果或未来退出0。
