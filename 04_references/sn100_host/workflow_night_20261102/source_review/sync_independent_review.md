# UP项目回包独立复核（2026-11-02）

本文件只读检查原Windows、当前Linux和已归档板B实际收发。没有修改正式源码，没有构建、运行程序或连接板卡。请由同步工作聊天实施，总窗口统一编译和联机验证。

## 已确认的事实

1. 原 `communication/commandreceiver.cpp:1169–1176` 将U类的P消息直接交 `pcReceiveProjectInfo`；该函数（:1238）把解析后的完整字段转给 `frmSystemMaterial::slotReceiveProjectInfo`。原主窗口 :533 将该信号接到项目设置。
2. 原 `frmsystem/frmsystemmaterial.cpp:3076` 从回包第2字段 project_en_name 找项目（:3082–3085），按这个名字更新基础、结果、试剂、样本步骤，成功后刷新项目清单（:3248–3258）。原接收方不假定“下一包必须是上一次请求项目”，也不按当前界面选中行写库。原选择项目的UP请求 :2185–2187 已注释，不能当作实际请求入口。
3. 现 `OriginalDeviceSync` 使用 `Ur→逐项Up+UP[name]→字段比较→按指纹写库`。一收到非当前项目就通过 matchesSentParameters 失败使整个同步 invalidate，导致随后同一连接的UP全部被拒。
4. 板B现有 user_operation_followup.json 记录2次UP[PT]、18条UP；头次为D-D，随后PT、FIB、FDP、TT、AT-III、APTT等，PT还出现重复。第一PT头为2026-08-07、second_light为800nm；稍后PT头变为2025-01-14、405nm。日志摘要仅保存部分头，不能据此宣称整包一致或成功。完整消息应由总窗口在原资料联机收集，不用模拟或旧日志回放代替。
5. “主控广播了当前整个项目列表”与顺序现象相符，但这是推断；没有主控对应源码或清晰约定，不能认定UP[name]永远返回全量，也不能直接重写所有本地项目。

## 推荐的最小行为与边界

- 先完整 parseDeviceProject，再按 project_en_name 精确查当前同步快照；坏包不能通过只读两个头字段当作合法无关消息。
- 已知的其他项目、尚未下发的项目回包只能标记“未确认当前项目”；不能写库、不能越过本轮Up、不能把它计为该项目成功，等待当前项目真正匹配的回包。
- 对已确认项目的重复，可只核身份并忽略，不重复写库或重新下发。若要判为重复成功，至少检查与该轮已确认值一致；同名不同参数是迟到/变更，不能静默计成功。
- 未知项目、重复身份或原项目指纹变化保留明确错误，不创建新库、不猜项目名。
- 当前项目仍调用现有 matchesSentParameters 和 applyDeviceProject；保留原上下行字段表、针名转换、数字等值比较及所有本地指纹检查。不能为接受主控顺序而删除这些检查。
- 同名但字段不匹配的旧广播可能先于Up生效后的正确回包。应明确选择“立即失败”或“保留未确认差异并继续等当前固定截止时间”；后一方式也不得写库/放行，超时要说明实际差异。仅有摘要无法决定所有字段兼容规则。
- 一条完整匹配回包只推进一个项目；每个项目必须经过本轮Up，不能缓存未来项目的早到包，在稍后发送Up后直接用旧包放行。
- 同步完成后仍可能有本轮请求产生的重复广播；应保持ready，不让重复包清空connection或把ready降false。不能自动启动测试。
- 断线、换连接、新一轮begin、原资料变更须清理所有pending/已确认记录，旧连接包不得影响新连接。连接代数检查保留。

## 不能只修改 projectReply 的原因

当前 `SampleStore::receiveDevicePacket` 的UP分支（original_sample_store.cpp:114起）：

- projectReply返回true就会原样本load、originalProjectsChanged、sendOriginalConfigurationStep；返回false就invalidate、停超时并发未就绪。
- `sendOriginalConfigurationStep`（:181起）每次先停计时，再在waiting时重启10秒，并把 `step.complete` 发给主窗口覆盖ready。

因此简单把“其他项目”或“重复项目”改成返回true有两项隐藏错误：

1. 无关或重复流量会每次重启10秒，使当前真正回包永远不到也不超时。
2. 同步完成后忽略重复却返回默认step.complete=false，会把主窗口已完成状态降为未就绪；此时waiting=false，不再有超时恢复。

建议 `OriginalConfigurationStep` 明确区分“确认推进并产生下一步”与“合法无关/重复未推进”，调用者对后者只保留可见通知，不reload、不重启计时、不覆盖ready。若工作聊天只能改sync两文件，必须把调用者最小改动交给总窗口；不要以改写step.complete或每次发空step掩盖语义。也可由sync内部用单调时钟维护每项目固定截止时间，但调用者仍应避免每条无关包重新读全部数据库。

## 超时和失败现状

- SampleStore初始化时创建单次10秒QTimer；超时invalidate、发未就绪，不重发Up、不发开始，这是现有安全边界。
- 主窗口每连接只启动一次配置同步（device_control_shell.cpp:207），失败不会下次Qj自动重发；必须保留这种“不因状态轮询自动重发”的行为。
- 主窗口以connectionGeneration及原资料root核隔离；原配置写入队列失败也会invalidate。
- 任务开始还调用canStart重新检查所有项目文件指纹，保持不变。

## 额外待核，不由本切片顺手修规则

`matchesSentParameters` 对expected键在received不存在时多处continue；样本下行needle_select对应上行suck_release_count的命名也不直接相同。本切片应保留当前规则，单独对照原上下行设计后再判断是否该增强；不能在修顺序时顺带放宽更多字段。

## 应交付总窗口的联机核对

在板B原配置和原资料上先核身份、实际INI、当前程序与主控状态。观察实际Ur/Up/UP，记录每项目发送/回读/确认，特别首包D-D先于PT、多条PT重复、同名参数差异。必须核：已知其他项目不写库/不推进、不重启当前截止时间；当前完整匹配才推进；末尾重复不撤销ready；连接改变后旧包不更新；坏包、未知项目、参数差异、超时均不启动测试。不能为构造异常发送未授权机械动作或修改主控现场数据。若现场无法出现某异常，标源码核查与待联机，不能用匿名数据、模拟服务或回环测试补通过结论。
