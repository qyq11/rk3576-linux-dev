# 新运行准备脚本：内核线程分类修复定向审查

2026-11-03。只读原失败、ERR定位、分类诊断及新 `prepare_flat_batch10_02.sh` 实物；没有执行生成器、分类函数、准备流程或板端命令，没有修改冻结cfg、九工具、原03模板、filephase或正式源码。只新增本报告。

## 结论

新脚本对已确证的内核线程瞬态状态误拒作了最小修复，所审后像未见新的确定分类阻点。旧失败原件保持；本结论不是新prepare-flat已通过，也不改变旧应用/current/主控的板端事实。

## 实际失败依据

`diagnose_proc_identity_batch10_02.log` 明确记录 `/proc/212/exe`、线程名 `sugov:4`，两次state由D变S，flags均为69238848、start_ticks均126。原函数在核无用户exe时要求两次state、完整flags和start_ticks全部相同，故以79拒绝同一内核线程的正常调度变化。

ERR定位为 `return "$status"` 及 `pid=$(transition_controller_pid)`。本次保存的 `prepare_flat_batch10_01.exit` 原始字节为 `79\r\n`，不能改写为成功或与另一个外层失败状态混记；只读分类诊断02的退出原件为 `0\r\n`，该0只说明诊断完成，不是prepare成功。

诊断原文还记录原主控5008的实际exe、旧GUI24414的旧发布exe、旧current和 `FLAT_NOT_CREATED=1`。本报告只转述这份固定现场，没有重新联板，也不把它延伸成当前仍然持续的实测状态。

## 最小分类条件与新实物

原逻辑对第一次flags和start_ticks分别要求十进制数值。新逻辑继续要求第二次flags为数值，并要求第二次start_ticks精确等于已经通过数值校验的第一次值；这也保证第二次start_ticks非空且为数值，之后才进入分类。

在同一启动身份前提下：

- 两次flags的 `PF_KTHREAD` 位（0x00200000）均置，才按没有存活用户exe跳过。S/R/D调度状态及其他flags位允许变化，不以线程名猜身份。
- 或两次均处在Z/X死亡状态，允许Z与X互变，才按无存活用户exe跳过。
- 其余情况仍返回79。普通活用户进程的exe不可读，没有变成“忽略错误”；PID启动身份改变、第二次flags不合法或不能明确分类仍拒绝。进程实际消失的原返回1语义未改。

这满足所需边界：稳定的是进程启动身份和两次均成立的内核/死亡类别，而非瞬态调度状态或全部flags。未增加名称白名单、无条件忽略readlink失败或stop/kill来绕检查。

## 改动范围和绑定

通过AST只读取修复生成器的old/new字符串常量，在内存核对原脚本恰好一次替换即精确得到新脚本；没有import或执行生成器、函数或模拟样本。该只读核对真实退出0。总控交接的新脚本bash-n为0，本Agent未重复执行正文或语法命令。

唯一差异是 `transition_process_exe()` 第二次stat之后的一处分类块，原 `set -- prepare-flat`、意图、许可、发布/程序SHA、配置、主控检查、安装目录及其余模板字节均不变。新脚本仍只用于准备平层，旧停、激活、唯一新启动须另据真实证据派生和审查。

| 实物 | SHA-256 |
| --- | --- |
| 原prepare_flat_batch10_01.sh | 8eae2de564c42cc8fd3fe10ff15c93c3daf3bbdaa41b0b90d763bc6184dbed93 |
| 新prepare_flat_batch10_02.sh | a6af67facbe29474c73fd887e071c4f5f93502a4b9b5628eeaa4dd1ba3087b67 |
| runtime_process_classification.diff | 9649fee902ccda8499ebc9a173ec41ec8122b4349dca246bc4acd8c612992db1 |
| 原prepare退出原件 | 60dae80579a054bc5a7703da02947ee145e19400d113054f3af8e654b1b1713f |
| ERR定位诊断日志 | 080bbbdee059f894febb6953f36ed69eb79d1b97988b4af995bc5c6cdbb6ae7e |
| 分类诊断02日志 | 3c25245728222f7773c5051bcc50a4692c3f00ff50d24edd7398d9a6729b54f1 |

固定工具目录的transition原件仍为 `f4ca11e27ea16ef2a5f65e894a180d62b58081b253008e6376ab015c41dd72ea`，cfg仍为 `50b94b7d026385816811ae720d1409b656518dde6929a7e481b4696944462d03`。本次不修改已完成文件阶段的身份链或归档。

下一步由总控在固定SSH/板卡身份和原配置守卫下实际执行新准备并分别记真实退出。旧失败与只读诊断保留；不提前填prepare成功、旧程序停止或新版本已启动。
