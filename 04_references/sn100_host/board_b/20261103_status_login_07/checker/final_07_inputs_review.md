# 07正式输入与检查程序的独立只读复核

复核日期：2026-11-03。复核者只读取本批已有源码、归档清单、冻结副本、对象文件和实际构建结果，计算文件校验值并核对接线；没有执行冻结、绑定、构建、产品、采集或联网操作。本次仅新增此说明，不改9份模板、冻结文件、06证据或正式源码。

结论：已查到的07正式输入、冻结副本、277个正式对象和本批检查程序相互对应，未发现确定性的路径、数量或校验值错误。唯一执行方生成的冻结、检查程序构建、依赖检查退出码均为0。这证明本批检查程序已按07正式输入完成ARM64构建与依赖检查；尚未证明07在RK上运行通过，也未证明试剂液位N消息的业务或提交中关闭通过。

## 本批身份

| 项目 | 实际校验值 |
| --- | --- |
| 07发布身份 | `94d1b9794f2d8f1ff776b498e62d29ba2ebd82fef807d7cda70b05837c3eccb1` |
| 正式应用 | `79dc525c85dcee4bf27fe9af95d07334b4b055631495467646854da84775834c` |
| 正式源码压缩包 | `18eaa7fb091d82cddee883c52ac8d6d50d2d86d9d65cd6f25cff99cd74520b41` |
| 正式源码快照文件 | `fd5614ad0673ab6896eeea1f7502ac09ec8a4ac4f98a338f5967a4ad3717fdaf` |
| 正式main源码 | `a0683da0485f71ac2b8af5abda6c7251fc35b65cc19288dd63c9d4ea1193bed7` |
| 正式main对象obj_0.o | `db02d56fcbf0da432db87cda5ef04fed918181ddd7badbc0a0f82097a34c6b41` |
| 本批检查程序 | `98192c94c239fe3e58c7e508af9871907ff790f3496efd49de232240c4a52806` |
| 链接记录内部身份record_sha256 | `782b1984985101433a23ab851b44e9182c4facd0fbeb01a05b757851f83013bc` |
| link_provenance.json整个文件 | `f5a009c83b7621a5a16397dd4a07174a3878cc7f2763500f1f6db8bf4ecb6365` |

`record_sha256`是链接记录按其约定内容计算的内部身份，和整个JSON文件的校验值是两个不同字段；实际构建结果分别引用了正确的值。

正式归档位置：[integration_07](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/07_backups/sn100_host/board_b/workflow_integration_20261102/integration_07/)。源码包名为`sn100_sources_94d1b9794f2d8f1ff776b498e62d29ba2ebd82fef807d7cda70b05837c3eccb1.tar.gz`。本次独立计算归档清单内8个文件的SHA-256，全部匹配；只列出源码包成员，没有再次解包，文件成员数663，`06_programs/sn100_host/shell/main.cpp`只有一份。

## 663份文件如何对应到本批构建

正式`source_snapshot.json`有663个路径。正式打包暂存目录是`temp/sn100_host/workflow_night_20261102/integration_07/packages/stage_20261103T024413_768954`；其中发布清单校验值、release_id均为上表07发布身份，应用文件校验值也匹配。暂存目录`release.json`内663项源码路径与校验值逐项对照正式快照，差异0。

与06的649份文件相比，07为663份；本批正式来源记录列明新增14、删除0、改变25。这是源码文件数量，不是对象数量。

本批[prepared_inputs.json](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/board_b/20261102_status_login/integration_07_review_preparation/prepared_inputs.json)的格式为`sn100-review-07-v1`，状态为`bound_to_final_07_archive`。其中正式来源、源码包、正式应用、main源码、暂存目录和构建输入身份均指向本批07。冻结阶段`object_count`仍为null；对象数由后续实际链接记录填写，不能把这个null解释为没有对象。

冻结副本`frozen_07/sn100_host`实际有663个文件。本次独立逐文件计算校验值，与正式663项快照比较，差异0；QA的`source_snapshot_07.json`内容字典与正式快照一致，差异0。该QA快照文件自身校验值为`760d81b3fe64f375e9600ed3deb71d6c67699cf2d9f53a82cc03d434a259af26`，与prepared记录一致。JSON排版不同会影响文件校验值，因此QA快照文件校验值与正式快照文件校验值不同，但663项内容一致。

prepared记录的9份模板和4份关闭检查合同文件，都与当时实际文件校验值一致。prepared所绑正式归档清单校验值为`6d5af9a27fa0162374a5c0d660e3adf5ddafc377af0314c04b7f0e77de00c926`，与实际文件一致。

## 277个正式对象与真实main

读取正式`build_inputs.tsv`及冻结的`build.sh`后，按真实编译顺序核对[link_provenance.json](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/board_b/20261102_status_login/integration_07_review_preparation/link_provenance.json)：

| 来源 | 数量和对象范围 | 独立核对结果 |
| --- | --- | --- |
| 普通C++源码 | 173，obj_0.o至obj_172.o | 源码顺序、输入路径差异0 |
| Qt为带Q_OBJECT头文件生成的源码 | 93，obj_173.o至obj_265.o | 135个头文件中93个带Q_OBJECT；头文件顺序与moc文件名映射差异0 |
| Qt资源文件生成的源码 | 11，obj_266.o至obj_276.o | 资源路径、build.sh中资源名、qrc文件名顺序差异0 |
| 合计 | 277 | 索引0至276连续；对象索引和路径差异0 |

本次独立计算277个实际对象和153个构建生成文件的校验值，与链接记录比较，均为0差异。链接记录中的prepared文件校验值、正式身份、模板和关闭合同绑定，也都与实际prepared记录一致。

首个对象确为正式`shell/main.cpp`对应的`obj_0.o`，`main_object_kept=true`。直接读取该对象ELF头，结果为64位、小端、可重定位、机器类型183（ARM64）。07的main源码字节与06相同，但本批main对象校验值为上表`db02...c6b41`，没有沿用06对象身份。检查程序构建来源记录只额外编译`status_login_review_07.cpp`，保留了这277个正式对象中的真实main。

## 实际构建结果与前后输入

唯一执行方生成的[execution_07_result.json](C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/board_b/20261102_status_login/integration_07_review_preparation/execution_07_result.json)记录：冻结0、检查程序构建0、检查程序依赖检查0。相应3个实际`.exit`文件也均为0。

本次独立计算检查程序、检查源码、AD只读观察头文件的校验值，分别与实际结果、prepared及链接记录匹配：

- 检查程序为上表`98192c...52806`；`review_binary.sha256`和实际文件一致，`review_release_id`为本批07发布身份。
- 检查源码为`621aedd2521a8e7460920ea657025a54652c890e14148375c9203eac731fac65`。
- AD只读观察头文件为`c1982e4435f5332f7103c5c5fad27dc1df9b61a6c02a872639ff0288433595a0`。

前后输入记录`execution_07_inputs_before.json`和`execution_07_inputs_after.json`的实际文件SHA-256同为`7c8041e3ab12764888842daae16ed077e408ee96d9c4a8abe213092155de7d14`，文件字节相同。实际结果记录也说明完整源码、归档、依赖与正式对象未改变；其中对象数277、源码数663，依赖文件数56。这里只读核对本次已有记录，没有再执行一轮构建或检查程序。

## 关闭合同与后续运行边界

最终07关闭合同为`sn100-shutdown-contract-07-v1`，要求18条源码中真实存在的标记。相比先前候选审查，最终已加入`REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false`，并绑定本批`frmMain::requestReagentLevelShutdown`及相关源码校验值；此前候选“尚无独立N关闭标记”的观察不能替代这个最终状态。

合同对该标记的范围写得明确：到达真实关闭保护成功分支时，活动请求和工作线程待处理任务都已空；试剂液位工作者复用已有ReagentStore线程，没有新增独立QThread。该标记与既有`REAGENT_STORE_CLOSED`、`THREADS_STOPPED true`一起检查，也仍不能单独证明N消息提交过程中关闭已验收。实际结果中的`n_function_verified`和`in_flight_n_close_verified`均为false。

本批检查源码和AD观察接口已按最终07输入重新绑定、重新构建；这些校验值不能把06实际运行结果变成07结果。原24项软件检查和1至24通道的96格AD原字符串显示仍是其运行检查范围，尚无本批运行数据可据此声称通过。`65535`即使将来原样显示且“已上报”标志为真，也只证明收到并显示了该字符串，不能据此认定物理测量有效。N试剂液位的接纳、写入、重载、同一仪器上下文显示以及提交中关闭，需要单独的真实运行证据。

QA实际结果明确记录`board_accessed=false`、`product_executed=false`。正式归档的较早来源记录另有板B暂存和依赖核对已通过，且记录未激活、当前发布仍未切换；这不等于本批QA已联板运行。当前能确认的是正式输入一致、ARM64构建和依赖检查通过；07的RK运行、真实仪器功能和上述N业务边界仍待后续授权运行核对。
