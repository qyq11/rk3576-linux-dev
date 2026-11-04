# 第10批实际板端证据：独立只读核对

2026-11-03。只读核实际Root脚本/日志/退出文件、已封口capture、既有解析器输出及取回的9份remote_proofs原件。没有联板、启动/关闭产品、重新执行检查器/解析器或重复业务验证，只用本地字节、SHA和JSON字段核对，并新增本报告。

## 结论与允许关闭的范围

本批**正式启动、被动LIS软件1项/0失败及这段留屏证据可以登记通过**。末次postlive已经封口为0，记录截至22:20:42新应用仍是同一进程、READY且无exit_code/result；原主控身份、七配置与三指针保持。没有发现需要为本段证据重启或重跑程序的缺口。

结论限于本段：不能扩成旧26项全部通过、B1六命令实际验证、真实LIS连接/下载/上传、仪器动作、人工登录、实体屏幕、资源门槛或长稳通过。历史26项25通过1失败及其原始退出不改写。

**旧版**b31的APP_EXIT=0、CLEAN_SHUTDOWN=1只属于旧PID24414、旧run；**新版**c7保持运行，application_exit=null，未评估关闭，不得填APP_EXIT=0或干净退出通过。

## 实际阶段退出

| 实际记录 | 退出及核对范围 |
| --- | --- |
| prepare_flat_batch10_01 | 原件79，内核线程D→S分类误拒，保留失败；无平层创建/停止/current变化 |
| diagnose_proc_identity_batch10_02 | 0，只读诊断完成，不是准备通过 |
| prepare_flat_batch10_02 | 0，新平层准备，CURRENT_CHANGED=0、PROGRAM_STARTED=0 |
| preflight_new_batch10_02 | 0，新包前置条件通过，旧应用仍运行 |
| stop_old_batch10_02 | 0，旧版真实退出0、clean1、显示释放后生成receipt |
| activate_new_batch10_01 | 0，只切current，PROGRAM_STARTED=0 |
| launch_new_batch10_02 | 0，新活PID及身份确认，软期限0 |
| wait_lis_batch10_02 | 0，waiter实际0，返回后同新进程仍运行 |
| post_live_batch10_01 | **最终实读0**，22:20:42 READY、同进程、无新退出结果、配置/指针保持 |
| collect_sealed / parser | 各实际0；解析器stderr空，未由本Agent重跑 |

preflight日志保留两条 `find ... /root: Permission denied` 的工作目录恢复提示；wait日志保留两条cp的-n兼容性提示，不能写成“完全无stderr”。实际阶段均已完成且封口字节核对成立，这些提示不被改写为本轮LIS业务失败，也没有因此安排重跑。

## 新应用身份与启动环境

capture前后两份身份文件原始字节完全相同，且与Root已执行launch、包后意图、解析器实际命令及postlive一致：

- 板B序列 `8314b1e8ffd0321f`；boot `0eb94389-f120-42ad-9042-9c21d96a1481`。
- 新发布 `c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e`。
- 新PID `669382`，start_ticks `2102645`；run `20261103_221819_662833`。
- exe为该平层发布下的sn100_shell，实际SHA `50d3f3b510772e717e456cc4e3e910060a82a6635b9d914f033e79f9ed6f0258`。
- current、release_id文件及其实际值均对应此新发布；同进程实物不能由systemd MainPID替代。

已执行launch脚本字节仍是前审的 `fb864ad51c15f190f8385bcba42d20fa6874d83407bed824aefdca4476b57577`，显式传三观察变量并走六参数supervise、0、空session。封口采用的ID/发布号、实际结果目录和新PID与该意图交叉相符，证明对应新进程采用了这一组观察输入；没有另取全环境或认证内容。

postlive实际命令行使用原runtime/data、原资料根、原主控config.ini和theme_config.ini，包含instrument-control；没有模拟会话/匿名seed参数。历史命令行的 `--offline` 字样不改变本次实际原配置连接模式。

## 封口与本次软件项

capture_manifest恰列5份封口采集文件，逐份SHA匹配；ready原始字节精确为一行观察ID、发布号、PID及JSON SHA，带结尾LF。取回tar包含这5份加清单，6份均为普通文件，归档内字节逐份精确等于本地capture，未靠重新生成JSON补证据。

JSON真实值：启动等待6344毫秒，小于60秒；观察15002毫秒，满足15秒下限；快照时窗口存在/可见。应用退出null，退出/关闭/实体屏幕/Qk观察请求标志均保持未评估或false。

唯一软件项为“顶部LIS对象、结构提示与原配置来源可核对”。实际状态与已提交配置来源指纹均为 `f13908c0f9e47ce9f4a2e18314056b8289777b65f9dd4b4b6c94f5405eaca575`；配置为原tcp client 127.0.0.1:3800，link true，自动获取/发送和simulation false，设置已就绪、无忙/草稿。严格路径 `frmMain/frmBg/statusArea/btnLis` 三层各1且实际类型/父对象正确。提示版本1、连接代次9、对端0与state逐项一致。checks恰1项true，failed实际0；独立字段核对支持既有parser的pass结果。

实际连接状态为ReconnectWaiting、closing false、对端0、connected false，business_verified false；软件能读对对象与来源不等于存在真实LIS对端。下载/上传/业务继续pending，旧登录、注销、主通信重开、Qk和关闭检查未执行。没有把pending状态改成业务通过。

## 旧停、原主控和附加原件

直接取回的 `old_stop_receipt.txt` SHA为 `4619885a1c8f3d5b99b034c7168ce7021a7aaefb4a154fc9ad0475a4912ebcb0`，精确等于Root停止日志中FORMAT起的原始receipt字节，也等于activate_derivation记录。旧run为 `20261103_180023_24243`、PID24414、ticks555014；旧APP_EXIT0/clean1和旧停止工具实际0归属明确。取回的old_stop.actual_exit与waiter.actual_exit分别实为0。

remote_proofs中的意图、Root批准和七配置清单与本地冻结三记录逐字节相等。postlive脚本在同一新PID上核实际exe/SHA、lckfb、READY和不存在exit_code/result，并重新核七配置/三指针；实际log和exit支持该段结果。原主控仍为5008/ticks118180，原exe/SHA/boot/0.0.0.0:5000身份与旧停前相同，未重复启动。

postlive还记录相机455358/8080、3A139148及服务活动；这只作同板现场快照。资源原件146行＝1行表头+145行采样，实际SHA `3ecc6708979f7e148aaba48a8947c4bb9e53d3ba50d49a4600e8fcadc69e7978`。本报告不据采样存在或单次服务存活判资源预算、相机业务或长稳达标。

## 关键证据指纹

| 原件 | SHA-256 |
| --- | --- |
| capture_manifest.sha256 | 0db5712708363a5a09f7c1274d6f7ad54088022f26d74284c4b9a514ceadb16a |
| lis_observation.json | a47effa967f7acb168a6fc5680c3af46691e276135510f75542c2b4da44d7b2b |
| lis_observation.ready | c1e933a6f3c98e6b831200e0eb76e8ca57cd763ed979d586d659df10ec7361b4 |
| 前后身份两原件（相同） | 8202dd185bcb9f078d27376678b9ad15437a1573250f59d392e7d401ad9f0f9c |
| capture_status.json | 5d8508e774a227899f0fc0741d61c4286a1ec699c0167a75ae76ba1b8cbde868 |
| collect_sealed.stdout.tar.gz | 0359a51f0dfaf692b3a3280f712a90343bf559fa3a7faa29477470a7bcf32d25 |
| parser.stdout.json | 9a1a1c686757a8aa368b01e2b818bc917e0d391009858e3d8b71a739dd160b6f |
| stop_old_batch10_02.log | f235c5023e52c26449d740781c4270ea1c48dfc34cbac8abb78168eb10b3c389 |
| launch_new_batch10_02.log | 3829731a48a18435dc978b4146df609cddbe6d1947875875cd60721f9b1819ba |
| wait_lis_batch10_02.log | 56c3a1a6e29ad3e12fb8be49c2a0c67294d8015981e691b385e86887ed9f6516 |
| post_live_batch10_01.log | b21f66ae28e51db6a6251b25f6442fc8f0256e324eaf14404e0aaea1abc8c6a0 |

本Agent的本地只读字节/SHA/字段一致性核对真实退出0，不是重新运行业务或既有解析器得到的第二轮结果。可以据此收口这段有限软件/留驻验证，并保持当前程序原样运行；其余待验范围继续保留，不为补齐旧项再启停或重跑。
