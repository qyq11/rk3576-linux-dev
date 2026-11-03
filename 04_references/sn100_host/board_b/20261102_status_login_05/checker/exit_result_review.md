# 真实窗口关闭与结果判定复核

2026-11-02 23:51，北京时间。本次只读复核本目录临时检查源码、runner、wait helper、汇总器及正式关闭链，只新增本笔记。未构建、运行脚本或产品、未SSH或操作板端，也未修改冻结输入、正式源码和旧证据。

旧 `script_review.md` 及 `before_window_close_fix/script_review.md` 的旧版本说明保留。本文件以关闭修正最终冻结版为准，不把旧审查哈希替换成新版本。

## 结论

最终版本将三件事分开保存：检查是否通过、应用是否真正退出、外层是否正常结束。在所读路径中，未再发现“检查失败或关闭等待超时，却因应用后来正常退出0而汇总为通过”的确定路径。

本轮曾发现15秒关闭等待超时只写日志、未改变独立检查结果的误报顺序，已立即反馈并由协调方修正；本笔记已按最终修订重新核对，不列为未修问题。

`case_exit=0` 只是阶段检查结果，不能单独当总验收通过。最终须同时看汇总器返回码、`software_checks_passed` 和人工记录；源码静态审查不构成关闭、告警排空或重开已实测通过。

## 冻结输入

所有路径均位于 `temp/sn100_host/board_b/20261102_status_login/integration_05_review_preparation/`。

```text
status_login_review_05.cpp    73d54a7802463d41c820dfbfd914612e2e78070b9f237ced60a2ec7bf8c8f11d
run_case_on_board.sh          4e502617172cdc6b749fbb4f2945343b1e6646aed20d9ef3d886620c7ec9498e
summarize_run.py              fb7b58c25452d55da9228a2b3a65470a354e34e32cb18ba1c271bc94c9264ea2
capture_application_exit.sh   a6f74ddd8b7b5b6620ef031312bcc26444119b44696a12345793f74a442a046c
prepared_inputs.json         a5c6eb3fd80537a8c7e246b26540b661f05326ec23d27b9d57554b91e166b2ae
preparation_checks.json      80415cd8ff1a81f2efc8ce19562f1afed2abeae737ef0f2341af6b3f5985876e
```

上述程序/脚本摘要由本次只读重新计算。读取的 `preparation_checks.json` 同样绑定73d检查器、4e50 runner及fb7b汇总器；记录5份Python AST解析、3份shell语法检查和639份冻结源码匹配，产品运行和板端访问均为false。该文件是协调方的静态检查记录，本子Agent没有重复执行检查或构建。后续构建必须核实际使用的源码/对象对应本冻结版本，不能把较早d8fa修订的产物归到73d。

## 真实关闭链没有被临时工具替代

临时 `status_login_review_05.cpp:55–115` 的 `finishReview()`：

- 先登记一次关闭意图并保存独立 `check_exit.txt`，不再调用 `qApp->exit()`；全部提前失败和末尾完成都进入该函数。
- 每100毫秒等待可见登录卡经原reject动画关闭；有其他模态框时等待，不自动确认草稿、不强删提示。
- 主窗可用时调用真实 `window()->close()`；主窗因告警排空被禁用时等待，不直接停止receiver。
- 超过15秒仍未确认关闭，记录 `REVIEW_WINDOW_CLOSE_NOT_CONFIRMED`，将原成功码改为4、保留原非0码，停止重试；依旧不绕过真实关闭守卫。

正式关闭链的依据：`06_programs/sn100_host/shell/frmmain.cpp:661` 调用 `requestOriginalAlarmShutdown()`；`shell/alarm_shell.cpp:141–152` 停止新设备输入、禁用主窗并请求原receiver正常停止，先拒绝当前Close；:132–136 在receiver的 `stopped` 后排队再次关闭窗口。临时工具等待的就是这一正式链。

正式 `shell/main.cpp:1408–1423` 在事件循环返回后再次核原告警接收器是否正常停止以及存储错误；未确认或有错误会将应用结果设为3。:1444–1492 顺序关闭后台、等待线程，再打印线程状态和实际退出值。临时工具没有直接把这些状态改成完成。

## 已修的15秒误报顺序

早一版会先写check_exit=0，15秒等待超时只记一条critical；若receiver在16秒等稍后时刻排空并自行关闭窗口，应用和外层都可退出0，旧汇总就有机会误报成功。

最终有三处阻断：

1. `status_login_review_05.cpp:74–84` 将超时结果持久化为原非0值或4。
2. `run_case_on_board.sh:138–143` 在auto且wrapper=0时，若check_exit缺失/非0或日志含关闭未确认标记，case仍为1。
3. `summarize_run.py:105–106、123–125` 独立拒绝关闭未确认和原告警关闭未确认标记，并要求check_exit=0。因此即使超时更新文件失败，保留下来的失败日志也不会被后来正常退出0掩盖。

以上是可由源码确定的修正，不是已运行复现。

## 各结果边界

| 输入事实 | runner结果 | 汇总结论 |
| --- | --- | --- |
| 检查失败，实际应用及外层正常退出0 | check_exit为1/2等，auto case=1；wrapper/app实际0照常保留 | false；不能用正常清理掩盖失败 |
| check_exit缺失，应用及外层为0 | auto case=1 | false；缺少独立检查结果不能通过 |
| 关闭等候超过15秒，之后正式链迟到关闭且实际退出0 | check_exit非0或失败日志使auto case=1 | false；后来退出0不消除已记录超时 |
| 应用正常退出但JSON缺失/未完成/failed非0，check_exit碰巧为0 | runner可能case=0，因为它不解析JSON | 汇总仍false；这是case不是总验收的明确边界 |
| 原告警未排空或发生存储错误 | 正式main实际结果3，helper保存应用码，外层/阶段非0 | false；汇总还显式拒绝原告警未确认标记 |
| timeout触发，应用在TERM后清理退出0 | helper可保存实际应用0；wrapper保留124等，case不改成0 | false；超时不能归为正常完成 |
| 硬终止导致helper来不及写应用退出码 | 实际wait文件缺失，封装失败保留 | false；不存在文件就不推定正常退出 |
| 所有检查结果、真实退出、关闭证据均齐备 | 只是具备成为通过的条件 | 还必须满足完整TX范围、INI核对和同一主控存活等原约束 |

`capture_application_exit.sh:8–18` 对本次应用执行wait并单独保存码；runner :123–125 保存外层wait码，:138–144另存case码，三者互不覆盖。汇总 :17–18、98–99 在对应文件缺失时取None；缺失必然不能满足成功条件。结果内容损坏使解析器非0退出时，也不能当作通过或仅根据旧日志结论补齐。

## 汇总的通过条件

当前 `summarize_run.py:100–125` 要求以下条件同时成立：

- 完整TX审计无错误、指定INI核对一致、同一主控仍存活。
- wrapper=0、实际application wait=0，并有明确应用退出0日志。
- 已启用的医院、账户、历史、项目、试剂、样本、维护、打印、LIS、质控及校准关闭标记齐备；两个告警worker停止及总线程停止均为true。
- 不含 `ORIGINAL_ALARM_SHUTDOWN_UNCONFIRMED` 和 `REVIEW_WINDOW_CLOSE_NOT_CONFIRMED`。
- auto另要求check_exit=0、检查JSON存在、failed=0及completed=true。

manual仍必须有原登录自动同步的已有授权登记，才允许Ur/Up/UP；自动项仍只允许Qj。此次关闭修订没有扩大报文范围，不解除42/48和项目800/405阻断，不新增主控启停、正式源码或current写入。

真实宿主Close已接回正式守卫，但检查器仍是程序内触发，并没有重开步骤。汇总中物理输入/实屏、管理员真实凭据、正常关闭重开、真实测试绿灯和机械功能均保持false；后续按实际人工及运行证据另记，不能由本静态审查代替。
