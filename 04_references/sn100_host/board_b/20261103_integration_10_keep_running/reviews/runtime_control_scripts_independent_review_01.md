# 四份实际运行控制脚本02：只读独审

2026-11-03。审 `make_runtime_controls_02.py` 及其已生成的preflight、stop、launch、wait四份02实物。没有执行生成器、脚本正文、产品、模拟、网络或板端操作，没有改正式、冻结代码或控制脚本，只新增本报告。01原件保持，不以本报告改写任何实际退出结果。

## 结论

所列后像中未发现确定的锁继承死锁、第二GUI启动、旧start约30秒READY超时关闭，或等待器失败后关闭新程序的问题。stop只有一次旧helper正常关闭调用；干净退出、图形/服务/显示安静和生命周期锁均满足后才写旧停凭据。launch只有一次系统服务创建，实际使用六参数supervise、0及空session，随后只等身份文件，不发超时TERM。waiter由lckfb执行，独立120秒截止不结束产品。

这只是脚本源码审查。启动工具退出0只说明新活进程身份核对成立，并不是READY、观察、实体屏幕或真实LIS业务通过；实际四阶段结果及postlive仍须分别登记。旧supervise本身异常/外部信号的TERM边界沿前报告，未被本脚本消除。

## 共同身份与冻结输入

四脚本的 `checked_record()` 到quiet检查的公共定义与已审 `prepare_flat_batch10_02.sh` 逐字节相同，只在新 `root_assert()` 前增加一个空白分隔行。内核线程分类修复没有倒退。

`root_assert()` 每次重新核板B序列、boot、目标current、Root批准/意图/七配置清单三记录原件SHA、实际七配置、三个指针原文字节摘要、新平层helper/SHA256SUMS及清单实物，并核唯一原主控完整身份等于本批活体guard。它没有stop/启动主控或修改INI路径。固定SSH主机公钥仍由总控外层通道核，不能以脚本内序列号取代。

preflight先核旧current，再以lckfb source已核新helper，仅调用其preflight函数。source不会触发helper的main直接执行guard，因此不会启动产品；它做库、字体、设备访问及缓存等前置核对。最后核旧GUI原exe/启动时刻，不能将preflight0当新程序已运行。

## stop：一次请求和真实凭据

stop先取不可变存储锁，不在请求旧程序关闭前取其仍持有的生命周期锁。它核旧latest_run、run规范路径、原PID/start_ticks、无旧exit_code、实际旧exe/SHA及lckfb身份，且拒绝既有本次停止日志或凭据。

只调用一次 `runuser -u lckfb -- bash <旧helper> stop BASE`，记录该调用真实退出。失败立即停止，不强杀、不调用systemctl stop、不再次发信号。成功后还要求旧run实际exit_code为0、APP_EXIT=0、CLEAN_SHUTDOWN=1，然后等quiet。

quiet最多4次，失败只等下一次检查，不对进程作操作。每次包含完整/proc扫描；据总控诊断单次约6秒，不能把四次间隔0.1秒误读为整个阶段只有0.4秒。此后才取已释放的生命周期锁，并再次核完整root_assert和quiet，才生成旧停receipt。receipt不是预写，实际停止工具退出和旧应用退出是分别核的。

stop子调用可能继承父FD8，但它只执行关闭旧进程的命令，不新启产品、也不等待同一把存储锁；父始终持锁，短子进程返回后再核生命周期锁，没有形成相互等待。

## launch：锁交接、TTY和唯一启动

launch按store→lifecycle顺序取锁，核新current、quiet、实际激活审计目录、意图SHA和真实旧停receipt摘要。再核观察目录规范、lckfb所有、700且完全空，BASE属于lckfb、runs规范。captures由root建新父后设lckfb:lckfb 700，waiter仍负责新建capture_01叶目录，未回到root750父目录不可写的问题。

新RUN为不存在的规范子目录、lckfb700；保存旧latest_run，再原子登记新latest_run和本批launched_run记录。已有captures等会拒绝重复执行，不是失败后自动再开一份程序的入口。

唯一启动调用为 `systemd-run --unit=sn100-shell --property=Type=exec` → openvt7 → runuser lckfb → env三变量 → 包内helper `supervise BASE FLAT RUN 0 ''`。没有调用start、没有旧自动退出检查CPP，没有机械动作、登录或Qk。三变量明确到实际普通用户命令层，原资料/配置指针保持已核原文。

锁交接没有确定死锁：这里创建的是由PID1启动的系统服务，没有 `--scope/--wait/--pipe` 或显式FD传递，服务不会继承调用CLI的FD8/9。Type=exec等待openvt本体exec成功，不等待其后代supervise取得锁或READY。父在systemd-run返回后关闭9/8；新supervise再取得同一生命周期锁并启动。若系统服务创建或10秒取锁交接异常，只会表现为启动未完成，源码没有用TERM/重试补救；不能据本源审承诺远端调度时限一定满足。

释放锁后只等app.pid/start_ticks，最多60次、每次0.25秒；不等待READY也不向新程序发关闭信号。随后核同PID/start、实际exe/SHA、lckfb及新current/主控/配置。`PROGRAM_LEFT_RUNNING=1`在这里是当次活体身份结果，之后仍需等待器和postlive，不是未来长稳保证。systemd独立持有进程，不随本检查脚本结束而主动清理。

## wait：只读结果和留屏

wait不申请生命周期锁，不会与正在运行的supervise互等。它从本批launched_run取得实际应用PID/ticks，先核新current/原主控/配置及应用身份，再由lckfb调用已修权限的waiter。

capture_01位于lckfb700的captures父目录内且须不存在，不在观察结果目录内。全部14参数显式传入，exe、发布号、观察ID、boot、序列及current均与本批匹配。120秒是waiter自身截止；真实退出单独保存，即使失败也不stop/restart或重发。

返回后再做root_assert及同PID/start/exe、无exit_code核对，最后把waiter真实退出作为本工具退出。没有把等待器0当应用退出0；后续解析器及Root实际postlive仍独立核。

## 实际源检查与绑定

只用Python AST解析生成器、读取字节并核共同定义与SHA，未import或调用生成器。首次自写比较把额外分隔空行也计入函数体而断言1；只读确认恰多一个空行后按原始字节复核，实际退出0，没有改任何候选来消除该比较问题。没有执行四脚本正文或构造运行样本。

| 文件 | 实际SHA-256 |
| --- | --- |
| make_runtime_controls_02.py | 7f41cafafb66b53926e56331bccc46d09a05d144cb26031bd7ff65a200083c23 |
| preflight_new_batch10_02.sh | 163f093c0a7a182954d04b67d8426d60e27abed5c66321b408b9110ba33a152c |
| stop_old_batch10_02.sh | 349014eeca36ee90f30b34695fc498293bf06ee00a02874375afe3f0c4d88fdb |
| launch_new_batch10_02.sh | fb864ad51c15f190f8385bcba42d20fa6874d83407bed824aefdca4476b57577 |
| wait_lis_batch10_02.sh | 4b732ecc74a177c3155f2084a3061fc6648f448531c9731441a063d223389b6d |

本报告仅适用于这些后像。后续实际旧停/激活需单独保存真实receipt，激活脚本须绑定它的实际SHA；当前脚本前缀中的旧停零摘要没有被用作成功凭据。新的控制脚本或字节变化须按增量再核，不用本报告预填任何板端通过。
