# 全部忙状态解除路径复核（round04）

审查时间：2026-11-02 21:30（北京时间）。本次只读正式源码和临时候选，只新增本审查文件；旧审查记录保留，正式源码、候选、计划均未编辑。未编译、运行产品、做脱机测试或操作 RK 板。

本次审查绑定 `temp/sn100_host/workflow_night_20261102/startup_login_followup/` 中的当前 round04 候选。候选有 7 个文件；逐一重新计算摘要，7 个正式文件全部匹配清单的 baseline，7 个候选文件全部匹配清单的 candidate。

```text
candidate_manifest.json          f05b9848094dbb91c131f3212d86db3a06565624ba1c901c7063526aa168199a
combined_candidate.patch         49dbf48f3669ac706bdfff8984d545f9e8f45c4ffb627ff0050609a46b7b2bab
startup_login_candidate.patch    901b14cb9c90efc1334082162e0753499b61ea32ae8e810602630291a4c15c59
history_busy_notification.patch  591681edb68e3463edc7b410511649f4997f2f8d937a0d4b1b0cc90864144f10
```

## 审查结论及边界

在 `businessPagesIdle()` 实际包含的全部字段中，未再发现“综合忙状态已经从真变假，却没有主界面唤醒通知”的确定遗漏。前一轮医院字典关闭缺口已由本候选的综合 `busyChanged` 补上；公开登录、注销入口在忙守卫前消费启动意图，也已消除“主动调用被拒后旧启动意图随后补弹”的源码路径。

在所读入口、状态字段写入和回调连接范围内，未发现已消费的启动意图重新变为 pending、旧队列重新激活意图，或启动标记覆盖注销重登标记的确定路径。

这是源码静态结论，不是运行复现或功能验收。异步工作一直没有返回、页面仍然声明忙，不能据此认定“忙已解除但漏通知”；候选不会猜测这类操作已经结束。本次也不把此前源码允许的丢请求顺序认定为 7c4 两轮失败原因。审查末尾读取了协调方生成的 `candidate_verification.json`：记录 6 个 AArch64 对象、7/7 正式及候选匹配、3/3 补丁试应用通过、5/5 候选头文件依赖匹配；产品未链接或运行、未访问板端，集成后仍须完整 ARM64 重编译。本审查没有重复编译或运行，不将对象编译写成板端通过。

## 审查入口与全部消费者

正式 `shell/login_shell.cpp:13–20` 的 `businessPagesIdle()` 读取医院、账户就绪及账户忙、历史、项目/试剂/样本、质控、校准、仪器、打印设置、LIS 设置和告警历史。其生产消费者为账户按钮更新、注销后重登、手动登录守卫；候选再将同一谓词用于启动登录登记后及排队回调内的两次检查。没有用另一个较弱的“当前页面空闲”条件替换它。

以下“正式”路径均相对 `06_programs/sn100_host/`；候选路径均相对 `temp/sn100_host/workflow_night_20261102/startup_login_followup/candidate/06_programs/sn100_host/`。标成候选的行号按当前 round04，其余按未修改的正式源码。

## 逐项忙状态解除与通知

| 条件 | 解除来源、成功/失败/取消分支 | 唤醒主界面的证据 |
| --- | --- | --- |
| 医院设置 `busy_` | `ui/frmhospinfosetting.cpp:106–125` 读取完成先清忙再发状态/完成；:166–185 错误同样清忙并发状态/完成。`setBusy()` 不单独发信号，但以上是当前全部清忙调用。 | `shell/frmmain.cpp:148–162` 将状态及完成接至 `updateAccountStatus()`。 |
| 账户 `ready_` 与 `busy_` | `ui/frmaccountsetting.cpp:81–85` ready 后进入初读；:99–122 读回处理在成功提示框返回后才清忙；:236–255 错误提示返回后清忙。:183–193 setter 在变化时发 busy。 | `shell/frmmain.cpp:136–137` 接 busy。就绪前账户不允许登录；若初始化没有就绪，主入口不会宣告完整启动并登记请求，不能把未就绪当漏唤醒。 |
| 历史普通数据库请求 `m_busy` | 正式 `history/history_page.cpp:541–555` 错误进入 finish；普通查询、明细、保存、导出完成也进入 finish。正式 :969–985 / 候选 :976–992 清忙后发完成。 | 原有 `history_shell.cpp:42–53` 接状态/完成；候选 :49 另接综合 busy。 |
| 历史报告 `m_reportBusy` | `history/history_page.cpp:704–707` 赋值后进入统一 `setBusy()`。报告控制器在预览销毁、文件选择取消/销毁、导出成功/错误、打印完成/失败等分支重新计算综合状态，详见下文。 | 正式 `shell/history_shell.cpp:93–111` 原有显式更新仍保留；候选新增综合 busy 连接提供相同唤醒。 |
| 历史医院字典 `m_dictionaryBusy` | 正式/候选 `history_page.cpp:749、784–796`：打开和 finished 清除子状态后均进入 `setBusy()`。查询回执本身不负责关闭字典，字典保持显示时继续为忙。 | 候选 `history_page.cpp:967–973` 比较 `m_lastReportedBusy` 与综合 `isBusy()`；候选 `history_shell.cpp:49` 接主界面更新。前轮明确缺口已补。 |
| 分析项目 `m_busy` | `projects/project_page.cpp:322、348、387` 读取、导出、错误清忙；:241、266、278 来源切换、关闭/销毁也清忙。结果显示配置对话框的读写、取消、失效、存储销毁通过 `operationBusyChanged(false)` 交回 Page；:1627–1632 finished 再清忙。 | setter :476–486 按字段变化发 busy；`shell/catalog_shell.cpp:77–80` 接 busy/完成。 |
| 试剂 `busy_` | `reagents/reagents_page.cpp:241–274` 读回和错误根据仍在途的设备请求决定是否清忙，避免普通读回提前宣告整个操作空闲；:196–206 超时、:373–383 连接变化、:445–453 完成设备请求都会清忙。 | setter :356–366 每次发 busy；`shell/catalog_shell.cpp:83–94` 接 busy、完成、ready。 |
| 样本 `busy_` | `samples/sample_page.cpp:352、394、420` 读回、错误、存储关闭清忙；保存成功先等重新读取，没有提前清忙。 | setter :469–474 发 busy；`shell/catalog_shell.cpp:141–144` 接 busy/完成。普通样本编辑弹窗本身没有被偷偷当作该字段。 |
| 质控 `busy_` / `historyBusy_` / 警告框 | `quality_control_page.cpp:213、249` 元数据完成/错误清忙；:183–184 历史 presenter 的 busy 更新综合状态；:311–317 警告 finished 清 QPointer 后再发当前综合 busy。 | setter / `updateEnabledState()` :319–333 发综合 busy；`shell/quality_shell.cpp:74–91` 接状态、busy、完成。其他子状态仍忙时会保持等待，最后一个子状态解除仍有通知。 |
| 校准 `m_busy` | `calibration_page.cpp:132、144、151` 读取、保存后重读、错误；:448–451 新建对话框返回，:645–647 方法对话框返回，取消分支均会调用清忙 setter。接受后立即开始下一异步操作，会重新为忙。 | setter :218–221 按变化发 busy；`shell/calibration_shell.cpp:40–55` 接状态、busy、完成。排队启动回调会再次检查，不能利用两个异步步骤之间的短暂空闲越过下一次忙。 |
| 仪器维护 `busy_` 与连接配置 `connectionBusy_` | `instrument_page.cpp:148–177` 连接配置成功/错误；:181–208 维护读回、提交重读、错误、关闭；:666–677、:680–694 投递失败也清连接忙。 | 两个 setter :610–615、:658–663 都按综合 `isBusy()` 变化发 busy；`shell/instrument_shell.cpp:39–48` 接 busy/完成。先解除一个子状态但另一项还忙时无需发空闲，最后一项解除会发。 |
| 打印设置 `busy_` | `ui/frmprintersetting.cpp:35–60、101–105、139` 读取、保存回显、错误清忙。 | setter :75–81 发变化；`shell/printer_shell.cpp:22–33` 接状态/busy。 |
| LIS 设置 `busy_` | `ui/frmlissetting.cpp:68–91、160–176` 读取、保存回显、错误清忙。 | setter :101–107 发变化；`shell/lis_shell.cpp:22–33` 接状态/busy。 |
| 告警历史综合 busy | `alarm_history_page.cpp:171–173` 包括打开、查询、关闭、待显示行、日历和警告。打开错误 :132–139；查询正常/取消 :348–396；失败及来源释放 :398–414、450–459；日历销毁 :434–435；警告销毁 :218；关闭动画 :115–116，最终均重新计算或由 host 销毁通知。 | Page 的 `updateControls()` :200–212 比较综合 busy 后发信号；host :21–23 转发 busy，页销毁也发 false/viewClosed；`shell/alarm_shell.cpp:39–50` 接主界面更新。关闭正在继续时保持 busy，真正解除才唤醒。 |

## 报告、通信及提示框的补充核对

报告控制器的综合忙状态在 `history/report_controller.cpp:172–179` 统一计算，包含引擎、文件选择框、导出和独立打印。预览销毁在 :285–291 清引擎后更新；文件选择框 :319–326 在 `finished` 和 `destroyed` 都释放并更新；文件导出 :387–403 在终态清 `m_exporting` 后更新，进程启动失败/异常退出/超时终态也进入该路径；独立打印 :573–598 清 `m_nativePrinting` 后更新。若预览仍存在，导出结束不会误报整个报告操作空闲；之后预览销毁仍会再次计算。

通信须区分实际用途：仪器连接配置文件读写计入 `connectionBusy_`；试剂设备操作计入 `busy_`，具有完整回执、连接改变、超时的解除通知。`DeviceControl` 的实际连接过程、运行灯态和仪器测试状态本身不在 `businessPagesIdle()` 中，本候选没有新增“等仪器连接或测试完成才允许登录”的规则。`shell/device_control_shell.cpp:141–154` 的实际动作可用状态变化还会额外更新账户显示，但不应把该通知当作某个缺失本地忙回调的唯一补偿。

提示框方面，账户页在同步成功/失败提示返回后才清忙；质控警告和告警历史警告/日历计入综合 busy，关闭后有通知；校准新建/方法对话框明确用 setBusy 包围，取消仍清忙。离页草稿确认和其他未纳入 busy 字段的界面沿用原守卫：启动候选在进入这类确认前已经消费自动请求，使用者取消后不会再自动询问。普通工具提示没有作为额外忙状态处理。本结论覆盖实际计入谓词的字段，不声称所有 QWidget 对话框都新增了登录互锁。

## 一次启动意图、主动入口、取消和退出

- 候选 `shell/login_shell.cpp:38–52`：唯一置 pending 为真的地方是 `requestStartupLogin()`，有 consumed/pending 守卫；`cancelStartupLogin()` 同时清 pending、queued 并置 consumed，之后没有重置 consumed 的代码。
- :55–83：真正排队前及排队执行时都检查相同综合忙状态。排队以后页面再次忙只留下 pending；该页面结束后通过上表通知再次检查。重复通知受 queued 守卫限制，最多一个待执行启动回调。
- :69–81：旧回调清 queued 后先检查 pending。即使撤销没有物理删除定时回调，已消费意图也不能复活；取消后不能重新登记，因此不存在旧、新两代启动请求互相覆盖的问题。
- round04 候选 :124–128 在公开 `requestLogin()` 入口即消费启动请求，早于未启用、注销中、已有卡、busy 和离页守卫；因此主动登录即使被拒绝，也不会留下旧启动意图。候选 `frmmain.cpp:412–416` 在公开 `requestLogout()` 入口同样处理。两处都只操作 startup 标记，不清除注销重登标记。
- 候选 `frmmain.cpp:390–391` 先恢复注销重登、再检查启动；启动检测到认证/注销意图会消费自己。认证、已有可见登录卡、隐藏宿主、正在退出均为结束启动请求的条件，未发现互相重建 pending 的写入。
- 候选 `frmmain.cpp:597–606` 退出确认取消后调用 `updateAccountStatus()`，同时重新检查仍 pending 的两类登录意图；已经消费的启动意图不会重建。仍有退出确认框时两个回调都只等待。
- 候选 `frmmain.cpp:608–609、619–622` 确认退出和任意 Close 入口都先消费启动意图；`login_shell.cpp:42–43` 还在 `aboutToQuit` 撤销。即使业务守卫拒绝 Close，也按候选注释停止本次启动自动弹窗；这是明确选择，不是漏重试。
- 候选 `login_shell.cpp:109–120` 的注销重登旧队列在宿主隐藏或正在退出时清除自己的待重登标记；它不会写启动 pending。登录卡 finished 只刷新账户显示，未重建启动意图。使用者关闭卡后只能主动再开。

本轮只读审查没有新的确定修正建议。仍须将编译、板端运行和连接真实仪器的验证结果分别登记。
