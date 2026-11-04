# 新版本留屏启动：既有监督入口只读审查

2026-11-03 21:46:50 +08:00读取。正式源冻结与完整构建进度沿总控交接，本审查不修改687源表、工具或正在构建的目录，不编译、不联板，不生成或执行板端脚本。这里只写本报告。

实际源码位于 `06_programs/sn100_host/shell/rk_board.sh`；仓库没有 `06_programs/sn100_host/rk_board.sh`。不可变程序包内的同一文件才位于包根 `rk_board.sh`。本报告按真实函数和参数说明，不以旧注释中的“offline”推定实际业务模式。工具修订03、实际WORK `integration_next_02` 是有意区分，本报告不再列为命名问题。

## 结论

可以复用现有六参数入口 `supervise BASE RELEASE RUN 0 ''`，由独立系统服务持有监督进程，启动唯一的新正式程序，再用只读等待器取得快照。它避开旧 `start` 外层30秒READY超时发TERM的分支，0也关闭监督软期限；不需要额外的业务或检查进程，也不链接旧自动退出检查CPP。

这不等于“任何失败都不会关闭”。既有监督器在自身异常退出、收到TERM/INT/HUP时仍有向应用发TERM的路径。总控应保证启动与SSH、观察工具的截止时间相互独立，观察结束只核结果与存活，不能以收尾stop、服务重启或超时杀整个进程组处理。若要求连监督器故障路径也绝不发TERM，则当前冻结helper不能作此保证，不能用参数0宣称已解决。

## 参数与0的准确含义

| 入口形态 | 实际含义 |
| --- | --- |
| 板端 `rk_board.sh` 无参数 | BASE为空，校验失败，不会启动 |
| `start BASE` | 默认1800秒软期限，仍有约30秒READY等待 |
| `start BASE 0 ''` | 关闭软期限，**仍保留30秒READY超时TERM**，本次留屏不采用 |
| `supervise BASE RELEASE RUN 0 ''` | 六参数；0关闭监督软期限，无start的READY等待器 |
| 缺少空SESSION的supervise | 参数数量不足，拒绝；必须显式保留最后一个空参数 |
| `run ...` | `rk_board.sh`没有run动作；RUN是目录参数，不是动作名 |
| 电脑端 `shell/rk.sh start 0` | 最终仍调用旧板端start，不能替代直接supervise；电脑端无参默认prepare，也不是留屏启动 |

这里的SESSION须为空。原资料根存在时，helper第220行明确拒绝模拟会话；不能填 `offlineAdmin`、传测试口令或用模拟登录绕过原配置/人工登录条件。`RK_RUN_TIMEOUT=0` 不会替代这个helper的显式seconds参数，也不会删除start的30秒分支。

## 启动前总控仍须核实的现场

直接supervise会核包、目录归属和运行锁，但不会代办start的全部前置检查，也不证明传入RELEASE就是current。总控需串行完成：

1. 通过已固定SSH公钥连接，并实际核板B序列号、boot、唯一原主控真实exe/SHA与全部5000监听。不得重启或重复启动主控。
2. 新包、源/对象/程序身份、包后启动意图、三个原资料/配置指针和七份实际配置指纹已固定。RELEASE必须是已切换的平层 `BASE/releases/<新发布号>`，实际等于current；发布号不是源表SHA或程序exe SHA。
3. 旧GUI、报告/检查进程及显示服务已按总控实际切换范围处理完毕，VT7/DRM无占用；重复核不是发stop/kill的许可。普通用户lckfb具备DRM/input/部署目录权限。包内Qt插件、动态库和字体等按既有preflight核实。
4. BASE、RUN为实际规范目录，BASE及RUN属于lckfb；RUN是新建的 `BASE/runs/<仅数字和下划线ID>`，不能重用历史run。`latest_run` 须由总控按新RUN登记；裸supervise不会代办这一项。已存在同名服务或运行锁占用应查真实进程，不删除锁文件绕过。
5. 原 `original_project_root`、`instrument_configuration_file`、`theme_configuration_file` 必须确为预期正规指针，工作根有原许可marker。不能让缺原根指针触发helper的匿名资料/账户seed后备路径。实际命令虽然仍带历史 `--offline` 字样，但原根、主控配置指针与 `--instrument-control` 会启用真实原主控连接；判断依据是生效参数和INI。

supervise自身不取不可变存储锁；切换阶段需按 `immutable_store → lifecycle` 完成后释放父级两锁，再交给新监督器。supervise第146–147行自己取 `lifecycle.lock`，从启动一直持到应用真实退出，最多等锁10秒。父进程不得持同一排他运行锁等待新监督器，否则只是交接失败；不能为拿锁关闭现有程序。

## 必要环境与最小命令形态

观察三变量必须在**实际lckfb监督器/产品进程**中生效，不能只在电脑SSH父环境或启动意图文件里写过。现有start的systemd-run没有显式传它们。建议在runuser切到lckfb之后，直接用 `/usr/bin/env` 放入三变量，再执行既有helper；这样不依赖sudo/systemd/runuser是否保留调用方环境。

- `SN100_LIS_OBSERVATION_DIR`：新的、规范绝对、无符号链接重定向、lckfb可写的空私有目录，权限0700。不要使用RUN/runtime本身：helper会创建缓存等内容，使观察入口的空目录校验失败。可由总控安排独立的 `RUN/lis_observation`；意图/工具/日志也不要先放进去。
- `SN100_LIS_OBSERVATION_ID`：1至64个英文字母、数字、下划线或减号，不含点号；采用CPP和等待器共同接受的最严格格式。
- `SN100_LIS_OBSERVATION_RELEASE`：实际新包的64位小写发布号，等于已核current包身份；不是环境自身独立核包的证明。

以下只是总控填写已核变量后的接口形态，不是本轮执行命令或新脚本。沿用现有系统服务→VT→普通用户结构，改为直接supervise并显式传环境：

```bash
sudo -n systemd-run --quiet --collect --unit=sn100-shell --property=Type=exec \
  /usr/bin/openvt -c 7 -s -w -- \
  /usr/sbin/runuser -u lckfb -- \
  /usr/bin/env SN100_LIS_OBSERVATION_DIR="$observation_dir" \
    SN100_LIS_OBSERVATION_ID="$observation_id" \
    SN100_LIS_OBSERVATION_RELEASE="$release_id" \
  /bin/bash "$release/rk_board.sh" supervise "$base" "$release" "$run" 0 ''
```

总控应保存真实systemd-run命令、退出和服务身份，但命令成功只表示系统服务启动请求成立，不是APP_READY或LIS通过。普通用户归属、当前release、run/app.pid、start_ticks、实际exe/SHA和观察封口参数还须逐项实核。不要把systemd的MainPID（openvt等管理进程）填成应用PID。

这条结构由systemd持有，不与SSH连接或观察工具共用进程组/退出清理；openvt为图形VT7提供会话并等待其子进程。不要改成SSH前台直接supervise、仅加后台 `&` 或把整个服务放入观察器timeout/EXIT清理。现有运行环境函数会设置EGLFS、KMS、库/插件/字体路径，监督器另设run/runtime的XDG目录、kms.json并从release启动；无需复制患者资料或倾倒环境。

实际服务的Restart、RuntimeMaxSec、KillMode、TimeoutStopSec及父子归属仍须由总控现场读取确认，当前源审不宣称远端属性已符合。留屏阶段不得主动停止/重启该服务；本次没有创建、检查或更改远端服务。

## 仍能触发退出或TERM的路径

| 路径 | 现有行为与本次边界 |
| --- | --- |
| start外层约30秒未见READY | 第393–395行对匹配应用发TERM；直接supervise避开此层 |
| supervise seconds非0到期 | 第297–302行发一次TERM；显式0避开此软期限 |
| 监督器收到TERM/INT/HUP | 第198行置标志，第292–295行对匹配应用发TERM；SSH/服务/父进程信号隔离仍必要 |
| 监督器异常EXIT | 第182–195行记录失败，若仍拥有子进程则发TERM并继续等待；set-e下日志/资源/写盘等未处理错误也可能到此。0不屏蔽 |
| helper显式stop | 第323–333行对实际匹配应用发TERM；不是观察结束步骤 |
| 外部服务/系统管理停止 | 可能同时作用于openvt、监督器和产品；helper无SIGKILL不代表外部服务永不终止cgroup |
| 产品自身退出/初始化失败/用户关闭 | 由产品实际行为决定；helper等待并记录真实退出，不由0改成永生。产品SIGTERM经ShutdownRequest请求窗口关闭，可能被草稿/忙守卫拒绝，不能据发信号就预填已干净退出 |

子进程身份检查失配会离开监督轮询再等待原来的子进程，不追逐新PID。`finalize_child()` 保存的是Bash wait的返回值，并以 `exit_code` 最后封口；在正常无信号收尾中它对应子进程退出，但被捕获的信号也可能中断wait，不能把该文件单独当成应用已消失的独立证明。本轮没有做信号注入或复现该异常。留屏验收应要求无本轮exit_code且实际进程持续匹配；没有结果不能补APP_EXIT=0、CLEAN_SHUTDOWN=1。正常READY不会停止supervise；内部观察封口也不结束它。

## 只读等待、采集和观察结束

已审等待器只等待总控已启动的一个受权PID。总控需先读实际run/app.pid/start_ticks，另核boot/current/release/exe/SHA及板身份，再显式传全14参数：结果目录、新采集目录、PID、start_ticks、release、exe、exe SHA、观察ID、等待秒数、boot、板序列、current链接、预期current真实目录、包内release_id文件。等待器实际参数总数14，以文件内列表为准，其中“release”指 `--release-id`。

采集目录不能存在、不能等于结果目录或位于其内；可以使用另一新私有兄弟目录。等待上限1至600秒；覆盖60秒就绪+15秒观察并留采集余量即可，例如总控选择120秒，但这仅是等待器自身截止。超时只退出等待器并记采集未完成，不TERM、不重启、不自动再开程序。

等待器前后核实际同一活PID、start_ticks、exe/SHA、current、发布号、序列和boot，再复制已封口JSON/ready及原文身份；它不采持续增长日志作“前后必须相同”证明。解析器核真实采集文件，分别给采集、这段留屏证据、软件项和真实LIS业务pending。等待/解析退出各由总控外层记录，应用退出仍null。

结束时仅以普通lckfb执行既有status或等价只读身份检查，确认新release、APP_RUNNING/READY、无exit_code、观察ID/发布号一致及原主控未重启。不要再执行stop、旧自动检查器、重发Qk、登录或机械动作。真实屏幕、真实LIS对端/业务和资源测量范围另记，不能由一次封口或工具0冒称全部完成。

## 本次绑定指纹

只读文件和SHA，未执行helper、等待器、解析器或任何阶段；未为本任务重复编译或做运行测试。

| 文件 | 实际SHA-256 |
| --- | --- |
| shell/rk_board.sh | e3587bad320003bf1a2ea7b3176bf14d23527ebaae65b60ffb4bd5388d3866d2 |
| shell/rk.sh | 8a02493a53fe6ff31e95ffd7887b63f2982ea3cb63bdcd00fc22ae6f9f573821 |
| shell/lis_keep_running_observation.cpp | ddeb47ff1b3175c0366c0671122117e2cdd585089f1e933a0846c5b0bc8cf543 |
| wait_lis_observation.sh | eff1fddabbb5d8bf89d54f3d2b45a78760e8d8a9738fd71047baa7730aad1de8 |
| review_lis_observation.py | a0ea76de798d3fee19e5bb0adf547d033d3619f5020e05a5f61ce6cbc1c46783 |
| build_archive_candidate_03/operation_contract.md | 95f24fdcc634be8049036dfee35e1aa6d933d0992be642fa54766474b7e653d7 |
| build_archive_candidate_03/root_decision_alignment.md | 3139b571aace6635c47fe717ed686a37a6e08df415babf3923fa14ea28545a4e |
| lis_observation_root_decision_v1.md | c27714ee5142d4125d40235da2f551977d5e9dc4580885e34ef8755f0242be3f |

本报告给出可由总控冻结的最小启动形态与真实限制，不表示新包已经切换、服务已经启动、环境已传达或RK留屏已经通过。
