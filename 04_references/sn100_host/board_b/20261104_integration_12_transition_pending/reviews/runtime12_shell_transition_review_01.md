# 第12批运行过渡：Root稳定快照Shell独审

2026-11-04。只审 `workflow_next_20261103/runtime_transition_root_review_12_01` 的runtime_common、七phase模板及必要的两个Python生成/输入校验边界，不追读作者持续修改的candidate。Root快照清单SHA为 `f6163b5a3fb07d4530ee49430ec92e4763b974a61c7da14f7de1800d39b40e27`；审前后十份代码的SHA/bytes均与该清单相同，0漂移。

结论：此固定快照未发现确定的阶段越权、关闭/锁交接倒置、重复GUI启动或观察结束关闭产品的阻点，可供Root继续冻结实际核心输入并审派生实物。它只是WIP供审快照，author_final_manifest_adopted、generation_approved、board_executed均为false；本报告不构成生成或动作批准。没有运行生成器、import候选、执行phase/产品、编译、SSH或板端操作，也没有改任何代码/旧原件；只新增本报告。两个Python仅由本审ast.parse读取语法成功。

## 意图和阶段许可

生成器check_core第249—270行严格核既有24字段规范ASCII原文、字段顺序及同一新包/观察输入；allow_prepare_flat、allow_activation、allow_old_app_stop、allow_unique_start明确要求全部false。它们是稳定目标声明，不能按旧模板解释成当前操作许可。

动作另由同一core、准确phase的独立授权决定：frozen/allowed必须为true，Root批准原件SHA明确；每阶段递归核前一授权、真实receipt、外层exit和log。metadata不能预填前阶段，stop前不能预填停止原件；activate必须直接承接本轮stop，launch/observe继续绑定同一停止意图及receipt。只有检查完成后，shell_globals才渲染template_frozen/phase_allowed=true，未把稳定意图的四false偷偷改成true。

停止证据还要求本轮捕获目录、transition ID、core/授权/意图SHA、boot、准确C7 oldrun/PID/start/UID/exe/manifest，以及结果、退出、stdout/stderr、实际停止工具exit和四份进程身份原件。APP_EXIT0/clean1、实际0及各SHA必须吻合；b31旧receipt或另一core/阶段原件不能直接接续。Python失败门在输出创建前，生成前重新核所有已读输入；各JSON False模板也仍保持未冻结/未许可。

字符串路径按规范ASCII白名单、固定BASE及直接子目录限制；shell_globals使用shlex.quote，原文载荷用BASE64，模板替换位置唯一。没有eval/source数据。实际source的只有固定helper，之前先核包清单、实物SHA及固定helper SHA。旧02抽取范围是七个函数定义，含PF分类修复，不包含旧24字段true许可解析和顶层动作。

## 七阶段实际控制流

| 阶段 | 本快照中的动作和边界 |
| --- | --- |
| metadata | 只取store锁；前后核准确活C7、原包、新nested包和环境。独占建立Root:lckfb 750元数据目录、冻结原文、lckfb 700空观察/采集目录，不切current、不关闭或启动产品，不取life锁。 |
| prepare | 只取store锁；flat不存在且非链接，完整cp-a，再核nested/flat同manifest和两程序实物。前后C7仍活，无生命周期锁/quiet要求，不为准备文件关C7。 |
| preflight | 只取store锁，以lckfb source已核helper并直接调用preflight函数；不调用main/start。前后核新包、准确活C7和原包。此处依赖/字体/设备权限检查不算新产品运行。 |
| stop | 存储锁下先核oldrun与准确活C7；仅一次普通lckfb旧helper stop。真实非0（含pending）立即以该码退出，不发第二次信号、强杀或进入后续阶段。只有准确旧run的真实0/clean1封口，再等quiet、取已释放life锁并复核后，才写本轮stop receipt。 |
| activate | store→life；先核旧已结束、quiet、七配置/指针/原主控/相机、新两份包和同一stop receipt。写审计后原子切前再核全部阶段材料及身份，只切到flat；随后要求formal current_release接受该flat，并核旧已结束和新包。没有启动产品。 |
| launch | store→life，只有旧已真实结束、显示quiet、切版审计/新包/空私有目录符合才建新run/登记latest_run。唯一systemd请求后释放父FD9/8再等新身份；不调用start或READY超时关闭路径。 |
| observe | 只取store，不取life、不要求显示quiet；核准确新进程，普通lckfb调用既有只读waiter。成功/超时都先复核新进程，非0只使工具失败；无stop、重启、回退或信号。 |

metadata/prepare/preflight后receipt仍核活C7；stop/activate核真实旧退出及quiet；launch/observe核活新程序，未在新程序运行后重新要求quiet。所有receipt在阶段材料和环境再次核实后才写，且各阶段拒绝已有执行/失败现场，不以覆盖原件重试来伪造成功。receipt的EXIT_CODE=0是阶段结果，Python还要求独立外层exit确为0和完整日志尾部，不能当应用退出0。

## 停止后的quiet时间口径

保留快照中已审4轮，不增加百轮等待。沿用02的transition_assert_quiet每次依次做完整 `/proc/[0-9]*/exe` 扫描、sn100-shell服务ActiveState核对、VT7/DRM的fuser核对；函数本身没有sleep或内部等待。失败后外层才sleep0.1秒。

因此4轮的sleep参数合计至多0.4秒，绝不是整个quiet阶段0.4秒固定上限。完整扫描及命令耗时另计；旧02诊断曾约6秒/轮，只作历史参考，本轮没有重测，也不承诺固定总时长。Root已明确保留4轮不改。失败仍只保留真实停后现场、拒绝后续切版，不补信号或通过关闭其他业务取得quiet。

## 启动、锁和留屏

launch的唯一命令形态为systemd-run Type=exec→openvt7→runuser lckfb→env三观察变量→新flat包内helper，CLI六参数为 `supervise BASE FLAT RUN 0 ''`。保留0和空session，不运行旧start的30秒READY超时TERM分支，也不传模拟登录。父在任何身份等待前关闭life/store锁FD，新服务由PID1管理，没有--wait/--pipe或显式额外FD传递造成同锁等待；监督器再取自己的life锁。

身份等候只等文件，最多60次×sleep0.25，超时不关闭新程序；之后按boot、PID/start、UID、实际exe/SHA、latest_run及无退出封口核对。三个观察变量从实际产品/proc环境白名单读取核同值，不倾倒其他环境。服务属性保存为原件；本源审不替代Root检查届时实际Restart/RuntimeMaxUSec/KillMode/TTY等值，也不承诺外部服务异常或监督器故障下永久不退出。

observe显式传完整等待器参数，截止只约束等待器。返回后即使失败，也没有停止、重启、复发业务命令或回退动作；成功再核SHELL_READY、包、实际环境、同一新PID及服务原件。APPLICATION_EXIT保持not_evaluated，SOFTWARE_RESULT也没有被生成器假写通过。软件封口仍要Root独立采集/解析；不得把工具0扩大为真实LIS连接、Uh回读、实体操作或资源通过。复核后程序保持运行。

## 配置、同板资源和PF修复

每个活体/收据边界都重新核board/boot、七份固定配置普通原件的准确ad11指纹、三指针原文字节、原主控PID/start/UID/exe/SHA及全部5000监听归属。camera与three_a使用分别固定的实际PID/start/UID/exe/SHA，另核opto-camera.service的Active/Sub/MainPID；收据绑定相同身份摘要。仅systemctl show和fuser等读取，没有启停相机、3A或原主控。

legacy直接来自固定prepare_flat02 SHA，保留两读start_ticks及PF_KTHREAD判定，不比较内核线程S/R/D瞬态；死亡Z/X按原合同处理，普通活用户exe不可读仍79。新唯一进程扫描对同名其他位置/deleted映像及多个实例保守拒绝，没有放宽quiet去隐藏第二GUI。

## 本快照指纹

| 文件 | 实读SHA-256 |
| --- | --- |
| root_code_snapshot.json | `f6163b5a3fb07d4530ee49430ec92e4763b974a61c7da14f7de1800d39b40e27` |
| runtime_common.sh.in | `dd304e7463482eec57b2af07443115fc0e176eff9014c431e7c4607988cd8900` |
| phase_metadata.sh.in | `abb6d4e665cbbf9fafd880747665c1b0924e7946ec2dc38eb008877dc7ad6e67` |
| phase_prepare.sh.in | `2b8a7f98bca8961fe42d237e8588c7e8fafb23aab2d849af1faf59fa46595845` |
| phase_preflight.sh.in | `74d4ecb535705728f510d02447e38d16b31a48a9f3fa785f84b64f43335d0350` |
| phase_stop.sh.in | `b3bf69af2db64ca379a5e925ffcc593cd4a53b760e293a517074ae9a8bd8258f` |
| phase_activate.sh.in | `6d1939463b58b91c5ac522341e61ff5561685d9c91aed605e4db50a9b0e00cda` |
| phase_launch.sh.in | `baf5d09b9334c11fd085a83811bea719cb96358560cb02863ff77a2773438501` |
| phase_observe.sh.in | `db09d654141e08eece835ad8d5fda1a4d534158d0641b8f8573f223c7ecdffb6` |
| generate_runtime_controls.py | `6e7864038f34f8d600899cba48823a04a271af1c74151b304a5e54958b343797` |
| runtime_file_phase.py | `b5683da170a338ab9d7448f0d8d5760ab364b11c259d47f497a63a7bc9783760` |

此结论不能按作者目录名自动转授最终代码；后续最终十份实物若不同，须做差异复核。Root实际核心输入、每阶段授权、生成出的脚本/清单及真实板端材料仍需逐项核定。本轮没有生成控制脚本或执行任何换版动作。
