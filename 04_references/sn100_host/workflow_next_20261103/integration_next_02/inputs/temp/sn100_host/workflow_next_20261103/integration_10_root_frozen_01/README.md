# 下一批构建与归档工具：独立未冻结修订03

先读[操作合同](operation_contract.md)及[总控裁定对齐](root_decision_alignment.md)。本目录保留修订01/02及原审不动，仅修总控窄审指出的切换复核、主控排重及批次标记。逻辑批次/预留输出仍为`integration_next_02`，不是已执行的修订02；Root最后只选一个已审副本冻结执行。当前cfg为`frozen=false`，所有动作许可为false，最终源表、对象、包及阶段结果仍未知。

最初模板的只读底稿是09冻结05九份工具、正式`rk_board.sh`及09实际切换脚本。`basis_inventory.json`中的684份等09仅证明当时起始前像，不是现在仍等旧表。总控随后实际采纳ASCII与CAL两片；本次通知的最新685份组合终态为`integration_10_source_acceptance_01/cal/line_ending_correction_01/source_after.json`，SHA`7cad7a7e401f619276cfea032dcf3337c76a4522a1f52ba594a845b381b1ea7e`。CAL首轮外层1和两文件原字节、独立字节修正入口0都须保留；`cal/source_after.json`是首轮失败表，不能当终态。685仍只是当前源码链，不是最终构建冻结。

## 文件分别做什么

| 文件 | 用途 |
| --- | --- |
| `integration_next_tool_support.py` | 冻结值、真实合入终态、全源码、构建/对象绑定、板端暂存原文和完整清单核对 |
| `build_integration_next.sh` | 新WORK全量ARM64编译、静态依赖和打包；清除对象复用环境，不运行产品或连接板卡 |
| `verify_integration_next_inputs.py` | 编译前后核实际输入，真实构建后保存每对象绑定与完成记录 |
| `integration_next_object_binding.py` | 按冻结规则和实际文件重建源→生成物→对象→链接对应，不冒称编译器原始`.d` |
| `stage_board_b_release_next.py/.sh` | 固定板B身份、完整包核对、独立存储锁下保存新不可变目录，记录原有运行身份前后值，不启停或切版 |
| `archive_integration_next.py` | 核真实新构建与暂存后，新建程序包、源码包、绑定副本及来源记录 |
| `verify_and_save_integration_next.py` | 收集本批明确输入与实际日志，核对象SHA及三处绑定原文 |
| `finish_integration_next_archive.py` | 重核完整集合、LFS及对象绑定，再写文件阶段完成记录 |
| `transition_release_next.sh.in` | 总控另行审查和冻结的切换草稿。原件首先退出78；无停止、启动、TERM或超时关闭命令，只交接总控唯一启动 |
| `runtime_launch_intent.template.json` | 新包确定后由Root单独冻结的启动意图，保存运行时3参数及已有包/程序身份的引用；不是GUI结果，也不回写文件阶段配置 |

九份文件工具从05派生；切换草稿是额外的默认拒绝模板，不被构建或归档工具自动调用。它的`prepare-flat`仅准备新平层副本，`activate-only`还需总控真实许可、旧停止凭据、包后启动意图、无GUI及释放显示的现场。它只切指针，返回`LIVE_HANDOFF_PENDING=1`和`START_OWNER=root`，不伪造应用已启动或留屏通过。

修订03把同组无GUI/报告/检查进程、服务和VT/DRM释放检查提成共同函数，在运行锁前、取锁后以及实际原子切指针前各核一次，未知或有持有者只拒绝。主控按实际exe精确basename及deleted排重，再核唯一精确路径、SHA、逐行5000归属和前后boot/PID/start_ticks/有效UID/exe/SHA/监听；活用户进程exe不可访问也拒绝。只有明确已退出、稳定僵尸/死亡或内核线程可跳过，不靠进程名猜，不停进程或重启服务。

## 新范围与冻结要求

本地新WORK为`temp/sn100_host/workflow_next_20261103/integration_next_02/`；证据为`04_references/sn100_host/workflow_next_20261103/integration_next_02/`；恢复输入为`07_backups/sn100_host/board_b/workflow_integration_20261103/integration_next_02/`。远端暂存为`/home/rkshare/programs/sn100_shell/releases/integration_next_02/<真实新release_id>/`。这些是新模板的写入边界，目前没有创建这些阶段输出。

总控在冻结前提供最终审查、实际合入终态、匹配起始源码包/表、完整最终源码表、实际合入脚本及其依赖/原始日志、明确历史失败原件、九份代码SHA、现场`expected_current`和`expected_controller_exe`。格式及值关系沿用09的最低契约：动态补丁从1连续编号，起止表和完整终态一致，历史清单显式可空，不固定旧批次数量。配置自身不放进自己的tools清单。

v2文件配置只有`allow_full_build/allow_stage`两项许可；晚授的准备平层、切换、旧停止和唯一新启动许可另在包后Root意图，不修改原cfg。观察合同采用Root裁定v1，裁定record必须进入冻结证据；真实源表、对象与发布仍由Root填写。Root从切换草稿派生新的实际脚本，独立固定新程序/报告/helper/主控、cfg/构建/包SHA、环境清单、批准记录、旧停止凭据和启动意图。保留原`.in`不动，不能只改`template_frozen`跳过空值。包内helper只有在批准记录、清单、意图语义和helper实物SHA核对后才会加载。

## 保存与留屏的证据口径

新保存工具使用`immutable_store.lock`，兼容旧GUI一直持有`lifecycle.lock`的现状。上传和解包提交各自取存储锁，两个SSH之间重新核current；只保存阶段不申请运行锁，也不修改旧程序目录或资料。存储锁不约束未采用本合同的旧写者，所以总控仍须串行管理其他部署操作。

`live_guard_before.txt/after.txt`来自板端持存储锁期间的实际只读身份，包含启动身份、唯一应用/主控的PID与启动时刻、exe/SHA、实际监听和三指针摘要；原文由板端生成，PC只保存。范围明确是`locked_unpack_and_commit`，不把它扩大成整轮界面/业务观察。两份原文、板端base64与SHA、实际SSH退出以及完成记录必须一致。

只有Root负责编排实际启动。状态窗口负责同进程被动CPP、只读等待和解析，本目录不写`run_case`或启动器。内部观察没有独立进程退出码；等待/解析分别记录`waiter_actual_exit`和`parser_actual_exit`。封口时`application_exit=null`及三项退出/关闭评价为false。READY或业务观察超时不自动发TERM、关闭窗口、停止服务或起第二主程序。切后的正式界面仍运行由真实前后身份采集判断，文件阶段完成不代替该结论。

发布号在核包后经`SN100_LIS_OBSERVATION_RELEASE`运行时传入，不编译进参与自身SHA清单的binary。3个实际观察参数写入独立`runtime_launch_intent`，不修改已进入构建终态SHA的文件阶段cfg。GUI不算大exe/包SHA；外部传来的预期SHA也不标成CPP独立实测值。`supervise 0`可避开旧start的30秒READY超时分支，但保留监督异常和信号关闭路径，详见对齐说明；本轮不把它直接实现为新的启动脚本。

## 本次交付和未验范围

交付包括底稿与候选完整指纹、仅作用于独立工具副本的审查补丁、静态检查的真实退出和独立源审。静态检查只解析文本和普通核补丁，不导入或调用阶段函数，不构建/联板，不运行Windows/WSL产品或脱机模拟。

保留旧09 true配置、原件、恢复包、证据及候选；不复用或重跑已清对象的阶段。当前正式程序和主控仍由总控管理，本窗口没有读取板端、启停或操作显示。下一批编译、暂存、切换、留屏、实际业务、真实仪器和资源均待对应实际验证；没有新增算法、Windows运行或RK通过结论。
