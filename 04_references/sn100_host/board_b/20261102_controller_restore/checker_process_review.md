# 检查器与旧启动脚本的进程影响审查

2026-11-02 23:19，北京时间。板 B 序列号 `8314b1e8ffd0321f`。本次只读本地脚本、固定日志和主 Agent 新收集到本目录的日志；未 SSH、未启动或停止程序、未改正式源码和旧证据，只新增本文件。板端恢复由主 Agent 独占操作。

## 当前能确定什么

未找到两轮 status_login 检查脚本或旧上位机控制脚本直接按主控名称、主控 PID、负进程组编号、`kill 0`、`pkill`、`killall` 清理正确主控的明确路径。首轮检查确实被 90 秒限时封装发送 SIGINT，最终封装退出 124；第二轮是检查器自己等待失败并退出 1。

更具体的现有证据是：正确主控的原 SSH 日志会话在 **19:53:12** 断开，主控日志也在同秒停止；原主控是经 SSH 前台 `ACA | tee` 启动，没有脱离该会话。该关联支持优先核查会话关闭的影响，但仍没有主控实际退出码、终止信号或内核审计记录，不能据此宣称已证实退出原因。

`sshd` 的 `disconnected by user` 是协议断开日志文字，只能证明对端发来了断开请求，不能据此认定使用者亲自点击关闭。用户已经明确表示没有手动关闭主控。本审查不作相反归因。

## 两轮检查的命令和退出事实

下列路径以仓库根为起点。

| 对象 | 确定事实及源码/日志位置 |
| --- | --- |
| 检查封装 | `temp/sn100_host/board_b/20261102_status_login/run_review.sh:26–38` 以 `runuser -u lckfb` 运行检查器，唯一超时命令是 :32 的 `timeout --signal=INT --kill-after=5s 90s "$review/status_login_connected_rk_review"`。:40–43 保存实际返回码。没有指定另一个主控 PID 或外部进程组，也没有 shell 清理 trap。不能把 `--kill-after` 的存在直接当成它实际发送了 SIGKILL。 |
| 首轮 | `rk/attempt_01/identity.txt:1–2`：发布 `7c4abe3062ead1f5b6fc6fd495b708be5f11436504bba5e900b6ccb2c4f5d5f9`；检查器 SHA-256 为 `0c1b32e6d912fd6e09483b0213520f74b277aa4da3f97762c26fbf37b84e39fb`。`review_01.log:307–308` 为 `SHUTDOWN_REQUEST signal= SIGINT` / `SHUTDOWN_ACCEPTED`；:323 应用自报退出 0；:324 记录封装 `ACTUAL_EXIT=124`。`rk/attempt_01/review_exit.txt:1` 同为 124。不能将应用退出 0 替代封装失败事实。 |
| 第二轮 | 同目录 `rk/attempt_02/identity.txt:1–2` 发布不变，检查器摘要改为 `ab88d5703b498851e8373b511fbbfe8bb884eaf87665387c3de02d24fa2e9df0`。`review_02.log:25` 为 `CONNECTED_REVIEW_STARTUP_NOT_READY`，:40–41 为应用及实际退出 1；`review_exit.txt:1` 同为 1。该轮没有首轮的 SIGINT 退出记录。冻结检查源码 `frozen/sn100_host/tests/status_login_connected_rk_review.cpp:288–295` 明确在 60 秒条件未满足时 `qApp->exit(1)`。 |
| 工具内部清理 | 同一冻结源码 :94–101 的失败回调调用 `DeviceControl::close()` 和应用退出；所查文件没有 fork、系统 kill、QProcess 终止外部主控的调用。该处 `close()` 是上位机连接对象的关闭调用，不能按名称理解为终止 Linux 主控进程。检查器已多次重建，两轮摘要不同，因此没有用当前源码替代首轮二进制的确切实现证据。 |
| 安装脚本 | `status_login/deploy.sh:14–18` 只锁生命周期并拒绝正在运行的上位机/本检查器；:28–30 source 发布内脚本后只调用 `verify_release` / `preflight`，没有调用 stop/supervise。该脚本自身没有终止主控命令。安装日志 `deploy.log:5` 为 `19:51:04+08:00`。 |

两轮目录前缀均为 `temp/sn100_host/board_b/20261102_status_login/`。已有日志没有保存检查器和主控当时的 PGID、SID 及父子树，因此不作“已经按当时进程组证明完全隔离”的过度结论；所读命令中没有跨组指定主控的路径。

## 旧上位机控制脚本的 kill 范围

以 status_login 发布冻结副本 `temp/sn100_host/board_b/20261102_status_login/frozen/sn100_host/shell/rk_board.sh` 为依据：

- :71–82 的 `process_matches()` 先校验数字正 PID、当前 boot_id、启动 ticks、UID 和可执行文件路径。
- :159–163 的 `child_is_owned()` 校验本次子进程启动 ticks 和 `release/sn100_shell` 路径；:188 的异常清理仅向该正 PID 发 TERM。
- :260–267 启动 `sn100_shell` 后以 `$!` 保存子进程 PID、路径、启动 ticks；不会把已运行的 ACA 进程作为 child。
- :293、:300 的监督关闭/限时关闭经过上述身份检查，只对 `$child` 正 PID 发 TERM；:323–327 的 stop 和 :395 的启动失败收尾同样经过 `process_matches()`。
- :101 起的 `preflight()` 是文件、动态库、显示和权限检查，不进入这些停止分支。

入口 `06_programs/sn100_host/rk_control.sh` 只将 start/stop/status 转给当前发布脚本，start 使用时长 0；没有额外按主控名清理进程的命令。

另外核到的旧命令不构成本次正确主控的明确结束路径：

- `04_references/sn100_host/board_b/20261102_deployment/board_smoke.sh:36–39、57–60` 只启动并 TERM/等待自己的上位机 `$!`。
- 旧 ACA `recheck_full_package_02/recheck_full_package.sh:38–43` 和 `fixed_path_03/deploy_fixed_path_03.sh:49–55` 确有 10 秒 timeout 包围当时启动的 ACA 子进程，但已分别保存退出 255，发生在正确主控 17:38 启动之前。前者 :17–23 的 cleanup 只解除当轮创建且指向本轮目录的符号链接，不发进程信号。
- `temp/sn100_host/board_b/20261102_sn500_connection/stop_superseded_build.py:6–29` 按本地 `/proc` 中明确的 `build_connection.sh` 命令找根编译进程，再对其后代正 PID 发 TERM；文件无 SSH 调用，不是板端主控清理脚本。

## 正确主控与 SSH 会话的现有时间线

1. `04_references/sn100_host/board_b/20261102_aca_test/correct_05/run_correct_live.sh:18–25` 在启动时写 started_at，以 `LD_LIBRARY_PATH=... stdbuf ... ./ACA_V1.0OffLineD-500 2>&1 | tee ...` 前台运行；正常返回后才写 `PROGRAM_EXIT` 和 ended_at。脚本无 nohup/setsid；:20 仅对启动 shell 设置 INT/TERM trap，没有 HUP trap。配套 `show_correct_live_log.ps1:5` 用 `ssh.exe -tt` 直接运行它，:6–8 只把 SSH 返回码打印到日志窗口，没有在现有归档中形成实际退出码文件。
2. 固定 `correct_05/running_snapshot.log:1–6` 在 **18:04:21** 核到 `/userdata/shared/ACA_V1.0OffLineD-500` 为 PID **39408**，有 6 个线程、5000 监听。原启动时间 **17:38:07** 见本目录 `before_restore.log:45`。主控自设的 `SN450_CPU3` 标题不能当作机型依据。
3. 本目录 `before_restore.log:133–136` 记录 **19:52:44** 的 `runuser[73677]` 和检查 SSH 会话关闭；:64–69 同秒主控 TCP 客户端 7 断开。该主控并未在这次断线时立即停止：后面继续输出主线程 PID39408和 CAN、AD、测试、监听线程日志，直到 :101 的 **19:53:12.162911** 仍有监听线程输出。也就是检查会话结束后仍有约 28 秒的主控输出。
4. 本目录 `preflight.log:26–30` 将原主控日志 SSH 会话明确关联为 **sshd[38601]**：**17:38:05** 来自 `192.168.3.37:61413` 的 root 会话打开；**19:53:12** 收到 disconnect、随后 session closed。`before_restore.log:140–142` 同样保留此记录。
5. `before_restore.log:44–45` 的旧运行目录只列 program.log 与 started_at；program.log 最后修改时间为 **19:53:12.1584932000**，没有 result.txt / ended_at。该采集的 :2–9 显示 **23:17:09** 时没有主控进程和5000监听。这些是恢复前事实，不是恢复后的状态。

现有记录足以建立“主控原日志 SSH 会话关闭与日志停止同秒”的关联；尚不足以区分 HUP、管道失效、程序自行退出或其他原因。未找到本次退出的有效崩溃转储或主控终态文件；不能把目录里 17:11 的历史 core 文件视为19:53事件。也不能因为没有退出文件就直接判定是某种信号。

## 后续核查建议

由独占板端操作的主 Agent 保留原始会话日志和时间线，继续核对本电脑原 SSH/日志窗口为何在19:53:12发送断开；恢复运行时使用能独立保存 PID、实际退出码和结束时间的长期运行方式，日志查看与主控进程寿命分开。本审查不自行执行这些操作。

本次读取的关键脚本 SHA-256：

```text
status_login/run_review.sh             11ea94897c34cfdc5fb57e300e9389158c3948120f9074c20b76b0616fe1bcee
status_login/deploy.sh                 dd799b1f1bc050e3904979dd054ff8aee9b79094c41cc1e776a1b03750b80858
status_login/frozen/.../rk_board.sh    e3587bad320003bf1a2ea7b3176bf14d23527ebaae65b60ffb4bd5388d3866d2
correct_05/run_correct_live.sh         d389fccbec46a7b112907a4717b845301752b227b71331877bb35c8ec944482d
```
