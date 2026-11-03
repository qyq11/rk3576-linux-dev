# 第07版检查器构建通过，等待RK运行

本次按总协调明确授权，只在电脑从匹配官方源包冻结输入、交叉编译完整检查器，并静态检查ARM64依赖。唯一driver实际退出0，冻结、编译链接、依赖检查三个实际退出码均为0。没有联板、上传、切current、运行产品或修改正式源码/对象。

| 本批身份 | SHA-256 |
| --- | --- |
| 发布编号 | `94d1b9794f2d8f1ff776b498e62d29ba2ebd82fef807d7cda70b05837c3eccb1` |
| 正式主程序 | `79dc525c85dcee4bf27fe9af95d07334b4b055631495467646854da84775834c` |
| 官方663份源码包 | `18eaa7fb091d82cddee883c52ac8d6d50d2d86d9d65cd6f25cff99cd74520b41` |
| 新检查器 | `98192c94c239fe3e58c7e508af9871907ff790f3496efd49de232240c4a52806` |
| 检查器源码 | `621aedd2521a8e7460920ea657025a54652c890e14148375c9203eac731fac65` |
| 独立原始回包观察头 | `c1982e4435f5332f7103c5c5fad27dc1df9b61a6c02a872639ff0288433595a0` |
| 最终退出合同 | `cada57d89d391cee3c7a5b7b5700acc54c2b9af7fee0bf21a7d1d2956498c4d7` |

正式663份源码和370项实际输入对应173份普通源文件、93份Qt自动连接源文件、11份资源源文件，共277个主程序对象。另2个报告辅助对象没有混入主链接。真实main保留为本次新`obj_0.o`，其SHA为`db02d56fcbf0da432db87cda5ef04fed918181ddd7badbc0a0f82097a34c6b41`；不是第06版对象。链接前后完整记录相同，摘要为`782b1984985101433a23ab851b44e9182c4facd0fbeb01a05b757851f83013bc`。

`execution_07_inputs_before.json`和`execution_07_inputs_after.json`完全相同，覆盖官方归档清单、complete、277个主对象及正式依赖报告列明的56文件、7项归档运行文件。新检查器自身的依赖检查是读取ELF和归档库，不是电脑运行测试；实际依赖报告在`checker_dependencies/`，板端实际依赖发现和程序行为仍待总协调运行。

唯一实际执行入口为`execute_final_07_build.py --execute --source-archive <官方07源包绝对路径> --source-archive-sha256 18eaa7fb091d82cddee883c52ac8d6d50d2d86d9d65cd6f25cff99cd74520b41`。`freeze_07.exit`、`build_review_07.exit`、`checker_dependency_check.exit`保存各步实际0，相应`.log`保留原输出。源包实际路径与全部前后指纹见执行记录，不从第06版或私有stage取头文件。

最终合同18项为原17项加`REAGENT_LEVEL_SHUTDOWN_READY active false worker_busy false`。N关闭保护、真实外层窗口重试和worker四份源码摘要已绑定；继续核既有ReagentStore关闭与线程等待，没有新增或虚构N线程停止日志。这个条件只证明关闭保护到达空闲就绪，不证明曾收到真实N、N保存/展示成功或在途N一次退出通过。

`qa_upload_07.sha256`列出总协调后续须一起上传的新检查器、发布绑定文件、运行脚本、真实wait脚本和四个合同文件，共8项。上传后核SHA，并由总协调核板卡身份、实际current、原主控和INI；此目录没有执行上传或板端操作。自动发送范围仍只允许精确Qj，原24项软件检查及真实新连接Qq/96格对照必须在RK实际执行后才可判定通过。

真人口令、实体输入/屏幕、绿色测试灯、N业务、明确保存操作和真实在途退出都保持原待验范围，不等待或重复询问用户当前无法提供的现场操作条件。
