# 第11批构建与存包工具候选

这套工具只负责把总窗口已接纳的源码交叉编译成ARM64程序，核对真实输入、对象文件和程序包，在板B另存新版本，并保存恢复包和证据。当前 `frozen=false`，完整构建和板端存包的两个许可均为 `false`。本窗口没有运行任何阶段。

本候选由已审查的 `build_archive_candidate_03` 九份文件工具派生，所有新输出统一使用 `integration_11_01`。旧03、总窗口冻结的第10批工具、已有C7包及记录保持原样。本目录没有切换版本、停止程序、启动程序的模板或启动器。

| 文件 | 用途 |
| --- | --- |
| `integration_11_01_tool_support.py` | 核对冻结输入、晋级台账、真实构建和板端存包记录 |
| `build_integration_11_01.sh` | 使用全新对象目录交叉编译、检查静态依赖和打包 |
| `verify_integration_11_01_inputs.py` | 核对构建前后的完整源码和实际输入 |
| `integration_11_01_object_binding.py` | 保存并复核每个对象的直接来源和生成文件指纹 |
| `stage_board_b_release_11_01.py`、`.sh` | 核包并在板B另存不可变版本，核对原程序、主控、七份配置和三份资料路径文件 |
| `archive_integration_11_01.py` | 回读程序包和源码包，保存恢复所需文件及来源说明 |
| `verify_and_save_integration_11_01.py` | 复核各阶段真实记录，再创建固定证据目录 |
| `finish_integration_11_01_archive.py` | 核对恢复包、证据和原始记录的完整对应关系 |

新工作目录为 `temp/sn100_host/workflow_next_20261103/integration_11_01/`，固定证据为 `04_references/sn100_host/workflow_next_20261103/integration_11_01/`，恢复包为 `07_backups/sn100_host/board_b/workflow_integration_20261103/integration_11_01/`。板端只允许新建 `/home/rkshare/programs/sn100_shell/releases/integration_11_01/<实际发布编号>/`，发布编号必须等真实打包产生。

## 已核到的基线

当前板端保护值固定为 `releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e`。该串是C7包内清单的指纹，不能充当压缩包自身的指纹。

| C7实物 | SHA-256 |
| --- | --- |
| 程序恢复包 | `0cfa398b008df64ebc92aae17e9837633b675adccee73c65083db08b255187ee` |
| 687份源码恢复包 | `56fbff3871eec9776205487ef59d1c9826b6605ec4f8f884bc90b6ceede6b4bf` |
| 归档 `source_snapshot.json` 原文 | `fd50fedc204888ba6277029715caa80054bd3c760d2c80e1e9c9b93762abb4fc` |
| 第10批最终源码表原文 | `f741feea28fcb6a2bcff8d6c2bda49faa67d3b72e3508062e5f1e368b2422b8b` |

上述两张源码表的687项路径和指纹相等，文件序列化不同，因此原文SHA不同。配置暂选归档的 `source_snapshot.json` 作基线。总窗口真实晋级台账中的 `source_snapshot_sha256` 必须绑定同一文件；若总窗口选择另一原文，须在冻结前明确同步记录，不能只因两表内容相等就混用SHA。

总窗口已提供第11批两片实际接纳的最终688项源码表，位于 `integration_11_source_acceptance_02/lis_profile_source_retry_01/source_after.json`，SHA为 `b0788f899b63a982c55ae85d098d7f4e843bdbedbd19b9f7c1ca0d5a3c569767`。本候选尚未收到总窗口完整晋级台账和最终冻结元数据，因此保留相关字段为空。对象数量、构建退出码、新程序包及发布编号均须以后续真实产物填写。

接纳过程的失败原件必须一并保留：第一次因Windows路径分隔符的比较退出1，尚未应用补丁；第二次试行已应用第一片，但第二片首次使用错误相对目录，检查退出1且未应用；随后只在正确目录续做第二片。第一片外层包装脚本因后续失败退出1，不能把整个包装脚本写成0。这些是接纳工具结果，不是业务验证或编译结果。

## 运行和配置保护

板B序列号、专用SSH主机指纹和原主控执行文件沿用已核身份。板端存包只使用 `immutable_store.lock` 存储锁；总窗口仍须串行安排。现场的主程序、原主控、进程启动身份和5000监听归属由当次板端读取，不采用历史PID。

旧03的22项运行快照包含三份资料路径文件的路径与文本指纹，并未核对七份实际配置正文。新候选额外绑定C7固定证据中的七行配置指纹清单（六份INI和一份CFG），只读取并保存路径和指纹，不读取或归档配置内容。保存前后都必须与冻结清单逐字节相等，原主程序和原主控也必须与冻结程序指纹相等。完整保护范围和各阶段要求见 `operation_contract.md`。

当前只准备源码、静态检查和供总窗口冻结的材料。静态语法检查和独立文本副本补丁检查均不代表编译、板端运行或真实仪器通过。板端存包也不等于启用新版本或功能通过。Windows原版核对、RK及真实仪器功能结果由各功能窗口和总窗口按实际条件分别登记。
