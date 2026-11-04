# 修订03怎样承接总控裁定v1与窄审

依据`temp/sn100_host/resume_dispatch_20261103/lis_observation_root_decision_v1.md`。此处只实现文件阶段与切换模板，不写CPP、等待器、解析器或启动编排；当前程序不附加、不注入，不启动第二界面。修订01及其独审、补丁、指纹原样保留。

修订02及原审也全部保留。总控后续窄审确认02仍缺锁后同组GUI/服务/显示复核，本修订只补这条P2和主控排重、末尾批次标记及历史前像措辞，不回写旧审结论。其余绑定/观察合同仍沿下文修订02设计，逻辑输出范围继续为`integration_next_02`；Root未来只选一个实际冻结副本。

## 三个身份分开绑定

文件阶段在构建前固定源码、工具、权限及Root裁定record。新完整构建实际产生源→生成物→对象→链接→两程序→程序包和发布号，后续暂存/归档继续核同一实物。观察CPP拟随未来业务程序同进程链接，缺参数零干预；其正式main/build接线只由Root处理，本文不宣称这些接线已经存在。

程序包内`SHA256SUMS`包含主程序，而发布号又来自该清单SHA。把未来发布号编进主程序会形成循环，所以Root核包后才提供`SN100_LIS_OBSERVATION_DIR`、`SN100_LIS_OBSERVATION_ID`及`SN100_LIS_OBSERVATION_RELEASE`。这些实际值不在文件阶段cfg里预填。cfg SHA已经进入`build_complete`，包后再改cfg也会破坏既有绑定，不能通过改完成记录绕过。

新增`runtime_launch_intent.template.json`只描述Root在包后单独冻结的意图：引用已经固定的cfg、build_complete、包和新程序身份，再填写三参数和私有新目录。引用单向从启动意图指向既有构建产物，不反向改变binary、包、cfg或源码表。切换草稿核Root冻结的意图record及平铺JSON的启动输入语义，交接其SHA；不负责启动或解析CPP业务结果。

文件阶段cfg格式升v2，只含构建/暂存许可。晚授的准备平层、切换、旧停止和唯一启动许可在包后意图中；不能在构建后改cfg许可、evidence或tools。Root填写意图前须核其cfg SHA等于真实build_complete.tools_config_sha256，发布号等于包身份，程序预期SHA等于真实构建/包；外部实际进程再另核，不能把预期当实测。

切换模板直接核意图语义，不只核存在和SHA。为不引入板端Python/jq，只接受模板的固定顺序、每行一字段的平铺JSON，使用2空格缩进、LF、ASCII安全字符串和明确布尔/0；不支持转义、嵌套、自由文本或其他格式。false、错发布、错程序/包/配置SHA、非Root启动、非0监督时限、缺许可及非新私有观察目录都停止。它不解析CPP业务JSON，也不替代等待/解析工具。

Root在包后生成新的实际intent和切换脚本，不覆盖本模板；意图不包含自身SHA或实际切换脚本SHA，脚本只引用已定的意图/输入批准记录。外层Root交接清单可同时记录这些SHA，但不能再被前两者反向包含，避免形成新的摘要循环。已封口的文件阶段归档/清单不追加包后意图；另留Root启动/观察证据。

## 同进程观察的结果语义

schema为`sn100-lis-keep-running-observation-v1`。被动CPP最多等就绪60秒，再有限观察15秒，原子写`lis_observation.json`和后写`lis_observation.ready`；ID、运行时发布号、PID与JSON SHA由状态窗口实现及独审。本窗口不生成假结果。

`observation_complete`只表示快照已封口，内部模块没有独立进程退出。`application_exit=null`，`application_exit_evaluated/shutdown_evaluated/close_verified=false`；只读等待和解析工具分别记录真实`waiter_actual_exit`、`parser_actual_exit`，未知不补0。只采两份封口结果、两份前后实际身份及采集状态/清单，不要求持续日志和resources前后一致。

CPP只读本次受影响LIS对象、已提交配置来源/版本、顶部控件和连接状态。大exe/current/包及SHA由外部采集器前后核；环境传来的预期SHA不能冒称程序已实测。软件观察、采集成功、真实LIS业务及应用仍活着分别判定，不由单个0代替。白名单和不收敏感正文要求沿Root裁定。

## 对现有supervise 0的只读评估

`rk_board.sh start ... 0`仍有约30秒READY失败后TERM，不能默认采用。直接交给已有`supervise ... 0`可避开start外层的这条等待分支，并通过0禁用运行软期限；Root可另用只读等待器按自身截止返回。启动必须脱离SSH/观察器的关闭或超时生命周期，且在切换模板释放生命周期锁后才允许监督器取锁。

这不是“任何失败绝不关闭”。现有`supervisor_exit()`在异常退出且子进程仍被拥有时会发TERM；TERM/INT/HUP会设置关闭标志，再由监督循环对匹配子进程发TERM。`set -e`下资源采样或日志写入失败也可能触发EXIT处理。这些是源码可达分支，本轮没有故障注入或实机验证。Root若选择复用，须另审实际启动编排、独立systemd/VT/运行用户、信号归属、目录权限及该异常路径；本窗口没有复制或修改监督器，也不写新launcher。

裸`supervise`不等于旧start全部前置检查：它不会自行证明传入release就是current，也不代办服务/显示分配和全局唯一性检查。Root须在运行锁交接点重核current/包/意图、唯一进程/服务/显示、lckfb及实际环境传递。旧systemd-run命令未显式传三观察变量；意图不能冒称参数已生效，必须以未来CPP封口ID/RELEASE和前后实际进程身份核采用结果。

## 本轮精确变化和剩余交接

- 复制修订01到新候选02；WORK、远端暂存和正式输出边界改用`integration_next_02`，不复用旧目录。
- filephase cfg以`observation_contract`固定Root裁定、schema、三个参数名称及Root唯一启动职责，真实值仍不填；冻结前须把裁定record加入evidence。
- 切换草稿移除旧“状态窗口runner负责启动”的假设，激活需Root包后启动意图；返回`PROGRAM_STARTED=0/LIVE_HANDOFF_PENDING=1/START_OWNER=root`。
- 实际构建绑定算法、独立存储锁、板端22字段保护、同包回读和归档闭链保留。本轮仍不构建、不运行阶段或产品、不联板、不操作显示、提交或清理。

Root下一步固定实际源码/补丁/对象/许可，状态窗口交同进程CPP与只读等/解析修订及独审。实际启动脚本、留屏、真实端点、仪器和资源均待Root另行审定与验证，本模板不放行激活。
