# 板 B 原主控：本地历史记录只读核对

## 结论与本次范围

已有证据足够确认应恢复运行的是 `/userdata/shared/ACA_V1.0OffLineD-500`，SHA-256 为 `ee8e1932f9a9f33d0bbffebfc9e161515dd79a53f50c78da274b04ef837d81ff`。原来从 `/userdata/shared` 目录以 root 启动，没有业务命令行参数，只明确设置 `LD_LIBRARY_PATH=/userdata/shared`，并用 `stdbuf` 和 `tee` 实时显示及保存输出。

旧主控最后日志与一个 SSH 会话关闭发生在同一秒，原启动方式又直接依附前台 SSH 终端，因此终端连接结束是应优先核查的线索。但本地旧进程快照未保存父进程号、会话号或终端号，尚不能证明该 SSH 会话就是主控的父级，也不能据此确定退出信号或认定人为关闭。用户已明确没有手动关闭主控；日志中的 `disconnected by user` 原文不能单独当作用户手动操作的证明。

本次只读取本地启动脚本、历史日志、说明及父任务新取回的 `before_restore.log`，没有 SSH，没有启动或停止程序，没有读账号、密码或认证文件，没有修改正式源码、原脚本或旧证据。只新增本记录；恢复执行及恢复后状态由父任务另行核对。

本记录所有仓库路径均从 `C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/` 起算。

## 已确认的原启动方式

原文件身份与替换结果：

- `temp/sn100_host/board_b/20261102_aca_replace/correct_replace_ssh.log:1–5` 记录 17:38:02 已安装正确 SHA，大小 10,131,952 字节、root:root、权限 755，误发活动程序已移除，配套文件替换前后一致。
- `04_references/sn100_host/board_b/20261102_aca_test/correct_05/README.md:5–9` 记录板 B 序列号 `8314b1e8ffd0321f`、SSH 指纹 `SHA256:S4ihaw0V/Z8w7v3VlJOQEFLEH3Hp6nr3KjiUTxanwPg`、17:38:07 实际启动、PID 39408。程序把标题自行改成 `SN450_CPU3`，不能仅用程序标题判断机型或进程是否存在。
- `04_references/sn100_host/board_b/20261102_aca_test/correct_05/running_snapshot.log:1–8` 在 18:04:21 仍记录正确可执行路径、PID 39408、6 个线程、5000 端口监听及同一正确 SHA。

原启动脚本为 `temp/sn100_host/board_b/20261102_aca_replace/run_correct_live.sh`，正式证据目录中另保存同名原件：

| 项目 | 原脚本依据 |
| --- | --- |
| 板卡保护 | 第 4 行先核序列号 `8314b1e8ffd0321f` |
| 工作目录 | 第 5 行 `cd /userdata/shared` |
| 正确文件保护 | 第 6 行核完整正确 SHA |
| 防止重复主控 | 第 7–12 行逐个读取 `/proc/*/exe`，匹配正确 OffLine 文件或此前误发 D 文件，有进程就退出 |
| 历史记录根 | 第 13 行 `/home/rkshare/programs/aca_flow_test/runs/correct_05` |
| 每次记录目录 | 第 14–18 行创建 `live_年月日_时分秒`，更新 `latest_live_path.txt` 并写 `started_at.txt` |
| 运行命令 | 第 21 行 `LD_LIBRARY_PATH=/userdata/shared stdbuf -oL -eL ./ACA_V1.0OffLineD-500 2>&1 \| tee "$live/program.log"`；可执行文件后没有参数 |
| 结束处理 | 第 22–25 行取主控对应管道退出码，再写 `result.txt` 和 `ended_at.txt` |

`temp/sn100_host/board_b/20261102_aca_replace/show_correct_live_log.ps1:5` 通过 `ssh.exe -tt ... root@192.168.3.59 'bash /home/rkshare/programs/aca_flow_test/run_correct_live.sh'` 运行它。这里强制分配终端；原脚本第 20 行只处理 INT 和 TERM，没有 HUP 处理、`nohup`、`setsid` 或其他脱离终端的安排。脚本未声明的其他继承环境，在现有记录中不能补造为已知事实。

## 退出前后能确认什么

本次最新本地输入是 `temp/sn100_host/board_b/20261102_controller_restore/before_restore.log`。这是父任务只读取回的板端状态，本子任务没有连接板卡。

| 时间/现象 | 具体证据 | 可确认范围 |
| --- | --- | --- |
| 17:38:07 启动 | 原 `correct_05/live_20261102_173807/program.log:4` 报告编译时间 2026-11-02 17:35:14；`:256–257` 创建监听线程并进入主循环 | 正确版本实际进入运行；不能据此认定完整仪器流程通过 |
| 19:12:56 仍运行 | `04_references/sn100_host/board_b/20261102_sn500_connection/final_status.log:1–6` 记录主控 PID 39408 仍在；同文件 `:7–11` 的 APP_EXIT=130/SIGINT 是上位机退出信息 | 不能把上位机退出码当作主控退出码 |
| 19:52:44 上位机连接断开 | `before_restore.log:65–69` 记录 `recv: Connection reset by peer` 和 tcp client 7 disconnected；`:72–101` 此后主控继续打印 | 客户端断开没有使主控立即停止，至少还继续输出至 19:53:12 |
| 19:53:12 主控最后输出 | `before_restore.log:44` 文件大小 1,918,618 字节，修改时间 19:53:12；`:101` 最后打印监听线程运行，前面仍有主循环/AD/CAN输出 | 这是最后已见输出时间，不能仅凭最后一行推定具体退出信号 |
| 同秒 SSH 会话关闭 | `before_restore.log:140–142` 记录 sshd[38601] 与 `192.168.3.37:61413` 断开并关闭会话；`:137–139` 同秒还有另一个 SSH 会话关闭 | 时刻相关，但未取得主控与该 sshd 的父子链 |
| 未见本轮正常退出记录 | `before_restore.log:43–45` 的旧运行目录只列 `program.log` 和 `started_at.txt`；原脚本正常走完本应写 result/ended | 不能报主控退出 0；可能是启动封装未走完，也可能涉及其他中断，现有记录不能唯一归因 |
| 原可执行仍正确 | `before_restore.log:39–41` SHA、大小、root:root、755 均与正确文件相符；`:6–9` 未见主控进程/5000监听 | 当前问题并非已有证据显示文件被换成错误版本；若仅恢复运行，无依据重写可执行文件 |
| 旧 core 文件 | `before_restore.log:103–109` 列出的 core 文件修改时间均为 17:11:51 | 这些不是 19:53 新生成崩溃记录，不能拿来证明本次主控崩溃 |
| 内核线索未命中 | `temp/sn100_host/workflow_night_20261102/board_b_controller_exit_readonly.log:10–14` 在指定 19:45–20:10 日志窗口匹配 0 行；最新 `before_restore.log:111–112` 相应区段为空 | 只说明已查窗口未找到内存不足、内核终止或段错误线索；不能排除未保存日志或其他退出原因 |

原正式证据中的 `04_references/sn100_host/board_b/20261102_aca_test/correct_05/live_20261102_173807/program.log` 是早期固定快照，末段仅到 18:04:20（`:2251`），并非持续同步到 19:53 的日志。`correct_05/README.md:9` 也明确它是复制时的快照。本次 19:53 结论来自新取回的 `before_restore.log`，不反写旧快照。

## 终端断开关联：仍缺哪一环

已有三个相互吻合的线索：原程序通过 `ssh -tt` 前台启动；原启动脚本没有把主控独立于终端管理；19:53:12 主控日志停止的同秒，sshd[38601] 记录会话关闭。因而“前台终端/连接结束后使主控或 tee 管道结束”是合理的排查方向。

但已读历史进程记录只包含 PID、运行时间、CPU、内存、线程及命令：`correct_05/running_snapshot.log:3–6`，以及 `04_references/sn100_host/board_b/20261102_sn500_connection/rk/reopened/resources_snapshot.txt:2–14`；没有 PPID（父进程号）、SID（会话号）、PGID（进程组号）或 TTY（终端号）。没有本地证据把 39408 → 原 bash/tee → sshd38601 连成实际进程链，也没有本次结束信号/主控退出码。因此这里只保留关联与假设，不认定由用户关闭窗口、HUP、管道中断或主控自身故障中的某一种造成。

## 恢复前后应保留的范围

- 保留 `/userdata/shared` 当前正确程序及全部配套资料，包括当前配置、流程目录、数据库、校准文件和已有 core；没有证据支持为本次恢复覆盖这些文件。本子任务未读取其中账号或认证内容。
- 保留旧 `correct_05/live_20261102_173807/` 全部原记录及 `latest_live_path.txt` 的原值。新运行应另建记录目录；若启动器更新最新路径，应先把原路径和本次变更记清，避免新日志覆盖旧事故现场。
- 保留本地旧程序恢复包和校验清单原样。`07_backups/sn100_host/board_b/aca_flow_test_20261102/correct_program_1735/README.md:5–9` 明确正确原程序 SHA、单程序包范围及只替换活动可执行文件的恢复边界；当前文件 SHA 仍正确，优先恢复运行而非重复替换文件。
- 保留 `before_restore.log`、此前两份只读检查及所有原始上位机检查失败记录，分别记录恢复前后，不能把重启后的稳定状态写成旧进程未退出。
- 恢复后需核对实际 `/proc/<PID>/exe`、工作目录、监听端口及持续输出，并保存新的父进程/会话/终端信息。主控会改标题为 `SN450_CPU3`，后续检测不能仅靠进程名。此项由父任务执行，本记录没有实施。

这份本地核对已经完成；退出原因目前仍待补证，不能因恢复成功就追认某一种退出原因。
