# 临时检查器：最新正常关闭与失败判定复核

## 结论与范围

最新候选已补齐上一版的 15 秒关闭超时判定空隙，未发现本次新增的确定性阻断。超时会保留原失败码，原成功码则改成 4；运行封装和汇总又分别拒绝关闭未确认标记。即使正式告警稍后才排空并使应用正常退出，也不会把已经发生的内部关闭超时算成软件通过。

只读源码、脚本、清单及现有本地文件校验值，没有构建、运行产品或 SSH，没有修改冻结输入、候选或旧审查。上一份 `window_close_review.md` 保留，绑定的是旧 `d8fa53a3…` 版本；其中未修问题不再描述当前候选。

所有仓库路径均从 `C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/` 起算。本目录为 `temp/sn100_host/board_b/20261102_status_login/integration_05_review_preparation/`。

## 本次固定版本

| 文件 | SHA-256 |
| --- | --- |
| `status_login_review_05.cpp` | `73d54a7802463d41c820dfbfd914612e2e78070b9f237ced60a2ec7bf8c8f11d` |
| `prepared_inputs.json` | `a5c6eb3fd80537a8c7e246b26540b661f05326ec23d27b9d57554b91e166b2ae` |
| `run_case_on_board.sh` | `4e502617172cdc6b749fbb4f2945343b1e6646aed20d9ef3d886620c7ec9498e` |
| `summarize_run.py` | `fb7b58c25452d55da9228a2b3a65470a354e34e32cb18ba1c271bc94c9264ea2` |

准备清单中的 `prepared_checker_sha256` 与上表最新候选一致。

## 关闭超时不会再被正常退出覆盖

候选 `status_login_review_05.cpp:58–67` 只允许进入一次收尾，并把最初检查码另存为 `connectedReviewInitialCheckExit`。

`:74–85` 的 15 秒超时分支：

- 打印 `REVIEW_WINDOW_CLOSE_NOT_CONFIRMED`；
- 原检查码非 0 时保留原码，原码为 0 时改为 4；
- 将失败码更新到独立 `check_exit.txt`，然后停止并延迟删除计时器；
- 没有直接退出应用、停止接收器、强删窗口或替用户确认草稿。

`run_case_on_board.sh:138–143` 在自动检查且应用封装退出 0 时，同时要求独立检查码为 0、日志中没有关闭未确认标记，否则本轮退出 1。原封装非 0 仍原样保持失败。

`summarize_run.py:105–106,123–125` 独立拒绝 `ORIGINAL_ALARM_SHUTDOWN_UNCONFIRMED` 和 `REVIEW_WINDOW_CLOSE_NOT_CONFIRMED`，并继续要求实际应用、封装及独立检查码都为 0、检查 `completed=true` 且 `failed=0`。所以即使更新检查码的文件写入失败，已保存日志中的超时标记也不能被汇总成通过。

## 正式告警排空和对象寿命没有被绕过

候选 `:87–107` 仍每次重新寻找真实主窗，等待登录忙态、原关闭动画及活动模态窗口，最终调用真实外层 `window()->close()`。正式告警排空使业务主窗禁用时，`:105–106` 继续等待原回调，不强制重新启用。

第 05 轮冻结源码中的原链保持：`ShellViewport::closeEvent()` → `frmMain::closeEvent()` → `requestOriginalAlarmShutdown()` → 原接收器排空并发 `stopped` → 原 `stopped` 回调再次关闭窗口。具体依据仍为冻结 `shell/main.cpp:221–229`、`shell/frmmain.cpp:623–667`、`shell/alarm_shell.cpp:132–152`、`alarm_history/original_alarm_receiver.cpp:308–340`。

最新候选没有 `qApp->exit()`、直接应用退出、直接接收器停止或终止线程调用。新改动只增加初始检查码和超时文件结果，不改变计时器父子关系或回调捕获：计时器由 `qApp` 持有，lambda 按值捕获计时器和共享时钟，没有持有已返回 `review()` 的栈对象。未发现因本修订引入的确定性悬空引用或二次删除。

## 收尾标记与版本交叉核对

`summarize_run.py:100–104` 现在要求 15 个必要标记，包含 `SHELL_READY`、11 个业务存储关闭标记、两个告警工作线程停止标记和总体线程停止标记。它们与自动运行参数启用的医院、账户、历史、项目、试剂、样本、维护、打印、LIS、质控、校准和两类告警相符。

冻结 `shell/main.cpp:826,896,1002,1029,1097,1158–1159,1256,1280,1336,1385` 提供相应业务关闭日志；`:1473,1478,1489–1492` 提供两个告警工作线程、总体线程以及真实退出码。汇总要求这些必要输出，并继续拒绝告警未确认，不拿检查器自己的成功码代替正式回收。

本次还读取了已生成的本地绑定记录：

- `link_provenance.json` SHA-256 为 `63d004aa6a2159ad13b38c7f6d80e2207eb3b97dd408e26bc516a5333000d3fd`；其中检查器源码为最新 `73d54a78…8f11d`，不是旧 `d8fa53a3…`。
- 记录的发布编号为 `e31491a11e2e51764ab49c2c4435ce2f6f1f136223a76323d508ed613a758270`，源码快照为 `e785702f907aecee9bc645614f122c05f1c6053d33ecb1b94f7e08ee8eaaf111`，主程序对象 261 个，保留正式 main。
- 当前检查器实际文件 SHA-256 为 `8b48f0cf698cfaf8f1aeee40323e32ccc9201e2650072b217a25c34e1b1f1f05`，与 `review_binary.sha256` 一致；`review_release_id` 也与上述发布编号一致。

这些现有记录没有指向旧候选，已提供核对构建交叉时序的依据。本子任务没有独立重编、运行该二进制或核板端部署；是否接受这次产物仍应结合协调方实际构建日志及冻结期间的操作记录。RK 收尾、告警排空、控件绘制及人工屏幕/密码操作尚需分别取证，不能由本次只读审查替代。
