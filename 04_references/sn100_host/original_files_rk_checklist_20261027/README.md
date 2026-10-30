# Windows原文件与RK逐项核对证据

对应[用户可逐行确认的清单](../../../01_docs/knowledge/sn100_original_files_rk_checklist.md)，实际核查时间为2026-10-27 19:19:23（北京时间）。Windows对象是原发行`bin/`；RK原资料根为`/home/rkshare/programs/sn100_shell/runtime/data`，部署版本为`f8c6e73f`。

29份DB、9份配置/规则及1份报表XML，共39份，RK全部存在，两次内容指纹稳定且与Windows原件一致，缺失0，没有数据库写日志边文件。原相对目录、文件名、空格及大小写保持；3个原空目录未创建，另行列出。该时点无主窗口进程。本次只读核查未启动应用、未部署新代码、未修改或覆盖资料，未导出原患者、口令或其他数据库记录。

| 文件 | 用途与核对范围 |
| --- | --- |
| [verification.json](verification.json) | 汇总39项完整Windows/RK路径、大小、原指纹和两次板端指纹，以及每项业务接入状态。文件复制齐全，业务未全部接入。 |
| [windows_inventory.json](windows_inventory.json) | 重新枚举原发行目录的39份DB、INI、CFG、容量JSON、告警CSV和报表XML，核对与既有原清单和141份部署清单一致。 |
| [rk_presence.tsv](rk_presence.tsv) | SSH实际读取的逐文件属性和两次指纹；未查询原库记录。 |
| [board_context.txt](board_context.txt) | 实际时间、资料根、发布版本、主程序及报告程序指纹，和当时的进程检查。 |
| [empty_directories.tsv](empty_directories.tsv) | `dectFile/`、`calibrationFile/face/`和`calibrationFile/vol/`三个原空目录在RK未创建；不计作缺失原文件。 |
| [source_binding_check.json](source_binding_check.json) | 核对主入口、项目原库代码、报告渲染及内嵌模板4份审计依据与已部署版本的源码身份一致；不代表所有新源码都已部署。 |
| [business_usage_audit.json](business_usage_audit.json) | 依据已部署版本源码、归档和既有RK结果，区分项目配置读写、有限读取、仅部署及新代码的实际阶段。此次不是逐功能运行验收。 |
| [windows_files.SHA256SUMS](windows_files.SHA256SUMS)、[rk_files.SHA256SUMS](rk_files.SHA256SUMS) | 分别列Windows原件和本次RK副本的内容指纹，相对路径一致；用户清单给出再次只读核对的命令。 |
| [SHA256SUMS](SHA256SUMS) | 本证据目录其他文件的内容指纹，用于检查归档本身是否改变。 |

“SHA-256”指文件内容指纹。以后正常保存配置会使指纹变化，应先核对业务修改是否正确，不能为了通过旧指纹而覆盖新资料。检测算法已由用户明确放到后续，本次保留参数，不把算法未实现算作39份文件缺失。
